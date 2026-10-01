#pragma once

#include "Core/TypedNodeInstance.h"
#include "Core/NodeType.h"
#include "Core/NodeLogging.h"
#include "State/StatefulContainer.h"
#include "Core/GPUPerformanceLogger.h"
#include "Data/Nodes/ComputeDispatchNodeConfig.h"
#include "Core/FrameSyncSchedule.h"
#include "Nodes/Common/SwapchainBarriers.h"

namespace Vixen::RenderGraph {

// DecideRenderTargetPriorLayoutAndUpdate (KI-007 fix) moved to
// Nodes/Common/SwapchainBarriers.h (Sampled Lighting Inc3 M1) so ComputeStageNode's
// IMAGE_WRITE role and the new BlitNode can reuse it too — this header still pulls it
// in transitively via the SwapchainBarriers.h include above.

// Baked-Perf M6 Task 6.1 (audit E2): the WSI acquire semaphore belongs to the FIRST
// submission that actually accesses the swapchain image — waiting it anywhere else
// cannot prove acquired-image ownership to Present, and validation reports a missing
// wait against whichever submit really does touch the image first. In the split baked
// path (writesNoImage==true: this dispatch writes only HitRecord, an SSBO — see
// PARAM_WRITES_NO_IMAGE's own doc comment) the real first swapchain-touching submit is
// BlitNode, several passes later. Cross-frame reuse safety for this dispatch's own
// resources (HitRecord, the command-buffer ring) is unaffected by which node waits the
// acquire: FrameSyncNode::ExecuteImpl already does a full CPU-side vkWaitForFences on
// the current flight's in-flight fence (UINT64_MAX timeout) before this node's own
// ExecuteImpl ever runs, which is what actually guarantees the previous frame using this
// flight-ring slot has finished — the acquire semaphore only ever gated the SWAPCHAIN
// image, which this dispatch never touches on the writesNoImage path.
[[nodiscard]] constexpr bool ComputeDispatchWaitsForSwapchainAcquire(bool writesNoImage) {
    return !writesNoImage;
}

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
    void TypedSetupImpl(TypedSetupContext& ctx) override;
    void TypedCompileImpl(TypedCompileContext& ctx) override;
    void TypedExecuteImpl(TypedExecuteContext& ctx) override;
    void TypedCleanupImpl(TypedCleanupContext& ctx) override;

private:
    void RecordComputeCommands(Context& ctx, VkCommandBuffer cmdBuffer, uint32_t imageIndex, uint32_t frameIndex, const void* pushConstantData, bool leaveImageInGeneral, bool writesNoImage);

    // Extracted helper methods for RecordComputeCommands
    void ReplayEntryBarriers(VkCommandBuffer cmd, const SubmitGroup& group,
                             uint32_t imageIndex,
                             Vixen::Vulkan::Resources::IRenderTarget* swapchainInfo);
    void BindComputePipeline(VkCommandBuffer cmdBuffer, VkPipeline pipeline, VkPipelineLayout layout, VkDescriptorSet descriptorSet);
    void SetPushConstants(Context& ctx, VkCommandBuffer cmdBuffer, VkPipelineLayout layout, const void* pushConstantData);

    // M4: render-scale decoupling. When RENDER_TARGET_INFO is connected, blits the offscreen
    // render target (already written by the dispatch, still GENERAL) to the swapchain image.
    // Sampled Lighting Inc3 M1: now calls the shared free function
    // SwapchainBarriers::BlitRenderTargetToSwapchain (extracted from this method's old body)
    // instead of owning a private copy — see that function's doc comment for the barrier
    // sequence.

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

public:
    /// Get GPU performance logger for external metrics extraction
    /// @return Pointer to GPUPerformanceLogger, or nullptr if not initialized
    GPUPerformanceLogger* GetGPUPerformanceLogger() const {
        return gpuPerfLogger_.get();
    }
};

} // namespace Vixen::RenderGraph
