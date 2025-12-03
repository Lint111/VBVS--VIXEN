#pragma once

#include "Core/RenderGraph.h"
#include "Core/GraphLifecycleHooks.h"
#include "Data/Core/ResourceConfig.h"
#include "Core/GraphTopology.h"
#include "Core/VariadicTypedNode.h"
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

        // Compile-time type validation with detailed error message
        static_assert(
            AreTypesCompatible_v<SourceType, TargetType>,
            "TYPE MISMATCH: Source output slot type is incompatible with target input slot type. "
            "Check that both slots use the same base type (e.g., both use 'const std::vector<VkSemaphore>&' "
            "or 'std::vector<VkSemaphore>', not a mix). "
            "See compiler error for SourceType and TargetType details."
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
     * @brief Connect with automatic field extraction from struct output
     *
     * Overload that accepts a member pointer for extracting specific fields from struct outputs.
     * Automatically handles field extraction behind the scenes.
     *
     * Example:
     * ```cpp
     * // Extract colorBuffers field from SwapChainPublicVariables
     * batch.Connect(swapChainNode, SwapChainNodeConfig::SWAPCHAIN_PUBLIC,
     *               descriptorNode, DescriptorSetNodeConfig::DESCRIPTOR_RESOURCES,
     *               &SwapChainPublicVariables::colorBuffers);
     * ```
     *
     * @param sourceNode Handle to source node
     * @param sourceSlot Output slot from source node (struct type)
     * @param targetNode Handle to target node
     * @param targetSlot Input slot on target node (field type)
     * @param memberPtr Pointer-to-member for field extraction
     * @param arrayIndex Array index for arrayable inputs (default: 0)
     */
    template<typename SourceSlot, typename TargetSlot, typename StructType, typename FieldType>
    ConnectionBatch& Connect(
        NodeHandle sourceNode,
        SourceSlot sourceSlot,
        NodeHandle targetNode,
        TargetSlot targetSlot,
        FieldType StructType::* memberPtr,  // Member pointer for field extraction
        uint32_t arrayIndex = 0
    ) {
        // Debug: Connect() called with member pointer
        // std::cout << "[FieldExtraction] Connect() called with member pointer - Source handle index: " << sourceNode.index
        //           << ", Target handle index: " << targetNode.index << std::endl;


        // Extract type information
        using SourceType = typename SourceSlot::Type;
        using TargetType = typename TargetSlot::Type;

        // Normalize SourceType to a plain struct type for comparison.
        // We accept pointer, reference, and const-qualified forms like
        // `T*`, `const T*`, `T&`, `const T&` - so strip pointer, reference and cv
        // qualifiers before comparing to the member's StructType.
        using SourceBaseType = std::remove_cv_t<std::remove_reference_t<std::remove_pointer_t<SourceType>>>;
        static_assert(std::is_same_v<SourceBaseType, StructType> ||
                     std::is_base_of_v<StructType, SourceBaseType> ||
                     std::is_same_v<std::remove_cv_t<std::remove_reference_t<SourceType>>, StructType> ||
                     std::is_base_of_v<StructType, std::remove_cv_t<std::remove_reference_t<SourceType>>>,
            "Source slot type must match or derive from struct type in member pointer");

        // Validate field type matches target
        using FieldResourceType = std::remove_reference_t<FieldType>;
        static_assert(std::is_same_v<TargetType, FieldResourceType> ||
                     (ResourceTypeTraits<FieldResourceType>::resourceType == TargetSlot::resourceType),
            "Target slot type must match extracted field type");

        // Register dependency and create placeholder input NOW (for validation)
        // Actual field extraction will happen post-compile
        {
            auto* srcNode = graph->GetInstance(sourceNode);
            auto* tgtNode = graph->GetInstance(targetNode);
            if (!srcNode || !tgtNode) {
                throw std::runtime_error("Connect with field extraction: Invalid node handle");
            }

            // RUNTIME VALIDATION: Check source slot lifetime is Persistent
            // Member extraction requires persistent resources because we're extracting a pointer
            // to a member field - the source struct must have stable memory address
            const auto* srcType = srcNode->GetType();
            if (srcType) {
                const auto* outputDesc = srcType->GetOutputDescriptor(sourceSlot.index);
                if (outputDesc && outputDesc->lifetime != ResourceLifetime::Persistent) {
                    throw std::runtime_error(
                        "Member field extraction requires source slot to have ResourceLifetime::Persistent. "
                        "Source slot '" + outputDesc->name + "' has lifetime Transient. "
                        "Change the slot configuration to use ResourceLifetime::Persistent."
                    );
                }
            }

            // Debug: Registering callback
            // std::cout << "[FieldExtraction] Registering callback: extract from " << srcNode->GetInstanceName()
            //           << " slot " << sourceSlot.index << " to " << tgtNode->GetInstanceName()
            //           << " slot " << targetSlot.index << std::endl;

            // Register dependency so topological sort works
            tgtNode->AddDependency(srcNode);

            // Create placeholder resource so validation passes
            using FieldResourceType = std::remove_reference_t<FieldType>;
            Resource placeholderRes = Resource::Create<FieldResourceType>(
                typename ResourceTypeTraits<FieldResourceType>::DescriptorT{});
            // Set with default value
            placeholderRes.SetHandle<FieldResourceType>(FieldResourceType{});
            tgtNode->SetInput(targetSlot.index, arrayIndex, &placeholderRes);
        }

        // Register callback with graph to execute after source node compiles
        graph->RegisterPostNodeCompileCallback([graph=this->graph, sourceNode, sourceSlot, targetNode, targetSlot, memberPtr, arrayIndex](NodeInstance* compiledNode) {
            // Debug: Log every callback invocation
            auto* srcNode = graph->GetInstance(sourceNode);
            // std::cout << "[FieldExtraction] Callback triggered for compiled node: " << compiledNode->GetInstanceName() << std::endl;
            // std::cout << "[FieldExtraction] Looking for source node: " << (srcNode ? srcNode->GetInstanceName() : "NULL") << std::endl;
            // std::cout << "[FieldExtraction] Pointer match: " << (compiledNode == srcNode ? "YES" : "NO") << std::endl;
            // std::cout << "[FieldExtraction] compiledNode ptr: " << compiledNode << ", srcNode ptr: " << srcNode << std::endl;

            // Only execute if this is the source node we're waiting for
            if (compiledNode != srcNode) {
                return;  // Not the node we're waiting for
            }

            // Debug: Field extraction executing
            // auto* tgtNodeCheck = graph->GetInstance(targetNode);
            // std::cout << "[FieldExtraction] Extracting field from " << srcNode->GetInstanceName()
            //           << " slot " << sourceSlot.index << " to " << (tgtNodeCheck ? tgtNodeCheck->GetInstanceName() : "NULL")
            //           << " slot " << targetSlot.index << std::endl;

            auto* tgtNode = graph->GetInstance(targetNode);
            if (!srcNode || !tgtNode) {
                throw std::runtime_error("Connect with field extraction: Invalid node handle");
            }

            // Get source output resource
            Resource* sourceRes = srcNode->GetOutput(sourceSlot.index, 0);
            if (!sourceRes) {
                throw std::runtime_error("Connect with field extraction: Source output not found");
            }

            // Create a lambda that extracts the field value on-demand
            // This lambda will be called when the target node accesses its input
            auto extractFieldCallback = [sourceRes, memberPtr]() -> FieldType {
                // Get the struct instance (handle both pointer and non-pointer types)
                StructType* structPtr = nullptr;
                if constexpr (std::is_pointer_v<SourceType>) {
                    // Source is a pointer type - GetHandle returns the pointer value directly
                    structPtr = sourceRes->GetHandle<SourceType>();
                } else {
                    // Source is a value type - get pointer to it
                    structPtr = sourceRes->GetHandle<StructType*>();
                }

                if (!structPtr) {
                    throw std::runtime_error("Field extraction: Failed to get struct from source");
                }

                // Extract and return the field
                return structPtr->*memberPtr;
            };

            // For now, extract once and store the value
            // TODO: Make this truly lazy by creating a proxy Resource that extracts on-demand
            using FieldResourceType = std::remove_reference_t<FieldType>;
            FieldType fieldValue = extractFieldCallback();

            // Debug: Extracted field value
            // std::cout << "[FieldExtraction] Extracted field value: " << fieldValue << std::endl;

            // Create resource as unique_ptr and let graph manage its lifetime
            // Note: We can't access graph->resources directly, so we create a static storage
            // This is a workaround - ideally RenderGraph would provide an AllocateResource() method
            static std::vector<std::unique_ptr<Resource>> extractedFieldResources;

            auto fieldResPtr = std::make_unique<Resource>(Resource::Create<FieldResourceType>(
                typename ResourceTypeTraits<FieldResourceType>::DescriptorT{}));
            fieldResPtr->SetHandle<FieldResourceType>(FieldResourceType(fieldValue));

            Resource* fieldRes = fieldResPtr.get();
            extractedFieldResources.push_back(std::move(fieldResPtr));

            // Debug: Created resource
            // std::cout << "[FieldExtraction] Created resource and setting input on target node" << std::endl;

            // Set as input on target node
            tgtNode->SetInput(targetSlot.index, arrayIndex, fieldRes);

            // Debug: Input set successfully
            // std::cout << "[FieldExtraction] Input set successfully" << std::endl;
        });

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
        std::cout << "[ConnectionBatch::RegisterAll] Registering " << connections.size()
                  << " connections, " << constantConnections.size() << " constants, "
                  << variadicConnections.size() << " variadic connections\n";

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
        std::cout << "[ConnectionBatch::RegisterAll] Executing " << variadicConnections.size() << " variadic lambdas...\n";
        for (size_t i = 0; i < variadicConnections.size(); ++i) {
            std::cout << "[ConnectionBatch::RegisterAll] Executing variadic lambda " << i << "\n";
            variadicConnections[i](); // Execute the lambda that adds variadic input
        }
        variadicConnections.clear();
        std::cout << "[ConnectionBatch::RegisterAll] Complete\n";
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
     * Direct resource connection - connects source output directly to variadic input.
     *
     * Examples:
     * ```cpp
     * // Auto-detect SlotRole based on source lifetime
     * batch.ConnectVariadic(textureNode, TextureConfig::IMAGE_VIEW,
     *                       gathererNode, ComputeShaderBindings::INPUT_IMAGE);
     *
     * // Override SlotRole explicitly
     * batch.ConnectVariadic(swapchainNode, SwapChainConfig::IMAGE_VIEW,
     *                       gathererNode, ComputeShaderBindings::OUTPUT_IMAGE,
     *                       SlotRole::ExecuteOnly);
     * ```
     *
     * @param sourceNode Handle to source node providing the resource
     * @param sourceSlot Output slot from source node
     * @param variadicNode Handle to variadic node (DescriptorResourceGathererNode)
     * @param bindingRef Binding constant from Names.h (e.g., ComputeShaderBindings::INPUT_IMAGE)
     * @param slotRoleOverride Optional SlotRole override (default auto-detects based on source lifetime)
     */
    template<typename SourceSlot, typename BindingRefType>
    ConnectionBatch& ConnectVariadic(
        NodeHandle sourceNode,
        SourceSlot sourceSlot,
        NodeHandle variadicNode,
        BindingRefType bindingRef,
        SlotRole slotRoleOverride = SlotRole::Output  // Output = sentinel for auto-detect
    ) {
        // Extract binding value from bindingRef
        // Handle both binding reference objects (with .binding member) and plain integers
        uint32_t bindingValue;
        
        if constexpr (std::is_integral_v<BindingRefType>) {
            // Plain integer binding index
            bindingValue = static_cast<uint32_t>(bindingRef);
        } else {
            // Binding reference object with .binding, .name, .type members
            bindingValue = bindingRef.binding;
        }
        
        std::cout << "[ConnectVariadic] Queuing variadic connection for binding " << bindingValue << std::endl;

        // Defer the variadic connection via lambda (applied during RegisterAll)
        variadicConnections.push_back([=]() {
            std::cout << "[ConnectVariadic] Creating tentative slot for binding " << bindingValue << std::endl;

            // Get the variadic node instance
            NodeInstance* node = graph->GetInstance(variadicNode);
            if (!node) {
                throw std::runtime_error("ConnectVariadic: Invalid variadic node handle");
            }

            // Cast to IVariadicNode interface to access UpdateVariadicSlot
            auto* variadicNodePtr = dynamic_cast<IVariadicNode*>(node);
            if (!variadicNodePtr) {
                throw std::runtime_error("ConnectVariadic: Node is not a variadic node");
            }

            // Get source resource
            NodeInstance* sourceNodeInst = graph->GetInstance(sourceNode);
            if (!sourceNodeInst) {
                throw std::runtime_error("ConnectVariadic: Invalid source node handle");
            }

            // Extract binding index from binding ref
            uint32_t bindingIndex = bindingValue;  // Use captured bindingValue from outer scope
            size_t bundleIndex = 0;

            // Create tentative slot using helper (no field extraction needed)
            VariadicSlotInfo tentativeSlot = CreateBaseTentativeSlot(
                sourceNode,
                sourceSlot.index,
                ResourceTypeTraits<typename SourceSlot::Type>::resourceType,
                bindingRef,
                sourceNodeInst,
                slotRoleOverride
            );

            // Update/create slot (always succeeds)
            variadicNodePtr->UpdateVariadicSlot(bindingIndex, tentativeSlot, bundleIndex);

            // Register dependency and topology edge
            RegisterVariadicDependency(node, sourceNodeInst, sourceSlot.index, bindingIndex, slotRoleOverride);

            // Register PostCompile hook for resource population
            RegisterVariadicResourcePopulationHook(variadicNodePtr, sourceNodeInst, sourceSlot.index,
                                                  bindingIndex, bundleIndex, "ConnectVariadic resource population");

            std::cout << "[ConnectVariadic] Created tentative slot at binding " << bindingIndex
                      << " (state=Tentative, will validate during Compile)" << std::endl;
        });

        return *this;
    }

    /**
     * @brief Connect to a variadic node with automatic field extraction from struct outputs
     *
     * Field extraction connection - extracts a specific field from struct-typed source output
     * using member pointer syntax.
     *
     * IMPORTANT: Field extraction requires the source output slot to be a pointer or reference type,
     * NOT a value type. This is enforced at compile-time to prevent bugs where field extraction
     * points to temporary copies that get destroyed each frame.
     *
     * Examples:
     * ```cpp
     * // Auto-detect SlotRole based on source lifetime
     * batch.ConnectVariadic(swapChainNode, SwapChainNodeConfig::SWAPCHAIN_PUBLIC,
     *                       gathererNode, ComputeTest::outputImage,
     *                       &SwapChainPublicVariables::colorBuffers);
     *
     * // Override SlotRole explicitly
     * batch.ConnectVariadic(swapChainNode, SwapChainNodeConfig::SWAPCHAIN_PUBLIC,
     *                       gathererNode, ComputeTest::outputImage,
     *                       &SwapChainPublicVariables::colorBuffers,
     *                       SlotRole::ExecuteOnly);
     * ```
     *
     * @param sourceNode Handle to source node providing the struct resource
     * @param sourceSlot Output slot from source node (must be struct type)
     * @param variadicNode Handle to variadic node (DescriptorResourceGathererNode)
     * @param bindingRef Binding constant from Names.h (e.g., ComputeTest::outputImage)
     * @param memberPtr Pointer-to-member for field extraction (e.g., &StructType::fieldName)
     * @param slotRoleOverride Optional SlotRole override (default auto-detects based on source lifetime)
     */
    template<typename SourceSlot, typename BindingRefType, typename StructType, typename FieldType>
    ConnectionBatch& ConnectVariadic(
        NodeHandle sourceNode,
        SourceSlot sourceSlot,
        NodeHandle variadicNode,
        BindingRefType bindingRef,
        FieldType StructType::* memberPtr,  // Member pointer for field extraction
        SlotRole slotRoleOverride = SlotRole::Output  // Output = sentinel for auto-detect
    ) {
        // Extract binding value from bindingRef
        // Handle both binding reference objects (with .binding member) and plain integers
        uint32_t bindingValue;
        const char* slotName = "";
        
        if constexpr (std::is_integral_v<BindingRefType>) {
            // Plain integer binding index (for push constant gatherer)
            bindingValue = static_cast<uint32_t>(bindingRef);
            slotName = "push_constant_field";  // Default name for push constant fields
        } else {
            // Binding reference object with .binding, .name, .type members
            bindingValue = bindingRef.binding;
            slotName = bindingRef.name;
        }
        
        std::cout << "[ConnectVariadic] Storing field extraction lambda for PostSetup execution at binding "
                  << bindingValue << std::endl;

        // Store lambda in variadicConnections to be executed during RegisterAll (before compilation)
        variadicConnections.push_back([=]() {
            std::cout << "[ConnectVariadic] Executing field extraction lambda - creating tentative slot at binding "
                      << bindingValue << std::endl;

            // Validate types at compile-time
            using SourceType = typename SourceSlot::Type;

            // Normalize SourceType to a plain struct type for comparison (strip pointer/ref/cv)
            using SourceBaseType = std::remove_cv_t<std::remove_reference_t<std::remove_pointer_t<SourceType>>>;
            static_assert(std::is_same_v<SourceBaseType, StructType> ||
                         std::is_base_of_v<StructType, SourceBaseType> ||
                         std::is_same_v<std::remove_cv_t<std::remove_reference_t<SourceType>>, StructType> ||
                         std::is_base_of_v<StructType, std::remove_cv_t<std::remove_reference_t<SourceType>>>,
                "Source slot type must match or derive from struct type in member pointer");

            // CRITICAL VALIDATION: Member extraction requires persistent pointer/reference types
            // This prevents the bug where field extraction from value types creates dangling pointers
            static_assert(std::is_pointer_v<SourceType> ||
                         std::is_reference_v<SourceType> ||
                         (std::is_class_v<SourceType> && std::is_same_v<SourceType, std::reference_wrapper<SourceBaseType>>),
                "Member field extraction requires source slot to be a pointer or reference type, "
                "not a value type. Value types are copied each frame, causing field extraction to "
                "point to temporary copies that get destroyed. Use a pointer type (e.g., const CameraData*) "
                "instead of value type (e.g., CameraData) for the output slot.");

            // Get node instances
            NodeInstance* node = graph->GetInstance(variadicNode);
            if (!node) {
                throw std::runtime_error("ConnectVariadic: Invalid variadic node handle");
            }

            auto* variadicNodePtr = dynamic_cast<IVariadicNode*>(node);
            if (!variadicNodePtr) {
                throw std::runtime_error("ConnectVariadic: Node is not a variadic node");
            }

            NodeInstance* sourceNodeInst = graph->GetInstance(sourceNode);
            if (!sourceNodeInst) {
                throw std::runtime_error("ConnectVariadic: Invalid source node handle");
            }

            // RUNTIME VALIDATION: Check source slot lifetime is Persistent
            // Member extraction requires persistent resources because we're extracting a pointer
            // to a member field - the source struct must have stable memory address
            const auto* srcType = sourceNodeInst->GetType();
            if (srcType) {
                const auto* outputDesc = srcType->GetOutputDescriptor(sourceSlot.index);
                if (outputDesc && outputDesc->lifetime != ResourceLifetime::Persistent) {
                    throw std::runtime_error(
                        "ConnectVariadic member field extraction requires source slot to have ResourceLifetime::Persistent. "
                        "Source slot '" + outputDesc->name + "' has lifetime Transient. "
                        "Change the slot configuration to use ResourceLifetime::Persistent."
                    );
                }
            }

            // Calculate member pointer offset (doesn't require resource to exist)
            size_t fieldOffset = reinterpret_cast<size_t>(
                &(static_cast<StructType*>(nullptr)->*memberPtr));

            // Type information for the extracted field
            using FieldResourceType = std::remove_reference_t<FieldType>;
            uint32_t bindingIndex = bindingValue;  // Use captured bindingValue from outer scope
            size_t bundleIndex = 0;

            // Create base tentative slot using helper
            VariadicSlotInfo tentativeSlot = CreateBaseTentativeSlot(
                sourceNode,
                sourceSlot.index,
                ResourceTypeTraits<FieldResourceType>::resourceType,
                bindingRef,
                sourceNodeInst,
                slotRoleOverride
            );

            // Set field-extraction-specific fields
            tentativeSlot.fieldOffset = fieldOffset;
            tentativeSlot.hasFieldExtraction = true;

            // Register the slot
            variadicNodePtr->UpdateVariadicSlot(bindingIndex, tentativeSlot, bundleIndex);

            // Register dependency and topology edge
            RegisterVariadicDependency(node, sourceNodeInst, sourceSlot.index, bindingIndex, slotRoleOverride);

            // Register PostCompile hook for resource population
            RegisterVariadicResourcePopulationHook(variadicNodePtr, sourceNodeInst, sourceSlot.index,
                                                  bindingIndex, bundleIndex, "ConnectVariadic field extraction resource population");

            std::cout << "[ConnectVariadic] Created tentative slot with field extraction at binding "
                      << bindingIndex << " (resource will be populated in PostCompile)" << std::endl;
        });

        return *this;
    }

private:
    RenderGraph* graph;
    std::vector<TypedConnectionDescriptor> connections;
    std::vector<std::function<void()>> constantConnections; // Deferred constant setters (run at RegisterAll)
    std::vector<std::function<void()>> variadicConnections; // Deferred variadic connections (run at RegisterAll)
    // postCompileConnections removed - now registered directly with RenderGraph callbacks

    /**
     * @brief Determine SlotRole for variadic connection
     *
     * Helper for ConnectVariadic overloads. Resolves SlotRole by checking:
     * 1. Explicit override if provided (slotRoleOverride != SlotRole::Output)
     * 2. Auto-detection based on source output lifetime otherwise
     *
     * @param sourceNodeInst Source node instance
     * @param sourceSlotIndex Source output slot index
     * @param slotRoleOverride Optional override (SlotRole::Output = auto-detect)
     * @return Resolved SlotRole for the connection
     */
    SlotRole DetermineVariadicSlotRole(NodeInstance* sourceNodeInst, size_t sourceSlotIndex, SlotRole slotRoleOverride) {
        if (slotRoleOverride != SlotRole::Output) {
            // User provided explicit SlotRole - use it
            std::cout << "[ConnectVariadic] Using explicit SlotRole override: "
                      << static_cast<int>(slotRoleOverride) << "\n";
            return slotRoleOverride;
        }

        // Auto-detect based on source output lifetime
        // Transient outputs need both Dependency (for initial setup) and Execute (for per-frame refresh)
        const ResourceDescriptor* outputDesc = sourceNodeInst->GetNodeType()->GetOutputDescriptor(static_cast<uint32_t>(sourceSlotIndex));
        if (outputDesc && outputDesc->lifetime == ResourceLifetime::Transient) {
            std::cout << "[ConnectVariadic] Detected transient output - marking slot as Dependency|Execute\n";
            return SlotRole::Dependency | SlotRole::Execute;
        } else {
            std::cout << "[ConnectVariadic] Marking slot as Dependency (static resource)\n";
            return SlotRole::Dependency;
        }
    }

    /**
     * @brief Register dependency and topology edge for variadic connection
     *
     * Helper for ConnectVariadic overloads. Registers node-level dependency and topology edge
     * only if the slot has Dependency role (checked bitwise).
     *
     * @param variadicNode Variadic node instance
     * @param sourceNodeInst Source node instance
     * @param sourceSlotIndex Source output slot index
     * @param bindingIndex Binding index for the connection
     * @param slotRole SlotRole for this connection (may include Dependency and/or ExecuteOnly)
     */
    void RegisterVariadicDependency(
        NodeInstance* variadicNode,
        NodeInstance* sourceNodeInst,
        size_t sourceSlotIndex,
        uint32_t bindingIndex,
        SlotRole slotRole
    ) {
        // Only register dependency if slot has Dependency role
        if (HasDependency(slotRole)) {
            std::cout << "[ConnectVariadic] Adding dependency: " << variadicNode->GetInstanceName()
                      << " -> " << sourceNodeInst->GetInstanceName() << std::endl;
            variadicNode->AddDependency(sourceNodeInst);
        }

        GraphEdge edge;
        edge.source = sourceNodeInst;
        edge.target = variadicNode;
        edge.sourceOutputIndex = static_cast<uint32_t>(sourceSlotIndex);
        edge.targetInputIndex = bindingIndex;
        graph->GetTopology().AddEdge(edge);
        std::cout << "[ConnectVariadic] Added topology edge for binding "
                    << bindingIndex << std::endl;
    }

    /**
     * @brief Register PostCompile hook for variadic resource population
     *
     * Helper for ConnectVariadic overloads. Registers hook to populate resource
     * pointer after source node compiles (skips transient/ExecuteOnly slots).
     *
     * @param variadicNodePtr Variadic node pointer
     * @param sourceNodeInst Source node instance
     * @param sourceSlotIndex Source output slot index
     * @param bindingIndex Binding index for the connection
     * @param bundleIndex Bundle index (typically 0)
     * @param hookDescription Description for the hook
     */
    template<typename VariadicNodeType>
    void RegisterVariadicResourcePopulationHook(
        VariadicNodeType* variadicNodePtr,
        NodeInstance* sourceNodeInst,
        size_t sourceSlotIndex,
        uint32_t bindingIndex,
        size_t bundleIndex,
        const char* hookDescription
    ) {
        // Check slot role to determine which hooks to register
        const VariadicSlotInfo* tentativeSlot = variadicNodePtr->GetVariadicSlotInfo(bindingIndex, bundleIndex);
        if (!tentativeSlot) {
            std::cout << "[ConnectVariadic] WARNING: No slot info found for binding " << bindingIndex << "\n";
            return;
        }

        SlotRole slotRole = tentativeSlot->slotRole;
        bool hasDependency = HasDependency(slotRole);
        bool hasExecute = HasExecute(slotRole);

        // Register PostCompile hook ONLY if slot has Dependency role
        if (hasDependency) {
            graph->GetLifecycleHooks().RegisterNodeHook(
                NodeLifecyclePhase::PostCompile,
                [=](NodeInstance* compiledNode) {
                    if (compiledNode != sourceNodeInst) return;

                    std::cout << "[ConnectVariadic PostCompile Hook] Populating Dependency-role resource for binding "
                              << bindingIndex << std::endl;

                    Resource* sourceRes = sourceNodeInst->GetOutput(static_cast<uint8_t>(sourceSlotIndex), 0);
                    if (!sourceRes || !sourceRes->IsValid()) {
                        std::cout << "[ConnectVariadic PostCompile Hook] WARNING: Source output " << sourceSlotIndex
                                  << " not yet available or invalid for binding " << bindingIndex
                                  << " (source node may not be fully compiled yet)\n";
                        return;
                    }

                    const VariadicSlotInfo* existingSlot = variadicNodePtr->GetVariadicSlotInfo(bindingIndex, bundleIndex);
                    if (existingSlot) {
                        VariadicSlotInfo updatedSlot = *existingSlot;
                        updatedSlot.resource = sourceRes;
                        updatedSlot.resourceType = sourceRes->GetType();
                        variadicNodePtr->UpdateVariadicSlot(bindingIndex, updatedSlot, bundleIndex);

                        std::cout << "[ConnectVariadic PostCompile Hook] Resource populated for binding "
                                  << bindingIndex << " with type " << static_cast<int>(updatedSlot.resourceType) << std::endl;
                    }
                },
                hookDescription
            );
        }

        // Register PreExecute hook ONLY if slot has Execute role
        if (hasExecute) {
            auto* variadicAsNodeInstance = dynamic_cast<NodeInstance*>(variadicNodePtr);
            graph->GetLifecycleHooks().RegisterNodeHook(
                NodeLifecyclePhase::PreExecute,
                [=](NodeInstance* executingNode) {
                    // PreExecute hook runs BEFORE the target node's Execute() - only populate if this is the target node
                    if (executingNode != variadicAsNodeInstance) return;

                    std::cout << "[ConnectVariadic PreExecute Hook] Refreshing Execute-role resource for binding "
                              << bindingIndex << std::endl;

                    // Fetch fresh resource from source node
                    Resource* sourceRes = sourceNodeInst->GetOutput(static_cast<uint8_t>(sourceSlotIndex), 0);
                    if (!sourceRes || !sourceRes->IsValid()) {
                        std::cout << "[ConnectVariadic PreExecute Hook] WARNING: Source output " << sourceSlotIndex
                                  << " is null or invalid for binding " << bindingIndex << "\n";
                        return;
                    }

                    // Update slot with fresh resource (node-side extraction handles hasFieldExtraction)
                    const VariadicSlotInfo* currentSlot = variadicNodePtr->GetVariadicSlotInfo(bindingIndex, bundleIndex);
                    if (currentSlot) {
                        VariadicSlotInfo updatedSlot = *currentSlot;
                        updatedSlot.resource = sourceRes;
                        updatedSlot.resourceType = sourceRes->GetType();
                        variadicNodePtr->UpdateVariadicSlot(bindingIndex, updatedSlot, bundleIndex);

                        std::cout << "[ConnectVariadic PreExecute Hook] Resource refreshed for binding "
                                  << bindingIndex << " (fieldExtraction=" << (updatedSlot.hasFieldExtraction ? "yes" : "no") << ")\n";
                    }
                },
                std::string(hookDescription) + " (PreExecute refresh)"
            );
        }
    }

    /**
     * @brief Create base tentative slot structure
     *
     * Helper for ConnectVariadic overloads. Creates common VariadicSlotInfo fields.
     * Caller should set field-extraction-specific fields (fieldOffset, hasFieldExtraction) if needed.
     *
     * @param sourceNode Source node handle
     * @param sourceSlotIndex Source output slot index
     * @param sourceResourceType Resource type from source slot
     * @param bindingRef Binding reference from Names.h
     * @param sourceNodeInst Source node instance (for SlotRole determination)
     * @param slotRoleOverride Optional SlotRole override
     * @return Base tentative slot with common fields populated
     */
    template<typename BindingRefType>
    VariadicSlotInfo CreateBaseTentativeSlot(
        NodeHandle sourceNode,
        size_t sourceSlotIndex,
        ResourceType sourceResourceType,
        BindingRefType bindingRef,
        NodeInstance* sourceNodeInst,
        SlotRole slotRoleOverride
    ) {
        VariadicSlotInfo tentativeSlot;
        tentativeSlot.resource = nullptr;  // Will be set in PostCompile hook
        tentativeSlot.resourceType = sourceResourceType;
        
        // Handle both binding reference objects and plain integers
        if constexpr (std::is_integral_v<BindingRefType>) {
            // Plain integer binding index
            tentativeSlot.slotName = "push_constant_field";  // Default name for simple bindings
            tentativeSlot.binding = static_cast<uint32_t>(bindingRef);
            tentativeSlot.descriptorType = VK_DESCRIPTOR_TYPE_MAX_ENUM;  // Not applicable for simple bindings
        } else {
            // Binding reference object with .binding, .name, .type members
            tentativeSlot.slotName = bindingRef.name;
            tentativeSlot.binding = bindingRef.binding;
            tentativeSlot.descriptorType = bindingRef.type;
        }
        tentativeSlot.state = SlotState::Tentative;
        tentativeSlot.sourceNode = sourceNode;
        tentativeSlot.sourceOutput = static_cast<uint32_t>(sourceSlotIndex);
        tentativeSlot.slotRole = DetermineVariadicSlotRole(sourceNodeInst, sourceSlotIndex, slotRoleOverride);

        // Field extraction fields default to false/0 (caller sets if needed)
        tentativeSlot.hasFieldExtraction = false;
        tentativeSlot.fieldOffset = 0;

        return tentativeSlot;
    }

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
