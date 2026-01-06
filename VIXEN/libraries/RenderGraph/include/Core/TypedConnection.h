#pragma once

#include "Core/RenderGraph.h"
#include "Core/GraphLifecycleHooks.h"
#include "Data/Core/ResourceConfig.h"
#include "Data/Core/ConnectionConcepts.h"  // ConnectionOrder
#include "Core/GraphTopology.h"
#include "Core/VariadicTypedNode.h"
#include "Connection/Rules/AccumulationConnectionRule.h"  // AccumulationState, AccumulationEntry
#include "Connection/ConnectionRuleRegistry.h"
#include "Connection/ConnectionPipeline.h"
#include "Connection/ConnectionModifier.h"  // ConnectionMeta, ConnectionModifier
#include "Connection/Modifiers/FieldExtractionModifier.h"
#include "Connection/Modifiers/SlotRoleModifier.h"
#include "Connection/Modifiers/AccumulationSortConfig.h"
#include "Data/Nodes/DescriptorResourceGathererNodeConfig.h"
#include "Data/Nodes/PushConstantGathererNodeConfig.h"
#include "Data/Nodes/StructSpreaderNodeConfig.h"
#include <vector>
#include <functional>

namespace Vixen::RenderGraph {

// Forward declare PassThroughStorage for type checking
class PassThroughStorage;

/**
 * @brief Type compatibility checker for slot connections
 *
 * Enforces strict type matching for connections:
 * - `const T&` can connect to `const T&` ✓
 * - `T&` can connect to `T&` or `const T&` ✓
 * - `T` can connect to `T` ✓
 * - `const T&` CANNOT connect to `T` (value) ✗
 * - `T&` CANNOT connect to `T` (value) ✗
 *
 * Special case: `PassThroughStorage` (generic type-erased storage) can connect to any type.
 * This allows ConstantNode (which outputs PassThroughStorage) to connect to typed inputs.
 *
 * Reference-based connections (const T&, T&) require exact reference semantics.
 * Adding const is allowed (T& → const T&), removing const is not (const T& → T&).
 */
template<typename SourceType, typename TargetType>
struct AreTypesCompatible {
    // Strip references and cv-qualifiers to get base types
    using SourceBase = std::remove_cv_t<std::remove_reference_t<SourceType>>;
    using TargetBase = std::remove_cv_t<std::remove_reference_t<TargetType>>;

    // PassThroughStorage is type-erased and can connect to anything
    static constexpr bool isGenericSource = std::is_same_v<SourceBase, PassThroughStorage>;
    static constexpr bool isGenericTarget = std::is_same_v<TargetBase, PassThroughStorage>;

    // Check if types are exactly the same
    static constexpr bool exactMatch = std::is_same_v<SourceType, TargetType>;

    // Check if we're adding const to a reference (T& → const T&)
    // This is safe because we're not removing const
    static constexpr bool addingConst =
        std::is_lvalue_reference_v<SourceType> &&
        std::is_lvalue_reference_v<TargetType> &&
        !std::is_const_v<std::remove_reference_t<SourceType>> &&
        std::is_const_v<std::remove_reference_t<TargetType>> &&
        std::is_same_v<
            std::remove_cv_t<std::remove_reference_t<SourceType>>,
            std::remove_cv_t<std::remove_reference_t<TargetType>>
        >;

    // Types are compatible if:
    // 1. Exactly the same
    // 2. Adding const to reference
    // 3. Source or target is PassThroughStorage (generic type-erased)
    static constexpr bool value = exactMatch || addingConst || isGenericSource || isGenericTarget;
};

template<typename SourceType, typename TargetType>
inline constexpr bool AreTypesCompatible_v = AreTypesCompatible<SourceType, TargetType>::value;

// ============================================================================
// SPRINT 6.0.1: ACCUMULATION TYPE HELPERS
// ============================================================================

/**
 * @brief Get the accumulated type for an element type
 *
 * For accumulation slots, the slot type is the element type T,
 * but storage and In() return type is std::vector<T>.
 *
 * Usage:
 *   using AccType = AccumulatedType<bool>::type;  // std::vector<bool>
 */
template<typename ElementType>
struct AccumulatedType {
    using type = std::vector<ElementType>;
};

template<typename ElementType>
using AccumulatedType_t = typename AccumulatedType<ElementType>::type;

/**
 * @brief Pending connection for deferred execution via pipeline
 *
 * Sprint 6.0.1: Stores validated connection context and matching rule
 * for deferred execution in RegisterAll().
 */
struct PendingConnection {
    ConnectionContext context;
    const ConnectionRule* rule = nullptr;

