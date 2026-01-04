#pragma once

#include "Data/Core/ResourceConfig.h"
#include "Data/Core/ConnectionConcepts.h"
#include "Data/Core/SlotInfo.h"  // Unified slot representation
#include <memory>
#include <vector>
#include <string>
#include <string_view>
#include <functional>
#include <optional>

namespace Vixen::RenderGraph {

// Forward declarations
class NodeInstance;
class RenderGraph;
class Resource;

// ============================================================================
// SPRINT 6.0.1: CONNECTION RULE SYSTEM
// ============================================================================
//
// This system provides extensible connection handling via a registry of rules.
// Each rule knows how to handle a specific type of connection (direct,
// accumulation, variadic). The unified Connect() API uses this registry
// to dispatch to the appropriate handler.
//
// Benefits:
// - Single Connect() API for all connection types
// - Extensible: new connection types = new rules, not new APIs
// - Clear separation of concerns: validation, resolution, and wiring
// - Type-safe with helpful error messages
//
// ============================================================================

// ============================================================================
// SLOT/BINDING DESCRIPTORS - Now using unified SlotInfo from SlotInfo.h
// ============================================================================
//
// SlotDescriptor is now an alias for SlotInfo (defined in SlotInfo.h)
// SlotInfo uses X-macros from SlotFields.h for single source of truth
//
// The old SlotDescriptor and BindingDescriptor are replaced by:
// - SlotInfo::FromInputSlot<T>() / FromOutputSlot<T>()
// - SlotInfo::FromBinding()
//

/**
 * @brief Backward compatibility: BindingDescriptor as simple extraction from SlotInfo
 *
 * For APIs that specifically need binding-only info.
 * Prefer using SlotInfo directly for new code.
 */
struct BindingDescriptor {
    uint32_t binding = UINT32_MAX;           ///< Shader binding index
    uint32_t descriptorType = 0;             ///< VkDescriptorType
    std::string_view name;                   ///< Binding name (for debugging)

    /**
     * @brief Create from BindingReference concept
     */
    template<typename BindingRefType>
        requires BindingReference<BindingRefType>
    static BindingDescriptor FromBinding(const BindingRefType& ref, std::string_view bindingName = "") {
        BindingDescriptor desc;
        desc.binding = ref.binding;
        desc.descriptorType = static_cast<uint32_t>(ref.descriptorType);
        desc.name = bindingName;
        return desc;
    }

    /**
     * @brief Extract from SlotInfo (for bindings)
     */
    static BindingDescriptor FromSlotInfo(const SlotInfo& info) {
        BindingDescriptor desc;
        desc.binding = info.binding;
        desc.descriptorType = static_cast<uint32_t>(info.descriptorType);
        desc.name = info.name;
        return desc;
    }
};

// ============================================================================
// CONNECTION CONTEXT: All info available during connection
// ============================================================================
//
// NOTE: Field extraction is now part of SlotInfo via WithFieldExtraction()
//

/**
 * @brief Context provided to ConnectionRule methods
 *
 * Contains all information available when making a connection:
 * - Source and target SlotInfo (unified representation)
 * - Node instances
 * - Optional connection metadata (ordering, role hints)
 * - Graph reference for resource creation
 *
 * Now uses unified SlotInfo which includes field extraction support.
 */
struct ConnectionContext {
    // Source information (SlotInfo includes field extraction if needed)
    NodeInstance* sourceNode = nullptr;
    SlotInfo sourceSlot;

    // Target information - unified SlotInfo handles both slots and bindings
    NodeInstance* targetNode = nullptr;
    SlotInfo targetSlot;                           ///< For all target types

    // Legacy: Optional separate binding descriptor for transition period
    std::optional<BindingDescriptor> targetBinding; ///< Deprecated: use targetSlot.IsBinding()

    // Connection metadata (for accumulation ordering)
    int32_t sortKey = 0;
    SlotRole roleOverride = SlotRole::None;

