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

namespace Vixen::RenderGraph {

/**
 * @brief Binary-WSI submission policy for a blit (Baked-Perf M6 Task 6.3, audit E4).
 *
 * A terminal blit (leaveImageInGeneral==false) is the last swapchain-touching submit on
 * that path and therefore owns both the binary in-flight fence and renderComplete semaphore
 * consumed by Present. A composite blit (leaveImageInGeneral==true) has downstream
 * sky-projection/UI submits. UI owns the binary fence for its existing synchronization role;
 * FrameSyncNode's frame-end marker guards full-frame resource reuse, while compute-to-UI ordering
 * is carried by the baked timeline edge (P5b M3). Before this fix, BlitNode unconditionally
 * signalled the per-image binary renderComplete semaphore even in composite mode: nothing waits it (UI's own
 * signal is a SEPARATE semaphore), so the signal sits permanently pending, and the NEXT
 * time this same swapchain image index comes back around, signalling it again is a
 * binary-semaphore re-signal-without-intervening-wait VUID
 * (VUID-vkQueueSubmit2-semaphore-03868) — an orphaned signal, audit finding E4.
 */
struct BlitSubmissionPolicy {
    bool ownsFrameFence = false;
    bool signalsPresentSemaphore = false;
    // Baked-Perf M6 Task 6.1 (audit E2): true in the default composite chain — this blit is
    // the real first swapchain-touching submit once the march stops waiting the acquire on
    // the writesNoImage path (see ComputeDispatchWaitsForSwapchainAcquire, ComputeDispatchNode.h).
    // Always true today (both terminal and composite blits are downstream of a writesNoImage
    // march); kept as an explicit field rather than an always-true constant so a FUTURE graph
    // shape where something else already consumed the acquire ahead of this node has a place
    // to say so without re-deriving the policy shape.
    bool waitsForSwapchainAcquire = true;
};

[[nodiscard]] constexpr BlitSubmissionPolicy ResolveBlitSubmissionPolicy(bool leaveImageInGeneral) {
    const bool isTerminal = !leaveImageInGeneral;
    return {isTerminal, isTerminal, true};
}

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
    void TypedSetupImpl(TypedSetupContext& ctx) override;
    void TypedCompileImpl(TypedCompileContext& ctx) override;
    void TypedExecuteImpl(TypedExecuteContext& ctx) override;
    void TypedCleanupImpl(TypedCleanupContext& ctx) override;

private:
    void RecordBlitCommands(Context& ctx, VkCommandBuffer cmd, uint32_t imageIndex,
                            uint32_t frameIndex, bool leaveImageInGeneral);

    VkCommandPool commandPool_ = VK_NULL_HANDLE;

    // Per-swapchain-image command buffers with dirty-state tracking.
    StatefulContainer<VkCommandBuffer> commandBuffers_;

    // Task 0.1 (Baked-Content Perf Audit, top action #9): GPU timing for the presentation
    // blit, same centralized-GPUQueryManager pattern as every other timed node.
    std::shared_ptr<GPUPerformanceLogger> gpuPerfLogger_;

public:
    /// Get GPU performance logger for external metrics extraction (e.g. PerfCsvWriter).
    /// @return Pointer to GPUPerformanceLogger, or nullptr if not initialized.
    [[nodiscard]] GPUPerformanceLogger* GetGPUPerformanceLogger() const { return gpuPerfLogger_.get(); }
};

} // namespace Vixen::RenderGraph
