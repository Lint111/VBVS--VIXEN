#pragma once

/**
 * @file UnifiedConnect.h
 * @brief Sprint 6.0.1: Unified Connection API
 *
 * Provides a single Connect() API for all connection types.
 * Uses C++20 concepts for compile-time overload resolution.
 *
 * Connection types handled:
 * - SlotRef → SlotRef: Direct connection (1:1)
 * - SlotRef → SlotRef (accumulation): Multi-connect with ordering
 * - SlotRef → BindingRef: Variadic connection
 * - SlotRef → BindingRef + memberPtr: Field extraction
 *
 * The appropriate ConnectionRule is selected via ConnectionRuleRegistry.
 * This file provides the user-facing API; wiring delegates to ConnectionBatch.
 *
 * USAGE:
 * - For validation-only (no wiring): include this header directly
 * - For full Connect() API: include after TypedConnection.h
 */

#include "Core/ConnectionRule.h"
#include "Data/Core/ConnectionConcepts.h"
#include "Data/Core/SlotInfo.h"
#include <concepts>
#include <string>

namespace Vixen::RenderGraph {

// Forward declarations - full definitions in TypedConnection.h
class RenderGraph;
class ConnectionBatch;
struct NodeHandle;

// ============================================================================
// VALIDATION HELPERS
// ============================================================================
// These can be used without full ConnectionBatch/RenderGraph includes

/**
 * @brief Validate connection using ConnectionRuleRegistry
 *
 * Checks if a connection is valid before attempting to wire.
 * Uses the unified SlotInfo representation for rule matching.
 *
 * @param registry Rule registry to use for validation
 * @param sourceSlot Source slot info
 * @param targetSlot Target slot info
 * @return ConnectionResult with success/error status
 */
inline ConnectionResult ValidateConnection(
    const ConnectionRuleRegistry& registry,
    const SlotInfo& sourceSlot,
    const SlotInfo& targetSlot
) {
    const ConnectionRule* rule = registry.FindRule(sourceSlot, targetSlot);
    if (!rule) {
        return ConnectionResult::Error("No connection rule found for this slot combination");
    }

    // Create minimal context for validation
    // Use non-null sentinel values for nodes since rules check for null
    ConnectionContext ctx;
    ctx.sourceNode = reinterpret_cast<NodeInstance*>(0x1);  // Sentinel for validation
    ctx.targetNode = reinterpret_cast<NodeInstance*>(0x2);  // Sentinel for validation
    ctx.sourceSlot = sourceSlot;
    ctx.targetSlot = targetSlot;

    return rule->Validate(ctx);
}

/**
 * @brief Create SlotInfo from compile-time slot for validation
 *
 * Helper to convert compile-time slot metadata to runtime SlotInfo
 * for use with ConnectionRuleRegistry.
 */
template<SlotReference SlotT>
SlotInfo CreateSlotInfo(std::string_view name, bool isOutput) {
    if (isOutput) {
        return SlotInfo::FromOutputSlot<SlotT>(name);
    } else {
        return SlotInfo::FromInputSlot<SlotT>(name);
    }
}

// ============================================================================
// UNIFIED CONNECT API - Concept-Constrained Overloads
// ============================================================================
// NOTE: These require ConnectionBatch to be fully defined.
// Include TypedConnection.h before using these functions.

#ifdef VIXEN_UNIFIED_CONNECT_FULL_API

/**
 * @brief Direct connection: SlotRef → SlotRef
 *
 * Standard 1:1 connection between static slots.
 * For accumulation targets, use the overload with ConnectionOrder metadata.
 */
template<SlotReference Src, SlotReference Tgt>
void Connect(
    ConnectionBatch& batch,
    NodeHandle srcNode,
    Src srcSlot,
    NodeHandle tgtNode,
    Tgt tgtSlot,
    uint32_t arrayIndex = 0
) {
    // Check for accumulation flag at compile time
    if constexpr (AccumulationSlot<Tgt>) {
        // Accumulation slots without metadata use default ordering
        static_assert(!Tgt::requiresExplicitOrder,
            "This accumulation slot requires explicit ordering metadata. "
            "Use Connect(batch, src, srcSlot, tgt, tgtSlot, ConnectionOrder{sortKey})");
    }

    // Delegate to ConnectionBatch::Connect
    batch.Connect(srcNode, srcSlot, tgtNode, tgtSlot, arrayIndex);
}

/**
 * @brief Accumulation connection with metadata: SlotRef → AccumulationSlot
 *
 * Connects source to accumulation slot with ordering metadata.
 */
template<SlotReference Src, AccumulationSlot Tgt>
void Connect(
    ConnectionBatch& batch,
    NodeHandle srcNode,
    Src srcSlot,
    NodeHandle tgtNode,
    Tgt tgtSlot,
    ConnectionOrder order
) {
    // TODO: Accumulation wiring will be added when AccumulationConnectionRule
    // is fully integrated with ConnectionBatch.
    (void)order;  // Suppress unused warning until implementation

    // For now, delegate to regular Connect
    batch.Connect(srcNode, srcSlot, tgtNode, tgtSlot, 0);
}

/**
 * @brief Variadic connection: SlotRef → BindingRef
 *
 * Connects source slot to variadic node's shader binding.
 */
template<SlotReference Src, BindingReference Tgt>
void Connect(
    ConnectionBatch& batch,
    NodeHandle srcNode,
    Src srcSlot,
    NodeHandle tgtNode,
    Tgt bindingRef,
    SlotRole role = SlotRole::Output  // Output = sentinel for auto-detect
) {
    batch.ConnectVariadic(srcNode, srcSlot, tgtNode, bindingRef, role);
}

/**
 * @brief Variadic with field extraction: SlotRef → BindingRef + memberPtr
 *
 * Extracts field from source struct for variadic connection.
 */
template<SlotReference Src, BindingReference Tgt, typename StructT, typename FieldT>
void Connect(
    ConnectionBatch& batch,
    NodeHandle srcNode,
    Src srcSlot,
    NodeHandle tgtNode,
    Tgt bindingRef,
    FieldT StructT::* memberPtr,
    SlotRole role = SlotRole::Output
) {
    batch.ConnectVariadic(srcNode, srcSlot, tgtNode, bindingRef, memberPtr, role);
}

#endif // VIXEN_UNIFIED_CONNECT_FULL_API

} // namespace Vixen::RenderGraph
