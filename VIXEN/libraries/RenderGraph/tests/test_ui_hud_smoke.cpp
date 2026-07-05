// test_ui_hud_smoke.cpp — headless smoke: RmlUi data model + hud.rml bindings.
//
// No GPU/Vulkan required. Uses a null RenderInterface to exercise:
//   1. RmlUi init with VixenRmlSystemInterface
//   2. Context::CreateDataModel("hud") / Bind("tick") / Bind("bodyCount")
//   3. LoadDocument("hud.rml") parses without error
//   4. SetHudData equivalence: update struct + DirtyVariable + Update()
//   5. Rml::Shutdown() cleans up without crash
//   6. (S1b) RegisterStruct<HudFaction/HudEvent> + RegisterArray<vector<...>>
//      + Bind("factions"/"events") + data-for list binding resolves correctly
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

#include "Nodes/UIRenderNode.h"
#include "Ui/VixenRmlSystemInterface.h"

#include <chrono>
#include <span>
#include <string>
#include <vector>

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

// ---------------------------------------------------------------------------
// S1b: list/struct data model — RegisterStruct + RegisterArray + data-for
// ---------------------------------------------------------------------------

// Internal struct types that mirror UIRenderNode's private HudFaction / HudEvent.
// We define them locally here so the smoke exercises the same RmlUi API path.
struct SmokeHudFaction { Rml::String name; float grievance = 0.f; };
struct SmokeHudEvent   { Rml::String kind; int tick = 0; };

// Verify RegisterStruct<> + RegisterArray<> + Bind() constructs successfully.
TEST_F(HudSmokeTest, S1b_ListDataModelConstructs) {
    std::vector<SmokeHudFaction> factions;
    std::vector<SmokeHudEvent>   events;
    int tick = 0, bodyCount = 0;
    Rml::DataModelHandle model;

    Rml::DataModelConstructor c = ctx_->CreateDataModel("hud");
    ASSERT_TRUE(static_cast<bool>(c)) << "CreateDataModel returned empty constructor";

    auto fh = c.RegisterStruct<SmokeHudFaction>();
    ASSERT_TRUE(static_cast<bool>(fh));
    EXPECT_TRUE(fh.RegisterMember("name",      &SmokeHudFaction::name));
    EXPECT_TRUE(fh.RegisterMember("grievance", &SmokeHudFaction::grievance));

    auto eh = c.RegisterStruct<SmokeHudEvent>();
    ASSERT_TRUE(static_cast<bool>(eh));
    EXPECT_TRUE(eh.RegisterMember("kind", &SmokeHudEvent::kind));
    EXPECT_TRUE(eh.RegisterMember("tick", &SmokeHudEvent::tick));

    EXPECT_TRUE(c.RegisterArray<std::vector<SmokeHudFaction>>());
    EXPECT_TRUE(c.RegisterArray<std::vector<SmokeHudEvent>>());

    EXPECT_TRUE(c.Bind("tick",      &tick));
    EXPECT_TRUE(c.Bind("bodyCount", &bodyCount));
    EXPECT_TRUE(c.Bind("factions",  &factions));
    EXPECT_TRUE(c.Bind("events",    &events));

    model = c.GetModelHandle();
    EXPECT_TRUE(static_cast<bool>(model));
}

