#include <gtest/gtest.h>
#include "AppFlowRuntime.h"
#include "MessageBus.h"
#include <vector>
using namespace Vixen::AppFlow;
using namespace Vixen::AppFlow::Generated;

// Inc-4 reframe R2: Return is an ordinary registry handler (design §4.3/D14) — Esc
// (via the seeded return-edge) and a back-button selector both resolve to the SAME
// registered Return handler, which calls NavPop() itself. No special framework case.
TEST(ReturnDispatch, EscAndBackButtonBothPop) {
    struct EventRecord { std::string edgeId; FlowTriggerCause cause; };
    Vixen::EventBus::MessageBus bus;
    std::vector<EventRecord> received;
    bus.Subscribe(AppFlowEdgeEvent::TYPE,
        [&](const Vixen::EventBus::BaseEventMessage& message) {
            const auto& event = static_cast<const AppFlowEdgeEvent&>(message);
            received.push_back({event.edgeId, event.cause});
            return true;
        });
    AppFlowRuntime rt(&bus, /*sender*/1);
    ASSERT_EQ(rt.Load(), LoadResult::Ok);
    rt.SetGuardResult(FlowGuardId::DocumentValid, true);

    int returns = 0;
    rt.RegisterHandler(FlowActionId::Return, [&](const AppFlowRuntime::Params&) {
        rt.NavPop();
        ++returns;
    });

    ASSERT_EQ(rt.NavTo(FlowEdgeId::ToSettings,
                       {FlowTriggerKind::System, "ReturnDispatchTest"}), DispatchResult::Ok);
    EXPECT_EQ(rt.DispatchByKey({KeyId::Escape, KeyMod::None}), DispatchResult::Ok);   // Esc -> Return
    EXPECT_EQ(rt.Current(), FlowStateId::Editing);
    EXPECT_EQ(returns, 1);

    ASSERT_EQ(rt.NavTo(FlowEdgeId::ToSettings,
                       {FlowTriggerKind::System, "ReturnDispatchTest"}), DispatchResult::Ok);
    EXPECT_EQ(rt.DispatchBySelector("back-button"), DispatchResult::Ok);              // button -> SAME handler
    EXPECT_EQ(rt.Current(), FlowStateId::Editing);
    EXPECT_EQ(returns, 2);

    ASSERT_EQ(received.size(), 4u);
    EXPECT_EQ(received[1].cause.kind, FlowTriggerKind::ReturnEdge);
    EXPECT_EQ(received[1].cause.identity, FlowReturnEdgeId::SettingsReturn);
    EXPECT_EQ(received[1].edgeId, FlowEdgeId::SettingsEditingEdge);
    EXPECT_EQ(received[3].cause.kind, FlowTriggerKind::ElementTrigger);
    EXPECT_EQ(received[3].cause.identity, FlowElementTriggerId::BackButtonTrigger);
    EXPECT_EQ(received[3].edgeId, FlowEdgeId::SettingsEditingEdge);

    EXPECT_EQ(rt.Stack().UndoDepth(), 0u);   // Return is nav, not data: no ActionStack entry
}
