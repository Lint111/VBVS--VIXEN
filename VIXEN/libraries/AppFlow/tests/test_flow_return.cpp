#include "FlowStateMachine.h"
#include <gtest/gtest.h>
using namespace Vixen::AppFlow;
using namespace Vixen::AppFlow::Generated;

static FlowStateMachine MakeFsm() {
    FlowStateMachine fsm;
    // Editing(0) -> Simulating(1) -> Settings(3) reachable via unguarded transitions for the test.
    static const AppFlowTransition t[] = {
        {FlowStateId::Editing, FlowStateId::Simulating, FlowGuardId::DocumentValid, "none"},
        {FlowStateId::Simulating, FlowStateId::Settings, FlowGuardId::DocumentValid, "none"},
    };
    fsm.LoadTransitions(t, std::size(t));
    fsm.SetGuardResult(FlowGuardId::DocumentValid, true);
    fsm.SetCurrent(FlowStateId::Editing);
    return fsm;
}

TEST(FlowReturn, PopsToPriorState) {
    auto fsm = MakeFsm();
    ASSERT_EQ(fsm.Request(FlowStateId::Simulating), DispatchResult::Ok);
    ASSERT_EQ(fsm.Request(FlowStateId::Settings), DispatchResult::Ok);
    EXPECT_EQ(fsm.Current(), FlowStateId::Settings);
    EXPECT_EQ(fsm.RequestReturn(), DispatchResult::Ok);
    EXPECT_EQ(fsm.Current(), FlowStateId::Simulating);   // popped
    EXPECT_EQ(fsm.RequestReturn(), DispatchResult::Ok);
    EXPECT_EQ(fsm.Current(), FlowStateId::Editing);
}

TEST(FlowReturn, EmptyHistoryIsNoOp) {
    auto fsm = MakeFsm();
    EXPECT_EQ(fsm.RequestReturn(), DispatchResult::RejectedByState);   // nothing to pop
    EXPECT_EQ(fsm.Current(), FlowStateId::Editing);
}
