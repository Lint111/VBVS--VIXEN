#pragma once
#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>
#include "AppFlowResults.h"
#include "generated/AppFlow.g.h"

namespace Vixen::AppFlow {

using Generated::FlowStateId;
using Generated::FlowGuardId;
using Generated::AppFlowTransition;

// Guarded transition primitive (design §3.1, §5 "Transitions"). Inc-1 stub: guards are
// set externally via SetGuardResult — there is no guard-opcode VM yet (that is a later
// increment). A guard that was never set is treated as passing, so a transition with an
// unset guard still fires; this Inc-1 simplification is intentional and documented here.
class FlowStateMachine {
public:
    void LoadTransitions(const AppFlowTransition* table, size_t count);
    void SetGuardResult(FlowGuardId g, bool pass);

    FlowStateId Current() const { return current_; }
    void SetCurrent(FlowStateId s) { current_ = s; }

    // Finds a (current_, to) transition via linear scan of the loaded table.
    // No match → RejectedByState. Match with a failing guard → GuardFailed.
    // Match with a passing (or unset) guard → mutates current_ and returns Ok.
    DispatchResult Request(FlowStateId to);

private:
    bool GuardPasses(FlowGuardId g) const;

    std::vector<AppFlowTransition> transitions_;
    FlowStateId current_{};
    std::unordered_map<uint16_t, bool> guardResults_;
};

} // namespace Vixen::AppFlow
