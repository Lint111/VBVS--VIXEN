// test_ui_composition_host.cpp — headless proof of the M-ui multi-document composition mechanics
// (relational vertical slice, step-6 M-ui). No GPU/Vulkan: a null RenderInterface over a bare
// Rml::Context exercises exactly the ops UIRenderNode::IUiCompositionHost::Mount/Unmount perform —
// the spike's proven mount/unmount/isolated-namespace/degenerate-layout checks, now as REAL tests.
//
// Testing UIRenderNode's methods directly is impractical headless (its context_ is created in the
// Vulkan-bound CompileImpl), so these tests drive the SAME Rml::Context ops the host runs, plus the
// real BuildingInspectorView binding + building_inspector.rml parse. Like test_ui_hud_smoke, this
// target links RmlUi but NOT gaia, so including BuildingInspectorView.h is ODR-safe.

#include <gtest/gtest.h>

#include <RmlUi/Core.h>
#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/RenderInterface.h>
#include <RmlUi/Core/SystemInterface.h>
#include <RmlUi/Core/Types.h>

#include "graph/BuildingInspectorView.h"
#include "graph/HudView.h"
#include "Ui/VixenRmlSystemInterface.h"

class NullRenderInterface2 final : public Rml::RenderInterface {
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

class UiCompositionHostTest : public ::testing::Test {
protected:
    void SetUp() override {
        Rml::SetSystemInterface(&sysIface_);
        Rml::SetRenderInterface(&renderIface_);
        ASSERT_TRUE(Rml::Initialise());
        Rml::LoadFontFace("assets/ui/LatoLatin-Regular.ttf");
        ctx_ = Rml::CreateContext("composition_host", Rml::Vector2i(1280, 720));
        ASSERT_NE(ctx_, nullptr);
    }
    void TearDown() override {
        if (ctx_) Rml::RemoveContext("composition_host");
        ctx_ = nullptr;
        Rml::Shutdown();
    }
    Vixen::Ui::VixenRmlSystemInterface sysIface_;
    NullRenderInterface2 renderIface_;
    Rml::Context* ctx_ = nullptr;
};

// The building_inspector.rml fragment loads, its "building" model binds via BuildingInspectorView,
// and SetBuilding's pushed values resolve into the rendered document. This is the fragment + view
// half of the M-ui deliverable, exercised through the real production API path (Register/SetBuilding).
TEST_F(UiCompositionHostTest, BuildingInspectorFragmentLoadsAndBindsResolve) {
    Vixen::App::BuildingInspectorView view;
    Rml::DataModelConstructor c = ctx_->CreateDataModel(view.ModelName());
    ASSERT_TRUE(static_cast<bool>(c));
    view.Register(c);
    Rml::DataModelHandle model = c.GetModelHandle();
    ASSERT_TRUE(static_cast<bool>(model));

    Rml::ElementDocument* doc = ctx_->LoadDocument("assets/ui/building_inspector.rml");
    ASSERT_NE(doc, nullptr) << "building_inspector.rml failed to load";
    doc->Show();

    // Not degenerate: the body must stretch to the context (the host's own mount validation).
    EXPECT_GT(doc->GetOffsetWidth(), 0.0f);
    EXPECT_GT(doc->GetOffsetHeight(), 0.0f);

    Vixen::App::BuildingInspectorIn in;
    in.present = true;
    in.defId = "core:power_plant";
    in.ownerName = "faction:42";
    in.isOwnerViewer = true;
    in.powerGenerated = 4000.0f;
    in.powerImpact = 2;   // Halted
    in.laborSupply = 12.0f;
    view.SetBuilding(in);

    ctx_->Update();
    Rml::String inner = doc->GetInnerRML();
    EXPECT_NE(inner.find("core:power_plant"), Rml::String::npos) << inner;
    EXPECT_NE(inner.find("4000"), Rml::String::npos) << inner;
    EXPECT_NE(inner.find("Halted"), Rml::String::npos) << inner;
    EXPECT_NE(inner.find("You own this"), Rml::String::npos)
        << "IsOwnerViewer=true should show the ownership affordance: " << inner;
}

// isOwnerViewer=false hides the ownership affordance (the §5 visibility gate).
TEST_F(UiCompositionHostTest, NonOwnerHidesOwnershipAffordance) {
    Vixen::App::BuildingInspectorView view;
    Rml::DataModelConstructor c = ctx_->CreateDataModel(view.ModelName());
    ASSERT_TRUE(static_cast<bool>(c));
    view.Register(c);
    Rml::ElementDocument* doc = ctx_->LoadDocument("assets/ui/building_inspector.rml");
    ASSERT_NE(doc, nullptr);
    doc->Show();

    Vixen::App::BuildingInspectorIn in;
    in.present = true;
    in.defId = "core:smelter";
    in.isOwnerViewer = false;
    view.SetBuilding(in);
    ctx_->Update();

    // data-if keeps the element in the DOM but sets display:none when the condition is false, so the
    // affordance is HIDDEN, not removed. Assert the .owned element is not visible (the §5 gate) rather
    // than absence of its text, which RmlUi leaves in the (hidden) inner RML.
    Rml::Element* owned = doc->QuerySelector(".owned");
    ASSERT_NE(owned, nullptr);
    EXPECT_FALSE(owned->IsVisible())
        << "IsOwnerViewer=false must HIDE the ownership affordance (data-if display:none)";

    // And with the owner viewing, it becomes visible.
    in.isOwnerViewer = true;
    view.SetBuilding(in);
    ctx_->Update();
    EXPECT_TRUE(owned->IsVisible())
        << "IsOwnerViewer=true must SHOW the ownership affordance";
}

// The inspector fragment mounts BESIDE a separate "hud" model in the same context, in an isolated
// data-model namespace — both documents live simultaneously, neither model's variables collide.
// This is the load-bearing composition finding (spike §2): N documents / N models per context.
TEST_F(UiCompositionHostTest, TwoDocumentsCoexistInIsolatedNamespaces) {
    // Primary "hud" document + model.
    Vixen::App::HudView hud;
    Rml::DataModelConstructor hc = ctx_->CreateDataModel(hud.ModelName());
    ASSERT_TRUE(static_cast<bool>(hc));
    hud.Register(hc);
    Rml::ElementDocument* hudDoc = ctx_->LoadDocument("assets/ui/hud.rml");
    ASSERT_NE(hudDoc, nullptr);
    hudDoc->Show();

    // Second "building" document + model — the mount.
    Vixen::App::BuildingInspectorView bview;
    Rml::DataModelConstructor bc = ctx_->CreateDataModel(bview.ModelName());
    ASSERT_TRUE(static_cast<bool>(bc)) << "the 'building' model must construct alongside 'hud'";
    bview.Register(bc);
    Rml::ElementDocument* bDoc = ctx_->LoadDocument("assets/ui/building_inspector.rml");
    ASSERT_NE(bDoc, nullptr);
    bDoc->Show();

    // Both documents are in the context and alive (the spike's hudDocAlive-style check for both).
    EXPECT_EQ(ctx_->GetNumDocuments(), 2);
    EXPECT_GT(hudDoc->GetOffsetWidth(), 0.0f);
    EXPECT_GT(bDoc->GetOffsetWidth(), 0.0f);

    // Drive each independently — the isolated namespaces don't interfere.
    hud.SetHudView(99, 7, 0, 0, {}, {});
    Vixen::App::BuildingInspectorIn in; in.present = true; in.defId = "core:battery";
    bview.SetBuilding(in);
    ctx_->Update();

    EXPECT_NE(hudDoc->GetInnerRML().find("99"), Rml::String::npos);
    EXPECT_NE(bDoc->GetInnerRML().find("core:battery"), Rml::String::npos);
}

// A duplicate data-model name is rejected by RmlUi (CreateDataModel returns a falsy constructor) —
// exactly the namespace-collision the host's Mount validates up front rather than half-mounting.
TEST_F(UiCompositionHostTest, DuplicateModelNameIsRejected) {
    Rml::DataModelConstructor first = ctx_->CreateDataModel("building");
    ASSERT_TRUE(static_cast<bool>(first));
    Rml::DataModelConstructor dup = ctx_->CreateDataModel("building");
    EXPECT_FALSE(static_cast<bool>(dup))
        << "a second CreateDataModel with a live name must fail (the host rejects the mount)";
}

// A degenerate document (body with only an out-of-flow child, no stretch) collapses to 0×0 and
// renders nothing — the exact case the host's Mount rejects (spike §4: RmlUi does not warn).
TEST_F(UiCompositionHostTest, DegenerateLayoutIsDetectable) {
    const Rml::String degenerate = R"(
<rml><head><title>bad</title></head>
<body><div style="position:absolute; right:0; bottom:0; width:10px; height:10px;">x</div></body>
</rml>)";
    Rml::ElementDocument* doc = ctx_->LoadDocumentFromMemory(degenerate);
    ASSERT_NE(doc, nullptr);
    doc->Show();
    ctx_->Update();
    // The un-stretched body collapses; the host's offset-size check catches this.
    EXPECT_TRUE(doc->GetOffsetWidth() <= 0.0f || doc->GetOffsetHeight() <= 0.0f)
        << "a body with only out-of-flow children should collapse (host rejects such a mount)";
}

