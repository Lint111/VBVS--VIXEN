#pragma once

#include "Core/TypedNodeInstance.h"
#include "Core/NodeType.h"
#include "Core/NodeLogging.h"
#include "Core/StatefulContainer.h"
#include "Data/Nodes/ComputeDispatchNodeConfig.h"

namespace Vixen::RenderGraph {

/**
 * @brief Node type for generic compute shader dispatch
 *
 * Generic dispatcher for ANY compute shader, separating dispatch logic
 * from pipeline creation (ComputePipelineNode).
 */
class ComputeDispatchNodeType : public TypedNodeType<ComputeDispatchNodeConfig> {
public:
    ComputeDispatchNodeType(const std::string& typeName = "ComputeDispatch")
        : TypedNodeType<ComputeDispatchNodeConfig>(typeName) {}
    virtual ~ComputeDispatchNodeType() = default;

    std::unique_ptr<NodeInstance> CreateInstance(
        const std::string& instanceName
    ) const override;
};

/**
 * @brief Generic compute shader dispatch node
 *
 * Records command buffer with vkCmdDispatch for ANY compute shader.
 * Separates dispatch logic from pipeline creation (ComputePipelineNode).
 *
 * Phase G.3: Generic compute dispatcher for research flexibility
 *
 * Node chain:
 * ShaderLibraryNode -> ComputePipelineNode -> ComputeDispatchNode -> Present
 *
 * Responsibilities:
 * - Allocate command buffer from pool
 * - Record vkCmdBindPipeline (compute)
 * - Record vkCmdBindDescriptorSets (if provided)
 * - Record vkCmdPushConstants (if provided)
 * - Record vkCmdDispatch
 * - Output command buffer for submission
 *
 * Generic design allows ANY compute shader:
 * - Ray marching (Phase G)
 * - Voxel generation
 * - Post-processing effects
 * - Algorithm testing (Phase L)
 */
class ComputeDispatchNode : public TypedNode<ComputeDispatchNodeConfig> {
public:

    ComputeDispatchNode(
        const std::string& instanceName,
        NodeType* nodeType
    );
    ~ComputeDispatchNode() override = default;

protected:
    void SetupImpl(TypedSetupContext& ctx) override;
    void CompileImpl(TypedCompileContext& ctx) override;
    void ExecuteImpl(TypedExecuteContext& ctx) override;
    void CleanupImpl(TypedCleanupContext& ctx) override;

private:
    void RecordComputeCommands(Context& ctx, VkCommandBuffer cmdBuffer, uint32_t imageIndex, const void* pushConstantData);

    // Device and command pool references
    VulkanDevicePtr vulkanDevice = nullptr;
    VkCommandPool commandPool = VK_NULL_HANDLE;

    // Per-swapchain-image command buffers with state tracking
    StatefulContainer<VkCommandBuffer> commandBuffers;

    // Previous frame inputs (for dirty detection)
    VkPipeline lastPipeline = VK_NULL_HANDLE;
    VkPipelineLayout lastPipelineLayout = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> lastDescriptorSets;

#if VIXEN_DEBUG_BUILD
    // Performance logging (debug only)
    std::unique_ptr<class ComputePerformanceLogger> perfLogger_;
#endif
};

} // namespace Vixen::RenderGraph
