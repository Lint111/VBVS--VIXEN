#include "AppFlowRuntime.h"
#include "Logger.h"
#include <utility>

namespace Vixen::AppFlow {

using namespace Generated;

namespace {
Logger& RuntimeLogger() {
    static Logger logger("AppFlowRuntime", true);
    return logger;
}

FlowTriggerCause ActionCause(FlowActionId action) {
    return {FlowTriggerKind::FlowAction,
            std::to_string(static_cast<uint16_t>(action))};
}
}

AppFlowRuntime::AppFlowRuntime(Vixen::EventBus::MessageBus* bus, Vixen::EventBus::SenderID sender)
    : bus_(bus), sender_(sender) {}

void AppFlowRuntime::Publish(AppFlowChangedEvent::Kind kind, FlowStateId state,
                              FlowActionId action, uint32_t group) {
    if (!bus_) return;
    AppFlowChangedEvent event(sender_, kind, state, action, group);
    bus_->PublishImmediate(event);
}

void AppFlowRuntime::PublishEdge(const FlowStateChange& change, FlowTriggerCause cause) {
    if (!bus_) return;
    AppFlowEdgeEvent edge(sender_, change.edgeId, change.from, change.to, std::move(cause));
    bus_->PublishImmediate(edge);
    Publish(AppFlowChangedEvent::Kind::StateChanged, change.to, FlowActionId{}, 0);
}

void AppFlowRuntime::ReportDiagnostic(const std::string& message) {
    RuntimeLogger().Error(message);
    if (bus_) {
        try {
            AppFlowDiagnosticEvent event(sender_, message);
            bus_->PublishImmediate(event);
        } catch (...) {
            RuntimeLogger().Error("AppFlow diagnostic event subscriber threw while reporting a refusal");
        }
    }
    if (diagnosticHandler_) {
        try {
            diagnosticHandler_(message);
        } catch (...) {
            RuntimeLogger().Error("AppFlow diagnostic callback threw while reporting a refusal");
        }
    }
}

LoadResult AppFlowRuntime::Load(const AppFlowContainerView* view, IViewDataProvider* dataProvider) {
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
    activeCause_.reset();
    handlers_.clear();
    return LoadResult::Ok;
}

LoadResult AppFlowRuntime::Load(IViewDataProvider* dataProvider) {
    return Load(nullptr, dataProvider);
}

DispatchResult AppFlowRuntime::DispatchData(FlowActionId id, uint32_t value) {
    auto it = dataTargets_.find(uint16_t(id));
    if (it == dataTargets_.end() || !dataProvider_) return DispatchResult::RejectedByState;
    dataProvider_->WriteU32(ViewNounKey{it->second}, value);
    return DispatchResult::Ok;
}

bool AppFlowRuntime::ReadData(FlowActionId id, uint32_t& out) const {
    auto it = dataTargets_.find(uint16_t(id));
    if (it == dataTargets_.end() || !dataProvider_) return false;
    return dataProvider_->ReadU32(ViewNounKey{it->second}, out);
}

DispatchResult AppFlowRuntime::NavTo(std::string_view edgeId) {
    if (!activeCause_) {
        ReportDiagnostic("AppFlow rejected edge '" + std::string(edgeId) +
                         "': direct transition requires an explicit System or Effect cause");
        return DispatchResult::RejectedByState;
    }
    return NavToWithCause(edgeId, *activeCause_);
}

DispatchResult AppFlowRuntime::NavTo(std::string_view edgeId, FlowTriggerCause cause) {
    return NavToWithCause(edgeId, cause);
}

DispatchResult AppFlowRuntime::NavToWithCause(std::string_view edgeId,
                                              const FlowTriggerCause& cause) {
    if (edgeId.empty() || cause.identity.empty()) {
        ReportDiagnostic("AppFlow rejected transition: edge and typed cause identities must be declared");
        return DispatchResult::RejectedByState;
    }
    FlowStateChange change;
    const DispatchResult result = fsm_.Request(edgeId, change);
    if (result == DispatchResult::Ok) {
        PublishEdge(change, cause);
    } else if (result == DispatchResult::RejectedByState) {
        ReportDiagnostic("AppFlow refused transition edge '" + std::string(edgeId) +
                         "' from the current state; no declared edge matches this request");
    }
    return result;
}

DispatchResult AppFlowRuntime::NavPop() {
    if (!activeCause_) {
        ReportDiagnostic("AppFlow rejected return: direct transition requires an explicit System or Effect cause");
        return DispatchResult::RejectedByState;
    }
    return NavPopWithCause(*activeCause_);
}

DispatchResult AppFlowRuntime::NavPop(FlowTriggerCause cause) {
    return NavPopWithCause(cause);
}

DispatchResult AppFlowRuntime::NavPopWithCause(const FlowTriggerCause& cause) {
    if (cause.identity.empty()) {
        ReportDiagnostic("AppFlow rejected return: typed cause identity must be declared");
        return DispatchResult::RejectedByState;
    }
    FlowStateChange change;
    const DispatchResult result = fsm_.RequestReturn(change);
    if (result == DispatchResult::Ok) {
        PublishEdge(change, cause);
    } else if (result == DispatchResult::RejectedByState) {
        ReportDiagnostic("AppFlow refused return from the current state: history has no unique matching declared edge");
    }
    return result;
}

void AppFlowRuntime::RegisterHandler(FlowActionId id, Handler fn) {
    handlers_[uint16_t(id)] = std::move(fn);
}

DispatchResult AppFlowRuntime::Dispatch(FlowActionId id, const Params& params) {
    return DispatchWithCause(id, params, ActionCause(id));
}

DispatchResult AppFlowRuntime::DispatchWithCause(FlowActionId id, const Params& params,
                                                  FlowTriggerCause cause) {
    auto it = handlers_.find(uint16_t(id));
    if (it == handlers_.end()) return DispatchResult::RejectedByState;
    if (cause.identity.empty()) cause = ActionCause(id);
    auto previous = std::move(activeCause_);
    activeCause_ = std::move(cause);
    try {
        it->second(params);
    } catch (...) {
        activeCause_ = std::move(previous);
        throw;
    }
    activeCause_ = std::move(previous);
    return DispatchResult::Ok;
}

DispatchResult AppFlowRuntime::DispatchById(FlowActionId id, const Params& params) {
    return Dispatch(id, params);
}

DispatchResult AppFlowRuntime::DispatchBySelector(const std::string& selector) {
    BoundAction bound;
    if (!bindings_.TryGetForSelector(selector, bound)) return DispatchResult::RejectedByState;
    if (bound.cause.identity.empty()) bound.cause = ActionCause(bound.action);
    return DispatchWithCause(bound.action, bound.params, std::move(bound.cause));
}

DispatchResult AppFlowRuntime::DispatchByKey(Generated::KeyChord chord) {
    FlowActionId action{};
    FlowTriggerCause cause;
    if (!inputProfile_.Resolve(chord, fsm_.Current(), action, &cause))
        return DispatchResult::RejectedByState;
    if (cause.identity.empty()) cause = ActionCause(action);
    return DispatchWithCause(action, {}, std::move(cause));
}

} // namespace Vixen::AppFlow
