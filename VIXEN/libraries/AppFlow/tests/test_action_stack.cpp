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

TEST(ActionStack, EmptyGroupIsNotAnUndoStep) {
    ActionStack st;
    st.LoadActions(AppFlowContainerView::actions().data(),
                   AppFlowContainerView::actions().size());
    st.BeginGroup(1);
    st.EndGroup();
    EXPECT_EQ(st.UndoDepth(), 0u);
    EXPECT_EQ(st.Undo(), DispatchResult::NothingToUndo);
}

TEST(ActionStack, GroupOfThreeDispatchesAddsOneUndoDepth) {
    ActionStack st;
    st.LoadActions(AppFlowContainerView::actions().data(),
                   AppFlowContainerView::actions().size());
    int value = 0; auto step = [&](bool fwd){ value += fwd ? 1 : -1; };
    st.Dispatch(FlowActionId::ToggleLayer, step);
    ASSERT_EQ(st.UndoDepth(), 1u);
    st.BeginGroup(7);
    for (int i = 0; i < 3; ++i) st.Dispatch(FlowActionId::ToggleLayer, step);
    st.EndGroup();
    EXPECT_EQ(st.UndoDepth(), 2u);
    EXPECT_EQ(value, 4);
    EXPECT_EQ(st.Undo(), DispatchResult::Ok);
    EXPECT_EQ(value, 1);
    EXPECT_EQ(st.UndoDepth(), 1u);
}

TEST(ActionStack, UndoEmptyReturnsNothingToUndo) {
    ActionStack st;
    st.LoadActions(AppFlowContainerView::actions().data(),
                   AppFlowContainerView::actions().size());
    EXPECT_EQ(st.Undo(), DispatchResult::NothingToUndo);
}
