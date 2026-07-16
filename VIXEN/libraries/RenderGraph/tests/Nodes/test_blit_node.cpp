/**
 * @file test_blit_node.cpp
 * @brief Submission-policy tests for terminal and composite blits.
 * @feature baked-voxel-rendering/frame-sync
 * @last-pass 2026-07-15
 */

#include <gtest/gtest.h>

#include "Nodes/BlitNode.h"

using namespace Vixen::RenderGraph;

TEST(BlitSubmissionPolicy, TerminalBlitOwnsFenceAndSignalsPresentSemaphore) {
    const BlitSubmissionPolicy policy = ResolveBlitSubmissionPolicy(false);

    EXPECT_TRUE(policy.ownsFrameFence);
    EXPECT_TRUE(policy.signalsPresentSemaphore);
    EXPECT_TRUE(policy.waitsForSwapchainAcquire);
}

TEST(BlitSubmissionPolicy, CompositeBlitLeavesFenceAndPresentSemaphoreToFinalPass) {
    const BlitSubmissionPolicy policy = ResolveBlitSubmissionPolicy(true);

    EXPECT_FALSE(policy.ownsFrameFence);
    EXPECT_FALSE(policy.signalsPresentSemaphore)
        << "an intermediate binary semaphore signal with no wait remains pending and is illegal to signal again";
    EXPECT_TRUE(policy.waitsForSwapchainAcquire);
}

TEST(BlitBarrierPolicy, RestoresSourceImageToGeneralForTheNextComputePass) {
    const VkImage image = reinterpret_cast<VkImage>(uintptr_t{1});
    const VkImageMemoryBarrier2 barrier =
        SwapchainBarriers::MakeRenderTargetPostBlitBarrier(image);

    EXPECT_EQ(barrier.image, image);
    EXPECT_EQ(barrier.oldLayout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    EXPECT_EQ(barrier.newLayout, VK_IMAGE_LAYOUT_GENERAL);
    EXPECT_EQ(barrier.srcStageMask, VK_PIPELINE_STAGE_2_BLIT_BIT);
    EXPECT_EQ(barrier.srcAccessMask, VK_ACCESS_2_TRANSFER_READ_BIT);
    EXPECT_EQ(barrier.dstStageMask, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
    EXPECT_EQ(barrier.dstAccessMask,
              VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT)
        << "DirectLighting reads imageSize before SpatialReuse writes this image next frame";
}
