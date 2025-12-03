#pragma once

#include "Core/TypedNodeInstance.h"
#include "Data/Core/CompileTimeResourceSystem.h"
#include "Data/Nodes/ConstantNodeConfig.h"
#include <optional>
#include <functional>
#include <cstdint>

namespace Vixen::RenderGraph {

// Forward declaration (NodeHandle defined in RenderGraph.h)
struct NodeHandle;

/**
 * @brief Generic constant/parameter node that passes a value through as output
 *
 * Accepts any REGISTERED resource type as a parameter, then provides it as an
 * output slot. This allows non-node resources (like manually created VulkanShader,
 * textures, etc.) to be injected into the render graph topology.
 *
 * IMPORTANT: Only types registered in RESOURCE_TYPE_REGISTRY can be used.
 * This ensures type safety and compile-time validation.
 *
 * Supports optional cleanup callback for externally-managed resources.
 *
 * Usage:
 *   auto shaderNode = renderGraph->AddNode("ShaderConstant", "shader_const");
 *   auto* constNode = static_cast<ConstantNode*>(renderGraph->GetInstance(shaderNode));
 *   constNode->SetValue<VulkanShaderPtr>(myShaderPtr);  // Type must be registered!
 *   constNode->SetCleanupCallback([myShaderPtr, device]() {
 *       myShaderPtr->DestroyShader(device);
 *       delete myShaderPtr;
 *   });
 *   Connect(shaderNode, ConstantNodeConfig::OUTPUT, pipelineNode, GraphicsPipelineNodeConfig::SHADER_STAGES);
 */
class ConstantNode : public TypedNode<ConstantNodeConfig> {
public:
    /**
     * @brief Construct constant node with typed value
     * @tparam T Type of the constant value (must be in RESOURCE_TYPE_REGISTRY)
     * @param name Node name
     * @param nodeType NodeType from registry
     * @param value Constant value to output
     */
    template<typename T>
    ConstantNode(const std::string& name, NodeType* nodeType, T value)
        : TypedNode<ConstantNodeConfig>(name, nodeType)
    {
        // Compile-time validation: T must be a registered resource type
        static_assert(ResourceTypeTraits<T>::isValid,
            "ConstantNode: Type must be registered in RESOURCE_TYPE_REGISTRY");

        // Create a Resource with the typed value
        storedResource = Resource::Create<T>(HandleDescriptor("Constant"));
        storedResource->SetHandle<T>(value);
    }

    /**
     * @brief Construct empty constant node (value set later via SetValue)
     * @param name Node name
     * @param nodeType NodeType from registry
     */
    explicit ConstantNode(const std::string& name, NodeType* nodeType)
        : TypedNode<ConstantNodeConfig>(name, nodeType)
    {
    }

    /**
     * @brief Set the constant value after construction
     * @tparam T Type of the constant value (must be in RESOURCE_TYPE_REGISTRY)
     * @param value Constant value to output
     */
    template<typename T>
    void SetValue(T value) {
        // Compile-time validation: T must be a registered resource type
        static_assert(ResourceTypeTraits<T>::isValid,
            "ConstantNode::SetValue: Type must be registered in RESOURCE_TYPE_REGISTRY");

        std::cout << "[ConstantNode::SetValue] Setting value for node: " << GetInstanceName() << std::endl;

        // Create a Resource with the typed value using the variant system
        storedResource = Resource::Create<T>(HandleDescriptor("Constant"));
        storedResource->SetHandle<T>(std::move(value));

        std::cout << "[ConstantNode::SetValue] Value set successfully" << std::endl;
    }

