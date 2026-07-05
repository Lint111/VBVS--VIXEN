// Built + run by Milestone 3 (Task 7 CMake); this milestone verifies via standalone compile.
#include <gtest/gtest.h>
#include "ActionStack.h"
#include "generated/AppFlow.g.h"
using namespace Vixen::AppFlow;
using namespace Vixen::AppFlow::Generated;

TEST(ActionStack, DispatchThenUndoRestores) {
    ActionStack st;
    st.LoadActions(AppFlowContainerView::actions().data(),
                   AppFlowContainerView::actions().size());
    int value = 0;
    auto flip = [&](bool fwd){ value += fwd ? 1 : -1; };
    st.BeginGroup(1); st.Dispatch(FlowActionId::ToggleLayer, flip); st.EndGroup();
    EXPECT_EQ(value, 1);
    EXPECT_EQ(st.Undo(), DispatchResult::Ok);
    EXPECT_EQ(value, 0);
}

TEST(ActionStack, RedoReapplies) {
    ActionStack st;
    st.LoadActions(AppFlowContainerView::actions().data(),
                   AppFlowContainerView::actions().size());
    int value = 0; auto flip = [&](bool fwd){ value += fwd ? 1 : -1; };
    st.BeginGroup(1); st.Dispatch(FlowActionId::ToggleLayer, flip); st.EndGroup();
    st.Undo();
    EXPECT_EQ(st.Redo(), DispatchResult::Ok);
    EXPECT_EQ(value, 1);
}

TEST(ActionStack, GroupUndoneAsOneUnit) {
    ActionStack st;
    st.LoadActions(AppFlowContainerView::actions().data(),
                   AppFlowContainerView::actions().size());
    int value = 0; auto flip = [&](bool fwd){ value += fwd ? 1 : -1; };
    st.BeginGroup(1);
    st.Dispatch(FlowActionId::ToggleLayer, flip);
    st.Dispatch(FlowActionId::ToggleLayer, flip);
    st.EndGroup();
    EXPECT_EQ(value, 2);
    EXPECT_EQ(st.Undo(), DispatchResult::Ok);   // one Undo reverts BOTH
    EXPECT_EQ(value, 0);
    EXPECT_EQ(st.UndoDepth(), 0u);
}

TEST(ActionStack, UndoEmptyReturnsNothingToUndo) {
    ActionStack st;
    st.LoadActions(AppFlowContainerView::actions().data(),
                   AppFlowContainerView::actions().size());
    EXPECT_EQ(st.Undo(), DispatchResult::NothingToUndo);
}
