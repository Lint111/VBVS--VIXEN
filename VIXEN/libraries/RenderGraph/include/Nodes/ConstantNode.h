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
        SetValue<T>(std::move(value));
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

        // Retain the value as a re-populator that rebuilds storedResource from a COPY of the value. The
        // graph must be rebuildable -- AR#1 Phase 3 device-loss recovery tears down and recompiles every
        // node -- but the old design moved the value into the output once and reset it on cleanup, so a
        // rebuild's second Compile threw "Value not set". Re-populating storedResource from the retained
        // value at the start of every Compile fixes that. storedResource is a STABLE member on purpose:
        // Resource::SetHandle's descriptor extractor captures `this`, and CompileImpl moves the resource
        // into the graph output -- the extractor keeps pointing at this member (which stays alive and
        // retains the handle), so it must not be a throwaway local. The re-populator survives Recompile/
        // DeviceLost cleanups (CPU config outlives a device loss, like the OS window).
        repopulateStored = [this, value = std::move(value)]() {
            storedResource = Resource::Create<T>(HandleDescriptor("Constant"));
            storedResource->SetHandle<T>(T(value));  // re-apply a COPY of the retained value
        };
        repopulateStored();  // populate now so the value is available before the first Compile
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
        // The re-populator rebuilds the stored value; absent it, no value was ever set.
        if (!repopulateStored) {
            throw std::runtime_error("ConstantNode '" + GetInstanceName() + "': Value not set before Compile()");
        }

        // Re-populate storedResource from the retained value so EVERY Compile (including a device-loss
        // rebuild) has a fresh resource to publish -- the old single-use move left it emptied.
        repopulateStored();

        // Get the Resource* that the graph allocated for our output
        Resource* outputRes = NodeInstance::GetOutput(0, 0);
        if (!outputRes) {
            throw std::runtime_error("ConstantNode '" + GetInstanceName() + "': Output resource not allocated");
        }

        // Move the resource data to the graph's output (Resource is move-only). The output's descriptor
        // extractor keeps pointing at storedResource (a stable member that retains the handle after the
        // move), so the moved-from member must stay alive -- which it does.
        *outputRes = std::move(*storedResource);

        // Register cleanup for an externally-managed resource (e.g. VulkanShader*) if a callback was set.
        if (cleanupCallback && GetOwningGraph()) {
            GetOwningGraph()->GetCleanupStack().Register(
                GetHandle(),
                GetInstanceName() + "_Cleanup",
                [this]() { this->Cleanup(); },
                cleanupDependencyHandles  // Dependencies: this must be cleaned up before these nodes
            );
        }
    }

    void ExecuteImpl(TypedExecuteContext& ctx) override {
        // No execution needed - this is a data node
    }

    void CleanupImpl(TypedCleanupContext& ctx) override {
        // The constant's value is CPU config that must SURVIVE a rebuild (recompile / device-loss recovery)
        // so the node can re-publish it -- keep the factory and the externally-managed-resource callback
        // across non-final cleanups; release them only on final teardown. (Mirrors WindowNode keeping the
        // window across recompiles: non-device state outlives a device loss.)
        if (ctx.reason != CleanupReason::FinalTeardown) {
            return;
        }

        // Final teardown: destroy the externally-managed resource (if any) and drop the retained value.
        if (cleanupCallback) {
            cleanupCallback();
            cleanupCallback = nullptr;
        }
        repopulateStored = nullptr;
        storedResource.reset();
    }

private:
    // Stable storage for the value Resource (NOT a local): the output's descriptor extractor captures the
    // address of this member, so it must outlive each Compile's move-to-output. Re-populated from the
    // retained value on every Compile via repopulateStored so the node is rebuildable.
    std::optional<Resource> storedResource;
    std::function<void()> repopulateStored;  // rebuilds storedResource from the retained value (see SetValue)
    std::function<void()> cleanupCallback;
    std::vector<NodeHandle> cleanupDependencyHandles;
};

} // namespace Vixen::RenderGraph
