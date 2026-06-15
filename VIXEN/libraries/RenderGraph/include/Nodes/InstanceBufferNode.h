#pragma once
#include "Core/TypedNodeInstance.h"
#include "Core/NodeType.h"
#include "Data/Nodes/InstanceBufferNodeConfig.h"
#include <memory>

namespace Vixen::RenderGraph {

/**
 * @brief Node type for the per-instance model-matrix storage buffer (SSBO)
 * Type ID: 122
 */
class InstanceBufferNodeType : public TypedNodeType<InstanceBufferNodeConfig> {
public:
    InstanceBufferNodeType(const std::string& typeName = "InstanceBuffer")
        : TypedNodeType<InstanceBufferNodeConfig>(typeName) {}
    virtual ~InstanceBufferNodeType() = default;

    std::unique_ptr<NodeInstance> CreateInstance(const std::string& instanceName) const override;
};

/**
 * @brief Allocates a storage buffer of N = gridDim^2 glm::mat4 per-instance model
 * matrices (a planar grid of translations) for hardware-instanced drawing (AR#31).
 *
 * Outputs the VkBuffer plus the instance count; a vertex shader indexes the buffer
 * by gl_InstanceIndex. The buffer is host-visible/host-coherent and filled once at
 * compile time (static upload).
 *
 * FR-7 lifecycle: the buffer persists across graph recompile; released only on
 * FinalTeardown.
 */
class InstanceBufferNode : public TypedNode<InstanceBufferNodeConfig> {
public:
    using Base = TypedNode<InstanceBufferNodeConfig>;

    InstanceBufferNode(const std::string& instanceName, NodeType* nodeType);
    ~InstanceBufferNode() override = default;

protected:
    void SetupImpl(TypedSetupContext&    ctx) override;
    void CompileImpl(TypedCompileContext& ctx) override;
    void ExecuteImpl(TypedExecuteContext& ctx) override;
    void CleanupImpl(TypedCleanupContext& ctx) override;

private:
    void CreateBuffer(Vixen::Vulkan::Resources::VulkanDevice* device);
    void DestroyBuffer();

    VkBuffer                                buffer_        = VK_NULL_HANDLE;
    VkDeviceMemory                          memory_        = VK_NULL_HANDLE;
    uint32_t                                instanceCount_ = 0;
    uint32_t                                gridDim_       = 8;
    float                                   spacing_       = 2.0f;
};

} // namespace Vixen::RenderGraph