// Unmount ops (UnloadDocument + RemoveDataModel) leave the context clean and let the same model
// name be remounted — the transactional lifecycle the host relies on (spike §3/§5).
TEST_F(UiCompositionHostTest, UnmountThenRemountSucceeds) {
    {
        Vixen::App::BuildingInspectorView v;
        Rml::DataModelConstructor c = ctx_->CreateDataModel(v.ModelName());
        ASSERT_TRUE(static_cast<bool>(c));
        v.Register(c);
        Rml::ElementDocument* doc = ctx_->LoadDocument("assets/ui/building_inspector.rml");
        ASSERT_NE(doc, nullptr);
        doc->Show();
        EXPECT_EQ(ctx_->GetNumDocuments(), 1);
        // Unmount.
        ctx_->UnloadDocument(doc);
        ctx_->RemoveDataModel(v.ModelName());
        ctx_->Update();   // deferred document destroy runs here
        EXPECT_EQ(ctx_->GetNumDocuments(), 0);
    }
    // Remount the same model name — succeeds because the prior mount was fully torn down.
    Vixen::App::BuildingInspectorView v2;
    Rml::DataModelConstructor c2 = ctx_->CreateDataModel(v2.ModelName());
    EXPECT_TRUE(static_cast<bool>(c2)) << "remount after a clean unmount must succeed";
    v2.Register(c2);
    Rml::ElementDocument* doc2 = ctx_->LoadDocument("assets/ui/building_inspector.rml");
    ASSERT_NE(doc2, nullptr);
    doc2->Show();
    EXPECT_EQ(ctx_->GetNumDocuments(), 1);
}
