#pragma once

#include "Core/RenderGraph.h"
#include "Data/Core/ResourceConfig.h"
#include "Core/GraphTopology.h"
#include "Core/VariadicTypedNode.h"
#include "Data/Nodes/DescriptorResourceGathererNodeConfig.h"
#include <vector>
#include <functional>

namespace Vixen::RenderGraph {

/**
 * @brief Type-safe connection descriptor
 * 
 * Represents a single typed connection between two nodes.
 * Stores edge information that will be registered with RenderGraph.
 */
struct TypedConnectionDescriptor {
    NodeHandle sourceNode;
    uint32_t sourceOutputIndex;
    NodeHandle targetNode;
    uint32_t targetInputIndex;
    uint32_t arrayIndex = 0;  // For arrayable inputs (which element in the array)
    
    // Type information for validation
    ResourceType sourceType;
    ResourceType targetType;
    bool isArray = false;
};

/**
 * @brief Connection builder with batch edge registration
 * 
 * Allows building multiple connections and registering them atomically.
 * Type information is automatically deduced from ResourceSlot constants.
 * 
 * Supports:
 * - Single connections (1-to-1)
 * - Array connections (1-to-many for arrayable inputs)
 * - Indexed connections (connect to specific array element)
 * 
 * Example usage:
 *   ConnectionBatch batch(renderGraph);
 *   
 *   // Simple connection - types automatically deduced from slots
 *   batch.Connect(windowNode, WindowNodeConfig::SURFACE,
 *                 swapChainNode, SwapChainNodeConfig::SURFACE);
 *   
 *   // Array connection (fan-out to multiple framebuffers)
 *   batch.ConnectToArray(renderPassNode, RenderPassNodeConfig::RENDER_PASS,
 *                        framebufferNode, FramebufferNodeConfig::RENDER_PASS,
 *                        {0, 1, 2});  // Connect to array indices 0, 1, 2
 *   
 *   batch.RegisterAll();  // Atomically register all connections
 */
class ConnectionBatch {
public:
    explicit ConnectionBatch(RenderGraph* graph) : graph(graph) {}

    /**
     * @brief Add a typed connection to the batch
     * 
     * Type compatibility is checked at compile-time by matching ResourceSlot types.
     * No need to specify types explicitly - they're deduced from the slot constants.
     * 
     * @param sourceNode Handle to source node
     * @param sourceSlot Output slot constant (e.g., DeviceNodeConfig::DEVICE)
     * @param targetNode Handle to target node
     * @param targetSlot Input slot constant (e.g., SwapChainNodeConfig::DEVICE)
     * @param arrayIndex Array index for arrayable inputs (default: 0)
     */
    template<typename SourceSlot, typename TargetSlot>
    ConnectionBatch& Connect(
        NodeHandle sourceNode,
        SourceSlot sourceSlot,
        NodeHandle targetNode,
        TargetSlot targetSlot,
        uint32_t arrayIndex = 0
    ) {
        // Extract type information from slots
        using SourceType = typename SourceSlot::Type;
        using TargetType = typename TargetSlot::Type;
        
        // Compile-time type validation
        // Types must match exactly, or both must map to the same ResourceType
        static_assert(
            std::is_same_v<SourceType, TargetType> || 
            (SourceSlot::resourceType == TargetSlot::resourceType),
            "Source and target slot types must be compatible"
        );

        TypedConnectionDescriptor desc;
        desc.sourceNode = sourceNode;
        desc.sourceOutputIndex = sourceSlot.index;
        desc.targetNode = targetNode;
        desc.targetInputIndex = targetSlot.index;
        desc.arrayIndex = arrayIndex;
        desc.sourceType = SourceSlot::resourceType;
        desc.targetType = TargetSlot::resourceType;
        desc.isArray = false;

        connections.push_back(desc);
        return *this;
    }

    /**
     * @brief Connect source output to multiple array elements of target input
     * 
     * For arrayable inputs (e.g., multiple framebuffers, multiple images).
     * Creates one edge per array index.
     * 
     * @param sourceNode Handle to source node
     * @param sourceSlot Output slot constant
     * @param targetNode Handle to target node
     * @param targetSlot Input slot constant (must support arrays)
     * @param arrayIndices List of array indices to connect to
     */
    template<typename SourceSlot, typename TargetSlot>
    ConnectionBatch& ConnectToArray(
        NodeHandle sourceNode,
        SourceSlot sourceSlot,
        NodeHandle targetNode,
        TargetSlot targetSlot,
        const std::vector<uint32_t>& arrayIndices
    ) {
        for (uint32_t index : arrayIndices) {
            Connect(sourceNode, sourceSlot, targetNode, targetSlot, index);
        }
        return *this;
    }