// Verify that a data-for list in an inline RML doc resolves to the bound values
// after SetHudView-equivalent mutation + Update(). Uses LoadDocumentFromMemory
// so the test is independent of hud.rml (which gets data-for in Task 4).
TEST_F(HudSmokeTest, S1b_SetHudViewListsResolveInRml) {
    std::vector<SmokeHudFaction> factions;
    std::vector<SmokeHudEvent>   events;
    int tick = 0, bodyCount = 0;
    Rml::DataModelHandle model;

    {
        Rml::DataModelConstructor c = ctx_->CreateDataModel("hud");
        ASSERT_TRUE(static_cast<bool>(c)) << "CreateDataModel failed";

        auto fh = c.RegisterStruct<SmokeHudFaction>();
        ASSERT_TRUE(static_cast<bool>(fh)) << "RegisterStruct<SmokeHudFaction> failed (type already registered?)";
        EXPECT_TRUE(fh.RegisterMember("name",      &SmokeHudFaction::name));
        EXPECT_TRUE(fh.RegisterMember("grievance", &SmokeHudFaction::grievance));

        auto eh = c.RegisterStruct<SmokeHudEvent>();
        ASSERT_TRUE(static_cast<bool>(eh)) << "RegisterStruct<SmokeHudEvent> failed";
        EXPECT_TRUE(eh.RegisterMember("kind", &SmokeHudEvent::kind));
        EXPECT_TRUE(eh.RegisterMember("tick", &SmokeHudEvent::tick));

        EXPECT_TRUE(c.RegisterArray<std::vector<SmokeHudFaction>>())
            << "RegisterArray<vector<SmokeHudFaction>> failed";
        EXPECT_TRUE(c.RegisterArray<std::vector<SmokeHudEvent>>())
            << "RegisterArray<vector<SmokeHudEvent>> failed";

        EXPECT_TRUE(c.Bind("tick",      &tick))      << "Bind tick failed";
        EXPECT_TRUE(c.Bind("bodyCount", &bodyCount)) << "Bind bodyCount failed";
        EXPECT_TRUE(c.Bind("factions",  &factions))  << "Bind factions failed";
        EXPECT_TRUE(c.Bind("events",    &events))    << "Bind events failed";
        model = c.GetModelHandle();
        ASSERT_TRUE(static_cast<bool>(model)) << "GetModelHandle failed";
    }

    // Inline RML with data-for.
    // IMPORTANT: data-model must be on an INNER element (a div inside body), NOT on <body> itself.
    // When data-model is on <body>, XMLNodeHandlerBody processes the body tag AFTER the document's
    // outer structure (<rml>) is already in the parse tree, and the data-model attribute is not
    // applied to the current parse frame element until AFTER parsing (when the doc is added to
    // context root). This means ApplyStructuralDataViews (called during inner_xml_data capture)
    // sees element->GetDataModel() == null and fails to register DataViewFor.
    // Putting data-model on an inner <div> means SetDataModel is called on that div DURING parse
    // (via SetParent/AppendChild), so the model is present when inner_xml_data fires.
    const Rml::String rml = R"(
<rml>
<head><title>smoke</title></head>
<body>
<div data-model="hud">
<span id="tick">{{tick}}</span>
<span id="bc">{{bodyCount}}</span>
<div data-for="f : factions"><span class="fn">{{f.name}}</span><span class="fg">{{f.grievance}}</span></div>
<div data-for="e : events"><span class="ek">{{e.kind}}</span><span class="et">{{e.tick}}</span></div>
</div>
</body>
</rml>)";

    // Pre-populate the vectors before loading the document so the data-for view
    // can query the container size during its first Update() pass.
    tick = 7;
    bodyCount = 3;
    factions.push_back({"Empire", 1.0f});
    events.push_back({"war", 81});

    Rml::ElementDocument* doc = ctx_->LoadDocumentFromMemory(rml);
    ASSERT_NE(doc, nullptr) << "Inline HUD doc with data-for failed to load";
    doc->Show();

    ASSERT_TRUE(static_cast<bool>(model));
    model.DirtyVariable("tick");
    model.DirtyVariable("bodyCount");
    model.DirtyVariable("factions");
    model.DirtyVariable("events");

    // Two Update() calls: first instantiates data-for clones, second resolves
    // data-text bindings inside the newly created clone elements.
    ctx_->Update();
    ctx_->Update();

    // Check the body's GetInnerRML for the resolved scalar values.
    Rml::String inner = doc->GetInnerRML();
    std::printf("[HudSmokeTest] S1b inner RML: %s\n", inner.c_str());

    EXPECT_NE(inner.find("7"), Rml::String::npos) << "Expected tick '7' in: " << inner;
    EXPECT_NE(inner.find("3"), Rml::String::npos) << "Expected bodyCount '3' in: " << inner;

    // For data-for clones, RmlUi inserts sibling elements that appear in the body's inner RML.
    // Query the parent element to count its children and find the faction clone.
    Rml::Element* body = doc->GetElementById("hud_body");
    if (!body) body = doc->GetFirstChild();  // fallback: body is first child of document

    // Use QuerySelector to find the faction/event clone spans.
    Rml::Element* fn_span = doc->QuerySelector(".fn");
    Rml::Element* ek_span = doc->QuerySelector(".ek");

    if (fn_span) {
        Rml::String fn_inner = fn_span->GetInnerRML();
        std::printf("[HudSmokeTest] S1b .fn inner: %s\n", fn_inner.c_str());
        EXPECT_NE(fn_inner.find("Empire"), Rml::String::npos)
            << "Expected 'Empire' in .fn: " << fn_inner;
    } else {
        // data-for clones may appear in the body inner RML as raw siblings
        EXPECT_NE(inner.find("Empire"), Rml::String::npos)
            << "Expected 'Empire' in body inner RML: " << inner;
    }

    if (ek_span) {
        Rml::String ek_inner = ek_span->GetInnerRML();
        std::printf("[HudSmokeTest] S1b .ek inner: %s\n", ek_inner.c_str());
        EXPECT_NE(ek_inner.find("war"), Rml::String::npos)
            << "Expected 'war' in .ek: " << ek_inner;
    } else {
        EXPECT_NE(inner.find("war"), Rml::String::npos)
            << "Expected 'war' in body inner RML: " << inner;
    }

    // Verify event tick 81 appears somewhere in the doc.
    Rml::Element* et_span = doc->QuerySelector(".et");
    if (et_span) {
        Rml::String et_inner = et_span->GetInnerRML();
        std::printf("[HudSmokeTest] S1b .et inner: %s\n", et_inner.c_str());
        EXPECT_NE(et_inner.find("81"), Rml::String::npos)
            << "Expected '81' in .et: " << et_inner;
    } else {
        EXPECT_NE(inner.find("81"), Rml::String::npos)
            << "Expected '81' in body inner RML: " << inner;
    }
}

