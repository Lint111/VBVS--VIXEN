#include <gtest/gtest.h>
#include "AppFlowRuntime.h"
#include "AppFlowLoader.h"
#include "LayerControllerViewDataProvider.h"
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

// Seam M2c end-to-end: the generated Data action targets EditorNouns_layerMask; with a
// LayerControllerViewDataProvider wired at Load(), DispatchData writes the mask through the
// provider and ReadData reads it back — the seam closes from generated dataTarget to real mask.
TEST(AppFlowRuntime, DataActionDrivesProviderMask) {
    AppFlowRuntime rt(nullptr, /*sender*/1);
    rt.Layers().SetLayerCount(16);   // LayerController::SetMask caps to count_ bits; 0 count = all masked off.
    LayerControllerViewDataProvider provider(rt.Layers());
    ASSERT_EQ(rt.Load(&provider), LoadResult::Ok);

    EXPECT_EQ(rt.DispatchData(FlowActionId::Data, 0xABCDu), DispatchResult::Ok);
    EXPECT_EQ(rt.Layers().Mask(), 0xABCDu);            // wrote through the provider (within 16 layers)
    uint32_t got = 0;
    EXPECT_TRUE(rt.ReadData(FlowActionId::Data, got));
    EXPECT_EQ(got, 0xABCDu);                            // read back through the provider

    // A non-Data action id has no target → rejected, not a crash.
    EXPECT_EQ(rt.DispatchData(FlowActionId::ToggleLayer, 1u), DispatchResult::RejectedByState);
}

// No provider wired (the default Load()) → the Data leg is inert: the mapping still loads, but
// dispatching a Data action is rejected rather than dereferencing a null provider.
TEST(AppFlowRuntime, DataLegInertWithoutProvider) {
    AppFlowRuntime rt(nullptr, /*sender*/1);
    ASSERT_EQ(rt.Load(), LoadResult::Ok);   // no provider
    EXPECT_EQ(rt.DispatchData(FlowActionId::Data, 1u), DispatchResult::RejectedByState);
    uint32_t got = 0;
    EXPECT_FALSE(rt.ReadData(FlowActionId::Data, got));
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