    // Optional modifiers to apply (field extraction, etc.)
    std::vector<std::unique_ptr<ConnectionModifier>> modifiers;
};

/**
 * @brief Connection builder with batch edge registration
 *
 * Sprint 6.0.1: Unified connection API with modifier-based customization.
 *
 * Single Connect() API handles all connection types:
 * - Direct (slot → slot)
 * - Accumulation (slot → accumulation slot)
 * - Variadic (slot → shader binding)
 *
 * Rule selection is automatic based on slot types. Customization via modifiers:
 * - AccumulationSortConfig: Set sort key for accumulation ordering
 * - SlotRoleModifier: Override dependency/execute role
 * - FieldExtractionModifier: Extract field from struct output
 *
 * Example usage:
 * @code
 *   ConnectionBatch batch(renderGraph);
 *
 *   // Direct connection
 *   batch.Connect(deviceNode, DeviceConfig::DEVICE,
 *                 swapchainNode, SwapChainConfig::DEVICE);
 *
 *   // Accumulation with ordering
 *   batch.Connect(passNode, PassConfig::OUTPUT,
 *                 multiDispatch, MultiDispatchConfig::PASSES,
 *                 ConnectionMeta{}.With<AccumulationSortConfig>(5));
 *
 *   // Variadic with role override
 *   batch.Connect(textureNode, TextureConfig::IMAGE_VIEW,
 *                 gatherer, ComputeShader::inputImage,
 *                 ConnectionMeta{}.With<SlotRoleModifier>(SlotRole::Execute));
 *
 *   // Field extraction
 *   batch.Connect(swapchain, SwapChainConfig::PUBLIC,
 *                 gatherer, ComputeShader::output,
 *                 ConnectionMeta{}.With(ExtractField(&SwapChainVars::colorBuffer)));
 *
 *   batch.RegisterAll();
 * @endcode
 */
class ConnectionBatch {
public:
    explicit ConnectionBatch(RenderGraph* graph)
        : graph(graph)
        , registry_(ConnectionRuleRegistry::CreateDefault()) {}

    // ========================================================================
    // UNIFIED CONNECT API
    // ========================================================================

