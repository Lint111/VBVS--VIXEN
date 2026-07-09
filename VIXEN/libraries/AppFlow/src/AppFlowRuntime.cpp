#include "AppFlowRuntime.h"
#include "AppFlowLoader.h"

namespace Vixen::AppFlow {

using namespace Generated;

AppFlowRuntime::AppFlowRuntime(Vixen::EventBus::MessageBus* bus, Vixen::EventBus::SenderID sender)
    : bus_(bus), sender_(sender) {}

void AppFlowRuntime::Publish(AppFlowChangedEvent::Kind kind, FlowStateId state,
                              FlowActionId action, uint32_t group) {
    if (!bus_) {
        return;
    }
    AppFlowChangedEvent evt(sender_, kind, state, action, group);
    bus_->PublishImmediate(evt);
}

// NOTE (Inc-1 filler-field convention — read before consuming AppFlowChangedEvent):
// For kinds where a field isn't semantically meaningful, the publishers below pass a
// FILLER value, NOT a sentinel: StateChanged/ActionUndone/ActionRedone pass
// action = FlowActionId{}, which value-inits to 0 == FlowActionId::ToggleLayer (a VALID
// id, not "none"). Inc-1 consumers key off `kind` (+ publish count) and never read
// `.action` on those kinds, so this is safe today. A future consumer MUST key off `kind`
// and not read `.action` on non-Action* kinds. Inc-2 should either carry the affected id
// or add an explicit "none" sentinel (there is no reserved sentinel enumerator yet).

LoadResult AppFlowRuntime::Load() {
    return AppFlowLoader::Load(AppFlowContainerView{}, fsm_, stack_, bindings_);
}

DispatchResult AppFlowRuntime::RequestState(FlowStateId to) {
    const DispatchResult result = fsm_.Request(to);
    if (result == DispatchResult::Ok) {
        Publish(AppFlowChangedEvent::Kind::StateChanged, fsm_.Current(), FlowActionId{}, 0);
    }
    return result;
}

DispatchResult AppFlowRuntime::DispatchAction(FlowActionId id, ActionStack::ApplyFn apply) {
    const DispatchResult result = stack_.Dispatch(id, std::move(apply));
    if (result == DispatchResult::Ok) {
        Publish(AppFlowChangedEvent::Kind::ActionApplied, fsm_.Current(), id, 0);
    }
    return result;
}

DispatchResult AppFlowRuntime::DispatchBySelector(const std::string& selector,
                                                   ActionStack::ApplyFn apply) {
    BoundAction bound;
    if (!bindings_.TryGetForSelector(selector, bound)) {
        return DispatchResult::RejectedByState;
    }
    return DispatchAction(bound.action, std::move(apply));
}

DispatchResult AppFlowRuntime::Undo() {
    const DispatchResult result = stack_.Undo();
    if (result == DispatchResult::Ok) {
        Publish(AppFlowChangedEvent::Kind::ActionUndone, fsm_.Current(), FlowActionId{}, 0);
    }
    return result;
}

DispatchResult AppFlowRuntime::Redo() {
    const DispatchResult result = stack_.Redo();
    if (result == DispatchResult::Ok) {
        Publish(AppFlowChangedEvent::Kind::ActionRedone, fsm_.Current(), FlowActionId{}, 0);
    }
    return result;
}

DispatchResult AppFlowRuntime::ToggleLayer(uint32_t index, std::function<void()> onChanged) {
    // Self-inverse: the same apply toggles both forward and inverse; onChanged fires on both.
    auto apply = [this, index, onChanged](bool /*forward*/) {
        layers_.Toggle(index);
        if (onChanged) onChanged();
    };
    const DispatchResult r = stack_.Dispatch(FlowActionId::ToggleLayer, apply);
    if (r == DispatchResult::Ok) {
        Publish(AppFlowChangedEvent::Kind::ActionApplied, fsm_.Current(), FlowActionId::ToggleLayer, 0);
    }
    return r;
}

} // namespace Vixen::AppFlow
