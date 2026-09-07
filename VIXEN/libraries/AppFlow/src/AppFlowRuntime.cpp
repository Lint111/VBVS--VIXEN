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

LoadResult AppFlowRuntime::Load(const AppFlowContainerView* view, IViewDataProvider* dataProvider) {
    // Build every reload into fresh primitives first. AppFlowLoader validates before mutating,
    // and the temporary primitives make a failed parse/load leave the live runtime untouched.
    FlowStateMachine freshFsm;
    ActionStack freshStack;
    BindingStore freshBindings;
    InputProfile freshInputProfile;
    DataTargetTable freshDataTargets;
    const AppFlowContainerView compiledIn{};
    const LoadResult result = AppFlowLoader::Load(view ? *view : compiledIn, freshFsm, freshStack,
                                                  freshBindings, freshInputProfile, &freshDataTargets);
    if (result != LoadResult::Ok) return result;

    fsm_ = std::move(freshFsm);
    stack_ = std::move(freshStack);
    bindings_ = std::move(freshBindings);
    inputProfile_ = std::move(freshInputProfile);
    dataTargets_ = std::move(freshDataTargets);
    dataProvider_ = dataProvider;
    handlers_.clear();
    return LoadResult::Ok;
}

LoadResult AppFlowRuntime::Load(IViewDataProvider* dataProvider) {
    return Load(nullptr, dataProvider);
}

DispatchResult AppFlowRuntime::DispatchData(FlowActionId id, uint32_t value) {
    auto it = dataTargets_.find(uint16_t(id));
    if (it == dataTargets_.end() || !dataProvider_) {
        return DispatchResult::RejectedByState;
    }
    dataProvider_->WriteU32(ViewNounKey{it->second}, value);
    return DispatchResult::Ok;
}

bool AppFlowRuntime::ReadData(FlowActionId id, uint32_t& out) const {
    auto it = dataTargets_.find(uint16_t(id));
    if (it == dataTargets_.end() || !dataProvider_) {
        return false;
    }
    return dataProvider_->ReadU32(ViewNounKey{it->second}, out);
}

DispatchResult AppFlowRuntime::NavTo(FlowStateId to) {
    const DispatchResult result = fsm_.Request(to);
    if (result == DispatchResult::Ok) {
        Publish(AppFlowChangedEvent::Kind::StateChanged, fsm_.Current(), FlowActionId{}, 0);
    }
    return result;
}

DispatchResult AppFlowRuntime::NavPop() {
    const DispatchResult result = fsm_.RequestReturn();
    if (result == DispatchResult::Ok) {
        Publish(AppFlowChangedEvent::Kind::StateChanged, fsm_.Current(), FlowActionId{}, 0);
    }
    return result;
}

void AppFlowRuntime::RegisterHandler(FlowActionId id, Handler fn) {
    handlers_[uint16_t(id)] = std::move(fn);
}

DispatchResult AppFlowRuntime::Dispatch(FlowActionId id, const Params& params) {
    auto it = handlers_.find(uint16_t(id));
    if (it == handlers_.end()) {
        return DispatchResult::RejectedByState;  // declared-but-unwired = caught, not a silent no-op
    }
    it->second(params);
    return DispatchResult::Ok;
}

DispatchResult AppFlowRuntime::DispatchById(FlowActionId id, const Params& params) {
    return Dispatch(id, params);
}

DispatchResult AppFlowRuntime::DispatchBySelector(const std::string& selector) {
    BoundAction bound;
    if (!bindings_.TryGetForSelector(selector, bound)) {
        return DispatchResult::RejectedByState;
    }
    return Dispatch(bound.action, bound.params);
}

DispatchResult AppFlowRuntime::DispatchByKey(Generated::KeyChord chord) {
    FlowActionId action{};
    if (!inputProfile_.Resolve(chord, fsm_.Current(), action)) {
        return DispatchResult::RejectedByState;
    }
    return Dispatch(action, {});
}

} // namespace Vixen::AppFlow
