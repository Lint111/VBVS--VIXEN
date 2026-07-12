#pragma once
// View<->Model Binding Inc-B (View-Model-Binding-Inc-B-Plan-2026-07.md, Task 4): the gaia-side of
// the external-change reconcile proof, split out of test_view_editor_layers_reconcile.cpp per the
// SAME robin_hood ODR isolation the editor itself uses (EditorLayersViewBridge.h's file header) --
// the proof test needs BOTH a real Gaia world (this bridge) AND a real RmlUi data-model
// (EditorLayersView/EditorLayers.g.h, in the test's own TU), which is exactly the combination
// that is unsafe in one TU. All gaia access (world/entity/provider/reconcile construction, the
// DIRECT external write that bypasses WriteU32) stays on this side; the test TU never sees gaia.h.
#include <cstdint>
#include <memory>

namespace Vixen::App {

// Opaque handle -- the test TU manipulates the fixture only through the plain-uint32_t functions
// below, never touching GaiaVoxelWorld/LayerMask/ViewReconcileNode types directly.
struct GaiaLayerReconcileFixture;

// Constructs a fixture: a GaiaVoxelWorld, one layer entity (seeded to `initialMask`), a
// GaiaLayerViewDataProvider bound to it, and a ViewReconcileNode's persistent .changed<LayerMask>()
// query. Returns an owning raw pointer (mirrors MakeEditorLayersView's incomplete-type rationale --
// the test TU only ever sees the forward-declared struct).
GaiaLayerReconcileFixture* MakeGaiaLayerReconcileFixture(uint32_t initialMask);
void DestroyGaiaLayerReconcileFixture(GaiaLayerReconcileFixture* fixture);

// THE key proof primitive (Task 4): writes `value` to the fixture's LayerMask component DIRECTLY
// via GaiaVoxelWorld::setComponent -- bypassing GaiaLayerViewDataProvider::WriteU32 entirely, so
// this is NOT the input path (no ToggleLayer handler, no same-frame echo). This is the
// "deterministic external mutation" the plan's proof vehicle calls for.
void ExternalWriteLayerMask(GaiaLayerReconcileFixture& fixture, uint32_t value);

// Runs the fixture's ViewReconcileNode for one frame. Returns true + writes the reconciled value
// into `outValue` if the bound entity's LayerMask chunk was marked changed since the last call
// (i.e. an external write, or the query's own unconditional-first-run per gaia v0.9.2); returns
// false if nothing changed this frame.
bool RunReconcile(GaiaLayerReconcileFixture& fixture, uint32_t& outValue);

// Reads the current mask through the SAME provider path the editor's ToggleLayer handler uses
// (GaiaLayerViewDataProvider::ReadU32) -- lets the test assert the provider itself observes the
// external write, independent of the reconcile.
bool ReadLayerMaskViaProvider(const GaiaLayerReconcileFixture& fixture, uint32_t& outValue);

}  // namespace Vixen::App
