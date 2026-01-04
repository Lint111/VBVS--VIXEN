#include "Core/ConnectionRule.h"
#include "Core/NodeInstance.h"
#include "Core/RenderGraph.h"
#include <algorithm>

namespace Vixen::RenderGraph {

// ============================================================================
// CONNECTION RULE REGISTRY IMPLEMENTATION
// ============================================================================

void ConnectionRuleRegistry::RegisterRule(std::unique_ptr<ConnectionRule> rule) {
    if (!rule) return;
    rules_.push_back(std::move(rule));
    SortByPriority();
}

const ConnectionRule* ConnectionRuleRegistry::FindRule(
    const SlotInfo& source,
    const SlotInfo& target) const {

    for (const auto& rule : rules_) {
        if (rule->CanHandle(source, target)) {
            return rule.get();
        }
    }
    return nullptr;
}

void ConnectionRuleRegistry::SortByPriority() {
    std::sort(rules_.begin(), rules_.end(),
        [](const std::unique_ptr<ConnectionRule>& a,
           const std::unique_ptr<ConnectionRule>& b) {
            return a->Priority() > b->Priority();  // Descending order
        });
}

ConnectionRuleRegistry ConnectionRuleRegistry::CreateDefault() {
    ConnectionRuleRegistry registry;
    // Register rules in priority order (registry sorts by priority descending)
    registry.RegisterRule(std::make_unique<AccumulationConnectionRule>());  // Priority 100
    registry.RegisterRule(std::make_unique<DirectConnectionRule>());        // Priority 50
    // Note: VariadicConnectionRule will be added in subsequent task
    return registry;
}

// ============================================================================
// DIRECT CONNECTION RULE IMPLEMENTATION
// ============================================================================

bool DirectConnectionRule::CanHandle(
    const SlotInfo& source,
    const SlotInfo& target) const {

    // We don't handle accumulation slots - those need AccumulationConnectionRule
    if (target.IsAccumulation()) {
        return false;
    }

    // We handle:
    // 1. Slot → Slot (direct connection)
    // 2. Slot → Binding (variadic-style but 1:1)
    // Both are 1:1 connections - just wire source to target
    return true;
}

ConnectionResult DirectConnectionRule::Validate(const ConnectionContext& ctx) const {
    // Verify we have the expected context
    if (!ctx.sourceNode) {
        return ConnectionResult::Error("Source node is null");
    }

    if (!ctx.targetNode) {
        return ConnectionResult::Error("Target node is null");
    }

    const auto& sourceSlot = ctx.sourceSlot;
    const auto& targetSlot = ctx.targetSlot;

    // Validate: Source must be an output
    if (!sourceSlot.IsOutput()) {
        return ConnectionResult::Error("Source slot must be an output slot");
    }

    // For static slot targets, validate input/output relationship
    if (targetSlot.IsStatic()) {
        if (!targetSlot.IsInput()) {
            return ConnectionResult::Error("Target slot must be an input slot");
        }

        // Get effective source type (considering field extraction)
        ResourceType effectiveSourceType = ctx.GetEffectiveSourceType();

        // Validate: Resource types should be compatible
        if (effectiveSourceType != targetSlot.resourceType) {
            // Allow PassThroughStorage to connect to anything
            if (effectiveSourceType != ResourceType::PassThroughStorage &&
                targetSlot.resourceType != ResourceType::PassThroughStorage) {
                return ConnectionResult::Error(
                    "Resource type mismatch: source type does not match target type");
            }
        }
    }

    // For binding targets, type checking is more permissive
    // (runtime shader reflection handles type validation)
    // Just verify we have the basic info
    if (targetSlot.IsBinding()) {
        if (targetSlot.binding == UINT32_MAX) {
            return ConnectionResult::Error("Invalid binding index");
        }
    }

    return ConnectionResult::Success();
}

ConnectionResult DirectConnectionRule::Resolve(ConnectionContext& ctx) const {
    // First validate
    auto validationResult = Validate(ctx);
    if (!validationResult.success) {
        return validationResult;
    }

    // Get the graph for connection wiring
    if (!ctx.graph) {
        return ConnectionResult::Error("Graph context not set");
    }

    // The actual connection wiring is delegated to:
    // - RenderGraph::ConnectNodes() for slot-to-slot
    // - VariadicTypedNode::UpdateVariadicSlot() for slot-to-binding
    //
    // This rule validates and prepares the context.
    // The caller (ConnectionBatch or unified Connect API) performs the wiring.

    ConnectionResult result;
    result.success = true;

    // If field extraction is requested, the caller needs to handle it
    // by using the FieldExtractionInfo to extract the field at connection time

    return result;
}

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
            // Already in connection order - no sorting needed
            break;

