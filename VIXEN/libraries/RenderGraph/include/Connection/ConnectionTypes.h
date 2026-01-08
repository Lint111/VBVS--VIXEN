#pragma once

/**
 * @file ConnectionTypes.h
 * @brief Core types for the connection system
 *
 * Contains:
 * - BindingDescriptor: Legacy binding info extraction
 * - ConnectionContext: All info available during connection
 * - ConnectionResult: Outcome of connection attempt
 */

#include "Data/Core/ResourceConfig.h"
#include "Data/Core/ResourceTypes.h"
#include "Data/Core/SlotInfo.h"
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Vixen::RenderGraph {

// Forward declarations
class NodeInstance;
class RenderGraph;
class Resource;

// ============================================================================
// BINDING DESCRIPTOR (Legacy compatibility)
// ============================================================================

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
// CONNECTION CONTEXT
// ============================================================================

/**
 * @brief Context provided to ConnectionRule and ConnectionModifier methods
 *
 * Contains all information available when making a connection:
 * - Source and target SlotInfo (unified representation)
 * - Node instances
 * - Optional connection metadata (ordering, role hints)
 * - Graph reference for resource creation
 * - Modifier support fields (effective type, lifetime)
 */
struct ConnectionContext {
    // Source information (SlotInfo includes field extraction if needed)
    NodeInstance* sourceNode = nullptr;
    SlotInfo sourceSlot;

    // Target information - unified SlotInfo handles both slots and bindings
    NodeInstance* targetNode = nullptr;
    SlotInfo targetSlot;

    // Legacy: Optional separate binding descriptor for transition period
    std::optional<BindingDescriptor> targetBinding;

    // Connection metadata (for accumulation ordering)
    int32_t sortKey = 0;
    SlotRole roleOverride = SlotRole::None;

    // Graph context
    RenderGraph* graph = nullptr;

    // Array index (for array slot connections)
    uint32_t arrayIndex = 0;

    // ========================================================================
    // MODIFIER SUPPORT
    // ========================================================================

    /// Source resource lifetime (for field extraction validation)
    ResourceLifetime sourceLifetime = ResourceLifetime::Transient;

    /// Effective resource type after modifier transforms
    ResourceType effectiveResourceType = ResourceType::PassThroughStorage;

    /// Whether effectiveResourceType has been explicitly set by a modifier
    bool hasEffectiveTypeOverride = false;

    // ========================================================================
    // ACCUMULATION SUPPORT
    // ========================================================================

    /// Opaque pointer to AccumulationState for accumulation connections
    /// Type-erased to avoid circular dependency with AccumulationConnectionRule.h
    void* accumulationState = nullptr;

    /// Skip dependency registration in Resolve (for unit tests with mock nodes)
    bool skipDependencyRegistration = false;

    // ========================================================================
    // DEBUG SUPPORT
    // ========================================================================

    /// Optional debug tag for visualization/logging
    std::string debugTag;

    // ========================================================================
    // HELPER ACCESSORS
    // ========================================================================

    [[nodiscard]] bool IsVariadic() const {
        return targetSlot.IsBinding();
    }

    [[nodiscard]] bool HasFieldExtraction() const {
        return sourceSlot.hasFieldExtraction;
    }

    [[nodiscard]] bool HasMetadata() const {
        return sortKey != 0 || roleOverride != SlotRole::None;
    }

    [[nodiscard]] bool TargetIsAccumulation() const {
        return targetSlot.IsAccumulation();
    }

    [[nodiscard]] ResourceType GetEffectiveSourceType() const {
        if (hasEffectiveTypeOverride) {
            return effectiveResourceType;
        }
        return sourceSlot.resourceType;
    }

    void SetEffectiveResourceType(ResourceType type) {
        effectiveResourceType = type;
        hasEffectiveTypeOverride = true;
    }

    [[nodiscard]] bool IsPersistentSource() const {
        return sourceLifetime == ResourceLifetime::Persistent;
    }
};

// ============================================================================
// CONNECTION RESULT
// ============================================================================

/**
 * @brief Result of a connection validation or resolution
 */
struct ConnectionResult {
    bool success = false;
    bool skipped = false;        ///< If true, modifier was skipped (no-op, not an error)
    std::string errorMessage;

    // Created during resolution
    Resource* createdResource = nullptr;

    static ConnectionResult Success() {
        return {true, false, {}};
    }

    static ConnectionResult Error(std::string_view msg) {
        return {false, false, std::string(msg)};
    }

    /**
     * @brief Graceful skip - modifier doesn't apply, continue without error
     *
     * Used when a RuleConfig is applied to the wrong rule type.
     * Logs a warning but doesn't fail the connection.
     */
    static ConnectionResult Skip(std::string_view reason = "") {
        return {true, true, std::string(reason)};
    }

    [[nodiscard]] bool IsSuccess() const { return success; }
    [[nodiscard]] bool IsSkipped() const { return skipped; }
};

// Forward declaration for ConnectionMeta (defined in ConnectionModifier.h)
class ConnectionModifier;

} // namespace Vixen::RenderGraph
