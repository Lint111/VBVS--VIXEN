#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include "AppFlowResults.h"
#include "generated/AppFlow.g.h"

namespace Vixen::AppFlow {

using Generated::FlowStateId;
using Generated::FlowGuardId;
using Generated::AppFlowTransition;

struct FlowStateChange {
    std::string edgeId;
    FlowStateId from{};
    FlowStateId to{};
};

// Guarded transition primitive. The generated declaration is the only route between states:
// callers request an edge ID, never a destination state.
class FlowStateMachine {
public:
    void LoadTransitions(const AppFlowTransition* table, size_t count, FlowStateId initialState);
    void SetGuardResult(FlowGuardId g, bool pass);

    FlowStateId Current() const { return current_; }

    // Resolves an exact declared edge ID. A missing ID leaves both state and history unchanged.
    DispatchResult Request(std::string_view edgeId, FlowStateChange& change);

    // Returns to the most recent entry-history state only through one unambiguous declared
    // edge from the current state to that state. History is popped only after the edge passes.
    DispatchResult RequestReturn(FlowStateChange& change);

private:
    bool GuardPasses(FlowGuardId g) const;
    struct OwnedTransition {
        std::string id;
        FlowStateId from;
        FlowStateId to;
        FlowGuardId guard;
    };
    DispatchResult Apply(const OwnedTransition& transition, FlowStateChange& change,
                         bool pushHistory);

    static constexpr size_t kHistoryCap = 16;
    std::vector<OwnedTransition> transitions_;
    FlowStateId current_{};
    std::unordered_map<uint16_t, bool> guardResults_;
    std::vector<FlowStateId> history_;   // bounded, drop-oldest
};

} // namespace Vixen::AppFlow
