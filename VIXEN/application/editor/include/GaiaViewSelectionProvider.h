#pragma once
// View<->Model Binding Inc-C (View-Model-Binding-Inc-C-Plan-2026-07.md): the Gaia-backed
// IViewSelectionProvider, wrapping a persistent query().all<Selected>() over the committed-
// selection tag component (SelectionComponents.h). Sibling to GaiaLayerViewDataProvider.h -- same
// file-placement rationale (gaia.h include, no RmlUi, safe in EditorApplication.cpp's TU).
//
// ORDERING (plan Task 2's "verify Gaia v0.9.2's actual behavior, don't assume"): verified against
// the vendored test suite (src/test/src/main.cpp, e.g. the ~line 4470 query.arr(ents) block) --
// gaia::ecs::Query::arr() appends matching entities in the query's own chunk-walk order (archetype
// storage order, then slot order within a chunk), and that test relies on this order lining up
// index-for-index with the tracked creation-order entity list. Gaia stores entities of one
// archetype in contiguous per-archetype chunks (SoA); for entities only ADDED to an archetype
// (never removed/moved) that walk order is stable creation order -- the same "chunk-granular"
// assumption ViewReconcileNode.h already documents and relies on. This provider therefore
// guarantees: for a selection set that is only grown/replaced wholesale (never had entities
// removed mid-lifetime causing a chunk-slot swap/defrag), arr() order == the order entities most
// recently gained the Selected tag. Inc-C's proof (Task 4) only ever ADDS Selected to a fixed set
// of pre-existing entities and never removes it mid-test, so this ordering is exercised under
// exactly the condition it's guaranteed for. A future increment doing add/remove churn on
// `Selected` should re-verify (or add an explicit sort key) before relying on iteration order
// across removals -- noted as a follow-up, not resolved here (see the plan's "constraints node"
// open decision, §11, for the broader query-engine question).
#include "IViewSelectionProvider.h"
#include "SelectionComponents.h"
#include "GaiaVoxelWorld.h"

namespace Vixen::App {

// Owns a persistent query().all<Selected>() over the supplied world. Non-owning of the world
// (mirrors GaiaLayerViewDataProvider's "owns no world" contract).
class GaiaViewSelectionProvider final : public Vixen::AppFlow::IViewSelectionProvider {
public:
    explicit GaiaViewSelectionProvider(Vixen::GaiaVoxel::GaiaVoxelWorld& world)
        : world_(world),
          query_(world.getWorld().query().all<Selected>()) {}

    size_t ids(std::vector<Vixen::AppFlow::SelectionEntityID>& out) const override {
        out.clear();
        std::vector<gaia::ecs::Entity> matched;
        // Query::arr() is non-const (it may lazily (re)fetch cached match info) -- const_cast is
        // safe here: it only mutates the query's internal cache, not any entity/component data,
        // matching ViewReconcileNode.h's own const-query-object note about v0.9.2's query API.
        const_cast<gaia::ecs::Query&>(query_).arr(matched);
        out.reserve(matched.size());
        for (auto entity : matched) out.push_back(EntityToSelectionId(entity));
        return out.size();
    }

    bool at(size_t index, Vixen::AppFlow::SelectionEntityID& out) const override {
        std::vector<Vixen::AppFlow::SelectionEntityID> all;
        ids(all);
        if (index >= all.size()) return false;
        out = all[index];
        return true;
    }

    // Resolves a selection id (as yielded by ids()/at()) back to a Gaia EntityID -- the "Nth
    // selected instance" resolution Task 3 wires into the existing IViewDataProvider machinery.
    // gaia::ecs::Entity (gaia/ecs/id.h) is a single 64-bit `Identifier` union with a public
    // value()/Entity(Identifier) round-trip constructor -- verified against the vendored header,
    // not assumed -- so this is a lossless, version-stable conversion (no hand-packed id/gen bits).
    static Vixen::GaiaVoxel::GaiaVoxelWorld::EntityID SelectionIdToEntity(Vixen::AppFlow::SelectionEntityID id) {
        return Vixen::GaiaVoxel::GaiaVoxelWorld::EntityID(static_cast<gaia::ecs::Identifier>(id));
    }

    static Vixen::AppFlow::SelectionEntityID EntityToSelectionId(Vixen::GaiaVoxel::GaiaVoxelWorld::EntityID entity) {
        return static_cast<Vixen::AppFlow::SelectionEntityID>(entity.value());
    }

private:
    Vixen::GaiaVoxel::GaiaVoxelWorld& world_;
    gaia::ecs::Query query_;
};

}  // namespace Vixen::App