    /**
     * @brief Unified connection API - handles all connection types
     *
     * Type compatibility is checked at compile-time. Rule selection is
     * automatic based on slot types. Customize via ConnectionMeta modifiers.
     *
     * Supports three target types:
     * 1. Static slots (from node configs) - detected by TargetSlot::index
     * 2. SDI binding types (from shader reflection) - detected by TargetSlot::BINDING
     * 3. Legacy binding refs (instance members) - detected by t.binding
     *
     * @tparam SourceSlot Output slot type (from node config)
     * @tparam TargetSlot Input slot, SDI binding type, or legacy binding ref
     * @param sourceNode Handle to source node
     * @param sourceSlot Output slot constant
     * @param targetNode Handle to target node
     * @param targetSlot Input slot or binding constant
     * @param meta Optional modifiers (AccumulationSortConfig, SlotRoleModifier, etc.)
     */
    template<typename SourceSlot, typename TargetSlot>
    ConnectionBatch& Connect(
        NodeHandle sourceNode,
        SourceSlot sourceSlot,
        NodeHandle targetNode,
        TargetSlot targetSlot,
        ConnectionMeta meta = {}
    ) {
        // Create pending connection
        PendingConnection pending;
        pending.context.sourceNode = graph->GetInstance(sourceNode);
        pending.context.targetNode = graph->GetInstance(targetNode);
        pending.context.graph = graph;

        // Build source SlotInfo from compile-time slot type
        pending.context.sourceSlot = SlotInfo::FromOutputSlot<SourceSlot>("");
        pending.context.sourceSlot.index = SourceSlot::index;

        // Build target SlotInfo - handle static slots, SDI bindings, legacy bindings, and raw integers
        if constexpr (requires { TargetSlot::BINDING; TargetSlot::DESCRIPTOR_TYPE; }) {
            // Target is an SDI-style binding type (uppercase static members)
            // Usage: batch.Connect(src, slot, tgt, VoxelRayMarch::esvoNodes{}, meta)
            pending.context.targetSlot = SlotInfo::FromBindingType<TargetSlot>("");
        } else if constexpr (requires { TargetSlot::binding; }) {
            // Target is a legacy binding reference (lowercase instance members)
            pending.context.targetSlot = SlotInfo::FromBinding(targetSlot, TargetSlot::name);
        } else if constexpr (std::is_integral_v<TargetSlot>) {
            // Target is a raw binding index (uint32_t, int, etc.)
            // Used by deprecated ConnectVariadic - prefer SDI binding types for new code
            pending.context.targetSlot = SlotInfo{};
            pending.context.targetSlot.binding = static_cast<uint32_t>(targetSlot);
            pending.context.targetSlot.kind = SlotKind::Binding;
            pending.context.targetSlot.state = SlotState::Tentative;
            pending.context.targetSlot.descriptorType = VK_DESCRIPTOR_TYPE_MAX_ENUM;
        } else if constexpr (requires { TargetSlot::index; }) {
            // Target is a static slot
            pending.context.targetSlot = SlotInfo::FromInputSlot<TargetSlot>("");
            pending.context.targetSlot.index = TargetSlot::index;

            // NOTE: Type checking moved to runtime to support field extraction.
            // When ConnectionMeta contains FieldExtractionModifier, the source type
            // differs from what's actually connected. Runtime validation in
            // the connection pipeline will catch type mismatches.
            //
            // For direct connections without field extraction, consider using
            // ConnectDirect() (if added) which can provide compile-time type safety.
        } else {
            static_assert(sizeof(TargetSlot) == 0,
                "TARGET TYPE ERROR: TargetSlot must be either a static slot (with ::index), "
                "an SDI binding type (with ::BINDING, ::DESCRIPTOR_TYPE), "
                "a legacy binding reference (with .binding, .descriptorType), "
                "or an integral binding index (uint32_t)");
        }

        // Find matching rule
        pending.rule = registry_.FindRule(pending.context.sourceSlot, pending.context.targetSlot);

        // Transfer modifiers from meta
        pending.modifiers = std::move(meta.modifiers);

        // Store for RegisterAll
        pendingConnections_.push_back(std::move(pending));
        return *this;
    }

