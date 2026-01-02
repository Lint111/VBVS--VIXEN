#pragma once
#include "Core/TypedNodeInstance.h"
#include "Core/NodeType.h"
#include "Core/PerFrameResources.h"
#include "State/StatefulContainer.h"
#include "Core/GPUPerformanceLogger.h"
#include "Data/Nodes/GeometryRenderNodeConfig.h"
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
class GeometryRenderNodeType : public TypedNodeType<GeometryRenderNodeConfig> {
public:
    GeometryRenderNodeType(const std::string& typeName = "GeometryRender")
        : TypedNodeType<GeometryRenderNodeConfig>(typeName) {}
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
    ~GeometryRenderNode() override = default;

    // Record draw commands for a specific framebuffer
    void RecordDrawCommands(Context& ctx, VkCommandBuffer cmdBuffer, uint32_t framebufferIndex, uint32_t frameIndex);

protected:
    // Command recording helpers (extracted from RecordDrawCommands)
    void BeginCommandBuffer(VkCommandBuffer cmdBuffer);
    void ValidateInputs(Context& ctx, VkRenderPass renderPass, VkPipeline pipeline,
                        VkPipelineLayout pipelineLayout, VkBuffer vertexBuffer,
                        SwapChainPublicVariables* swapchainInfo);
    void BeginRenderPassWithClear(VkCommandBuffer cmdBuffer, VkRenderPass renderPass,
                                  VkFramebuffer framebuffer, uint32_t width, uint32_t height);
    void BindPipelineAndDescriptors(VkCommandBuffer cmdBuffer, VkPipeline pipeline,
                                    VkPipelineLayout pipelineLayout,
                                    const std::vector<VkDescriptorSet>& descriptorSets);
    void SetPushConstants(Context& ctx, VkCommandBuffer cmdBuffer, VkPipelineLayout pipelineLayout);
    void BindVertexAndIndexBuffers(VkCommandBuffer cmdBuffer, Context& ctx, VkBuffer vertexBuffer);
    void SetViewportAndScissor(VkCommandBuffer cmdBuffer, const VkExtent2D& extent);
    void RecordDrawCall(VkCommandBuffer cmdBuffer, Context& ctx);
    void EndCommandBuffer(VkCommandBuffer cmdBuffer);
	// Template method pattern - override *Impl() methods
	void SetupImpl(TypedSetupContext& ctx) override;
	void CompileImpl(TypedCompileContext& ctx) override;
	void ExecuteImpl(TypedExecuteContext& ctx) override;
	void CleanupImpl(TypedCleanupContext& ctx) override;

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

    // GPU performance metrics (timestamp queries)
    std::shared_ptr<GPUPerformanceLogger> gpuPerfLogger_;

public:
    /// Get GPU performance logger for external metrics extraction
    /// @return Pointer to GPUPerformanceLogger, or nullptr if not initialized
    GPUPerformanceLogger* GetGPUPerformanceLogger() const {
        return gpuPerfLogger_.get();
    }
};

} // namespace Vixen::RenderGraph

