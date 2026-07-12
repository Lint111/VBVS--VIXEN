#pragma once
// View<->Model Binding Inc-D (View-Model-Binding-Inc-D-Plan-2026-07.md, Milestone 1 / Task 1-2):
// "hire 10 selected characters" -- one declared action, dispatched once, writes to every entity
// IViewSelectionProvider::ids() yields at dispatch time, undoable/redoable as one gesture.
//
// TASK 1 DECISION (documented here, not assumed): snapshot shape = option 2 -- compose N per-
// entity ActionStack::Dispatch() calls (NOT DispatchWithSnapshot) under one BeginGroup()/EndGroup()
// bracket. Verified against the REAL ActionStack::Undo()/Redo() group-iteration code
// (ActionStack.cpp): Undo() already pops one Group and runs every Entry's inverse in reverse order
// as a single atomic step (see the existing GroupUndoneAsOneUnit test, test_action_stack.cpp) --
// "the whole set undoes as one unit" falls out of the EXISTING group mechanism for free. Zero
// changes to ActionStack itself.
//
// DispatchWithSnapshot was rejected: its footprint is a raw void* that ActionStack memcpy's into
// directly on Undo(), with no liveness check -- safe only for a footprint the CALLER owns (a plain
// field), not a pointer into Gaia component storage (a dead/reused chunk slot would make that
// memcpy a real hazard, exactly the plan's flagged crux). GaiaLayerViewDataProvider never hands out
// such a pointer anyway -- ReadU32/WriteU32 go through GaiaVoxelWorld::getComponentValue/
// setComponent, which take entities BY VALUE and internally guard on getWorld().valid(id) (verified
// in GaiaVoxelWorld.h). So each per-entity apply(bool) lambda below captures (entity, priorValue,
// newValue) BY VALUE -- never a pointer -- and re-checks world.valid(entity) itself before writing,
// which is exactly where the dead-entity hazard needs to be resolved (real Gaia semantics, not
// assumed): Vixen::GaiaVoxel::GaiaVoxelWorld::getWorld() returns the underlying gaia::ecs::World,
// whose valid(Entity) is the confirmed liveness check (already used throughout GaiaArchetypes/
// ArchetypeBuilder.cpp, RelationshipObserver.cpp).
//
// DEAD-ENTITY OBSERVABILITY (Task 1 decision): a skipped-count return value on
// DispatchSetMutation() -- the smallest addition that makes a skip provable, with zero changes to
// ActionStack's Entry/DispatchResult shape. Undo()/Redo()'s OWN skip-counting (Milestone 2 / Task 3)
// is a separate, later concern -- this milestone only proves the forward dispatch touches exactly
// the selected entities via the existing provider chain, wrapped so the group undoes/redoes
// atomically; it does not implement the inverse/redo restore logic (Task 3) or the dead-entity-at-
// undo-time proof suite (Task 4) -- those are Milestone 2's scope.
#include <cstdint>
#include <utility>
#include <vector>

#include "ActionStack.h"
#include "IViewSelectionProvider.h"
#include "GaiaViewSelectionProvider.h"
#include "GaiaLayerViewDataProvider.h"
#include "GaiaVoxelWorld.h"
#include "generated/AppFlow.g.h"

namespace Vixen::App {

// Result of one set-mutation dispatch: how many of the selection's entities were actually written
// (dead entities at DISPATCH time are skipped the same way as at undo time -- world.valid() gates
// the write either way), out of how many the selection provider yielded.
struct SetMutationResult {
    size_t selectedCount = 0;  // IViewSelectionProvider::ids() count at dispatch time
    size_t writtenCount = 0;   // entities actually written (selectedCount - dead-at-dispatch skips)
};

// Applies one identity write (WriteU32 through GaiaLayerViewDataProvider, unchanged) to every
// entity the selection provider yields RIGHT NOW, as one ActionStack group -- Undo() reverts all
// of them as a single gesture, Redo() re-applies all of them, per ActionStack's existing group
// semantics (verified, not assumed -- see the file header). No projection/transform: `newValue` is
// written verbatim to every selected entity's LayerMask (this increment proves set/undo mechanics,
// not a new binding kind, per the plan's scope boundary).
//
// A selected entity that is already dead AT DISPATCH TIME is skipped (not written, not asserted on)
// -- same world.valid() gate WriteU32's own callee (GaiaVoxelWorld::setComponent) already applies
// internally; this function's own pre-check exists only so the per-entity ActionStack::Dispatch
// entry captures a CORRECT prior value (reading a dead entity's "prior value" is meaningless) and so
// writtenCount is accurate.
inline SetMutationResult DispatchSetMutation(Vixen::AppFlow::ActionStack& stack,
                                              Vixen::GaiaVoxel::GaiaVoxelWorld& world,
                                              const Vixen::AppFlow::IViewSelectionProvider& selection,
                                              uint32_t newValue) {
    using Vixen::AppFlow::Generated::FlowActionId;
    using Vixen::AppFlow::ViewNounId;
    using Vixen::AppFlow::ViewNounKey;

    SetMutationResult result;

    std::vector<Vixen::AppFlow::SelectionEntityID> ids;
    result.selectedCount = selection.ids(ids);

    stack.BeginGroup(0);
    for (const auto selId : ids) {
        const auto entity = GaiaViewSelectionProvider::SelectionIdToEntity(selId);
        if (!world.getWorld().valid(entity)) {
            continue;  // dead at dispatch time -- skip, don't fabricate a prior value
        }

        auto priorValueOpt = world.getComponentValue<Vixen::GaiaVoxel::LayerMask>(entity);
        if (!priorValueOpt.has_value()) {
            continue;  // no LayerMask component to mutate -- nothing to write or undo
        }
        const uint32_t priorValue = *priorValueOpt;

        // Captured BY VALUE (entity, priorValue, newValue) -- never a pointer into ECS storage.
        // Re-checks world.valid(entity) on every invocation (forward AND inverse) because this
        // same lambda is what Undo()/Redo() call later, when liveness may have changed.
        auto apply = [&world, entity, priorValue, newValue](bool forward) {
            if (!world.getWorld().valid(entity)) return;  // dead-entity skip+log is Task 3's full
                                                            // wiring; this guard is the mechanism
                                                            // Task 3 builds the observable skip atop.
            GaiaLayerViewDataProvider provider(world, entity);
            provider.WriteU32(ViewNounKey{ViewNounId::LayerMask, 0}, forward ? newValue : priorValue);
        };

        // ActionStack::Dispatch() itself invokes apply(true) exactly once (ActionStack.cpp) --
        // do not pre-apply here, that would double-write.
        stack.Dispatch(FlowActionId::ToggleLayer, std::move(apply));
        ++result.writtenCount;
    }
    stack.EndGroup();

    return result;
}

}  // namespace Vixen::App
