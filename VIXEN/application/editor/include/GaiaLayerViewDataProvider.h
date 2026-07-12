#pragma once
// View<->Model Binding Inc-B (View-Model-Binding-Inc-B-Plan-2026-07.md): the Gaia-backed
// IViewDataProvider for the editor layer datum -- swaps LayerControllerViewDataProvider's direct
// LayerController field access for a real Gaia component (LayerMask, VoxelComponents.h), so an
// ordinary WriteU32 goes through GaiaVoxelWorld::setComponent<LayerMask> (auto version-bump +
// func_set hook, design §5) instead of a bare field write. This is what makes the per-frame
// .changed<LayerMask>() reconcile (EditorApplication::ReconcileLayersView) meaningful -- there is
// now a real ECS write for it to observe.
//
// TU placement: this header includes gaia.h (via GaiaVoxelWorld.h) but NOT any RmlUi data-model
// header, so it is safe in EditorApplication.cpp -- the same TU that already includes
// Recipe/RecipeBaker.h (-> gaia.h) AND Nodes/UIRenderNode.h (RmlUi's DataModelConstructor, but not
// the inline data-model TEMPLATE instantiation that only happens where EditorLayersView.h's
// generated Bind*/struct code is compiled). See EditorLayersViewBridge.h's file header for the
// exact ODR hazard this line avoids crossing.
#include "IViewDataProvider.h"
#include "GaiaVoxelWorld.h"
#include "VoxelComponents.h"

namespace Vixen::App {

// Creates the editor's one layer-mask entity: a bare entity (no MortonKey/spatial identity --
// unlike GaiaVoxelWorld::createVoxel's voxel entities) carrying only a LayerMask component,
// seeded to the all-enabled default (mirrors LayerController::SetLayerCount's own "all layers
// enabled" default -- see LayerController.h). LoadDocument re-seeds it to the real per-document
// mask via the provider's WriteU32 once the document's actual layer count is known.
inline Vixen::GaiaVoxel::GaiaVoxelWorld::EntityID MakeGaiaLayerEntity(Vixen::GaiaVoxel::GaiaVoxelWorld& world) {
    auto entity = world.getWorld().add();
    world.setComponent<Vixen::GaiaVoxel::LayerMask>(entity, 0xFFFFFFFFu);
    return entity;
}

// Owns no world -- binds to a GaiaVoxelWorld + one entity supplied by the caller (the editor's
// single document-layer-mask entity today; a future multi-instance provider would resolve
// ViewNounKey::instance to an entity instead of ignoring it, same seam LayerControllerViewDataProvider
// documents).
class GaiaLayerViewDataProvider final : public Vixen::AppFlow::IViewDataProvider {
public:
    GaiaLayerViewDataProvider(Vixen::GaiaVoxel::GaiaVoxelWorld& world, Vixen::GaiaVoxel::GaiaVoxelWorld::EntityID entity)
        : world_(world), entity_(entity) {}

    bool ReadU32(Vixen::AppFlow::ViewNounKey key, uint32_t& out) const override {
        if (key.noun != Vixen::AppFlow::ViewNounId::LayerMask) return false;
        auto value = world_.getComponentValue<Vixen::GaiaVoxel::LayerMask>(entity_);
        if (!value.has_value()) return false;  // fallible: entity valid but component absent
        out = *value;
        return true;
    }

    void WriteU32(Vixen::AppFlow::ViewNounKey key, uint32_t value) override {
        if (key.noun != Vixen::AppFlow::ViewNounId::LayerMask) return;
        // setComponent<T> -> world.add<T>(id, T{value}) -- Gaia's add() overwrites an existing
        // component AS an ordinary mutable write, so it auto-bumps the component's chunk version
        // and fires the func_set hook (design §3/§5) exactly like a direct set<T> would. This is
        // the write half of the loop the .changed<LayerMask>() reconcile closes.
        world_.setComponent<Vixen::GaiaVoxel::LayerMask>(entity_, value);
    }

private:
    Vixen::GaiaVoxel::GaiaVoxelWorld& world_;
    Vixen::GaiaVoxel::GaiaVoxelWorld::EntityID entity_;
};

}  // namespace Vixen::App
