#pragma once

#include "Core/TypedNodeInstance.h"
#include "Core/NodeType.h"
#include "Core/NodeLogging.h"
#include "Data/Nodes/TraceRaysNodeConfig.h"
#include "Data/Nodes/FrameSyncNodeConfig.h"
#include <memory>

namespace Vixen::Vulkan::Resources {
    class VulkanDevice;
}

namespace Vixen::RenderGraph {

/**
 * @brief Node type for ray tracing dispatch
 */
class TraceRaysNodeType : public TypedNodeType<TraceRaysNodeConfig> {
public:
    TraceRaysNodeType(const std::string& typeName = "TraceRays")
        : TypedNodeType<TraceRaysNodeConfig>(typeName) {}
    virtual ~TraceRaysNodeType() = default;

    std::unique_ptr<NodeInstance> CreateInstance(
        const std::string& instanceName
    ) const override;
};

/**
 * @brief Dispatches ray tracing using vkCmdTraceRaysKHR
 *
 * Phase K: Hardware Ray Tracing
 *
 * This node records the ray tracing dispatch command into a command buffer.
 * Follows the same pattern as ComputeDispatchNode for frame synchronization.
 *
 * Execute Phase:
 * 1. Wait for in-flight fence
 * 2. Allocate command buffer
 * 3. Begin command buffer
 * 4. Transition output image to GENERAL layout
 * 5. Bind RT pipeline
 * 6. Bind descriptor set (TLAS, output image)
 * 7. Push camera constants
 * 8. vkCmdTraceRaysKHR(width, height, depth)
 * 9. Transition output image to PRESENT_SRC layout
 * 10. End and submit command buffer
 */
class TraceRaysNode : public TypedNode<TraceRaysNodeConfig> {
public:
    TraceRaysNode(
        const std::string& instanceName,
        NodeType* nodeType
    );
    ~TraceRaysNode() override = default;

protected:
    void SetupImpl(TypedSetupContext& ctx) override;
    void CompileImpl(TypedCompileContext& ctx) override;
    void ExecuteImpl(TypedExecuteContext& ctx) override;
    void CleanupImpl(TypedCleanupContext& ctx) override;

private:
    /**
     * @brief Allocate command buffers (one per swapchain image)
     */
    bool AllocateCommandBuffers();

    /**
     * @brief Load RTX function pointers
     */
    bool LoadRTXFunctions();

    /**
     * @brief Cleanup resources (command buffers only - descriptors managed by DescriptorSetNode)
     */
    void DestroyResources();

    // Device reference
    Vixen::Vulkan::Resources::VulkanDevice* vulkanDevice_ = nullptr;
    VkCommandPool commandPool_ = VK_NULL_HANDLE;

    // Cached pipeline and accel data
    RayTracingPipelineData* pipelineData_ = nullptr;
    AccelerationStructureData* accelData_ = nullptr;

    // NOTE: Descriptor resources are now managed by DescriptorSetNode
    // TraceRaysNode receives descriptor sets via DESCRIPTOR_SETS input slot
    // This follows the same pattern as ComputeDispatchNode

    // Command buffers (one per swapchain image - matches ComputeDispatchNode)
    std::vector<VkCommandBuffer> commandBuffers_;
    uint32_t swapChainImageCount_ = 0;

    // Dispatch dimensions
    uint32_t width_ = 1920;
    uint32_t height_ = 1080;
    uint32_t depth_ = 1;

    // RTX function pointers
    PFN_vkCmdTraceRaysKHR vkCmdTraceRaysKHR_ = nullptr;
};

} // namespace Vixen::RenderGraph
