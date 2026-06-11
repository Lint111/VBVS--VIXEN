#include "Connection/Rules/AccumulationConnectionRule.h"
#include "Core/NodeInstance.h"
#include <algorithm>

namespace Vixen::RenderGraph {

namespace {

[[nodiscard]] inline bool IsValidNodePtr(const void* p) {
    return reinterpret_cast<uintptr_t>(p) > 0x10000;
}

} // anonymous namespace

// ============================================================================
// ACCUMULATION STATE IMPLEMENTATION
// ============================================================================

void AccumulationState::SortEntries(OrderStrategy strategy) {
    switch (strategy) {
        case OrderStrategy::ByMetadata:
            std::sort(entries.begin(), entries.end(),
                [](const AccumulationEntry& a, const AccumulationEntry& b) {
                    return a.sortKey < b.sortKey;
                });
            break;

        case OrderStrategy::BySourceSlot:
            std::sort(entries.begin(), entries.end(),
                [](const AccumulationEntry& a, const AccumulationEntry& b) {
                    return a.sourceSlot.index < b.sourceSlot.index;
                });
            break;

        case OrderStrategy::ConnectionOrder:
        case OrderStrategy::Unordered:
            break;
    }
}

bool AccumulationState::ValidateCount(std::string& errorMsg) const {
    if (entries.size() < config.minConnections) {
        errorMsg = "Accumulation slot requires at least " +
                   std::to_string(config.minConnections) + " connections, got " +
                   std::to_string(entries.size());
        return false;
    }

    if (entries.size() > config.maxConnections) {
        errorMsg = "Accumulation slot allows at most " +
                   std::to_string(config.maxConnections) + " connections, got " +
                   std::to_string(entries.size());
        return false;
    }

    return true;
}

bool AccumulationState::ValidateDuplicates(std::string& errorMsg) const {
    if (config.allowDuplicateKeys) {
        return true;
    }

    if (config.orderStrategy != OrderStrategy::ByMetadata) {
        return true;
    }

    std::vector<int32_t> keys;
    keys.reserve(entries.size());
    for (const auto& entry : entries) {
        keys.push_back(entry.sortKey);
    }
    std::sort(keys.begin(), keys.end());

    auto it = std::adjacent_find(keys.begin(), keys.end());
    if (it != keys.end()) {
        errorMsg = "Duplicate sort key " + std::to_string(*it) +
                   " in accumulation slot (allowDuplicateKeys = false)";
        return false;
    }

    return true;
}

// ============================================================================
// ACCUMULATION CONNECTION RULE IMPLEMENTATION
// ============================================================================

bool AccumulationConnectionRule::CanHandle(
    const SlotInfo& source,
    const SlotInfo& target) const {

    (void)source;
    return target.IsAccumulation();
}

ConnectionResult AccumulationConnectionRule::Validate(const ConnectionContext& ctx) const {
    if (!ctx.sourceNode) {
        return ConnectionResult::Error("Source node is null");
    }

    if (!ctx.targetNode) {
        return ConnectionResult::Error("Target node is null");
    }

    const auto& sourceSlot = ctx.sourceSlot;
    const auto& targetSlot = ctx.targetSlot;

    if (!sourceSlot.IsOutput()) {
        return ConnectionResult::Error("Source slot must be an output slot");
    }

    if (!targetSlot.IsAccumulation()) {
        return ConnectionResult::Error("Target slot must have Accumulation flag");
    }

    if (!targetSlot.IsInput()) {
        return ConnectionResult::Error("Accumulation target must be an input slot");
    }

    return ConnectionResult::Success();
}

ConnectionResult AccumulationConnectionRule::Resolve(ConnectionContext& ctx) const {
    // Add entry to accumulation state FIRST (doesn't require real nodes)
    // This allows unit tests with mock node pointers to still test entry storage
    if (ctx.accumulationState) {
        auto* state = static_cast<AccumulationState*>(ctx.accumulationState);

        AccumulationEntry entry;
        entry.sourceNode = ctx.sourceNode;
        entry.sourceOutputIndex = ctx.sourceSlot.index;
        entry.sortKey = ctx.sortKey;
        entry.roleOverride = ctx.roleOverride;
        entry.sourceSlot = ctx.sourceSlot;
        entry.storageMode = AccumulationStorage::ByValue;  // Default

        state->AddEntry(std::move(entry));
    }

    // Register dependency only if we have real nodes and not in test mode
    if (!ctx.skipDependencyRegistration &&
        IsValidNodePtr(ctx.sourceNode) &&
        IsValidNodePtr(ctx.targetNode)) {
        ctx.targetNode->AddDependency(ctx.sourceNode);
    }

    return ConnectionResult::Success();
}

} // namespace Vixen::RenderGraph
