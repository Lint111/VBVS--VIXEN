#include "AppFlowLoader.h"
#include <string>
#include <unordered_set>

namespace Vixen::AppFlow {

using Generated::FlowStateId;

namespace {

bool ContainsState(const std::unordered_set<uint16_t>& states, FlowStateId state) {
    return states.contains(static_cast<uint16_t>(state));
}

} // namespace

LoadResult AppFlowLoader::Load(const AppFlowContainerView& view, FlowStateMachine& fsm,
                                ActionStack& stack, BindingStore& bindings, InputProfile& input,
                                DataTargetTable* dataTargets) {
    const auto actions = view.actionTable();
    const auto transitions = view.transitionTable();
    const auto states = view.stateTable();
    const auto terminals = view.terminalStateTable();

    if (actions.empty()) return LoadResult::EmptyArtifact;
    if (states.empty()) return LoadResult::BadStateMarkers;

    std::unordered_set<uint16_t> validStates;
    for (FlowStateId state : states) {
        if (!validStates.insert(static_cast<uint16_t>(state)).second)
            return LoadResult::BadStateMarkers;
    }
    if (!ContainsState(validStates, view.initialState())) return LoadResult::BadStateMarkers;
    std::unordered_set<uint16_t> terminalIds;
    for (FlowStateId state : terminals) {
        if (!ContainsState(validStates, state) ||
            !terminalIds.insert(static_cast<uint16_t>(state)).second)
            return LoadResult::BadStateMarkers;
    }

    std::unordered_set<std::string> edgeIds;
    for (const auto& transition : transitions) {
        if (!transition.id || !*transition.id || !edgeIds.insert(transition.id).second)
            return LoadResult::DuplicateEdgeId;
        if (!ContainsState(validStates, transition.from) ||
            !ContainsState(validStates, transition.to))
            return LoadResult::BadTransitionRef;
    }
    for (const auto& trigger : view.elementTriggerTable())
        if (!trigger.id || !*trigger.id) return LoadResult::BadTransitionRef;
    for (const auto& key : view.keyDefaultTable())
        if (!key.id || !*key.id) return LoadResult::BadTransitionRef;
    for (const auto& edge : view.returnEdgeTable())
        if (!edge.id || !*edge.id || !ContainsState(validStates, edge.from))
            return LoadResult::BadTransitionRef;

    fsm.LoadTransitions(transitions.data(), transitions.size(), view.initialState());
    stack.LoadActions(actions.data(), actions.size());
    bindings.RegisterActions(actions);

    for (const auto& trigger : view.elementTriggerTable()) bindings.AddElementTrigger(trigger);
    for (const auto& key : view.keyDefaultTable()) {
        input.Bind(key.scope, key.state, key.chord, key.action,
                   {FlowTriggerKind::KeyDefault, key.id});
    }
    for (const auto& edge : view.returnEdgeTable()) {
        input.Bind(Generated::FlowScope::State, edge.from, edge.trigger,
                   Generated::FlowActionId::Return,
                   {FlowTriggerKind::ReturnEdge, edge.id});
    }

    if (dataTargets) {
        for (const auto& target : view.dataTargetTable())
            (*dataTargets)[static_cast<uint16_t>(target.action)] = target.viewNoun;
    }
    return LoadResult::Ok;
}

} // namespace Vixen::AppFlow
