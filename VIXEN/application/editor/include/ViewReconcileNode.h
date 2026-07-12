#pragma once
// View<->Model Binding Inc-B (View-Model-Binding-Inc-B-Plan-2026-07.md, design §4/§4a/§4b): the
// per-frame Gaia .changed<T>() reconcile -- model->view for changes NOT driven by the editor's own
// input (Inc-A2's same-frame echo already covers the input case). Runs a PERSISTENT query so
// .changed<LayerMask>() actually reflects delta-since-last-run rather than matching everything (a
// fresh query has version 0, per the plan's Gaia v0.9.2 facts). VALUE-PUSH (design §4): no
// per-entity diff cache -- a changed chunk re-pushes the bound entity's current value, which is
// idempotent and harmless to run redundantly after the same-frame echo (DirtyVariable coalesces).
//
// This header is gaia-only (no RmlUi) so it is safe in EditorApplication.cpp -- ReconcileLayersView
// returns "did the bound value change" as a plain bool/uint32_t; the caller (EditorApplication,
// through the EditorLayersViewBridge) is the one that turns that into a DirtyVariable call on the
// RmlUi side, keeping the ODR isolation the bridge already establishes (see
// EditorLayersViewBridge.h's file header).
#include "GaiaVoxelWorld.h"
#include "VoxelComponents.h"
#include <optional>

namespace Vixen::App {

// Owns the persistent .changed<LayerMask>() query for ONE bound entity (the editor's single
// document-layer-mask entity today -- a future multi-instance reconcile would iterate the query's
// chunks/entities instead of checking one id, same seam ViewNounKey::instance documents elsewhere).
class ViewReconcileNode {
public:
    explicit ViewReconcileNode(Vixen::GaiaVoxel::GaiaVoxelWorld& world)
        : world_(world),
          // .all<T>() IMMUTABLE read (design §4b): a mutable .all<T&>() + a const each() trips
          // v0.9.2's hard query-constness GAIA_ASSERT. Persistent (member, not re-created per
          // call) so its last-seen world version carries frame to frame -- a fresh query object
          // has version 0 and would match everything on every call (verified against gaia
          // v0.9.2's own test: "Query Filter - no systems", src/test/src/main.cpp).
          query_(world.getWorld().query().all<Vixen::GaiaVoxel::LayerMask>().changed<Vixen::GaiaVoxel::LayerMask>()) {}

    // Runs the query for this frame. If the bound entity's LayerMask chunk was marked changed
    // since the last call, re-reads the CURRENT value (value-push, not diff) and returns it;
    // returns nullopt if nothing changed (nothing to reconcile this frame) or the entity/component
    // is absent (mirrors the provider's own fallible ReadU32 contract).
    //
    // NOTE (gaia v0.9.2 quirk, verified against the vendored test suite): a .changed<T>() query's
    // VERY FIRST .each() call always fires (its stored last-seen version starts at 0), even though
    // this reconcile is constructed after the entity's initial LayerMask write. That first fire is
    // harmless here -- value-push (design §4) means re-pushing the current (already-correct) value
    // is idempotent, and RmlUi's DirtyVariable coalescing absorbs the redundant call.
    std::optional<uint32_t> Reconcile(Vixen::GaiaVoxel::GaiaVoxelWorld::EntityID boundEntity) {
        bool changed = false;
        // Entity+component lambda (matches GaiaVoxelWorld.cpp's established query.each() usage).
        // Chunk-granular (design §3): a single-entity world's one chunk is either wholly changed
        // or not, so no additional per-entity filtering is needed -- the equality check below just
        // confirms boundEntity is (still) the entity this reconcile is bound to.
        query_.each([&](gaia::ecs::Entity entity, const Vixen::GaiaVoxel::LayerMask&) {
            if (entity == boundEntity) changed = true;
        });
        if (!changed) return std::nullopt;
        return world_.getComponentValue<Vixen::GaiaVoxel::LayerMask>(boundEntity);
    }

private:
    Vixen::GaiaVoxel::GaiaVoxelWorld& world_;
    gaia::ecs::Query query_;
};

}  // namespace Vixen::App
