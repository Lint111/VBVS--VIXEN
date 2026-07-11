#pragma once

#include "Core/TypedNodeInstance.h"
#include "Core/NodeType.h"
#include "Core/NodeLogging.h"
#include "State/StatefulContainer.h"
#include "Core/GPUPerformanceLogger.h"
#include "Data/Nodes/ComputeDispatchNodeConfig.h"
#include "Core/FrameSyncSchedule.h"
#include "Nodes/Common/SwapchainBarriers.h"
#include <unordered_map>

namespace Vixen::RenderGraph {

// DecideRenderTargetPriorLayoutAndUpdate (KI-007 fix) moved to
// Nodes/Common/SwapchainBarriers.h (Sampled Lighting Inc3 M1) so ComputeStageNode's
// IMAGE_WRITE role and the new BlitNode can reuse it too — this header still pulls it
// in transitively via the SwapchainBarriers.h include above.

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

    // M4 (KI-007 fix): tracks the LAST KNOWN layout of each render-target VkImage handle (the ring
    // has imageCount_ of them, cycling per in-flight frame). RenderTargetNode keeps its images
    // persistent across a same-extent recompile (FR-7), so a new Compile does NOT imply new
    // handles. A plain seen/not-seen set (the pre-fix scheme) assumed every handle alternates
    // UNDEFINED->GENERAL->[blit]->TRANSFER_SRC_OPTIMAL->GENERAL->... in lockstep, which breaks once
    // multiple frames are in flight: a command buffer can be RE-RECORDED against a ring slot
    // whose actual last real transition doesn't match that two-state guess, producing a genuine
    // oldLayout mismatch (VUID-vkCmdDraw-None-09600) and visibly corrupt/flickering frames on real
    // hardware, not just validation noise. Tracking the actual last-recorded layout per handle
    // (updated at both the compute-write barrier and the post-blit transition) is exact instead of
    // guessed. See DecideRenderTargetPriorLayout (free function, unit-testable without a device).
    std::unordered_map<VkImage, VkImageLayout> renderTargetImageLayouts_;

public:
    /// Get GPU performance logger for external metrics extraction
    /// @return Pointer to GPUPerformanceLogger, or nullptr if not initialized
    GPUPerformanceLogger* GetGPUPerformanceLogger() const {
        return gpuPerfLogger_.get();
    }
};

} // namespace Vixen::RenderGraph