    // Graph context
    RenderGraph* graph = nullptr;

    // Array index (for array slot connections)
    uint32_t arrayIndex = 0;

    // Helper accessors
    [[nodiscard]] bool IsVariadic() const {
        return targetSlot.IsBinding();
    }

    [[nodiscard]] bool HasFieldExtraction() const {
        return sourceSlot.hasFieldExtraction;
    }

    [[nodiscard]] bool HasMetadata() const {
        return sortKey != 0 || roleOverride != SlotRole::None;
    }

    /**
     * @brief Check if target requires accumulation handling
     */
    [[nodiscard]] bool TargetIsAccumulation() const {
        return targetSlot.IsAccumulation();
    }

    /**
     * @brief Get effective resource type (considering field extraction)
     */
    [[nodiscard]] ResourceType GetEffectiveSourceType() const {
        // SlotInfo already updates resourceType when field extraction is added
        return sourceSlot.resourceType;
    }
};

// ============================================================================
// CONNECTION RESULT: Outcome of connection attempt
// ============================================================================

/**
 * @brief Result of a connection validation or resolution
 */
struct ConnectionResult {
    bool success = false;
    std::string errorMessage;

    // Created during resolution
    Resource* createdResource = nullptr;

    static ConnectionResult Success() {
        return {true, {}};
    }

    static ConnectionResult Error(std::string_view msg) {
        return {false, std::string(msg)};
    }
};

// ============================================================================
// CONNECTION RULE: Abstract base class
// ============================================================================

/**
 * @brief Abstract base class for connection handlers
 *
 * Each rule knows how to handle a specific type of connection.
 * Rules are registered with ConnectionRuleRegistry and matched
 * based on source/target slot properties.
 *
 * Lifecycle:
 * 1. CanHandle() - Check if rule applies to this connection
 * 2. Validate() - Check if connection is valid
 * 3. Resolve() - Perform the actual connection wiring
 */
class ConnectionRule {
public:
    virtual ~ConnectionRule() = default;

    /**
     * @brief Check if this rule can handle the given connection
     *
     * Called during rule matching to find the appropriate handler.
     * Should be fast - just check slot flags and types.
     *
     * Now uses unified SlotInfo for both source and target.
     * Use target.IsBinding() to check if it's a variadic connection.
     *
     * @param source Source slot info
     * @param target Target slot info (may be static slot or binding)
     * @return true if this rule can handle the connection
     */
    [[nodiscard]] virtual bool CanHandle(
        const SlotInfo& source,
        const SlotInfo& target) const = 0;

    /**
     * @brief Validate the connection
     *
     * Performs semantic validation: type compatibility, nullability,
     * ordering requirements, etc. Called before Resolve().
     *
     * @param ctx Full connection context
     * @return ConnectionResult with success/error status
     */
    [[nodiscard]] virtual ConnectionResult Validate(const ConnectionContext& ctx) const = 0;

    /**
     * @brief Resolve (execute) the connection
     *
     * Performs the actual wiring: creates resources, registers
     * dependencies, updates topology, etc.
     *
     * @param ctx Full connection context (may be modified)
     * @return ConnectionResult with success/error and created resource
     */
    virtual ConnectionResult Resolve(ConnectionContext& ctx) const = 0;

    /**
     * @brief Priority for rule matching (higher = checked first)
     *
     * When multiple rules could handle a connection, the highest
     * priority rule wins. Default is 0.
     *
     * Suggested priorities:
     * - 100: Specific rules (AccumulationConnectionRule)
     * - 50:  Standard rules (DirectConnectionRule)
     * - 0:   Fallback rules (VariadicConnectionRule)
     */
    [[nodiscard]] virtual uint32_t Priority() const { return 0; }

