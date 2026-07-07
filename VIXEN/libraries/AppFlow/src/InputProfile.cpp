#include "InputProfile.h"

namespace Vixen::AppFlow {

void InputProfile::Bind(FlowScope scope, FlowStateId state, KeyChord chord, FlowActionId action) {
    if (scope == FlowScope::Global) {
        global_[Pack(chord)] = action;
    } else {
        // State + Context both keyed by state here (Context deferred — see design §D8).
        byState_[(uint64_t(uint16_t(state)) << 32) | Pack(chord)] = action;
    }
}

bool InputProfile::Resolve(KeyChord chord, FlowStateId active, FlowActionId& out) const {
    // Tightest first: state-scoped, then global.
    auto sIt = byState_.find((uint64_t(uint16_t(active)) << 32) | Pack(chord));
    if (sIt != byState_.end()) { out = sIt->second; return true; }
    auto gIt = global_.find(Pack(chord));
    if (gIt != global_.end()) { out = gIt->second; return true; }
    return false;
}

}  // namespace Vixen::AppFlow
