// Built + run by Milestone 3 (Task 7 CMake); this milestone verifies via standalone compile.
#include <gtest/gtest.h>
#include "FlowStateMachine.h"
#include "generated/AppFlow.g.h"
using namespace Vixen::AppFlow;
using namespace Vixen::AppFlow::Generated;

TEST(FlowStateMachine, TransitionPassesWhenGuardTrue) {
    FlowStateMachine fsm;
    fsm.LoadTransitions(AppFlowContainerView::transitions().data(),
                        AppFlowContainerView::transitions().size());
    fsm.SetCurrent(FlowStateId::Editing);
    fsm.SetGuardResult(FlowGuardId::DocumentValid, true);
    EXPECT_EQ(fsm.Request(FlowStateId::Simulating), DispatchResult::Ok);
    EXPECT_EQ(fsm.Current(), FlowStateId::Simulating);
}

TEST(FlowStateMachine, TransitionFailsWhenGuardFalse) {
    FlowStateMachine fsm;
    fsm.LoadTransitions(AppFlowContainerView::transitions().data(),
                        AppFlowContainerView::transitions().size());
    fsm.SetCurrent(FlowStateId::Editing);
    fsm.SetGuardResult(FlowGuardId::DocumentValid, false);
    EXPECT_EQ(fsm.Request(FlowStateId::Simulating), DispatchResult::GuardFailed);
    EXPECT_EQ(fsm.Current(), FlowStateId::Editing);
}

TEST(FlowStateMachine, UndeclaredTransitionRejected) {
    FlowStateMachine fsm;
    fsm.LoadTransitions(AppFlowContainerView::transitions().data(),
                        AppFlowContainerView::transitions().size());
    fsm.SetCurrent(FlowStateId::Editing);
    EXPECT_EQ(fsm.Request(FlowStateId::Paused), DispatchResult::RejectedByState);
}
