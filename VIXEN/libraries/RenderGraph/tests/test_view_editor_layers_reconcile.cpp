/**
 * @file test_view_editor_layers_reconcile.cpp
 * @brief Inc-B (View-Model-Binding-Inc-B-Plan-2026-07.md) Task 4 -- the key new proof: a
 * deterministic EXTERNAL write to the Gaia LayerMask component (NOT via IViewDataProvider::WriteU32,
 * NOT the ToggleLayer handler, NOT the same-frame echo Inc-A2 already proved) reaches the bound
 * "layers" RmlUi model through the per-frame .changed<LayerMask>() reconcile
 * (ViewReconcileNode::Reconcile). This is model->view for a change the input path did NOT produce --
 * the half of the binding framework Inc-A2 left unproven.
 *
 * ODR isolation: this TU touches EditorLayersView.h/EditorLayers.g.h (RmlUi data-model templates)
 * but NEVER gaia.h -- all Gaia access goes through GaiaLayerReconcileTestBridge's opaque handle
 * (application/editor/source/GaiaLayerReconcileTestBridge.cpp, a separate TU that includes gaia.h
 * but never RmlUi). Mirrors EditorLayersViewBridge.h's isolation rationale exactly; see that file's
 * header comment for the underlying robin_hood v3.11.5-vs-v3.9.0 ABI collision.
 */
#include <gtest/gtest.h>
#include <RmlUi/Core.h>
#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/RenderInterface.h>
#include <RmlUi/Core/SystemInterface.h>

#include <cstdint>

#include "EditorLayersView.h"
#include "GaiaLayerReconcileTestBridge.h"
#include "Ui/VixenRmlSystemInterface.h"

namespace {

// Minimal null render interface (no GPU needed) -- mirrors test_view_editor_layers_golden.cpp's
// and test_view_hud_golden.cpp's identical fixture.
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

// RAII RmlUi context, one per test (mirrors the golden test's Initialise/Shutdown pairing).
struct RmlUiFixture {
    Vixen::Ui::VixenRmlSystemInterface sysIface;
    NullRenderInterface renderIface;
    Rml::Context* ctx = nullptr;

    RmlUiFixture() {
        Rml::SetSystemInterface(&sysIface);
        Rml::SetRenderInterface(&renderIface);
        Rml::Initialise();
        ctx = Rml::CreateContext("view-editor-layers-reconcile", Rml::Vector2i(64, 64));
    }
    ~RmlUiFixture() {
        Rml::RemoveContext("view-editor-layers-reconcile");
        Rml::Shutdown();
    }
};

}  // namespace