    /**
     * @brief Connect a constant/direct value to a node input (not from another node output)
     * 
     * MVP helper: Allows setting input values directly without creating placeholder nodes.
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
        // Type validation - ensure value type matches slot type
        using SlotType = typename TargetSlot::Type;
        static_assert(
            std::is_same_v<SlotType, ValueType> || std::is_convertible_v<ValueType, SlotType>,
            "Value type must match or be convertible to slot type"
        );

        // Store as deferred constant connection
        constantConnections.push_back([this, targetNode, targetSlot, value, arrayIndex]() {
            auto* node = graph->GetInstance(targetNode);
            if (!node) {
                throw std::runtime_error("ConnectConstant: Invalid target node handle");
            }

            // Create a Resource with the constant value
            Resource res = Resource::Create<SlotType>(typename ResourceTypeTraits<SlotType>::DescriptorT{});
            res.SetHandle(value);
            
            // Use the protected SetInput method via friend access
            // (ConnectionBatch is a friend of NodeInstance)
            node->SetInput(targetSlot.index, arrayIndex, &res);
        });

        return *this;
    }

    /**
     * @brief Register all connections with the RenderGraph
     *
     * Validates handles, creates GraphEdges, and registers with topology.
     * Also processes constant and variadic connections.
     * Throws if any connection is invalid.
     */
    void RegisterAll() {
        // First, register node-to-node connections
        for (const auto& conn : connections) {
            ValidateConnection(conn);

            // Use RenderGraph's existing ConnectNodes method
            // This handles resource creation, dependency tracking, and topology
            graph->ConnectNodes(
                conn.sourceNode,
                conn.sourceOutputIndex,
                conn.targetNode,
                conn.targetInputIndex
            );
        }
        connections.clear();

        // Then, apply constant connections
        for (auto& constantConn : constantConnections) {
            constantConn(); // Execute the lambda that sets the input
        }
        constantConnections.clear();

        // Finally, apply variadic connections
        for (auto& variadicConn : variadicConnections) {
            variadicConn(); // Execute the lambda that adds variadic input
        }
        variadicConnections.clear();
    }

    /**
     * @brief Get number of pending connections (node-to-node only)
     */
    size_t GetConnectionCount() const { return connections.size(); }

    /**
     * @brief Clear all pending connections without registering
     */
    void Clear() {
        connections.clear();
        constantConnections.clear();
        variadicConnections.clear();
    }

