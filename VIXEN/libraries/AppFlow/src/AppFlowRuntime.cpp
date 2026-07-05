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

} // namespace Vixen::AppFlow
