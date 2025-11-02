#pragma once
#include "Core/TypedNodeInstance.h"
#include "Core/NodeType.h"
#include "Core/PerFrameResources.h"
#include "Core/StatefulContainer.h"
#include "GeometryRenderNodeConfig.h"
#include <memory>
#include <vector>

namespace Vixen::RenderGraph {

/**
 * @brief Node type for recording geometry rendering commands
 * 
 * Records draw commands into command buffers including:
 * - Begin render pass
 * - Bind pipeline
 * - Bind descriptor sets
 * - Bind vertex/index buffers
 * - Set viewport and scissor
 * - Draw commands
 * - End render pass
 * 
 * Type ID: 109
 */
class GeometryRenderNodeType : public NodeType {
public:
    GeometryRenderNodeType(const std::string& typeName = "GeometryRender");
    virtual ~GeometryRenderNodeType() = default;

    std::unique_ptr<NodeInstance> CreateInstance(
        const std::string& instanceName
    ) const override;
};

/**
 * @brief Node instance for recording geometry render commands
 * 
 * Now uses TypedNode<GeometryRenderNodeConfig> for compile-time type safety.
 * All inputs/outputs are accessed via the typed config slot API.
 * 
 * See GeometryRenderNodeConfig.h for slot definitions and parameters.
 */
class GeometryRenderNode : public TypedNode<GeometryRenderNodeConfig> {
public:
    GeometryRenderNode(
        const std::string& instanceName,
        NodeType* nodeType
    );
    virtual ~GeometryRenderNode();

    // Record draw commands for a specific framebuffer
    void RecordDrawCommands(VkCommandBuffer cmdBuffer, uint32_t framebufferIndex);

protected:
	// Template method pattern - override *Impl() methods
	void SetupImpl() override;
	void CompileImpl() override;
	void ExecuteImpl(TaskContext& ctx) override;
	void CleanupImpl() override;

private:
    // Draw parameters (from node parameters)
    uint32_t vertexCount = 0;
    uint32_t instanceCount = 1;
    uint32_t firstVertex = 0;
    uint32_t firstInstance = 0;
    bool useIndexBuffer = false;
    uint32_t indexCount = 0;

    // Clear values
    VkClearValue clearColor{};
    VkClearValue clearDepthStencil{};

    // Phase 0.1: Per-frame command buffer management
    // Command buffers are allocated per swapchain image to prevent race conditions.
    VulkanDevicePtr vulkanDevice = nullptr;
    VkCommandPool commandPool = VK_NULL_HANDLE;

    // Phase 0.2: Semaphores now managed by FrameSyncNode (per-flight pattern)
    // Removed: std::vector<VkSemaphore> renderCompleteSemaphores

    // Phase 0.3: Command buffer with state tracking
    // Tracks command buffers + their dirty/ready state
    StatefulContainer<VkCommandBuffer> commandBuffers;  // One per swapchain image

    // Previous frame inputs (for dirty detection)
    VkRenderPass lastRenderPass = VK_NULL_HANDLE;
    VkPipeline lastPipeline = VK_NULL_HANDLE;
    VkBuffer lastVertexBuffer = VK_NULL_HANDLE;
    VkDescriptorSet lastDescriptorSet = VK_NULL_HANDLE;
};

} // namespace Vixen::RenderGraph
