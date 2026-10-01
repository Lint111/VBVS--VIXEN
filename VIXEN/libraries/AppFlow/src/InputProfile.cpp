#include "InputProfile.h"
#include <utility>

namespace Vixen::AppFlow {

void InputProfile::Bind(FlowScope scope, FlowStateId state, KeyChord chord, FlowActionId action,
                        FlowTriggerCause cause) {
    Binding binding{action, std::move(cause)};
    if (scope == FlowScope::Global) {
        global_[Pack(chord)] = std::move(binding);
    } else {
        // State + Context both keyed by state here (Context deferred — see design §D8).
        byState_[(uint64_t(uint16_t(state)) << 32) | Pack(chord)] = std::move(binding);
    }
}

bool InputProfile::Resolve(KeyChord chord, FlowStateId active, FlowActionId& out,
                           FlowTriggerCause* cause) const {
    // Tightest first: state-scoped, then global.
    auto sIt = byState_.find((uint64_t(uint16_t(active)) << 32) | Pack(chord));
    if (sIt != byState_.end()) {
        out = sIt->second.action;
        if (cause) *cause = sIt->second.cause;
        return true;
    }
    auto gIt = global_.find(Pack(chord));
    if (gIt != global_.end()) {
        out = gIt->second.action;
        if (cause) *cause = gIt->second.cause;
        return true;
    }
    return false;
}

}  // namespace Vixen::AppFlow
