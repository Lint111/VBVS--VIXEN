#pragma once
#include <string>
#include "AppFlowEvents.h"
#include "AppFlowResults.h"
#include "ActionStack.h"
#include "BindingStore.h"
#include "FlowStateMachine.h"
#include "MessageBus.h"

namespace Vixen::AppFlow {

// The Inc-1 façade (design §5 "A user gesture, end to end"): owns the FSM, ActionStack, and
// BindingStore, and is the one thing consumers talk to. Holds a nullable, non-owning
// MessageBus*; every publish is if(bus_)-guarded so a runtime built with no bus (as in
// offline tests) is fully usable. Uses PublishImmediate (not the queued Publish) for Inc-1
// determinism — there is no drain step in this increment.
class AppFlowRuntime {
public:
    AppFlowRuntime(Vixen::EventBus::MessageBus* bus, Vixen::EventBus::SenderID sender);

    // Builds the generated AppFlowContainerView and loads it into the fsm/stack/bindings.
    LoadResult Load();

    // Pass-throughs to the fsm, exposed so a consumer (or test) can drive Inc-1's
    // externally-set guard stub without reaching into the owned FlowStateMachine.
    void SetGuardResult(Generated::FlowGuardId g, bool pass) { fsm_.SetGuardResult(g, pass); }
    void SetCurrent(Generated::FlowStateId s) { fsm_.SetCurrent(s); }
    Generated::FlowStateId Current() const { return fsm_.Current(); }

    // Delegates to the fsm; on Ok publishes AppFlowChangedEvent{StateChanged}.
    DispatchResult RequestState(Generated::FlowStateId to);

    // Delegates to the stack; on Ok publishes AppFlowChangedEvent{ActionApplied}.
    DispatchResult DispatchAction(Generated::FlowActionId id, ActionStack::ApplyFn apply);

    // THE walking-skeleton spine: resolve selector -> bound action via the BindingStore,
    // then dispatch it through the stack. A selector with no binding is RejectedByState.
    DispatchResult DispatchBySelector(const std::string& selector, ActionStack::ApplyFn apply);

    // Pass-through so tests/consumers author bindings without reaching into the store.
    bool AddBinding(const BindingStore::BindingSpec& spec, std::string& warn) {
        return bindings_.AddBinding(spec, warn);
    }

    // Publish ActionUndone/ActionRedone on success.
    DispatchResult Undo();
    DispatchResult Redo();

private:
    void Publish(AppFlowChangedEvent::Kind kind, Generated::FlowStateId state,
                 Generated::FlowActionId action, uint32_t group);

    Vixen::EventBus::MessageBus* bus_;
    Vixen::EventBus::SenderID sender_;
    FlowStateMachine fsm_;
    ActionStack stack_;
    BindingStore bindings_;
};

} // namespace Vixen::AppFlow
