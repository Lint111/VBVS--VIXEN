#include "AppFlowLoader.h"

namespace Vixen::AppFlow {

using Generated::FlowStateId;

namespace {

// Inc-1 has no reflection over enum member count, so validate a FlowStateId against the
// pinned, explicit range declared in AppFlow.g.h (Editing=0 .. Paused=2). A future increment
// with a real codegen emitter can emit a kFlowStateCount constant instead of this literal.
bool IsValidState(FlowStateId s) {
    const auto v = static_cast<uint16_t>(s);
    return v <= static_cast<uint16_t>(FlowStateId::Paused);
}

} // namespace

LoadResult AppFlowLoader::Load(const AppFlowContainerView& view, FlowStateMachine& fsm,
                                ActionStack& stack, BindingStore& bindings) {
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

    return LoadResult::Ok;
}

} // namespace Vixen::AppFlow