    /**
     * @brief Human-readable name for debugging
     */
    [[nodiscard]] virtual std::string_view Name() const = 0;
};

// ============================================================================
// CONNECTION RULE REGISTRY
// ============================================================================

/**
 * @brief Registry for connection rules
 *
 * Maintains a prioritized list of rules and finds the appropriate
 * handler for each connection. Used by the unified Connect() API.
 *
 * Usage:
 * ```cpp
 * ConnectionRuleRegistry registry;
 * registry.RegisterRule(std::make_unique<DirectConnectionRule>());
 * registry.RegisterRule(std::make_unique<AccumulationConnectionRule>());
 * registry.RegisterRule(std::make_unique<VariadicConnectionRule>());
 *
 * const ConnectionRule* rule = registry.FindRule(sourceSlot, targetSlot);
 * if (rule) {
 *     auto result = rule->Validate(ctx);
 *     if (result.success) {
 *         result = rule->Resolve(ctx);
 *     }
 * }
 * ```
 */
class ConnectionRuleRegistry {
public:
    ConnectionRuleRegistry() = default;
    ~ConnectionRuleRegistry() = default;

    // Non-copyable, movable
    ConnectionRuleRegistry(const ConnectionRuleRegistry&) = delete;
    ConnectionRuleRegistry& operator=(const ConnectionRuleRegistry&) = delete;
    ConnectionRuleRegistry(ConnectionRuleRegistry&&) = default;
    ConnectionRuleRegistry& operator=(ConnectionRuleRegistry&&) = default;

    /**
     * @brief Register a connection rule
     *
     * Rules are sorted by priority (descending) on insertion.
     */
    void RegisterRule(std::unique_ptr<ConnectionRule> rule);

    /**
     * @brief Find rule that can handle the given connection
     *
     * Searches rules in priority order, returns first that CanHandle().
     *
     * @param source Source slot info
     * @param target Target slot info
     * @return Pointer to matching rule, or nullptr if none found
     */
    [[nodiscard]] const ConnectionRule* FindRule(
        const SlotInfo& source,
        const SlotInfo& target) const;

    /**
     * @brief Get all registered rules (for debugging/introspection)
     */
    [[nodiscard]] const std::vector<std::unique_ptr<ConnectionRule>>& GetRules() const {
        return rules_;
    }

    /**
     * @brief Get number of registered rules
     */
    [[nodiscard]] size_t RuleCount() const {
        return rules_.size();
    }

    /**
     * @brief Create registry with default rules
     *
     * Registers:
     * - DirectConnectionRule (priority 50)
     * - AccumulationConnectionRule (priority 100)
     * - VariadicConnectionRule (priority 0)
     */
    static ConnectionRuleRegistry CreateDefault();

private:
    std::vector<std::unique_ptr<ConnectionRule>> rules_;

    // Re-sort rules by priority (called after RegisterRule)
    void SortByPriority();
};

// ============================================================================
// DIRECT CONNECTION RULE
// ============================================================================

/**
 * @brief Rule for standard 1:1 connections
 *
 * Handles any direct connection where one source connects to one target.
 * Works for both slot-to-slot AND slot-to-binding connections.
 *
 * Matches when:
 * - Target is NOT an accumulation slot (those need AccumulationConnectionRule)
 * - Single source → single target (1:1 relationship)
 *
 * Does NOT match when:
 * - Target has Accumulation flag (multi-connect)
 *
 * Supports:
 * - Slot → Slot connections
 * - Slot → Binding connections (variadic targets)
 * - Field extraction via member pointers
 *
 * Validation:
 * - Type compatibility (source type assignable to target, considering extraction)
 * - Nullability (required inputs must have connection)
 * - No existing connection for slot targets (inputs can only have one driver)
 */
class DirectConnectionRule : public ConnectionRule {
public:
    [[nodiscard]] bool CanHandle(
        const SlotInfo& source,
        const SlotInfo& target) const override;

    [[nodiscard]] ConnectionResult Validate(const ConnectionContext& ctx) const override;

    ConnectionResult Resolve(ConnectionContext& ctx) const override;

    [[nodiscard]] uint32_t Priority() const override { return 50; }

