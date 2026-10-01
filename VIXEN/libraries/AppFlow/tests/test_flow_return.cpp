#include "FlowStateMachine.h"
#include <gtest/gtest.h>
using namespace Vixen::AppFlow;
using namespace Vixen::AppFlow::Generated;

static FlowStateMachine MakeFsm() {
    FlowStateMachine fsm;
    // The return destination is reachable only through an explicitly declared edge.
    static const AppFlowTransition transitions[] = {
        {"EditToSimulate", FlowStateId::Editing, FlowStateId::Simulating, FlowGuardId::DocumentValid},
        {"SimulateToSettings", FlowStateId::Simulating, FlowStateId::Settings, FlowGuardId::DocumentValid},
        {"SettingsToSimulate", FlowStateId::Settings, FlowStateId::Simulating, FlowGuardId::DocumentValid},
        {"SimulateToEditing", FlowStateId::Simulating, FlowStateId::Editing, FlowGuardId::DocumentValid},
    };
    fsm.LoadTransitions(transitions, std::size(transitions), FlowStateId::Editing);
    fsm.SetGuardResult(FlowGuardId::DocumentValid, true);
    return fsm;
}

TEST(FlowReturn, PopsThroughDeclaredEdges) {
    auto fsm = MakeFsm();
    FlowStateChange change;
    ASSERT_EQ(fsm.Request("EditToSimulate", change), DispatchResult::Ok);
    ASSERT_EQ(fsm.Request("SimulateToSettings", change), DispatchResult::Ok);
    EXPECT_EQ(fsm.Current(), FlowStateId::Settings);
    EXPECT_EQ(fsm.RequestReturn(change), DispatchResult::Ok);
    EXPECT_EQ(change.edgeId, "SettingsToSimulate");
    EXPECT_EQ(fsm.Current(), FlowStateId::Simulating);
    EXPECT_EQ(fsm.RequestReturn(change), DispatchResult::Ok);
    EXPECT_EQ(change.edgeId, "SimulateToEditing");
    EXPECT_EQ(fsm.Current(), FlowStateId::Editing);
}

TEST(FlowReturn, MissingDeclaredReturnEdgePreservesStateAndHistory) {
    FlowStateMachine fsm;
    static const AppFlowTransition transition[] = {
        {"EditToSettings", FlowStateId::Editing, FlowStateId::Settings, FlowGuardId::DocumentValid},
    };
    fsm.LoadTransitions(transition, std::size(transition), FlowStateId::Editing);
    FlowStateChange change;
    ASSERT_EQ(fsm.Request("EditToSettings", change), DispatchResult::Ok);
    EXPECT_EQ(fsm.RequestReturn(change), DispatchResult::RejectedByState);
    EXPECT_EQ(fsm.Current(), FlowStateId::Settings);
    EXPECT_EQ(fsm.RequestReturn(change), DispatchResult::RejectedByState);
}

TEST(FlowReturn, EmptyHistoryIsNoOp) {
    auto fsm = MakeFsm();
    FlowStateChange change;
    EXPECT_EQ(fsm.RequestReturn(change), DispatchResult::RejectedByState);
    EXPECT_EQ(fsm.Current(), FlowStateId::Editing);
}