    /**
     * @brief Streamlined connection with implicit modifier construction
     *
     * Accepts modifiers directly without ConnectionMeta{} boilerplate.
     * Enables cleaner syntax for common case (1-3 modifiers).
     *
     * Examples:
     * @code
     * // Single modifier - no ConnectionMeta{}
     * batch.Connect(cameraNode, CameraNodeConfig::CAMERA_DATA,
     *               gatherer, VoxelRayMarch::cameraPos::BINDING,
     *               ExtractField(&CameraData::cameraPos, SlotRole::Execute));
     *
     * // Multiple modifiers - comma-separated
     * batch.Connect(nodeA, ConfigA::OUTPUT,
     *               nodeB, ConfigB::INPUT,
     *               ExtractField(&MyStruct::field),
     *               SlotRoleModifier(SlotRole::Execute));
     * @endcode
     *
     * @tparam SourceSlot Output slot type (from node config)
     * @tparam TargetSlot Input slot, SDI binding type, or legacy binding ref
     * @tparam Modifiers Variadic modifier types (must derive from ConnectionModifier)
     * @param sourceNode Handle to source node
     * @param sourceSlot Output slot constant
     * @param targetNode Handle to target node
     * @param targetSlot Input slot or binding constant
     * @param modifiers Modifiers to apply (ExtractField, SlotRoleModifier, etc.)
     */
    template<typename SourceSlot, typename TargetSlot, typename... Modifiers>
        requires (sizeof...(Modifiers) > 0) &&  // At least one modifier
                 (std::derived_from<std::remove_cvref_t<Modifiers>, ConnectionModifier> && ...)
    ConnectionBatch& Connect(
        NodeHandle sourceNode,
        SourceSlot sourceSlot,
        NodeHandle targetNode,
        TargetSlot targetSlot,
        Modifiers&&... modifiers
    ) {
        // Construct ConnectionMeta from modifiers
        ConnectionMeta meta;

        // Fold expression to move each modifier into meta
        ([&meta, &modifiers] {
            using ModType = std::remove_cvref_t<decltype(modifiers)>;
            meta.modifiers.push_back(
                std::make_unique<ModType>(std::forward<decltype(modifiers)>(modifiers))
            );
        }(), ...);

        // Delegate to existing Connect() with constructed meta
        return Connect(sourceNode, sourceSlot, targetNode, targetSlot, std::move(meta));
    }

    // ========================================================================
    // LEGACY COMPATIBILITY (deprecated - use Connect with modifiers)
    // ========================================================================

    /**
     * @brief [DEPRECATED] Connect to accumulation slot with ordering
     *
     * Use Connect() with AccumulationSortConfig modifier instead:
     * @code
     * batch.Connect(src, SrcConfig::OUT, tgt, TgtConfig::ACCUM,
     *               ConnectionMeta{}.With<AccumulationSortConfig>(sortKey));
     * @endcode
     */
    template<typename SourceSlot, typename TargetSlot>
    [[deprecated("Use Connect() with ConnectionMeta{}.With<AccumulationSortConfig>(sortKey)")]]
    ConnectionBatch& ConnectAccumulation(
        NodeHandle sourceNode,
        SourceSlot sourceSlot,
        NodeHandle targetNode,
        TargetSlot targetSlot,
        ConnectionOrder order
    ) {
        return Connect(sourceNode, sourceSlot, targetNode, targetSlot,
                       ConnectionMeta{}.With<AccumulationSortConfig>(order.sortKey));
    }

    /**
     * @brief [DEPRECATED] Connect to variadic node
     *
     * Use Connect() with SlotRoleModifier if needed:
     * @code
     * batch.Connect(src, SrcConfig::OUT, gatherer, Shader::binding,
     *               ConnectionMeta{}.With<SlotRoleModifier>(role));
     * @endcode
     */
    template<typename SourceSlot, typename BindingRefType>
    [[deprecated("Use Connect() with ConnectionMeta{}.With<SlotRoleModifier>(role)")]]
    ConnectionBatch& ConnectVariadic(
        NodeHandle sourceNode,
        SourceSlot sourceSlot,
        NodeHandle variadicNode,
        BindingRefType bindingRef,
        SlotRole slotRoleOverride = SlotRole::Output
    ) {
        ConnectionMeta meta;
        if (slotRoleOverride != SlotRole::Output) {
            meta = ConnectionMeta{}.With<SlotRoleModifier>(slotRoleOverride);
        }
        return Connect(sourceNode, sourceSlot, variadicNode, bindingRef, std::move(meta));
    }

    /**
     * @brief [DEPRECATED] Connect to variadic with field extraction
     *
     * Use Connect() with ExtractField() modifier:
     * @code
     * batch.Connect(src, SrcConfig::STRUCT, gatherer, Shader::binding,
     *               ConnectionMeta{}.With(ExtractField(&Struct::field)));
     * @endcode
     */
    template<typename SourceSlot, typename BindingRefType, typename StructType, typename FieldType>
    [[deprecated("Use Connect() with ConnectionMeta{}.With(ExtractField(&Struct::field))")]]
    ConnectionBatch& ConnectVariadic(
        NodeHandle sourceNode,
        SourceSlot sourceSlot,
        NodeHandle variadicNode,
        BindingRefType bindingRef,
        FieldType StructType::* memberPtr,
        SlotRole slotRoleOverride = SlotRole::Output
    ) {
        ConnectionMeta meta = ConnectionMeta{}.With(ExtractField(memberPtr));
        if (slotRoleOverride != SlotRole::Output) {
            meta = std::move(meta).With<SlotRoleModifier>(slotRoleOverride);
        }
        return Connect(sourceNode, sourceSlot, variadicNode, bindingRef, std::move(meta));
    }

