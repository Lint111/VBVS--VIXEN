// Built + run by Milestone 3 (Task 5 CMake registration).
#include <gtest/gtest.h>
#include "ActionStack.h"
#include "AppFlowRuntime.h"
#include "LayerController.h"
#include "generated/AppFlow.g.h"
#include <cstring>
using namespace Vixen::AppFlow;
using namespace Vixen::AppFlow::Generated;

TEST(SnapshotUndo, SnapshotRestoresFootprintOnUndo) {
    ActionStack st;
    st.LoadActions(AppFlowContainerView::actions().data(), AppFlowContainerView::actions().size());
    uint32_t state = 5;              // a 4-byte footprint
    int restores = 0;
    // snapshot-mode dispatch: save `state` bytes, then apply mutates it; undo memcpy's them back.
    st.DispatchWithSnapshot(FlowActionId::ToggleLayer, &state, sizeof(state),
                            [&](bool fwd){ if (fwd) state = 99; },
                            [&]{ ++restores; });
    EXPECT_EQ(state, 99u);
    EXPECT_EQ(st.Undo(), DispatchResult::Ok);
    EXPECT_EQ(state, 5u);            // footprint bytes restored
    EXPECT_EQ(restores, 1);          // onRestore fired
}

TEST(SnapshotUndo, SnapshotRedoReappliesForward) {
    ActionStack st;
    st.LoadActions(AppFlowContainerView::actions().data(), AppFlowContainerView::actions().size());
    uint32_t state = 5;
    st.DispatchWithSnapshot(FlowActionId::ToggleLayer, &state, sizeof(state),
                            [&](bool fwd){ if (fwd) state = 99; }, []{});
    st.Undo();
    EXPECT_EQ(st.Redo(), DispatchResult::Ok);
    EXPECT_EQ(state, 99u);           // forward apply re-ran
}

TEST(SnapshotUndo, GenericOverFootprintSize) {
    // an 8-byte footprint proves the engine keys off footprintBytes, not a hardcoded type
    ActionStack st;
    st.LoadActions(AppFlowContainerView::actions().data(), AppFlowContainerView::actions().size());
    uint64_t big = 0xAAAAAAAABBBBBBBBull;
    st.DispatchWithSnapshot(FlowActionId::ToggleLayer, &big, sizeof(big),
                            [&](bool fwd){ if (fwd) big = 0; }, []{});
    EXPECT_EQ(big, 0ull);
    st.Undo();
    EXPECT_EQ(big, 0xAAAAAAAABBBBBBBBull);
}

// The design headline: undo-via-inverse and undo-via-snapshot of an equivalent change
// leave LayerController in byte-identical state.
TEST(SnapshotUndo, InverseAndSnapshotParity) {
    // Path A — inverse: toggle layer 2 via a self-inverse apply.
    LayerController a; a.SetLayerCount(3);
    ActionStack sa;
    sa.LoadActions(AppFlowContainerView::actions().data(), AppFlowContainerView::actions().size());
    sa.Dispatch(FlowActionId::ToggleLayer, [&](bool /*fwd*/){ a.Toggle(2); });  // self-inverse: toggle both ways
    sa.Undo();

    // Path B — snapshot: same net change, undone by restoring the footprint.
    LayerController b; b.SetLayerCount(3);
    ActionStack sb;
    sb.LoadActions(AppFlowContainerView::actions().data(), AppFlowContainerView::actions().size());
    uint32_t mask = b.Mask();
    sb.DispatchWithSnapshot(FlowActionId::ToggleLayer, &mask, sizeof(mask),
                            [&](bool fwd){ if (fwd) { b.Toggle(2); mask = b.Mask(); } },
                            [&]{ b.SetMask(mask); });
    sb.Undo();

    EXPECT_EQ(a.Mask(), b.Mask());          // both back to 0b111
    EXPECT_EQ(a.Snapshot().enabledMask, b.Snapshot().enabledMask);
}

// A group mixing a self-inverse and a snapshot action undoes as one unit, both reversed.
TEST(SnapshotUndo, MixedGroupUndoesAsOneUnit) {
    LayerController lc; lc.SetLayerCount(3);
    ActionStack st;
    st.LoadActions(AppFlowContainerView::actions().data(), AppFlowContainerView::actions().size());
    uint32_t mask = lc.Mask();
    st.BeginGroup(1);
    st.Dispatch(FlowActionId::ToggleLayer, [&](bool){ lc.Toggle(0); });                     // inverse
    mask = lc.Mask();  // keep the footprint synced to lc right before ITS OWN dispatch snapshots
                        // it — DispatchWithSnapshot saves whatever bytes are in `footprint` at call
                        // time, not lc's live state; a stale footprint captures the wrong baseline
                        // and corrupts the sibling inverse entry's effect when a strict-reverse
                        // Undo composes both restores (see ActionStack::Undo doc).
    st.DispatchWithSnapshot(FlowActionId::ToggleLayer, &mask, sizeof(mask),
                            [&](bool fwd){ if (fwd) { lc.Toggle(1); mask = lc.Mask(); } },
                            [&]{ lc.SetMask(mask); });                                        // snapshot
    st.EndGroup();
    EXPECT_EQ(lc.Mask(), 0b100u);           // layers 0 and 1 disabled
    EXPECT_EQ(st.Undo(), DispatchResult::Ok);
    EXPECT_EQ(lc.Mask(), 0b111u);           // ONE undo reverted BOTH
    EXPECT_EQ(st.UndoDepth(), 0u);
}

// Task 4: AppFlowRuntime owns LayerController + a self-inverse ToggleLayer dispatch whose
// onChanged (re-flatten) hook fires on both forward apply and undo.
TEST(SnapshotUndo, RuntimeToggleLayerAndUndoFireOnChanged) {
    AppFlowRuntime rt(nullptr, /*sender*/1);
    ASSERT_EQ(rt.Load(), LoadResult::Ok);
    rt.Layers().SetLayerCount(3);
    int changed = 0;
    EXPECT_EQ(rt.ToggleLayer(2, [&]{ ++changed; }), DispatchResult::Ok);
    EXPECT_FALSE(rt.Layers().IsEnabled(2));
    EXPECT_EQ(changed, 1);                 // onChanged fired on apply
    EXPECT_EQ(rt.Undo(), DispatchResult::Ok);
    EXPECT_TRUE(rt.Layers().IsEnabled(2)); // reverted
    EXPECT_EQ(changed, 2);                 // onChanged fired again on undo
}
