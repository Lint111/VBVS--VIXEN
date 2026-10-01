#include "FlowStateMachine.h"

namespace Vixen::AppFlow {

void FlowStateMachine::LoadTransitions(const AppFlowTransition* table, size_t count,
                                       FlowStateId initialState) {
    transitions_.clear();
    history_.clear();
    transitions_.reserve(count);
    for (size_t i = 0; i < count; ++i)
        transitions_.push_back({table[i].id ? table[i].id : "", table[i].from,
                                table[i].to, table[i].guard});
    current_ = initialState;
}

void FlowStateMachine::SetGuardResult(FlowGuardId g, bool pass) {
    guardResults_[static_cast<uint16_t>(g)] = pass;
}

bool FlowStateMachine::GuardPasses(FlowGuardId g) const {
    auto it = guardResults_.find(static_cast<uint16_t>(g));
    // Unset guard → treated as pass (Inc-1 simplification, see design §3.1).
    return it == guardResults_.end() || it->second;
}

DispatchResult FlowStateMachine::Apply(const OwnedTransition& transition,
                                       FlowStateChange& change, bool pushHistory) {
    if (!GuardPasses(transition.guard)) return DispatchResult::GuardFailed;
    const FlowStateId from = current_;
    const FlowStateId to = transition.to;
    if (pushHistory) {
        history_.push_back(from);
        if (history_.size() > kHistoryCap) history_.erase(history_.begin());
    }
    current_ = to;
    change = {transition.id, from, to};
    return DispatchResult::Ok;
}

DispatchResult FlowStateMachine::Request(std::string_view edgeId, FlowStateChange& change) {
    for (const auto& transition : transitions_) {
        if (edgeId == transition.id) {
            if (transition.from != current_) return DispatchResult::RejectedByState;
            return Apply(transition, change, true);
        }
    }
    return DispatchResult::RejectedByState;
}

DispatchResult FlowStateMachine::RequestReturn(FlowStateChange& change) {
    if (history_.empty()) return DispatchResult::RejectedByState;
    const FlowStateId target = history_.back();
    const OwnedTransition* match = nullptr;
    for (const auto& transition : transitions_) {
        if (transition.from != current_ || transition.to != target) continue;
        if (match) return DispatchResult::RejectedByState; // ambiguous declared return edge
        match = &transition;
    }
    if (!match) return DispatchResult::RejectedByState;
    const DispatchResult result = Apply(*match, change, false);
    if (result == DispatchResult::Ok) history_.pop_back();
    return result;
}

} // namespace Vixen::AppFlow