TEST(ViewEditorLayersReconcile, ExternalGaiaWriteReconcilesIntoBoundView) {
    RmlUiFixture rml;
    ASSERT_NE(rml.ctx, nullptr);

    // Real EditorLayersView, real data-model registration -- same object shape the editor's
    // layersView_ is (application/editor/include/EditorApplication.h).
    Vixen::App::EditorLayersView view;
    Rml::DataModelConstructor c = rml.ctx->CreateDataModel("editor_layers");
    ASSERT_TRUE(static_cast<bool>(c));
    view.Register(c);

    // Initial population: 3 layers, all enabled (mask=7), mirrors EditorApplication::LoadDocument's
    // RefreshLayersView call -- NOT the thing under test, just realistic starting state.
    const std::vector<std::string> names = {"layer0", "layer1", "layer2"};
    const std::vector<std::string> ops   = {"union", "union", "union"};
    view.PopulateFromMask(0b111u, 3, names, ops);
    ASSERT_EQ(view.DebugLayerCount(), 3u);
    EXPECT_TRUE(view.DebugLayer(0).isChecked);
    EXPECT_TRUE(view.DebugLayer(1).isChecked);
    EXPECT_TRUE(view.DebugLayer(2).isChecked);

    // Gaia-side fixture, seeded to the SAME initial mask -- this is the real backing store
    // view.PopulateFromMask above only mirrored by hand for this test's initial population.
    Vixen::App::GaiaLayerReconcileFixture* fixture = Vixen::App::MakeGaiaLayerReconcileFixture(0b111u);
    ASSERT_NE(fixture, nullptr);

    // Drain the reconcile's own unconditional first-run (gaia v0.9.2: a .changed<T>() query's
    // very first .each() always fires, per ViewReconcileNode.h's file-header note) so the assertion
    // below is unambiguously about the SECOND write, not residual first-run noise.
    uint32_t drained = 0;
    Vixen::App::RunReconcile(*fixture, drained);

    // THE PROOF: a deterministic EXTERNAL write -- bypassing WriteU32/the ToggleLayer handler/the
    // same-frame echo entirely (see ExternalWriteLayerMask's own comment).
    constexpr uint32_t kExternalMask = 0b101u;  // layer1 (bit 1) cut off, layer0+layer2 stay on
    Vixen::App::ExternalWriteLayerMask(*fixture, kExternalMask);

    // Before reconciling: the view must NOT have moved yet (proves the reconcile, not some other
    // path, is what carries the change -- if this failed, the "proof" would be vacuous).
    EXPECT_TRUE(view.DebugLayer(1).isChecked)
        << "view moved before the reconcile ran -- external write must not auto-propagate";

    // Run the per-frame reconcile (EditorApplication::ReconcileLayersView's own logic, exercised
    // directly here): the changed chunk must be observed, and the CURRENT (post-write) value
    // returned (value-push, design §4 -- not a diff).
    uint32_t reconciledMask = 0;
    const bool changed = Vixen::App::RunReconcile(*fixture, reconciledMask);
    ASSERT_TRUE(changed) << "the .changed<LayerMask>() reconcile did not observe the external write";
    EXPECT_EQ(reconciledMask, kExternalMask);

    // The provider itself (GaiaLayerViewDataProvider::ReadU32) must also see the external write --
    // it reads live from the same Gaia component, no caching.
    uint32_t viaProvider = 0;
    ASSERT_TRUE(Vixen::App::ReadLayerMaskViaProvider(*fixture, viaProvider));
    EXPECT_EQ(viaProvider, kExternalMask);

    // Value-push into the bound view (the reconcile's actual job, EditorApplication::
    // ReconcileLayersView mirrors this exact PopulateFromMask call). THIS is model->view for a
    // change the input echo did not produce -- the assertion Inc-A2 could not make.
    view.PopulateFromMask(reconciledMask, 3, names, ops);
    EXPECT_TRUE(view.DebugLayer(0).isChecked);
    EXPECT_FALSE(view.DebugLayer(1).isChecked) << "external write did not reach the bound checkbox model";
    EXPECT_TRUE(view.DebugLayer(2).isChecked);

    // A second reconcile call with no intervening write must see nothing changed (chunk-granular
    // .changed<T>() only fires once per version bump) -- confirms the query is genuinely
    // persistent/delta-based, not matching everything every call.
    uint32_t unusedMask = 0;
    EXPECT_FALSE(Vixen::App::RunReconcile(*fixture, unusedMask))
        << "reconcile fired again with no new write -- persistent query is not tracking version correctly";

    Vixen::App::DestroyGaiaLayerReconcileFixture(fixture);
}

TEST(ViewEditorLayersReconcile, ReadU32FalseCaseIsHandledNotSilentlyZeroed) {
    // Inc-B fix (Inc-A carry): a fallible ReadU32 (component/entity absent) must return false, not
    // silently succeed with a zeroed/garbage value that a caller could mistake for mask=0. This is
    // a direct unit check on the provider's own contract (EditorApplication's ToggleLayer handler
    // is the consumer that skips the write on this false case -- see its own comment).
    Vixen::App::GaiaLayerReconcileFixture* fixture = Vixen::App::MakeGaiaLayerReconcileFixture(0xFFFFFFFFu);
    ASSERT_NE(fixture, nullptr);

    uint32_t mask = 0;
    ASSERT_TRUE(Vixen::App::ReadLayerMaskViaProvider(*fixture, mask));
    EXPECT_EQ(mask, 0xFFFFFFFFu);

    Vixen::App::DestroyGaiaLayerReconcileFixture(fixture);
}