    // ========================================================================
    // ARRAY AND CONSTANT HELPERS
    // ========================================================================

    /**
     * @brief Connect source output to multiple array elements of target input
     *
     * For arrayable inputs (e.g., multiple framebuffers, multiple images).
     * Creates one connection per array index.
     *
     * @param sourceNode Handle to source node
     * @param sourceSlot Output slot constant
     * @param targetNode Handle to target node
     * @param targetSlot Input slot constant (must support arrays)
     * @param arrayIndices List of array indices to connect to
     * @param meta Optional modifiers applied to each connection
     */
    template<typename SourceSlot, typename TargetSlot>
    ConnectionBatch& ConnectToArray(
        NodeHandle sourceNode,
        SourceSlot sourceSlot,
        NodeHandle targetNode,
        TargetSlot targetSlot,
        const std::vector<uint32_t>& arrayIndices,
        ConnectionMeta meta = {}
    ) {
        for (size_t i = 0; i < arrayIndices.size(); ++i) {
            // Clone modifiers for each connection (first one uses original)
            if (i == 0) {
                // Create context with array index
                PendingConnection pending;
                pending.context.sourceNode = graph->GetInstance(sourceNode);
                pending.context.targetNode = graph->GetInstance(targetNode);
                pending.context.graph = graph;
                pending.context.arrayIndex = arrayIndices[i];

                pending.context.sourceSlot = SlotInfo::FromOutputSlot<SourceSlot>("");
                pending.context.sourceSlot.index = SourceSlot::index;
                pending.context.targetSlot = SlotInfo::FromInputSlot<TargetSlot>("");
                pending.context.targetSlot.index = TargetSlot::index;

                pending.rule = registry_.FindRule(pending.context.sourceSlot, pending.context.targetSlot);
                pending.modifiers = std::move(meta.modifiers);
                pendingConnections_.push_back(std::move(pending));
            } else {
                // Subsequent indices get no modifiers (meta was moved)
                PendingConnection pending;
                pending.context.sourceNode = graph->GetInstance(sourceNode);
                pending.context.targetNode = graph->GetInstance(targetNode);
                pending.context.graph = graph;
                pending.context.arrayIndex = arrayIndices[i];

                pending.context.sourceSlot = SlotInfo::FromOutputSlot<SourceSlot>("");
                pending.context.sourceSlot.index = SourceSlot::index;
                pending.context.targetSlot = SlotInfo::FromInputSlot<TargetSlot>("");
                pending.context.targetSlot.index = TargetSlot::index;

                pending.rule = registry_.FindRule(pending.context.sourceSlot, pending.context.targetSlot);
                pendingConnections_.push_back(std::move(pending));
            }
        }
        return *this;
    }

    /**
     * @brief Connect a constant/direct value to a node input
     *
     * Sets input values directly without creating placeholder nodes.
     * Useful for passing raw pointers, constants, or external resources.
     *
     * @param targetNode Handle to target node
     * @param targetSlot Input slot constant
     * @param value Direct value to set as input
     * @param arrayIndex Array index for arrayable inputs (default: 0)
     */
    template<typename TargetSlot, typename ValueType>
    ConnectionBatch& ConnectConstant(
        NodeHandle targetNode,
        TargetSlot targetSlot,
        ValueType value,
        uint32_t arrayIndex = 0
    ) {
        using SlotType = typename TargetSlot::Type;
        static_assert(
            std::is_same_v<SlotType, ValueType> || std::is_convertible_v<ValueType, SlotType>,
            "Value type must match or be convertible to slot type"
        );

        constantConnections.push_back([this, targetNode, targetSlot, value, arrayIndex]() {
            auto* node = graph->GetInstance(targetNode);
            if (!node) {
                throw std::runtime_error("ConnectConstant: Invalid target node handle");
            }

            Resource res = Resource::Create<SlotType>(typename ResourceTypeTraits<SlotType>::DescriptorT{});
            res.SetHandle(value);
            node->SetInput(targetSlot.index, arrayIndex, &res);
        });

        return *this;
    }

