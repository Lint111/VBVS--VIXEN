#include "AppFlowRuntime.h"
#include "Logger.h"
#include <algorithm>
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

void AppFlowRuntime::CaptureCatalog(const AppFlowContainerView& view) {
    catalogActions_.clear();
    for (const auto& decl : view.actionTable()) {
        CatalogAction action;
        action.id = decl.id;
        for (uint32_t i = 0; i < decl.paramCount; ++i)
            action.params.emplace_back(decl.params[i].name ? decl.params[i].name : "", decl.params[i].type);
        catalogActions_.push_back(std::move(action));
    }
    catalogTransitions_.clear();
    for (const auto& edge : view.transitionTable())
        catalogTransitions_.push_back({edge.id ? edge.id : "", edge.from, edge.to, edge.guard});
}

std::vector<FlowActionId> AppFlowRuntime::UnimplementedActions() const {
    std::vector<FlowActionId> result;
    for (const auto& action : catalogActions_)
        if (!IsImplemented(action.id)) result.push_back(action.id);
    return result;
}

void AppFlowRuntime::ReportUnimplementedAction(FlowActionId id, const std::string& what) {
    const std::string message = declarationSource_ + ": action '" + ActionName(id) + "' " + what;
    ReportDiagnostic(message);
    if (unimplementedHandler_) {
        try { unimplementedHandler_(id, message); }
        catch (...) { RuntimeLogger().Error("AppFlow unimplemented-action callback threw"); }
    }
}

std::string AppFlowRuntime::ActionName(FlowActionId id) const {
    if (actionNamer_) {
        std::string name = actionNamer_(id);
        if (!name.empty()) return name;
    }
    return "#" + std::to_string(static_cast<uint16_t>(id));
}

size_t AppFlowRuntime::ReportUnimplemented() {
    const auto missing = UnimplementedActions();
    for (const FlowActionId id : missing)
        ReportUnimplementedAction(id, "is declared but no handler is registered (inert until RegisterHandler lands or the declaration is removed)");
    return missing.size();
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
    CaptureCatalog(view ? *view : compiledIn);
    return LoadResult::Ok;
}

LoadResult AppFlowRuntime::Reload(const AppFlowContainerView* view, IViewDataProvider* dataProvider) {
    FlowStateMachine candidateFsm;   // validated and then discarded: the running fsm/stack keep their state
    ActionStack candidateStack;
    BindingStore freshBindings;
    InputProfile freshInputProfile;
    DataTargetTable freshDataTargets;
    const AppFlowContainerView compiledIn{};
    const LoadResult result = AppFlowLoader::Load(view ? *view : compiledIn, candidateFsm, candidateStack,
                                                  freshBindings, freshInputProfile, &freshDataTargets);
    if (result != LoadResult::Ok) return result;

    bindings_ = std::move(freshBindings);
    inputProfile_ = std::move(freshInputProfile);
    dataTargets_ = std::move(freshDataTargets);
    dataProvider_ = dataProvider;
    // The declarations may have changed (a hand-edited action row passes the shape gate): the catalogue and the stack's
    // action table follow the data; the undo/redo entries and the registered handlers stay.
    CaptureCatalog(view ? *view : compiledIn);
    const auto actions = (view ? *view : compiledIn).actionTable();
    stack_.LoadActions(actions.data(), actions.size());
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
    if (it == handlers_.end()) {
        const bool declared = std::any_of(catalogActions_.begin(), catalogActions_.end(),
                                          [id](const CatalogAction& a) { return a.id == id; });
        if (!declared) return DispatchResult::RejectedByState;
        ReportUnimplementedAction(id, "was dispatched but no handler is registered (declared, not implemented)");
        return DispatchResult::NoHandler;
    }
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
