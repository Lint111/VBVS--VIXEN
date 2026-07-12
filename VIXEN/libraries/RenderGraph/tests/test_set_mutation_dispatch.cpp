/**
 * @file test_set_mutation_dispatch.cpp
 * @brief Inc-D (View-Model-Binding-Inc-D-Plan-2026-07.md) Milestone 1 -- smoke proof that the
 * forward set-mutation dispatch (SetMutationDispatch.h) writes to EXACTLY the entities a committed
 * selection subset yields, through the existing (unchanged) Inc-B/C provider chain, wrapped as one
 * ActionStack group. Mirrors test_view_selection_provider.cpp's 3-entity/subset fixture so a wrong-
 * entity bug shows up as a wrong VALUE, not just a wrong bool.
 *
 * Milestone 1 scope (Tasks 1-2): forward dispatch + non-vacuous entity-set proof, above this
 * comment block's original two tests.
 *
 * Milestone 2 scope (Tasks 3-4, below): undo restores the captured snapshot exactly; redo re-
 * applies the captured forward values; the dead-entity-at-undo proof (destroy one touched entity
 * between dispatch and Undo(), assert no crash + correct partial restore + observable skip via
 * SetMutationSkipCounters, per SetMutationDispatch.h's Task 3 doc comment) -- the increment's
 * hardest correctness bar; and a live-query-independence proof that mutates the Selected tag set
 * itself between dispatch and Undo() and asserts Undo() still restores the ORIGINAL dispatch-time
 * entities.
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

// ---------------------------------------------------------------------------------------------
// Milestone 2 (Tasks 3-4): undo/redo over the captured snapshot, dead-entity-at-undo, and live-
// query independence.
// ---------------------------------------------------------------------------------------------

TEST(SetMutationDispatch, UndoRestoresExactPreDispatchValuesAndRedoReapplies) {
    Vixen::GaiaVoxel::GaiaVoxelWorld world;

    auto e0 = world.getWorld().add();
    world.setComponent<Vixen::GaiaVoxel::LayerMask>(e0, 0x11u);
    auto e1 = world.getWorld().add();
    world.setComponent<Vixen::GaiaVoxel::LayerMask>(e1, 0x22u);  // unselected sibling
    auto e2 = world.getWorld().add();
    world.setComponent<Vixen::GaiaVoxel::LayerMask>(e2, 0x33u);

    world.getWorld().add<Vixen::App::Selected>(e0);
    world.getWorld().add<Vixen::App::Selected>(e2);

    Vixen::App::GaiaViewSelectionProvider selection(world);
    ActionStack stack;
    stack.LoadActions(AppFlowContainerView::actions().data(), AppFlowContainerView::actions().size());

    const auto result = Vixen::App::DispatchSetMutation(stack, world, selection, 0x99u);
    ASSERT_EQ(result.writtenCount, 2u);
    EXPECT_EQ(stack.UndoDepth(), 1u);

    // Undo: BOTH selected entities restored to their EXACT pre-dispatch values (not just
    // "different from new"), unselected sibling still untouched.
    EXPECT_EQ(stack.Undo(), DispatchResult::Ok);
    auto e0AfterUndo = world.getComponentValue<Vixen::GaiaVoxel::LayerMask>(e0);
    ASSERT_TRUE(e0AfterUndo.has_value());
    EXPECT_EQ(*e0AfterUndo, 0x11u) << "e0 must be restored to its EXACT pre-dispatch value";
    auto e2AfterUndo = world.getComponentValue<Vixen::GaiaVoxel::LayerMask>(e2);
    ASSERT_TRUE(e2AfterUndo.has_value());
    EXPECT_EQ(*e2AfterUndo, 0x33u) << "e2 must be restored to its EXACT pre-dispatch value";
    auto e1AfterUndo = world.getComponentValue<Vixen::GaiaVoxel::LayerMask>(e1);
    ASSERT_TRUE(e1AfterUndo.has_value());
    EXPECT_EQ(*e1AfterUndo, 0x22u) << "unselected sibling must remain untouched across undo";
    EXPECT_EQ(result.skipCounters->undoSkips, 0u) << "no dead entities -- undo must not skip anyone";

    // Redo: both selected entities back to the post-dispatch (new) value.
    EXPECT_EQ(stack.Redo(), DispatchResult::Ok);
    auto e0AfterRedo = world.getComponentValue<Vixen::GaiaVoxel::LayerMask>(e0);
    ASSERT_TRUE(e0AfterRedo.has_value());
    EXPECT_EQ(*e0AfterRedo, 0x99u);
    auto e2AfterRedo = world.getComponentValue<Vixen::GaiaVoxel::LayerMask>(e2);
    ASSERT_TRUE(e2AfterRedo.has_value());
    EXPECT_EQ(*e2AfterRedo, 0x99u);
    auto e1AfterRedo = world.getComponentValue<Vixen::GaiaVoxel::LayerMask>(e1);
    ASSERT_TRUE(e1AfterRedo.has_value());
    EXPECT_EQ(*e1AfterRedo, 0x22u) << "unselected sibling must remain untouched across redo";
    EXPECT_EQ(result.skipCounters->forwardSkips, 0u) << "no dead entities -- redo must not skip anyone";
}

TEST(SetMutationDispatch, DeadEntityBetweenDispatchAndUndoIsSkippedNotCrashedAndSurvivorRestoresCorrectly) {
    // The increment's hardest correctness bar (plan §Task 4 item 4, design doc §6 critic item 8):
    // an entity destroyed AFTER dispatch but BEFORE Undo() must not crash/assert, the surviving
    // entity must still restore correctly, the skip must be observable, and a subsequent Redo()
    // must symmetrically skip the dead entity without crashing either.
    Vixen::GaiaVoxel::GaiaVoxelWorld world;

    auto e0 = world.getWorld().add();
    world.setComponent<Vixen::GaiaVoxel::LayerMask>(e0, 0x11u);
    auto e1 = world.getWorld().add();
    world.setComponent<Vixen::GaiaVoxel::LayerMask>(e1, 0x22u);

    world.getWorld().add<Vixen::App::Selected>(e0);
    world.getWorld().add<Vixen::App::Selected>(e1);

    Vixen::App::GaiaViewSelectionProvider selection(world);
    ActionStack stack;
    stack.LoadActions(AppFlowContainerView::actions().data(), AppFlowContainerView::actions().size());

    const auto result = Vixen::App::DispatchSetMutation(stack, world, selection, 0x99u);
    ASSERT_EQ(result.writtenCount, 2u);

    // Destroy e1 AFTER dispatch, BEFORE Undo() -- the exact hazard window the plan flags.
    world.getWorld().del(e1);
    ASSERT_FALSE(world.getWorld().valid(e1));

    // (a) no crash/assert.
    EXPECT_EQ(stack.Undo(), DispatchResult::Ok);

    // (b) the SURVIVING entity (e0) is correctly restored to its prior value.
    auto e0AfterUndo = world.getComponentValue<Vixen::GaiaVoxel::LayerMask>(e0);
    ASSERT_TRUE(e0AfterUndo.has_value());
    EXPECT_EQ(*e0AfterUndo, 0x11u) << "surviving entity must restore correctly despite sibling's death";

    // (c) the destroyed entity's skip is observable.
    EXPECT_EQ(result.skipCounters->undoSkips, 1u) << "e1's dead-entity skip at undo must be observable";

    // (d) a subsequent Redo() on the same group also skips the dead entity, symmetrically, without
    // crashing -- and does not resurrect/write anything for it.
    EXPECT_EQ(stack.Redo(), DispatchResult::Ok);
    auto e0AfterRedo = world.getComponentValue<Vixen::GaiaVoxel::LayerMask>(e0);
    ASSERT_TRUE(e0AfterRedo.has_value());
    EXPECT_EQ(*e0AfterRedo, 0x99u) << "surviving entity must still redo correctly";
    EXPECT_EQ(result.skipCounters->forwardSkips, 1u) << "e1's dead-entity skip at redo must be observable";
    EXPECT_FALSE(world.getWorld().valid(e1)) << "redo must not resurrect the destroyed entity";
}

TEST(SetMutationDispatch, UndoNeverReRunsTheLiveSelectionQueryAfterSelectionDrifts) {
    // Direct test of "never re-run the live query" (plan gates, design doc §6 critic item 8): after
    // dispatch, mutate the Selected tag set itself (remove Selected from one originally-selected
    // entity, add it to a previously-unselected one) BEFORE calling Undo(). If Undo() re-queried
    // IViewSelectionProvider::ids() at undo time, it would restore the WRONG set (the drifted
    // selection, not the dispatch-time one). Assert it restores based on the ORIGINAL entities.
    Vixen::GaiaVoxel::GaiaVoxelWorld world;

    auto e0 = world.getWorld().add();
    world.setComponent<Vixen::GaiaVoxel::LayerMask>(e0, 0x11u);
    auto e1 = world.getWorld().add();
    world.setComponent<Vixen::GaiaVoxel::LayerMask>(e1, 0x22u);  // NOT selected at dispatch time
    auto e2 = world.getWorld().add();
    world.setComponent<Vixen::GaiaVoxel::LayerMask>(e2, 0x33u);

    world.getWorld().add<Vixen::App::Selected>(e0);
    world.getWorld().add<Vixen::App::Selected>(e2);

    Vixen::App::GaiaViewSelectionProvider selection(world);
    ActionStack stack;
    stack.LoadActions(AppFlowContainerView::actions().data(), AppFlowContainerView::actions().size());

    const auto result = Vixen::App::DispatchSetMutation(stack, world, selection, 0x99u);
    ASSERT_EQ(result.writtenCount, 2u);  // e0, e2 written; e1 untouched

    // Drift the selection AFTER dispatch, BEFORE Undo(): drop e2 from Selected, add e1 to Selected.
    // If ids() were re-read at undo time, the "current selection" would now be {e0, e1}, not the
    // dispatch-time {e0, e2}.
    world.getWorld().del<Vixen::App::Selected>(e2);
    world.getWorld().add<Vixen::App::Selected>(e1);

    EXPECT_EQ(stack.Undo(), DispatchResult::Ok);

    // Undo must have restored e0 and e2 (the ORIGINAL dispatch-time entities) to their pre-dispatch
    // values, and must NOT have touched e1 (which only entered Selected after dispatch and was
    // never part of the captured snapshot).
    auto e0Value = world.getComponentValue<Vixen::GaiaVoxel::LayerMask>(e0);
    ASSERT_TRUE(e0Value.has_value());
    EXPECT_EQ(*e0Value, 0x11u) << "e0 (originally selected) must restore from the captured snapshot";

    auto e2Value = world.getComponentValue<Vixen::GaiaVoxel::LayerMask>(e2);
    ASSERT_TRUE(e2Value.has_value());
    EXPECT_EQ(*e2Value, 0x33u) << "e2 (originally selected, later dropped from Selected) must still "
                                  "restore -- proves undo uses the captured snapshot, not a live requery";

    auto e1Value = world.getComponentValue<Vixen::GaiaVoxel::LayerMask>(e1);
    ASSERT_TRUE(e1Value.has_value());
    EXPECT_EQ(*e1Value, 0x22u) << "e1 (added to Selected AFTER dispatch) must be untouched by undo -- "
                                  "if undo re-ran ids(), it would have wrongly tried to restore e1 too";
}