    /**
     * @brief Set cleanup callback for externally-managed resource
     * 
     * Use this when the constant node stores a pointer to an external resource
     * that needs cleanup (e.g., VulkanShader*). The callback will be invoked
     * during CleanupImpl() before the node is destroyed.
     * 
     * @param callback Cleanup function to invoke during node cleanup
     * @param dependencyHandles List of node handles that must be cleaned up AFTER this node
     *                          (e.g., if shader depends on device, pass {deviceNodeHandle})
     */
    void SetCleanupCallback(std::function<void()> callback, 
                           std::vector<NodeHandle> dependencyHandles = {}) {
        std::cout << "[ConstantNode::SetCleanupCallback] Setting callback for: " 
                  << GetInstanceName() << std::endl;
        cleanupCallback = std::move(callback);
        cleanupDependencyHandles = std::move(dependencyHandles);
        
        std::cout << "[ConstantNode::SetCleanupCallback] Callback set, valid="
                  << (cleanupCallback ? "true" : "false")
                  << ", dependencies=" << cleanupDependencyHandles.size() << std::endl;
    }

protected:
    // Template method pattern - override *Impl() methods
    void SetupImpl(TypedSetupContext& ctx) override {
        // No setup needed - graph will allocate output Resource
    }

    void CompileImpl(TypedCompileContext& ctx) override {
        std::cout << "[ConstantNode::Compile] START for node: " << GetInstanceName() << std::endl;

        // Ensure value has been set
        if (!storedResource.has_value()) {
            std::cout << "[ConstantNode::Compile] ERROR: Value not set!" << std::endl;
            throw std::runtime_error("ConstantNode '" + GetInstanceName() + "': Value not set before Compile()");
        }

        std::cout << "[ConstantNode::Compile] Value is set, retrieving output resource..." << std::endl;

        // Get the Resource* that the graph allocated for our output
        Resource* outputRes = NodeInstance::GetOutput(0, 0);
        if (!outputRes) {
            std::cout << "[ConstantNode::Compile] ERROR: Output resource not allocated!" << std::endl;
            throw std::runtime_error("ConstantNode '" + GetInstanceName() + "': Output resource not allocated");
        }

        std::cout << "[ConstantNode::Compile] Moving resource to output..." << std::endl;
        std::cout << "[ConstantNode::Compile] Output resource address: " << outputRes << std::endl;
        std::cout << "[ConstantNode::Compile] Source resource is valid: " << storedResource->IsValid() << std::endl;

        // Move the resource data to the graph's output (Resource only supports move semantics)
        *outputRes = std::move(*storedResource);

        std::cout << "[ConstantNode::Compile] Transferred resource is valid: " << outputRes->IsValid() << std::endl;
        std::cout << "[ConstantNode::Compile] SUCCESS - Resource transferred" << std::endl;
        
        // CRITICAL: Register cleanup if we have a callback
        // This ensures externally-managed resources (like VulkanShader*) are properly destroyed
        std::cout << "[ConstantNode::Compile] Checking cleanup callback: " 
                  << (cleanupCallback ? "EXISTS" : "NULL") << std::endl;
        
        if (cleanupCallback && GetOwningGraph()) {
            std::cout << "[ConstantNode::Compile] Registering cleanup callback in CleanupStack..." << std::endl;

            GetOwningGraph()->GetCleanupStack().Register(
                GetHandle(),
                GetInstanceName() + "_Cleanup",
                [this]() { this->Cleanup(); },
                cleanupDependencyHandles  // Dependencies: this must be cleaned up before these nodes
            );
            std::cout << "[ConstantNode::Compile] Cleanup callback registered successfully" << std::endl;
        } else {
            std::cout << "[ConstantNode::Compile] NOT registering cleanup (callback=" 
                      << (cleanupCallback ? "EXISTS" : "NULL") 
                      << ", graph=" << (GetOwningGraph() ? "EXISTS" : "NULL") << ")" << std::endl;
        }
    }

    void ExecuteImpl(TypedExecuteContext& ctx) override {
        // No execution needed - this is a data node
    }

    void CleanupImpl(TypedCleanupContext& ctx) override {
        // Invoke custom cleanup callback if set (for externally-managed resources)
        if (cleanupCallback) {
            std::cout << "[ConstantNode::CleanupImpl] Invoking cleanup callback for: " 
                      << GetInstanceName() << std::endl;
            cleanupCallback();
            cleanupCallback = nullptr;
        }
        
        storedResource.reset();
    }

private:
    std::optional<Resource> storedResource;
    std::function<void()> cleanupCallback;
    std::vector<NodeHandle> cleanupDependencyHandles;
};

} // namespace Vixen::RenderGraph
