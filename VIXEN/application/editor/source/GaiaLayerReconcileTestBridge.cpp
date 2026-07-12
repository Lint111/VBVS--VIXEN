// Gaia-only side of the Inc-B external-change reconcile proof -- see the header's file comment
// for the ODR-isolation rationale. This TU includes gaia.h (via GaiaVoxelWorld.h/
// GaiaLayerViewDataProvider.h/ViewReconcileNode.h) but NEVER RmlUi -- the test TU is the mirror.
#include "GaiaLayerReconcileTestBridge.h"
#include "GaiaLayerViewDataProvider.h"
#include "ViewReconcileNode.h"

namespace Vixen::App {

struct GaiaLayerReconcileFixture {
    Vixen::GaiaVoxel::GaiaVoxelWorld world;
    Vixen::GaiaVoxel::GaiaVoxelWorld::EntityID entity;
    GaiaLayerViewDataProvider provider;
    ViewReconcileNode reconcile;

    explicit GaiaLayerReconcileFixture(uint32_t initialMask)
        : entity(world.getWorld().add()),
          provider(world, entity),
          reconcile(world) {
        world.setComponent<Vixen::GaiaVoxel::LayerMask>(entity, initialMask);
    }
};

GaiaLayerReconcileFixture* MakeGaiaLayerReconcileFixture(uint32_t initialMask) {
    return new GaiaLayerReconcileFixture(initialMask);
}

void DestroyGaiaLayerReconcileFixture(GaiaLayerReconcileFixture* fixture) {
    delete fixture;
}

void ExternalWriteLayerMask(GaiaLayerReconcileFixture& fixture, uint32_t value) {
    // Direct world.setComponent -- NOT fixture.provider.WriteU32. This is the point: an external
    // mutation that never touches the view->model seam, exactly as a future simulation/sync system
    // writing the same component would (design §4's "the write auto-feeds change detection" event
    // chain works for ANY writer, not just the provider).
    fixture.world.setComponent<Vixen::GaiaVoxel::LayerMask>(fixture.entity, value);
}

bool RunReconcile(GaiaLayerReconcileFixture& fixture, uint32_t& outValue) {
    const auto reconciled = fixture.reconcile.Reconcile(fixture.entity);
    if (!reconciled.has_value()) return false;
    outValue = *reconciled;
    return true;
}

bool ReadLayerMaskViaProvider(const GaiaLayerReconcileFixture& fixture, uint32_t& outValue) {
    using Vixen::AppFlow::ViewNounKey;
    using Vixen::AppFlow::ViewNounId;
    return fixture.provider.ReadU32(ViewNounKey{ViewNounId::LayerMask}, outValue);
}

}  // namespace Vixen::App
