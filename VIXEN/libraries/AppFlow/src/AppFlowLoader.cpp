#include "AppFlowLoader.h"

namespace Vixen::AppFlow {

using Generated::FlowStateId;

namespace {

// No reflection over enum member count at this call site, so validate a FlowStateId against
// the pinned, explicit range declared in AppFlow.g.h (Editing=0 .. Settings=3, Inc-4 M2). A
// future increment with a real codegen emitter can emit a kFlowStateCount constant instead
// of this literal.
bool IsValidState(FlowStateId s) {
    const auto v = static_cast<uint16_t>(s);
    return v <= static_cast<uint16_t>(FlowStateId::Settings);
}

} // namespace

LoadResult AppFlowLoader::Load(const AppFlowContainerView& view, FlowStateMachine& fsm,
                                ActionStack& stack, BindingStore& bindings, InputProfile& input) {
    const auto actions = view.actions();
    const auto transitions = view.transitions();

    if (actions.empty()) {
        return LoadResult::EmptyArtifact;
    }
    for (const auto& t : transitions) {
        if (!IsValidState(t.from) || !IsValidState(t.to)) {
            return LoadResult::BadTransitionRef;
        }
    }

    fsm.LoadTransitions(transitions.data(), transitions.size());
    stack.LoadActions(actions.data(), actions.size());
    bindings.RegisterActions(actions);

    // Seed element triggers into the BindingStore (Inc-4 §4.2).
    for (const auto& t : view.elementTriggers()) {
        bindings.AddElementTrigger(t);
    }
    // Seed key defaults into the InputProfile, by scope (Inc-4 §4.1/§4.5).
    for (const auto& k : view.keyDefaults()) {
        input.Bind(k.scope, k.state, k.chord, k.action);
    }
    // Seed return edges as Return-action key bindings (Esc in <from> -> Return). The FROM state
    // scopes the binding so Esc only pops where a return edge is declared.
    for (const auto& r : view.returnEdges()) {
        input.Bind(Generated::FlowScope::State, r.from, r.trigger, Generated::FlowActionId::Return);
    }

    return LoadResult::Ok;
}

} // namespace Vixen::AppFlow
