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
    // Match with a passing (or unset) guard → mutates current_ and returns Ok, pushing the
    // state being left onto the entry-history stack (see RequestReturn).
    DispatchResult Request(FlowStateId to);

    // Pops the entry-history stack and transitions back to the popped state (design §D6 —
    // navigation "Return", distinct from ActionStack::Undo's data revert). Empty history is a
    // logged no-op (RejectedByState), never an underflow. Does NOT push onto history itself —
    // a return is not a forward nav.
    DispatchResult RequestReturn();

private:
    bool GuardPasses(FlowGuardId g) const;

    static constexpr size_t kHistoryCap = 16;

    std::vector<AppFlowTransition> transitions_;
    FlowStateId current_{};
    std::unordered_map<uint16_t, bool> guardResults_;
    std::vector<FlowStateId> history_;   // bounded, drop-oldest
};

} // namespace Vixen::AppFlow
