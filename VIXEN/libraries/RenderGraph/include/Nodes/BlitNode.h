// Copyright (C) 2025 Lior Yanai (eLiorg). Licensed under the MIT License.
// Sampled Lighting Inc3 M1 (KI-018): presentation-only render-target->swapchain blit node.
#pragma once

#include "Core/TypedNodeInstance.h"
#include "Core/NodeType.h"
#include "Core/NodeLogging.h"
#include "State/StatefulContainer.h"
#include "Core/GPUPerformanceLogger.h"
#include "Data/Nodes/BlitNodeConfig.h"
#include "Core/FrameSyncSchedule.h"
#include "Nodes/Common/SwapchainBarriers.h"
#include <unordered_map>

namespace Vixen::RenderGraph {

/**
 * @brief Node type for the presentation-only blit node.
 */
class BlitNodeType : public TypedNodeType<BlitNodeConfig> {
public:
    BlitNodeType(const std::string& typeName = "Blit")
        : TypedNodeType<BlitNodeConfig>(typeName) {}
    ~BlitNodeType() override = default;

    std::unique_ptr<NodeInstance> CreateInstance(const std::string& instanceName) const override;
};

/**
 * @brief Presentation-only render-target->swapchain blit node (Sampled Lighting Inc3 M1, KI-018).
 *
 * One node = one blit = one vkQueueSubmit2 = its OWN SubmitGroup, same shape as
 * ComputeStageNode's consumer role, but with a blit (SwapchainBarriers::
 * BlitRenderTargetToSwapchain) instead of a compute dispatch — no pipeline, no
 * descriptor sets, no push constants. See BlitNodeConfig.h for the full slot contract
 * and the architectural rationale (why this is a separate node, not folded into
 * ComputeStageNode or ComputeDispatchNode).
 */
class BlitNode : public TypedNode<BlitNodeConfig> {
public:
    using Base = TypedNode<BlitNodeConfig>;

    BlitNode(const std::string& instanceName, NodeType* nodeType);
    ~BlitNode() override = default;

protected:
    void SetupImpl(TypedSetupContext& ctx) override;
    void CompileImpl(TypedCompileContext& ctx) override;
    void ExecuteImpl(TypedExecuteContext& ctx) override;
    void CleanupImpl(TypedCleanupContext& ctx) override;

private:
    void RecordBlitCommands(Context& ctx, VkCommandBuffer cmd, uint32_t imageIndex,
                            uint32_t frameIndex, bool leaveImageInGeneral);

    VkCommandPool commandPool_ = VK_NULL_HANDLE;

    // Per-swapchain-image command buffers with dirty-state tracking.
    StatefulContainer<VkCommandBuffer> commandBuffers_;

    // Sampled Lighting Inc3 M1: tracks the LAST KNOWN layout of every VkImage handle this
    // node touches (both the render-target source and the swapchain destination — same
    // KI-007-fix pattern ComputeDispatchNode's own renderTargetImageLayouts_ uses, and the
    // same map SwapchainBarriers::BlitRenderTargetToSwapchain expects a caller to own).
    std::unordered_map<VkImage, VkImageLayout> layoutTracking_;

    // Task 0.1 (Baked-Content Perf Audit, top action #9): GPU timing for the presentation
    // blit, same centralized-GPUQueryManager pattern as every other timed node.
    std::shared_ptr<GPUPerformanceLogger> gpuPerfLogger_;

public:
    /// Get GPU performance logger for external metrics extraction (e.g. PerfCsvWriter).
    /// @return Pointer to GPUPerformanceLogger, or nullptr if not initialized.
    [[nodiscard]] GPUPerformanceLogger* GetGPUPerformanceLogger() const { return gpuPerfLogger_.get(); }
};

} // namespace Vixen::RenderGraph
