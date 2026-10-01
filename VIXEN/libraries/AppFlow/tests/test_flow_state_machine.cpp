#include <gtest/gtest.h>
#include "FlowStateMachine.h"
#include "generated/AppFlow.g.h"
using namespace Vixen::AppFlow;
using namespace Vixen::AppFlow::Generated;

TEST(FlowStateMachine, TransitionPassesWhenGuardTrue) {
    FlowStateMachine fsm;
    fsm.LoadTransitions(AppFlowContainerView::transitions().data(),
                        AppFlowContainerView::transitions().size(), kInitialState);
    fsm.SetGuardResult(FlowGuardId::DocumentValid, true);
    FlowStateChange change;
    EXPECT_EQ(fsm.Request(FlowEdgeId::Transitions, change), DispatchResult::Ok);
    EXPECT_EQ(change.from, FlowStateId::Editing);
    EXPECT_EQ(change.to, FlowStateId::Simulating);
    EXPECT_STREQ(change.edgeId.c_str(), FlowEdgeId::Transitions);
    EXPECT_EQ(fsm.Current(), FlowStateId::Simulating);
}

TEST(FlowStateMachine, TransitionFailsWhenGuardFalse) {
    FlowStateMachine fsm;
    fsm.LoadTransitions(AppFlowContainerView::transitions().data(),
                        AppFlowContainerView::transitions().size(), kInitialState);
    fsm.SetGuardResult(FlowGuardId::DocumentValid, false);
    FlowStateChange change;
    EXPECT_EQ(fsm.Request(FlowEdgeId::Transitions, change), DispatchResult::GuardFailed);
    EXPECT_EQ(fsm.Current(), FlowStateId::Editing);
}

TEST(FlowStateMachine, UndeclaredEdgeRejected) {
    FlowStateMachine fsm;
    fsm.LoadTransitions(AppFlowContainerView::transitions().data(),
                        AppFlowContainerView::transitions().size(), kInitialState);
    FlowStateChange change;
    EXPECT_EQ(fsm.Request("NoSuchEdge", change), DispatchResult::RejectedByState);
    EXPECT_EQ(fsm.Current(), FlowStateId::Editing);
}

TEST(FlowStateMachine, InitialStateComesFromGeneratedMarker) {
    FlowStateMachine fsm;
    fsm.LoadTransitions(AppFlowContainerView::transitions().data(),
                        AppFlowContainerView::transitions().size(), kInitialState);
    EXPECT_EQ(fsm.Current(), FlowStateId::Editing);
}
