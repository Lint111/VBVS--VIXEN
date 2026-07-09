#include "FlowStateMachine.h"

namespace Vixen::AppFlow {

void FlowStateMachine::LoadTransitions(const AppFlowTransition* table, size_t count) {
    transitions_.assign(table, table + count);
}

void FlowStateMachine::SetGuardResult(FlowGuardId g, bool pass) {
    guardResults_[static_cast<uint16_t>(g)] = pass;
}

bool FlowStateMachine::GuardPasses(FlowGuardId g) const {
    auto it = guardResults_.find(static_cast<uint16_t>(g));
    // Unset guard → treated as pass (Inc-1 simplification, see header comment).
    return it == guardResults_.end() || it->second;
}

DispatchResult FlowStateMachine::Request(FlowStateId to) {
    for (const auto& t : transitions_) {
        if (t.from == current_ && t.to == to) {
            if (GuardPasses(t.guard)) {
                current_ = to;
                return DispatchResult::Ok;
            }
            return DispatchResult::GuardFailed;
        }
    }
    return DispatchResult::RejectedByState;
}

} // namespace Vixen::AppFlow
