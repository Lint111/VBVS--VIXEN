/**
 * @file test_view_hud_golden.cpp
 * @brief View Contract Inc-2 gate. Proves the generated Hud.g.h RmlUi data-model registration
 * block compiles and binds against a real Rml::DataModelConstructor with no RmlUi error (a
 * missing struct/array registration or a failed Bind is logged as an error and fails the test).
 * That the header matches the canonical Hud [View] schema (codegen/view-schemas/Hud.cs) is NOT
 * asserted here: the view_hud_check codegen target regenerates the header from the schema and
 * fails the build on any difference, so a hand-copied expected sequence would only track it.
 */
#include <gtest/gtest.h>
#include <RmlUi/Core.h>
#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/RenderInterface.h>
#include <RmlUi/Core/SystemInterface.h>

#include <string>
#include <vector>

#include "Generated/Hud.g.h"   // the artifact under test
#include "Ui/IView.h"
#include "Ui/VixenRmlSystemInterface.h"
#include "Text/RmlFontEngine.h"

namespace {

// Minimal null render interface (no GPU needed) — mirrors test_ui_hud_smoke.cpp's fixture;
// RmlUi requires both a SystemInterface and a RenderInterface set before Initialise()/
// CreateContext() will succeed, even headlessly.
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

// Records RmlUi errors/warnings (RmlUi logs a failed Bind or an unregistered type through the
// SystemInterface and keeps going, so the log is where a broken registration shows up).
class RecordingSystemInterface final : public Rml::SystemInterface {
public:
    double GetElapsedTime() override { return 0.0; }
    bool LogMessage(Rml::Log::Type type, const Rml::String& message) override {
        if (type == Rml::Log::LT_ERROR || type == Rml::Log::LT_WARNING || type == Rml::Log::LT_ASSERT)
            problems.push_back(message);
        return Rml::SystemInterface::LogMessage(type, message);
    }
    std::vector<std::string> problems;
};

}  // namespace

TEST(ViewHudGolden, GeneratedBindFunctionCompilesAndBinds) {
    RecordingSystemInterface sysIface;
    NullRenderInterface renderIface;
    Vixen::Text::RmlFontEngine fontEngine;
    Rml::SetSystemInterface(&sysIface);
    Rml::SetRenderInterface(&renderIface);
    Rml::SetFontEngineInterface(&fontEngine);
    ASSERT_TRUE(Rml::Initialise());
    Rml::Context* ctx = Rml::CreateContext("view-golden", Rml::Vector2i(64, 64));
    ASSERT_NE(ctx, nullptr);

    int tick = 7, bodyCount = 3, activeLensCount = 2;
    Rml::String activeLensName = "lens";
    std::vector<Vixen::Views::HudFaction> factions;
    std::vector<Vixen::Views::HudEvent> events;
    Vixen::Views::HudBind bind{ &tick, &bodyCount, &activeLensName, &activeLensCount, &factions, &events };

    Rml::DataModelConstructor c = ctx->CreateDataModel("hud");
    ASSERT_TRUE(static_cast<bool>(c));
    Vixen::Views::BindHudModel(c, bind);
    EXPECT_TRUE(static_cast<bool>(c.GetModelHandle()));
    EXPECT_TRUE(sysIface.problems.empty())
        << "RmlUi reported a problem while binding the generated Hud model: "
        << (sysIface.problems.empty() ? "" : sysIface.problems.front());

    Rml::RemoveContext("view-golden");
    Rml::Shutdown();
}

namespace {
struct TrivialView : Vixen::RenderGraph::IView {
    int x = 5;
    const char* ModelName() const override { return "triv"; }
    const char* DocumentPath() const override { return "assets/ui/hud.rml"; }
    void Register(Rml::DataModelConstructor& c) override { c.Bind("x", &x); }
};
}  // namespace

TEST(ViewHudGolden, IViewSeamIsViewAgnostic) {
    Vixen::Ui::VixenRmlSystemInterface sysIface;
    NullRenderInterface renderIface;
    Vixen::Text::RmlFontEngine fontEngine;
    Rml::SetSystemInterface(&sysIface);
    Rml::SetRenderInterface(&renderIface);
    Rml::SetFontEngineInterface(&fontEngine);
    ASSERT_TRUE(Rml::Initialise());
    Rml::Context* ctx = Rml::CreateContext("iview", Rml::Vector2i(64, 64));
    ASSERT_NE(ctx, nullptr);
    TrivialView v;
    Rml::DataModelConstructor c = ctx->CreateDataModel(v.ModelName());
    ASSERT_TRUE(static_cast<bool>(c));
    v.Register(c);
    EXPECT_TRUE(static_cast<bool>(c.GetModelHandle()));
    Rml::RemoveContext("iview");
    Rml::Shutdown();
}