    [[nodiscard]] std::string_view Name() const override { return "DirectConnectionRule"; }
};

// ============================================================================
// ACCUMULATION CONNECTION RULE
// ============================================================================

/**
 * @brief Pending connection in accumulation slot
 *
 * Tracks individual connections before they're resolved into the final array.
 * Supports both single values and iterable containers (C++ IEnumerable equivalent).
 *
 * When resolved:
 * - Single values (isIterable=false) are added directly to accumulation
 * - Iterables (isIterable=true) are flattened into the accumulation (if flattenIterables=true)
 *
 * Storage modes:
 * - ByValue: Copy the source value/elements
 * - ByReference: Store pointer to source
 * - BySpan: Store span view (source must be contiguous)
 */
struct AccumulationEntry {
    NodeInstance* sourceNode = nullptr;
    uint32_t sourceOutputIndex = 0;
    int32_t sortKey = 0;                              ///< For ordering (OrderStrategy::ByMetadata)
    SlotRole roleOverride = SlotRole::None;
    SlotInfo sourceSlot;                              ///< Copy of source slot info
    bool isIterable = false;                          ///< True if source is a container
    bool shouldFlatten = true;                        ///< Flatten iterable or add as single element
    size_t iterableSize = 0;                          ///< Estimated size (for pre-allocation, 0 = unknown)
    AccumulationStorage storageMode = AccumulationStorage::ByValue; ///< How to store the value

    // For sorting
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

    /**
     * @brief Add an entry to the accumulation
     */
    void AddEntry(AccumulationEntry entry) {
        entries.push_back(std::move(entry));
    }

    /**
     * @brief Sort entries based on order strategy
     */
    void SortEntries(OrderStrategy strategy);

    /**
     * @brief Validate connection count constraints
     */
    [[nodiscard]] bool ValidateCount(std::string& errorMsg) const;

    /**
     * @brief Validate no duplicate sort keys (if required)
     */
    [[nodiscard]] bool ValidateDuplicates(std::string& errorMsg) const;
};

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

    [[nodiscard]] uint32_t Priority() const override { return 100; }  // Higher than Direct

    [[nodiscard]] std::string_view Name() const override { return "AccumulationConnectionRule"; }
};

// ============================================================================
// VARIADIC CONNECTION RULE
// ============================================================================

/**
 * @brief Rule for variadic (slot-to-binding) connections
 *
 * Handles connections where:
 * - Source is a static output slot
 * - Target is a shader binding (SlotKind::Binding)
 *
 * This is the connection rule equivalent of ConnectVariadic() in TypedConnection.h.
 * It validates the connection and prepares context for the VariadicTypedNode infrastructure.
 *
 * Matches when:
 * - Target is a binding slot (SlotKind::Binding)
 * - Target is NOT accumulation (those go to AccumulationConnectionRule)
 *
 * Validation:
 * - Source is an output slot
 * - Target has valid binding index
 * - If field extraction: source lifetime is Persistent
 *
 * Resolution:
 * - Prepares context for IVariadicNode::UpdateVariadicSlot()
 * - Actual wiring delegated to caller (uses existing VariadicTypedNode infrastructure)
 *
 * Migration note:
 * - Currently works alongside existing ConnectVariadic() in TypedConnection.h
 * - Future: unified Connect() API will use this rule via ConnectionRuleRegistry
 */
class VariadicConnectionRule : public ConnectionRule {
public:
    [[nodiscard]] bool CanHandle(
        const SlotInfo& source,
        const SlotInfo& target) const override;

    [[nodiscard]] ConnectionResult Validate(const ConnectionContext& ctx) const override;

    ConnectionResult Resolve(ConnectionContext& ctx) const override;

    [[nodiscard]] uint32_t Priority() const override { return 25; }  // Lower than Direct (50)

    [[nodiscard]] std::string_view Name() const override { return "VariadicConnectionRule"; }
};

} // namespace Vixen::RenderGraph