// Verify HudFactionIn / HudEventIn + SetHudView API compile correctly (type check).
// This test exercises the public UIRenderNode API at compile time (linking into the
// RenderGraph library) without GPU — just calls the function; the node is not
// initialised so hudModel_ is null (DirtyVariable is safely guarded).
TEST_F(HudSmokeTest, S1b_SetHudViewApiCompiles) {
    // UIRenderNode requires a NodeType; skip construction but validate the types.
    using Faction = Vixen::RenderGraph::HudFactionIn;
    using Event   = Vixen::RenderGraph::HudEventIn;

    std::vector<Faction> fv = {{"Empire", 1.0f}, {"Resistance", 0.3f}};
    std::vector<Event>   ev = {{"war", 81}, {"trade", 42}};

    // Confirm span construction from vectors compiles.
    std::span<const Faction> fs(fv);
    std::span<const Event>   es(ev);

    EXPECT_EQ(fs.size(), 2u);
    EXPECT_EQ(es.size(), 2u);
    EXPECT_STREQ(fs[0].name, "Empire");
    EXPECT_FLOAT_EQ(fs[0].grievance, 1.0f);
    EXPECT_STREQ(es[0].kind, "war");
    EXPECT_EQ(es[0].tick, 81);
}

// Verify SetHudView copies the inspect row into the Rml-owned mirror (and clears it on
// selected=false). No GPU/context init needed — SetHudView's inspect copy is pure data.
TEST(UIRenderNodeInspect, SetHudViewCopiesInspectRow) {
    Vixen::RenderGraph::UIRenderNodeType type;
    Vixen::RenderGraph::UIRenderNode node("inspect_test", &type);

    Vixen::RenderGraph::HudInspectIn in{true, "Ceres Combine", 3.5f, -1.0f, "", 0.0f, "at war with X"};
    node.SetHudView(0, 0, 0, 0, {}, {}, in);
    const auto& insp = node.InspectForTest();
    EXPECT_TRUE(insp.selected);
    EXPECT_EQ(insp.name, "Ceres Combine");
    EXPECT_FLOAT_EQ(insp.strength, -1.0f);
    EXPECT_EQ(insp.cause, "at war with X");

    Vixen::RenderGraph::HudInspectIn cleared{false, "", 0.0f, 0.0f, "", 0.0f, ""};
    node.SetHudView(0, 0, 0, 0, {}, {}, cleared);
    EXPECT_FALSE(node.InspectForTest().selected);
}

// Verify SetDebugText / SetDebugOverlayVisible are safe no-ops before the document exists (spec
// 2026-07-05 f12-debug-overlay): the node is never Compile()'d here, so document_ stays null —
// exactly the state these methods must tolerate for an older/modded HUD with no #debug-overlay.
TEST(UIRenderNodeDebugOverlay, SetDebugTextAndVisibleNoopWithoutDocument) {
    Vixen::RenderGraph::UIRenderNodeType type;
    Vixen::RenderGraph::UIRenderNode node("debug_overlay_test", &type);

    // Must not crash with no document at all.
    node.SetDebugText("total=12.3ms sim=0.1ms update=0.2ms render=12.0ms fps=81.3");
    node.SetDebugOverlayVisible(true);
    node.SetDebugOverlayVisible(false);
}
