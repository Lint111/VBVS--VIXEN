#include <gtest/gtest.h>
#include "AppFlowRuntime.h"
#include "AppFlowLoader.h"
#include "MessageBus.h"
#include "AppFlowEvents.h"
using namespace Vixen::AppFlow;
using namespace Vixen::AppFlow::Generated;

TEST(AppFlowLoader, LoadsGeneratedViewOk) {
    FlowStateMachine fsm; ActionStack st; BindingStore bindings; InputProfile input;
    EXPECT_EQ(AppFlowLoader::Load(AppFlowContainerView{}, fsm, st, bindings, input), LoadResult::Ok);
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
    EXPECT_EQ(rt.NavTo(FlowStateId::Simulating), DispatchResult::Ok);
    EXPECT_EQ(seen, 1);          // PublishImmediate — no drain needed (see Step 1 note)
}

// THE walking-skeleton spine, end to end: a UI selector resolves (via the generalized
// binding store) to a bound action, which dispatches undoably through the stack via a
// registered handler.
TEST(AppFlowRuntime, DispatchBySelectorRunsBoundActionUndoably) {
    AppFlowRuntime rt(nullptr, /*sender*/1);
    ASSERT_EQ(rt.Load(), LoadResult::Ok);
    std::string warn;
    ASSERT_TRUE(rt.AddBinding(
        {"#layer-0-toggle", FlowActionId::ToggleLayer, "click",
         {{"layerIndex", "dom:attr:data-layer"}}}, warn));
    int value = 0;
    auto flip = [&](bool fwd){ value += fwd ? 1 : -1; };
    rt.RegisterHandler(FlowActionId::ToggleLayer, [&](const AppFlowRuntime::Params&){
        rt.Stack().Dispatch(FlowActionId::ToggleLayer, flip);
    });
    EXPECT_EQ(rt.DispatchBySelector("#layer-0-toggle"), DispatchResult::Ok);
    EXPECT_EQ(value, 1);
    EXPECT_EQ(rt.Stack().Undo(), DispatchResult::Ok);
    EXPECT_EQ(value, 0);
    EXPECT_EQ(rt.DispatchBySelector("#no-binding"), DispatchResult::RejectedByState);
}

// Load() seeds kKeyDefaults into the InputProfile — a chord resolves without any manual Bind.
TEST(AppFlowLoader, SeedsKeyDefaultsResolvableByChord) {
    AppFlowRuntime rt(nullptr, /*sender*/1);
    ASSERT_EQ(rt.Load(), LoadResult::Ok);
    FlowActionId out{};
    // Ctrl+Z seeded Global -> Undo (from kKeyDefaults).
    ASSERT_TRUE(rt.Input().Resolve({KeyId::Z, KeyMod::Ctrl}, FlowStateId::Editing, out));
    EXPECT_EQ(out, FlowActionId::Undo);
    // The same chord, in Settings, resolves to the tighter State-scoped override.
    ASSERT_TRUE(rt.Input().Resolve({KeyId::Z, KeyMod::Ctrl}, FlowStateId::Settings, out));
    EXPECT_EQ(out, FlowActionId::UndoSettingChange);
}

// DispatchByKey resolves the chord through the InputProfile then dispatches undoably,
// mirroring DispatchBySelector's shape but for the typed key path.
TEST(AppFlowRuntime, DispatchByKeyRunsBoundActionUndoably) {
    AppFlowRuntime rt(nullptr, /*sender*/1);
    ASSERT_EQ(rt.Load(), LoadResult::Ok);
    int value = 0;
    auto flip = [&](bool fwd){ value += fwd ? 1 : -1; };
    rt.RegisterHandler(FlowActionId::Undo, [&](const AppFlowRuntime::Params&){
        rt.Stack().Dispatch(FlowActionId::Undo, flip);
    });
    EXPECT_EQ(rt.DispatchByKey({KeyId::Z, KeyMod::Ctrl}), DispatchResult::Ok);
    EXPECT_EQ(value, 1);
    EXPECT_EQ(rt.Stack().Undo(), DispatchResult::Ok);
    EXPECT_EQ(value, 0);
    // An unbound chord is a no-op rejection, never a crash.
    EXPECT_EQ(rt.DispatchByKey({KeyId::A, KeyMod::None}), DispatchResult::RejectedByState);
}

// NavPop is an encapsulated pass-through to the FSM's entry-history pop — no raw
// FlowStateMachine& is exposed (design §D6 / Inc-1 encapsulation discipline).
TEST(AppFlowRuntime, NavPopPopsToPriorState) {
    AppFlowRuntime rt(nullptr, /*sender*/1);
    ASSERT_EQ(rt.Load(), LoadResult::Ok);
    rt.SetGuardResult(FlowGuardId::DocumentValid, true);
    rt.SetCurrent(FlowStateId::Editing);
    ASSERT_EQ(rt.NavTo(FlowStateId::Settings), DispatchResult::Ok);
    EXPECT_EQ(rt.Current(), FlowStateId::Settings);
    EXPECT_EQ(rt.NavPop(), DispatchResult::Ok);
    EXPECT_EQ(rt.Current(), FlowStateId::Editing);
}
