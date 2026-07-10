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
                history_.push_back(current_);
                if (history_.size() > kHistoryCap) {
                    history_.erase(history_.begin());   // bounded: drop oldest
                }
                current_ = to;
                return DispatchResult::Ok;
            }
            return DispatchResult::GuardFailed;
        }
    }
    return DispatchResult::RejectedByState;
}

DispatchResult FlowStateMachine::RequestReturn() {
    if (history_.empty()) {
        return DispatchResult::RejectedByState;   // logged no-op, never underflow
    }
    current_ = history_.back();
    history_.pop_back();
    return DispatchResult::Ok;
}

} // namespace Vixen::AppFlow
