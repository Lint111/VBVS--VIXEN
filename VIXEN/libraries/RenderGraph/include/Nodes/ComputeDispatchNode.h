#pragma once

#include "Core/TypedNodeInstance.h"
#include "Core/NodeType.h"
#include "Core/NodeLogging.h"
#include "State/StatefulContainer.h"
#include "Core/GPUPerformanceLogger.h"
#include "Data/Nodes/ComputeDispatchNodeConfig.h"
#include "Core/FrameSyncSchedule.h"
#include <unordered_map>

namespace Vixen::RenderGraph {

// KI-007 fix: given the tracked last-known layout for a render-target VkImage handle (absent =
// never seen -> fresh/recreated image, true prior layout UNDEFINED), returns the layout to declare
// as the barrier's oldLayout and updates the map to the new layout the caller is about to
// transition to. Pure/free so it's unit-testable with fake VkImage handles, no device needed.
inline VkImageLayout DecideRenderTargetPriorLayoutAndUpdate(
    std::unordered_map<VkImage, VkImageLayout>& tracked,
    VkImage image,
    VkImageLayout newLayout)
{
    auto it = tracked.find(image);
    const VkImageLayout priorLayout = (it != tracked.end()) ? it->second : VK_IMAGE_LAYOUT_UNDEFINED;
    tracked[image] = newLayout;
    return priorLayout;
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
