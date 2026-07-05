#include <gtest/gtest.h>
#include "AppFlowRuntime.h"
#include "AppFlowLoader.h"
#include "MessageBus.h"
#include "AppFlowEvents.h"
using namespace Vixen::AppFlow;
using namespace Vixen::AppFlow::Generated;

TEST(AppFlowLoader, LoadsGeneratedViewOk) {
    FlowStateMachine fsm; ActionStack st; BindingStore bindings;
    EXPECT_EQ(AppFlowLoader::Load(AppFlowContainerView{}, fsm, st, bindings), LoadResult::Ok);
}

TEST(AppFlowRuntime, StateChangePublishesEvent) {
    Vixen::EventBus::MessageBus bus;
    int seen = 0;
    bus.Subscribe(AppFlowChangedEvent::TYPE,
        [&](const Vixen::EventBus::BaseEventMessage&){ ++seen; return true; });
    AppFlowRuntime rt(&bus, /*sender*/1);
    ASSERT_EQ(rt.Load(), LoadResult::Ok);
    rt.SetGuardResult(FlowGuardId::DocumentValid, true);
    rt.SetCurrent(FlowStateId::Editing);
    EXPECT_EQ(rt.RequestState(FlowStateId::Simulating), DispatchResult::Ok);
    EXPECT_EQ(seen, 1);          // PublishImmediate — no drain needed (see Step 1 note)
}

// THE walking-skeleton spine, end to end: a UI selector resolves (via the generalized
// binding store) to a bound action, which dispatches undoably through the stack.
TEST(AppFlowRuntime, DispatchBySelectorRunsBoundActionUndoably) {
    AppFlowRuntime rt(nullptr, /*sender*/1);
    ASSERT_EQ(rt.Load(), LoadResult::Ok);
    std::string warn;
    ASSERT_TRUE(rt.AddBinding(
        {"#layer-0-toggle", FlowActionId::ToggleLayer, "click",
         {{"layerIndex", "dom:attr:data-layer"}}}, warn));
    int value = 0;
    auto flip = [&](bool fwd){ value += fwd ? 1 : -1; };
    EXPECT_EQ(rt.DispatchBySelector("#layer-0-toggle", flip), DispatchResult::Ok);
    EXPECT_EQ(value, 1);
    EXPECT_EQ(rt.Undo(), DispatchResult::Ok);
    EXPECT_EQ(value, 0);
    EXPECT_EQ(rt.DispatchBySelector("#no-binding", flip), DispatchResult::RejectedByState);
}