    // ========================================================================
    // BATCH EXECUTION
    // ========================================================================

    /**
     * @brief Register all connections with the RenderGraph
     *
     * Executes each pending connection through the pipeline with its modifiers.
     * All logic is delegated to rules and modifiers.
     */
    void RegisterAll() {
        // Process all connections through pipeline
        for (auto& pending : pendingConnections_) {
            if (pending.rule) {
                // Create pipeline and add modifiers for this connection
                ConnectionPipeline pipeline;
                for (auto& mod : pending.modifiers) {
                    pipeline.AddModifier(std::move(mod));
                }

                auto result = pipeline.Execute(pending.context, *pending.rule);
                if (!result.success) {
                    throw std::runtime_error("Connection failed: " + result.errorMessage);
                }
            }
        }
        pendingConnections_.clear();

        // Apply constant connections
        for (auto& constantConn : constantConnections) {
            constantConn();
        }
        constantConnections.clear();
    }


    /**
     * @brief Get number of pending connections
     */
    size_t GetConnectionCount() const { return pendingConnections_.size(); }

    /**
     * @brief Clear all pending connections without registering
     */
    void Clear() {
        pendingConnections_.clear();
        constantConnections.clear();
    }

private:
    RenderGraph* graph;
    ConnectionRuleRegistry registry_;                      ///< Rule registry (owns rules)
    std::vector<PendingConnection> pendingConnections_;    ///< Pending connections for RegisterAll
    std::vector<std::function<void()>> constantConnections; ///< SetConstant lambdas
};

// ============================================================================
// FREE-STANDING HELPERS (immediate registration)
// ============================================================================

/**
 * @brief Simplified single-connection helper (immediate registration)
 *
 * For quick one-off connections without batching.
 *
 * @code
 * Connect(graph, sourceNode, SourceConfig::OUTPUT, targetNode, TargetConfig::INPUT);
 *
 * // With modifiers
 * Connect(graph, src, SrcConfig::OUT, tgt, TgtConfig::ACCUM,
 *         ConnectionMeta{}.With<AccumulationSortConfig>(5));
 * @endcode
 */
template<typename SourceSlot, typename TargetSlot>
inline void Connect(
    RenderGraph* graph,
    NodeHandle sourceNode,
    SourceSlot sourceSlot,
    NodeHandle targetNode,
    TargetSlot targetSlot,
    ConnectionMeta meta = {}
) {
    ConnectionBatch(graph)
        .Connect(sourceNode, sourceSlot, targetNode, targetSlot, std::move(meta))
        .RegisterAll();
}

/**
 * @brief Helper for array connections (immediate registration)
 *
 * @code
 * ConnectToArray(graph, sourceNode, SourceConfig::OUTPUT,
 *                targetNode, TargetConfig::INPUT_ARRAY, {0, 1, 2});
 * @endcode
 */
template<typename SourceSlot, typename TargetSlot>
inline void ConnectToArray(
    RenderGraph* graph,
    NodeHandle sourceNode,
    SourceSlot sourceSlot,
    NodeHandle targetNode,
    TargetSlot targetSlot,
    const std::vector<uint32_t>& arrayIndices,
    ConnectionMeta meta = {}
) {
    ConnectionBatch(graph)
        .ConnectToArray(sourceNode, sourceSlot, targetNode, targetSlot, arrayIndices, std::move(meta))
        .RegisterAll();
}

} // namespace Vixen::RenderGraph
