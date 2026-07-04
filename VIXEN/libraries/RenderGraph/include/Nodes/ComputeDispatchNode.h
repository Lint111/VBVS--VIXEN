#pragma once

#include "Core/TypedNodeInstance.h"
#include "Core/NodeType.h"
#include "Core/NodeLogging.h"
#include "State/StatefulContainer.h"
#include "Core/GPUPerformanceLogger.h"
#include "Data/Nodes/ComputeDispatchNodeConfig.h"
#include "Core/FrameSyncSchedule.h"

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
    void RecordComputeCommands(Context& ctx, VkCommandBuffer cmdBuffer, uint32_t imageIndex, uint32_t frameIndex, const void* pushConstantData, bool leaveImageInGeneral);

    // Extracted helper methods for RecordComputeCommands
    void ReplayEntryBarriers(VkCommandBuffer cmd, const SubmitGroup& group,
                             uint32_t imageIndex,
                             Vixen::Vulkan::Resources::IRenderTarget* swapchainInfo);
    // oldLayout defaults to UNDEFINED (WSI acquire / first-use contract, pre-M4 behavior). M4 passes
    // the render target's actual prior layout (TRANSFER_SRC_OPTIMAL after a blit) on every frame
    // after the first, so the barrier's declared oldLayout matches what synchronization validation
    // actually tracked instead of relying on UNDEFINED's "discard, don't care" escape hatch.
    void TransitionImageToGeneralBarrier2(VkCommandBuffer cmdBuffer, VkImage image,
                                          VkImageLayout oldLayout = VK_IMAGE_LAYOUT_UNDEFINED);
    void TransitionImageToPresentBarrier2(VkCommandBuffer cmdBuffer, VkImage image);
    void BindComputePipeline(VkCommandBuffer cmdBuffer, VkPipeline pipeline, VkPipelineLayout layout, VkDescriptorSet descriptorSet);
    void SetPushConstants(Context& ctx, VkCommandBuffer cmdBuffer, VkPipelineLayout layout, const void* pushConstantData);

    // M4: render-scale decoupling. When RENDER_TARGET_INFO is connected, blits the offscreen
    // render target (already written by the dispatch, still GENERAL) to the swapchain image,
    // ending in the same layout contract RecordComputeCommands already applies to the swapchain
    // (leaveImageInGeneral -> GENERAL for the UI pass, else -> PRESENT_SRC).
    void BlitRenderTargetToSwapchain(VkCommandBuffer cmdBuffer,
                                     Vixen::Vulkan::Resources::IRenderTarget* renderTarget,
                                     VkImage swapchainImage,
                                     VkExtent2D swapchainExtent,
                                     bool leaveImageInGeneral);

    // Device and command pool references
    VulkanDevice* vulkanDevice = nullptr;
    VkCommandPool commandPool = VK_NULL_HANDLE;

    // Per-swapchain-image command buffers with state tracking
    StatefulContainer<VkCommandBuffer> commandBuffers;

    // Previous frame inputs (for dirty detection)
    VkPipeline lastPipeline = VK_NULL_HANDLE;
    VkPipelineLayout lastPipelineLayout = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> lastDescriptorSets;

    // Performance logging (disabled by default, enable as needed)
    std::shared_ptr<class ComputePerformanceLogger> perfLogger_;  // Shared ownership for hierarchy

    // GPU performance metrics (timestamp queries + pipeline stats)
    std::shared_ptr<GPUPerformanceLogger> gpuPerfLogger_;

    // Task profile for cost estimation (Sprint 6.5: Profile integration)
    ITaskProfile* gpuProfile_ = nullptr;

    // M4: tracks which render-target VkImage handles (the ring has imageCount_ of them, cycling
    // per in-flight frame) this node has already written at least once. RenderTargetNode keeps its
    // images persistent across a same-extent recompile (FR-7), so a new Compile does NOT imply new
    // handles — only compare-and-update against the actual handle tells us whether THIS image is a
    // first-use (fresh/recreated -> true prior layout is UNDEFINED) or a steady-state write (its
    // last write's blit left it TRANSFER_SRC_OPTIMAL, unchanged since). A std::set (not a single
    // scalar) because the ring cycles through multiple distinct handles per frame-in-flight index.
    std::set<VkImage> seenRenderTargetImages_;

public:
    /// Get GPU performance logger for external metrics extraction
    /// @return Pointer to GPUPerformanceLogger, or nullptr if not initialized
    GPUPerformanceLogger* GetGPUPerformanceLogger() const {
        return gpuPerfLogger_.get();
    }
};

} // namespace Vixen::RenderGraph
