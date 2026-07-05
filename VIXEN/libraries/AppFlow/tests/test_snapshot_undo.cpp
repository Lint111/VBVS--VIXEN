// Built + run by Milestone 3 (Task 7 CMake); this milestone verifies via standalone compile.
#include <gtest/gtest.h>
#include "ActionStack.h"
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
