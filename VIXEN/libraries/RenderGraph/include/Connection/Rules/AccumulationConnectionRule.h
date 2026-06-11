#pragma once

/**
 * @file AccumulationConnectionRule.h
 * @brief Rule for accumulation (multi-connect) connections
 */

#include "Connection/ConnectionRule.h"
#include <vector>

namespace Vixen::RenderGraph {

// ============================================================================
// ACCUMULATION TYPES
// ============================================================================

/**
 * @brief Pending connection in accumulation slot
 *
 * Tracks individual connections before they're resolved into the final array.
 */
struct AccumulationEntry {
    NodeInstance* sourceNode = nullptr;
    uint32_t sourceOutputIndex = 0;
    int32_t sortKey = 0;
    SlotRole roleOverride = SlotRole::None;
    SlotInfo sourceSlot;
    bool isIterable = false;
    bool shouldFlatten = true;
    size_t iterableSize = 0;
    AccumulationStorage storageMode = AccumulationStorage::ByValue;

    [[nodiscard]] bool operator<(const AccumulationEntry& other) const {
        return sortKey < other.sortKey;
    }
};

/**
 * @brief Accumulation state for a slot
 *
 * Maintained per accumulation slot to track all connections before resolve.
 */
struct AccumulationState {
    std::vector<AccumulationEntry> entries;
    AccumulationConfig config;
    bool resolved = false;

    void AddEntry(AccumulationEntry entry) {
        entries.push_back(std::move(entry));
    }

    void SortEntries(OrderStrategy strategy);

    [[nodiscard]] bool ValidateCount(std::string& errorMsg) const;

    [[nodiscard]] bool ValidateDuplicates(std::string& errorMsg) const;
};

// ============================================================================
// ACCUMULATION CONNECTION RULE
// ============================================================================

/**
 * @brief Rule for accumulation (multi-connect) connections
 *
 * Handles slots that accept multiple connections merged into a vector<T>.
 * This is the key enabler for MultiDispatchNode and similar patterns.
 *
 * Matches when:
 * - Target has SlotFlags::Accumulation
 *
 * Validation:
 * - Source is an output slot
 * - Target has Accumulation flag
 * - Connection count within [minConnections, maxConnections]
 * - No duplicate sort keys (if !allowDuplicateKeys)
 * - Ordering metadata present (if ExplicitOrder flag set)
 *
 * Resolution:
 * - Adds entry to AccumulationState
 * - Sorts based on OrderStrategy
 * - Flattens vector<T> sources during final resolve
 */
class AccumulationConnectionRule : public ConnectionRule {
public:
    [[nodiscard]] bool CanHandle(
        const SlotInfo& source,
        const SlotInfo& target) const override;

    [[nodiscard]] ConnectionResult Validate(const ConnectionContext& ctx) const override;

    ConnectionResult Resolve(ConnectionContext& ctx) const override;

    [[nodiscard]] uint32_t Priority() const override { return 100; }

    [[nodiscard]] std::string_view Name() const override { return "AccumulationConnectionRule"; }
};

} // namespace Vixen::RenderGraph