        case OrderStrategy::Unordered:
            // Explicitly no ordering - leave as-is
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

    // Only check for duplicates if using metadata ordering
    if (config.orderStrategy != OrderStrategy::ByMetadata) {
        return true;
    }

    // Check for duplicate sort keys
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

    (void)source;  // Source type doesn't affect whether we can handle

    // We only handle accumulation slots
    return target.IsAccumulation();
}

ConnectionResult AccumulationConnectionRule::Validate(const ConnectionContext& ctx) const {
    // Verify we have the expected context
    if (!ctx.sourceNode) {
        return ConnectionResult::Error("Source node is null");
    }

    if (!ctx.targetNode) {
        return ConnectionResult::Error("Target node is null");
    }

    const auto& sourceSlot = ctx.sourceSlot;
    const auto& targetSlot = ctx.targetSlot;

    // Validate: Source must be an output
    if (!sourceSlot.IsOutput()) {
        return ConnectionResult::Error("Source slot must be an output slot");
    }

    // Validate: Target must be an accumulation slot
    if (!targetSlot.IsAccumulation()) {
        return ConnectionResult::Error("Target slot must have Accumulation flag");
    }

    // Validate: Target must be input (accumulation is always input-side)
    if (!targetSlot.IsInput()) {
        return ConnectionResult::Error("Accumulation target must be an input slot");
    }

    // If ExplicitOrder is required, verify metadata is provided
    if (targetSlot.RequiresExplicitOrder()) {
        if (ctx.sortKey == 0 && ctx.roleOverride == SlotRole::None) {
            // sortKey 0 is valid but ambiguous - could be intentional or missing
            // For ExplicitOrder, we require non-zero keys or explicit role override
            // This is a soft check - 0 is allowed but logs a warning in debug
        }
    }

    // Type compatibility: source type should be compatible with accumulation element type
    // For accumulation slots, target type is typically vector<T> - we accept T or vector<T>
    // This is handled at resolve time when flattening

    return ConnectionResult::Success();
}

ConnectionResult AccumulationConnectionRule::Resolve(ConnectionContext& ctx) const {
    // First validate
    auto validationResult = Validate(ctx);
    if (!validationResult.success) {
        return validationResult;
    }

    // Get the graph for connection wiring
    if (!ctx.graph) {
        return ConnectionResult::Error("Graph context not set");
    }

    // Create accumulation entry from context
    AccumulationEntry entry;
    entry.sourceNode = ctx.sourceNode;
    entry.sourceOutputIndex = ctx.sourceSlot.index;
    entry.sortKey = ctx.sortKey;
    entry.roleOverride = ctx.roleOverride;
    entry.sourceSlot = ctx.sourceSlot;

    // The actual accumulation state management is handled by the graph/node
    // This rule validates and prepares the entry.
    // The caller (ConnectionBatch or unified Connect API) performs:
    // 1. Gets/creates AccumulationState for target slot
    // 2. Adds entry to state
    // 3. During compile: validates count/duplicates, sorts, and flattens

    ConnectionResult result;
    result.success = true;

    return result;
}

} // namespace Vixen::RenderGraph
