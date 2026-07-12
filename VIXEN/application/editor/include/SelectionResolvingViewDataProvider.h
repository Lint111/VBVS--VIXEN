#pragma once
// View<->Model Binding Inc-C (View-Model-Binding-Inc-C-Plan-2026-07.md, Task 3): thin wiring that
// resolves "the Nth selected instance" (via IViewSelectionProvider::at(index)) to a concrete Gaia
// entity, then routes ReadU32/WriteU32 to that entity through the EXISTING, UNCHANGED
// GaiaLayerViewDataProvider machinery from Inc-B. This class adds NO new read/write logic of its
// own -- it only resolves WHICH entity a ViewNounKey::instance means before delegating, exactly as
// the plan's Task 3 scopes it ("primarily WIRING, not new machinery").
//
// ViewNounKey::instance (IViewDataProvider.h) is repurposed here as a selection INDEX (not a raw
// entity handle) -- key.instance == 0 means "selection index 0", resolved through
// IViewSelectionProvider::at(0). This mirrors design §6's "instance is the provider-interpreted
// handle" contract: different providers are free to interpret the same opaque uint64_t
// differently (GaiaLayerViewDataProvider ignores it; this provider treats it as an index).
#include "IViewDataProvider.h"
#include "IViewSelectionProvider.h"
#include "GaiaViewSelectionProvider.h"
#include "GaiaLayerViewDataProvider.h"
#include "GaiaVoxelWorld.h"

namespace Vixen::App {

// Delegates every ReadU32/WriteU32 to a GaiaLayerViewDataProvider-shaped call against whichever
// entity IViewSelectionProvider::at(key.instance) resolves to. Does not own the selection provider,
// the world, or construct per-entity providers up front -- entity resolution + a fresh throwaway
// GaiaLayerViewDataProvider-equivalent read/write happens per call, same cost shape as
// GaiaLayerViewDataProvider's own getComponentValue/setComponent calls (no caching to invalidate).
class SelectionResolvingViewDataProvider final : public Vixen::AppFlow::IViewDataProvider {
public:
    SelectionResolvingViewDataProvider(Vixen::GaiaVoxel::GaiaVoxelWorld& world,
                                        const Vixen::AppFlow::IViewSelectionProvider& selection)
        : world_(world), selection_(selection) {}

    bool ReadU32(Vixen::AppFlow::ViewNounKey key, uint32_t& out) const override {
        Vixen::GaiaVoxel::GaiaVoxelWorld::EntityID entity;
        if (!ResolveEntity(key.instance, entity)) return false;
        // Delegates to the SAME provider Inc-B ships -- no duplicated read logic (Task 3's "keep
        // this thin" requirement).
        GaiaLayerViewDataProvider provider(world_, entity);
        return provider.ReadU32(key, out);
    }

    void WriteU32(Vixen::AppFlow::ViewNounKey key, uint32_t value) override {
        Vixen::GaiaVoxel::GaiaVoxelWorld::EntityID entity;
        if (!ResolveEntity(key.instance, entity)) return;
        GaiaLayerViewDataProvider provider(world_, entity);
        provider.WriteU32(key, value);
    }

private:
    bool ResolveEntity(uint64_t selectionIndex, Vixen::GaiaVoxel::GaiaVoxelWorld::EntityID& outEntity) const {
        Vixen::AppFlow::SelectionEntityID selId;
        if (!selection_.at(static_cast<size_t>(selectionIndex), selId)) return false;
        outEntity = GaiaViewSelectionProvider::SelectionIdToEntity(selId);
        return true;
    }

    Vixen::GaiaVoxel::GaiaVoxelWorld& world_;
    const Vixen::AppFlow::IViewSelectionProvider& selection_;
};

}  // namespace Vixen::App
