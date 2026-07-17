/**
 * @file test_blit_node.cpp
 * @brief Unit tests for BlitNode's Baked-Perf M6 sync-hygiene fixes (Tasks 6.1-6.3).
 *
 * BlitSubmissionPolicy centralizes the fence/present-semaphore/acquire-wait ownership decision
 * BlitNode::ExecuteImpl makes from PARAM_LEAVE_IMAGE_IN_GENERAL -- see its own doc comment
 * (BlitNode.h) for the VUIDs this fixes (audit E2/E4). MakeRenderTargetPostBlitBarrier is the
 * Task 6.2 fix (audit E3): returns the blit's render-target source to GENERAL, the stable
 * cross-node boundary layout, instead of leaving it at TRANSFER_SRC_OPTIMAL across the frame
 * boundary.
 */

#include <gtest/gtest.h>

#include "Nodes/BlitNode.h"
#include "Nodes/Common/SwapchainBarriers.h"

using namespace Vixen::RenderGraph;

// VkImage is a dispatchable-handle typedef (opaque pointer); fake a distinct "handle" via
// reinterpret_cast of a small integer -- never dereferenced, only compared.
static VkImage FakeImage(uintptr_t id) { return reinterpret_cast<VkImage>(id); }

TEST(BlitSubmissionPolicy, TerminalBlitOwnsFenceAndSignalsPresentSemaphore) {
    const BlitSubmissionPolicy policy = ResolveBlitSubmissionPolicy(/*leaveImageInGeneral=*/false);

    EXPECT_TRUE(policy.ownsFrameFence)
        << "a terminal blit (no downstream sky/UI submit) is the frame's last compute-queue "
           "submit and must own the in-flight fence";
    EXPECT_TRUE(policy.signalsPresentSemaphore)
        << "a terminal blit is the one that hands the swapchain image to Present, so it must "
           "signal the binary renderComplete semaphore Present waits on";
    EXPECT_TRUE(policy.waitsForSwapchainAcquire)
        << "a terminal blit is also the first (and only) swapchain-touching submit, so it must "
           "consume the WSI acquire wait";
}

TEST(BlitSubmissionPolicy, CompositeBlitLeavesFenceAndPresentSemaphoreToTheFinalPass) {
    const BlitSubmissionPolicy policy = ResolveBlitSubmissionPolicy(/*leaveImageInGeneral=*/true);

    EXPECT_FALSE(policy.ownsFrameFence)
        << "in composite mode (Blit -> sky-projection -> UI) the UI composite node is the "
           "frame-final submit and the sole legitimate fence owner";
    EXPECT_FALSE(policy.signalsPresentSemaphore)
        << "audit E4: nothing ever waits this per-image binary semaphore in composite mode "
           "(UI owns the real present handoff via its own signal) -- signalling it here left an "
           "orphaned pending signal that became an illegal re-signal "
           "(VUID-vkQueueSubmit2-semaphore-03868) the next time this image index came around";
    EXPECT_TRUE(policy.waitsForSwapchainAcquire)
        << "audit E2: BlitNode is the real first swapchain-touching submit in the split baked "
           "path once the march stops waiting the acquire on the writesNoImage path (Task 6.1) "
           "-- true in BOTH terminal and composite mode";
}

TEST(BlitBarrierPolicy, RestoresSourceImageToGeneralForTheNextComputePass) {
    const VkImage image = FakeImage(1);
    const VkImageMemoryBarrier2 barrier = SwapchainBarriers::MakeRenderTargetPostBlitBarrier(image);

    EXPECT_EQ(barrier.image, image);
    EXPECT_EQ(barrier.oldLayout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL)
        << "the blit itself transitioned the render target GENERAL->TRANSFER_SRC_OPTIMAL to read "
           "it as the blit source -- this barrier's oldLayout must match that real prior state";
    EXPECT_EQ(barrier.newLayout, VK_IMAGE_LAYOUT_GENERAL)
        << "audit E3 fix: GENERAL is the stable cross-node boundary layout every ComputeStageNode "
           "IMAGE_WRITE producer's own private layout map already assumes for this handle -- "
           "before this fix the image was left at TRANSFER_SRC_OPTIMAL, and the next writer's map "
           "(which only ever stores GENERAL) declared a stale oldLayout=GENERAL, a genuine "
           "layout mismatch (VUID-vkCmdDraw-None-09600) the validation layer would catch";
    EXPECT_EQ(barrier.srcStageMask, VK_PIPELINE_STAGE_2_BLIT_BIT);
    EXPECT_EQ(barrier.srcAccessMask, VK_ACCESS_2_TRANSFER_READ_BIT);
    EXPECT_EQ(barrier.dstStageMask, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
    EXPECT_EQ(barrier.dstAccessMask,
              VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT)
        << "a future reader (e.g. DirectLighting's imageSize query) and the next writer "
           "(SpatialReuse) both need this image synchronized against the blit's read";
}
