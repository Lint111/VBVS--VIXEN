/**
 * @file test_set_mutation_dispatch.cpp
 * @brief Inc-D (View-Model-Binding-Inc-D-Plan-2026-07.md) Milestone 1 -- smoke proof that the
 * forward set-mutation dispatch (SetMutationDispatch.h) writes to EXACTLY the entities a committed
 * selection subset yields, through the existing (unchanged) Inc-B/C provider chain, wrapped as one
 * ActionStack group. Mirrors test_view_selection_provider.cpp's 3-entity/subset fixture so a wrong-
 * entity bug shows up as a wrong VALUE, not just a wrong bool.
 *
 * Scope: Milestone 1 (Tasks 1-2) only -- forward dispatch + non-vacuous entity-set proof. Undo/
 * redo restore logic (Task 3) and the full proof suite incl. dead-entity-at-undo skip (Task 4) are
 * explicitly Milestone 2's scope, not asserted here.
 */
#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

#include "ActionStack.h"
#include "generated/AppFlow.g.h"
#include "GaiaVoxelWorld.h"
#include "VoxelComponents.h"
#include "SelectionComponents.h"
#include "GaiaViewSelectionProvider.h"
#include "SetMutationDispatch.h"

using Vixen::AppFlow::ActionStack;
using Vixen::AppFlow::Generated::AppFlowContainerView;
using Vixen::AppFlow::DispatchResult;

TEST(SetMutationDispatch, WritesExactlyTheSelectedSubsetNotTheWholeWorld) {
    Vixen::GaiaVoxel::GaiaVoxelWorld world;

    // 3 entities, distinct LayerMask values -- a wrong-entity write would show up as a wrong value.
    auto e0 = world.getWorld().add();
    world.setComponent<Vixen::GaiaVoxel::LayerMask>(e0, 0x1u);
    auto e1 = world.getWorld().add();
    world.setComponent<Vixen::GaiaVoxel::LayerMask>(e1, 0x2u);
    auto e2 = world.getWorld().add();
    world.setComponent<Vixen::GaiaVoxel::LayerMask>(e2, 0x3u);

    // Commit a genuine SUBSET: e0 and e2, NOT e1.
    world.getWorld().add<Vixen::App::Selected>(e0);
    world.getWorld().add<Vixen::App::Selected>(e2);

    Vixen::App::GaiaViewSelectionProvider selection(world);

    ActionStack stack;
    stack.LoadActions(AppFlowContainerView::actions().data(), AppFlowContainerView::actions().size());

    const auto result = Vixen::App::DispatchSetMutation(stack, world, selection, 0x9u);

    EXPECT_EQ(result.selectedCount, 2u) << "expected the committed subset (2 of 3)";
    EXPECT_EQ(result.writtenCount, 2u) << "expected both live selected entities to be written";

    // Both selected entities changed to the new value.
    auto e0Value = world.getComponentValue<Vixen::GaiaVoxel::LayerMask>(e0);
    ASSERT_TRUE(e0Value.has_value());
    EXPECT_EQ(*e0Value, 0x9u) << "selected entity e0 was not written";

    auto e2Value = world.getComponentValue<Vixen::GaiaVoxel::LayerMask>(e2);
    ASSERT_TRUE(e2Value.has_value());
    EXPECT_EQ(*e2Value, 0x9u) << "selected entity e2 was not written";

    // The UNSELECTED sibling is untouched -- proves the action didn't silently touch the whole
    // world.
    auto e1Value = world.getComponentValue<Vixen::GaiaVoxel::LayerMask>(e1);
    ASSERT_TRUE(e1Value.has_value());
    EXPECT_EQ(*e1Value, 0x2u) << "unselected entity e1 was incorrectly perturbed";

    // The whole set was recorded as ONE undoable group (ActionStack's existing group semantics --
    // ONE UndoDepth entry covers both entities' writes, per test_action_stack.cpp's
    // GroupUndoneAsOneUnit precedent). Full undo/redo restore correctness is Milestone 2 (Task 3/4);
    // this only proves the dispatch-side group shape is what Task 1 decided.
    EXPECT_EQ(stack.UndoDepth(), 1u) << "expected the whole selection to be recorded as ONE group";
}

TEST(SetMutationDispatch, DirectListProviderWithoutLayerMaskComponentIsSkippedNotAsserted) {
    // NOTE: this is NOT the dead-entity-BETWEEN-dispatch-and-undo hazard the plan flags as the
    // increment's hardest correctness bar -- that requires Undo()/Redo() over a captured snapshot,
    // which is Milestone 2 (Task 3/4) scope. This only proves DispatchSetMutation() itself fails
    // closed (skip, no crash/assert) when a selected id resolves to an entity with no LayerMask
    // component to mutate -- exercising the `!priorValueOpt.has_value()` guard.
    Vixen::GaiaVoxel::GaiaVoxelWorld world;

    auto e0 = world.getWorld().add();
    world.setComponent<Vixen::GaiaVoxel::LayerMask>(e0, 0x1u);
    // e1 has no LayerMask component at all.
    auto e1 = world.getWorld().add();

    world.getWorld().add<Vixen::App::Selected>(e0);
    world.getWorld().add<Vixen::App::Selected>(e1);

    Vixen::App::GaiaViewSelectionProvider selection(world);
    ActionStack stack;
    stack.LoadActions(AppFlowContainerView::actions().data(), AppFlowContainerView::actions().size());

    const auto result = Vixen::App::DispatchSetMutation(stack, world, selection, 0x9u);

    EXPECT_EQ(result.selectedCount, 2u) << "both e0 and e1 are committed to Selected";
    EXPECT_EQ(result.writtenCount, 1u) << "only e0 has a LayerMask component to mutate";

    auto e0Value = world.getComponentValue<Vixen::GaiaVoxel::LayerMask>(e0);
    ASSERT_TRUE(e0Value.has_value());
    EXPECT_EQ(*e0Value, 0x9u) << "e0 (has LayerMask) was not written";

    EXPECT_FALSE(world.getComponentValue<Vixen::GaiaVoxel::LayerMask>(e1).has_value())
        << "e1 (no LayerMask) must not have gained one from the skip path";
}