    /**
     * @brief Connect to a variadic node using shader binding metadata from Names.h
     *
     * Uses binding constant from Names.h (generated from SDI) as a config reference.
     * The binding constant contains all metadata needed to wire the connection.
     *
     * **Phase G Update**: Now validates against registered variadic slot metadata
     * and adds proper dependency tracking for topological sort.
     *
     * Example:
     * ```cpp
     * // Using Names.h binding constant directly
     * batch.ConnectVariadic(gathererNode, ComputeShaderBindings::INPUT_IMAGE,
     *                       textureNode, TextureConfig::IMAGE_VIEW);
     * ```
     *
     * @param variadicNode Handle to variadic node (DescriptorResourceGathererNode)
     * @param bindingRef Binding constant from Names.h (e.g., ComputeShaderBindings::INPUT_IMAGE)
     * @param sourceNode Handle to source node providing the resource
     * @param sourceSlot Output slot from source node
     */
    template<typename BindingRefType, typename SourceSlot>
    ConnectionBatch& ConnectVariadic(
        NodeHandle variadicNode,
        BindingRefType bindingRef,
        NodeHandle sourceNode,
        SourceSlot sourceSlot
    ) {
        std::cout << "[ConnectVariadic] Queuing variadic connection for binding " << bindingRef.binding << std::endl;

        // Defer the variadic connection via lambda (applied during RegisterAll)
        variadicConnections.push_back([=]() {
            std::cout << "[ConnectVariadic] Creating tentative slot for binding " << bindingRef.binding << std::endl;

            // Get the variadic node instance
            NodeInstance* node = graph->GetInstance(variadicNode);
            if (!node) {
                throw std::runtime_error("ConnectVariadic: Invalid variadic node handle");
            }

            // Cast to VariadicTypedNode to access UpdateVariadicSlot
            auto* variadicNodePtr = dynamic_cast<VariadicTypedNode<DescriptorResourceGathererNodeConfig>*>(node);
            if (!variadicNodePtr) {
                throw std::runtime_error("ConnectVariadic: Node is not a variadic node");
            }

            // Get source resource
            NodeInstance* sourceNodeInst = graph->GetInstance(sourceNode);
            if (!sourceNodeInst) {
                throw std::runtime_error("ConnectVariadic: Invalid source node handle");
            }

            Resource* sourceRes = sourceNodeInst->GetOutput(sourceSlot.index, 0);
            if (!sourceRes) {
                throw std::runtime_error("ConnectVariadic: Source output not found");
            }

            // Extract binding index from binding ref
            uint32_t bindingIndex = bindingRef.binding;
            size_t bundleIndex = 0;

            // Create tentative slot (optimistic - trust user, defer validation to Compile)
            VariadicSlotInfo tentativeSlot;
            tentativeSlot.resource = sourceRes;
            tentativeSlot.resourceType = sourceRes->GetType();  // Infer from resource
            tentativeSlot.slotName = bindingRef.name;
            tentativeSlot.binding = bindingIndex;
            tentativeSlot.descriptorType = bindingRef.type;
            tentativeSlot.state = SlotState::Tentative;  // Mark as unvalidated
            tentativeSlot.sourceNode = sourceNode;
            tentativeSlot.sourceOutput = sourceSlot.index;

            // Update/create slot (always succeeds)
            variadicNodePtr->UpdateVariadicSlot(bindingIndex, tentativeSlot, bundleIndex);

            // Register dependency for topological sort (variadic node depends on source)
            std::cout << "[ConnectVariadic] Adding dependency: " << node->GetInstanceName()
                      << " -> " << sourceNodeInst->GetInstanceName() << std::endl;
            node->AddDependency(sourceNodeInst);

            std::cout << "[ConnectVariadic] Created tentative slot at binding " << bindingIndex
                      << " (state=Tentative, will validate during Compile)" << std::endl;
        });

        return *this;
    }

private:
    RenderGraph* graph;
    std::vector<TypedConnectionDescriptor> connections;
    std::vector<std::function<void()>> constantConnections; // Deferred constant setters
    std::vector<std::function<void()>> variadicConnections; // Deferred variadic connections

    void ValidateConnection(const TypedConnectionDescriptor& conn) {
        if (!conn.sourceNode.IsValid()) {
            throw std::runtime_error("TypedConnection: Invalid source node handle");
        }
        if (!conn.targetNode.IsValid()) {
            throw std::runtime_error("TypedConnection: Invalid target node handle");
        }
        
        // Verify types match (already checked at compile-time, this is runtime sanity check)
        if (conn.sourceType != conn.targetType) {
            throw std::runtime_error("TypedConnection: Type mismatch between source and target");
        }
    }
};

/**
 * @brief Simplified single-connection helper (immediate registration)
 * 
 * For quick one-off connections without batching.
 * Types are automatically deduced from slot constants.
 * 
 * Usage:
 *   Connect(graph, sourceNode, SourceConfig::OUTPUT, 
 *           targetNode, TargetConfig::INPUT);
 */
template<typename SourceSlot, typename TargetSlot>
inline void Connect(
    RenderGraph* graph,
    NodeHandle sourceNode,
    SourceSlot sourceSlot,
    NodeHandle targetNode,
    TargetSlot targetSlot
) {
    ConnectionBatch(graph)
        .Connect(sourceNode, sourceSlot, targetNode, targetSlot)
        .RegisterAll();
}

/**
 * @brief Helper for array connections (immediate registration)
 * 
 * Types are automatically deduced from slot constants.
 * 
 * Usage:
 *   ConnectToArray(graph, sourceNode, SourceConfig::OUTPUT,
 *                  targetNode, TargetConfig::INPUT_ARRAY, {0, 1, 2});
 */
template<typename SourceSlot, typename TargetSlot>
inline void ConnectToArray(
    RenderGraph* graph,
    NodeHandle sourceNode,
    SourceSlot sourceSlot,
    NodeHandle targetNode,
    TargetSlot targetSlot,
    const std::vector<uint32_t>& arrayIndices
) {
    ConnectionBatch(graph)
        .ConnectToArray(sourceNode, sourceSlot, targetNode, targetSlot, arrayIndices)
        .RegisterAll();
}

} // namespace Vixen::RenderGraph
