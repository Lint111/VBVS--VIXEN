// Copyright (C) 2025 Lior Yanai (eLiorg). Licensed under the MIT License.
// Sampled Lighting Inc3 M1 (KI-018): presentation-only render-target->swapchain blit node.
#pragma once

#include "Core/TypedNodeInstance.h"
#include "Core/NodeType.h"
#include "Core/NodeLogging.h"
#include "State/StatefulContainer.h"
#include "Data/Nodes/BlitNodeConfig.h"
#include "Core/FrameSyncSchedule.h"
#include "Nodes/Common/SwapchainBarriers.h"
#include <unordered_map>

namespace Vixen::RenderGraph {

/**
 * @brief Binary-WSI ownership for a blit submission.
 *
 * A terminal blit is the frame-final submit and therefore owns both the in-flight fence and
 * the binary semaphore consumed by Present. A composite blit has downstream sky/UI submits;
 * signalling a binary semaphore there is not merely redundant: because nobody waits it, the
 * next signal of the same per-image semaphore is invalid (VUID-vkQueueSubmit2-semaphore-03868).
 */
struct BlitSubmissionPolicy {
    bool ownsFrameFence = false;
    bool signalsPresentSemaphore = false;
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
    void SetupImpl(TypedSetupContext& ctx) override;
    void CompileImpl(TypedCompileContext& ctx) override;
    void ExecuteImpl(TypedExecuteContext& ctx) override;
    void CleanupImpl(TypedCleanupContext& ctx) override;

private:
    void RecordBlitCommands(Context& ctx, VkCommandBuffer cmd, uint32_t imageIndex, bool leaveImageInGeneral);

    VkCommandPool commandPool_ = VK_NULL_HANDLE;

    // Per-swapchain-image command buffers with dirty-state tracking.
    StatefulContainer<VkCommandBuffer> commandBuffers_;

    // Sampled Lighting Inc3 M1: tracks the LAST KNOWN layout of every VkImage handle this
    // node touches (both the render-target source and the swapchain destination — same
    // KI-007-fix pattern ComputeDispatchNode's own renderTargetImageLayouts_ uses, and the
    // same map SwapchainBarriers::BlitRenderTargetToSwapchain expects a caller to own).
    std::unordered_map<VkImage, VkImageLayout> layoutTracking_;
};

} // namespace Vixen::RenderGraph
