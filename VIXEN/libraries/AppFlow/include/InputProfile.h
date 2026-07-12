#pragma once
#include <cstdint>
#include <unordered_map>
#include "generated/AppFlow.g.h"

namespace Vixen::AppFlow {

using Generated::KeyChord; using Generated::KeyId; using Generated::KeyMod;
using Generated::FlowScope; using Generated::FlowStateId; using Generated::FlowActionId;

// Hierarchical KeyChord->action registry (design §4.1). Resolve walks tightest-to-widest
// scope: state-scoped bindings win over Global for the same chord. Seeded from kKeyDefaults
// at Load(); mutable (the deferred rebind/Steam seam). Qualifiers are a composable set —
// KeyMod is the only kind shipped, but resolution keys on the whole chord, so a future
// timing/sequence qualifier extends the chord without a resolver rewrite (design §D8).
class InputProfile {
public:
    void Bind(FlowScope scope, FlowStateId state, KeyChord chord, FlowActionId action);
    bool Resolve(KeyChord chord, FlowStateId active, FlowActionId& out) const;

private:
    // key = packed (KeyId<<8 | KeyMod); one map per scope tier.
    static uint32_t Pack(KeyChord c) { return (uint32_t(uint16_t(c.key)) << 8) | uint8_t(c.mods); }
    std::unordered_map<uint32_t, FlowActionId> global_;
    std::unordered_map<uint64_t, FlowActionId> byState_;    // (stateId<<32 | packedChord)
};

}  // namespace Vixen::AppFlow
