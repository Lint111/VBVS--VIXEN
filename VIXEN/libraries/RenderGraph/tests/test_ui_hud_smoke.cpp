// test_ui_hud_smoke.cpp — headless smoke: RmlUi data model + hud.rml bindings.
//
// No GPU/Vulkan required. Uses a null RenderInterface to exercise:
//   1. RmlUi init with VixenRmlSystemInterface
//   2. Context::CreateDataModel("hud") / Bind("tick") / Bind("bodyCount")
//   3. LoadDocument("hud.rml") parses without error
//   4. SetHudData equivalence: update struct + DirtyVariable + Update()
//   5. Rml::Shutdown() cleans up without crash
//
// The font must be loaded for the document to parse without assertion. Assets
// are staged next to the test binary by the POST_BUILD rule added in CMakeLists.

#include <gtest/gtest.h>

#include <RmlUi/Core.h>
#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/ElementText.h>
#include <RmlUi/Core/RenderInterface.h>
#include <RmlUi/Core/SystemInterface.h>
#include <RmlUi/Core/Types.h>

#include "Ui/VixenRmlSystemInterface.h"

#include <chrono>
#include <string>

// ---------------------------------------------------------------------------
// Minimal null render interface (no GPU needed)
// ---------------------------------------------------------------------------
class NullRenderInterface final : public Rml::RenderInterface {
public:
    Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex>, Rml::Span<const int>) override { return 0; }
    void RenderGeometry(Rml::CompiledGeometryHandle, Rml::Vector2f, Rml::TextureHandle) override {}
    void ReleaseGeometry(Rml::CompiledGeometryHandle) override {}
    void EnableScissorRegion(bool) override {}
    void SetScissorRegion(Rml::Rectanglei) override {}
    Rml::TextureHandle LoadTexture(Rml::Vector2i&, const Rml::String&) override { return 0; }
    Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte>, Rml::Vector2i) override { return 0; }
    void ReleaseTexture(Rml::TextureHandle) override {}
};

// ---------------------------------------------------------------------------
// Test fixture — one RmlUi global init/shutdown per TEST_F suite
// ---------------------------------------------------------------------------
class HudSmokeTest : public ::testing::Test {
protected:
    void SetUp() override {
        Rml::SetSystemInterface(&sysIface_);
        Rml::SetRenderInterface(&renderIface_);
        ASSERT_TRUE(Rml::Initialise());
        // Font must load for the document to parse (RmlUi logs a warning but doesn't crash without it;
        // load it so the text-resolve path works as it would in production).
        Rml::LoadFontFace("assets/ui/LatoLatin-Regular.ttf");
        ctx_ = Rml::CreateContext("hud_smoke", Rml::Vector2i(1280, 720));
        ASSERT_NE(ctx_, nullptr);
    }

    void TearDown() override {
        if (ctx_) Rml::RemoveContext("hud_smoke");
        ctx_ = nullptr;
        Rml::Shutdown();
    }

    Vixen::Ui::VixenRmlSystemInterface sysIface_;
    NullRenderInterface renderIface_;
    Rml::Context* ctx_ = nullptr;
};

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

// Verify the data model constructs and Bind() succeeds for int scalars.
TEST_F(HudSmokeTest, DataModelConstructsAndBinds) {
    struct HudData { int tick = 0; int bodyCount = 0; } hud;
    Rml::DataModelHandle model;

    Rml::DataModelConstructor c = ctx_->CreateDataModel("hud");
    ASSERT_TRUE(static_cast<bool>(c)) << "CreateDataModel returned empty constructor";
    EXPECT_TRUE(c.Bind("tick", &hud.tick));
    EXPECT_TRUE(c.Bind("bodyCount", &hud.bodyCount));
    model = c.GetModelHandle();
    EXPECT_TRUE(static_cast<bool>(model));
}

// Verify hud.rml loads, the data model drives SetHudData equivalence, and
// GetInnerRML() contains the expected substituted values after Update().
TEST_F(HudSmokeTest, HudDocumentLoadsAndBindsResolve) {
    struct HudData { int tick = 0; int bodyCount = 0; } hud;
    Rml::DataModelHandle model;

    // Build the model before loading the document (same order as UIRenderNode::CompileImpl).
    {
        Rml::DataModelConstructor c = ctx_->CreateDataModel("hud");
        ASSERT_TRUE(static_cast<bool>(c));
        c.Bind("tick", &hud.tick);
        c.Bind("bodyCount", &hud.bodyCount);
        model = c.GetModelHandle();
    }

    Rml::ElementDocument* doc = ctx_->LoadDocument("assets/ui/hud.rml");
    ASSERT_NE(doc, nullptr) << "hud.rml failed to load";
    doc->Show();

    // --- Simulate SetHudData(42, 7) ---
    hud.tick = 42;
    hud.bodyCount = 7;
    ASSERT_TRUE(static_cast<bool>(model));
    model.DirtyVariable("tick");
    model.DirtyVariable("bodyCount");

    // Update() processes data-model bindings and propagates to the DOM.
    ctx_->Update();

    // Inspect the rendered inner RML; data-binding replaces {{tick}} with "42" etc.
    Rml::String inner = doc->GetInnerRML();
    EXPECT_NE(inner.find("42"), Rml::String::npos)
        << "Expected '42' in inner RML after SetHudData(42,7). Got: " << inner;
    EXPECT_NE(inner.find("7"), Rml::String::npos)
        << "Expected '7' in inner RML after SetHudData(42,7). Got: " << inner;

    std::printf("[HudSmokeTest] inner RML after SetHudData(42,7): %s\n", inner.c_str());
}

// Verify DirtyVariable works across multiple SetHudData-equivalent calls.
TEST_F(HudSmokeTest, DirtyVariableUpdatesCorrectly) {
    struct HudData { int tick = 0; int bodyCount = 0; } hud;
    Rml::DataModelHandle model;

    {
        Rml::DataModelConstructor c = ctx_->CreateDataModel("hud");
        ASSERT_TRUE(static_cast<bool>(c));
        c.Bind("tick", &hud.tick);
        c.Bind("bodyCount", &hud.bodyCount);
        model = c.GetModelHandle();
    }

    Rml::ElementDocument* doc = ctx_->LoadDocument("assets/ui/hud.rml");
    ASSERT_NE(doc, nullptr);
    doc->Show();

    // First push
    hud.tick = 100; hud.bodyCount = 5;
    model.DirtyVariable("tick"); model.DirtyVariable("bodyCount");
    ctx_->Update();

    Rml::String inner1 = doc->GetInnerRML();
    EXPECT_NE(inner1.find("100"), Rml::String::npos) << "Expected 100 in: " << inner1;

    // Second push
    hud.tick = 200; hud.bodyCount = 12;
    model.DirtyVariable("tick"); model.DirtyVariable("bodyCount");
    ctx_->Update();

    Rml::String inner2 = doc->GetInnerRML();
    EXPECT_NE(inner2.find("200"), Rml::String::npos) << "Expected 200 in: " << inner2;
    EXPECT_NE(inner2.find("12"), Rml::String::npos) << "Expected 12 in: " << inner2;

    std::printf("[HudSmokeTest] inner RML after tick=200 bodyCount=12: %s\n", inner2.c_str());
}
