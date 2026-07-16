/**
 * @file test_compute_dispatch_node.cpp
 * @brief Unit tests for ComputeDispatchNode's KI-007 fix (DecideRenderTargetPriorLayoutAndUpdate).
 *
 * DecideRenderTargetPriorLayoutAndUpdate is a pure function (no device needed) that tracks the
 * ACTUAL last-recorded layout of a render-target VkImage handle, replacing a prior seen/not-seen
 * guess that assumed every handle strictly alternates GENERAL<->TRANSFER_SRC_OPTIMAL in lockstep --
 * an assumption that breaks once multiple frames are in flight and a command buffer can be
 * re-recorded against a ring slot whose actual last transition doesn't match the guess, producing
 * a real oldLayout mismatch (VUID-vkCmdDraw-None-09600) and visibly corrupt/flickering frames.
 */

#include <gtest/gtest.h>

#include "Nodes/ComputeDispatchNode.h"

using namespace Vixen::RenderGraph;

// VkImage is a dispatchable-handle typedef (opaque pointer); fake distinct "handles" via
// reinterpret_cast of small integers -- never dereferenced, only compared/hashed as map keys.
static VkImage FakeImage(uintptr_t id) { return reinterpret_cast<VkImage>(id); }

TEST(DecideRenderTargetPriorLayoutAndUpdate, FirstUseOfAHandleIsUndefined) {
    std::unordered_map<VkImage, VkImageLayout> tracked;
    const VkImage img = FakeImage(1);

    const VkImageLayout prior = DecideRenderTargetPriorLayoutAndUpdate(tracked, img, VK_IMAGE_LAYOUT_GENERAL);

    EXPECT_EQ(prior, VK_IMAGE_LAYOUT_UNDEFINED) << "a never-seen handle must report UNDEFINED (fresh/recreated image)";
}

TEST(DecideRenderTargetPriorLayoutAndUpdate, SecondUseReportsTheActualTrackedLayoutNotAGuess) {
    std::unordered_map<VkImage, VkImageLayout> tracked;
    const VkImage img = FakeImage(1);

    DecideRenderTargetPriorLayoutAndUpdate(tracked, img, VK_IMAGE_LAYOUT_GENERAL);          // compute write
    tracked[img] = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;                                    // simulates the blit's exit barrier
    const VkImageLayout prior = DecideRenderTargetPriorLayoutAndUpdate(tracked, img, VK_IMAGE_LAYOUT_GENERAL);

    EXPECT_EQ(prior, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL)
        << "must report the image's REAL last-recorded layout, not a hardcoded second-use guess";
}

TEST(DecideRenderTargetPriorLayoutAndUpdate, DistinctRingSlotsAreTrackedIndependently) {
    // This is the actual KI-007 regression: a std::set<VkImage> (seen/not-seen) cannot distinguish
    // "this ring slot was last a blit source" from "this ring slot was last something else" once
    // MULTIPLE distinct handles are in play -- each handle needs its OWN tracked state.
    std::unordered_map<VkImage, VkImageLayout> tracked;
    const VkImage slotA = FakeImage(1);
    const VkImage slotB = FakeImage(2);

    DecideRenderTargetPriorLayoutAndUpdate(tracked, slotA, VK_IMAGE_LAYOUT_GENERAL);
    tracked[slotA] = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;  // slotA has been blitted once

    // slotB is a DIFFERENT physical image that has never been touched -- must still report
    // UNDEFINED even though slotA (a different handle) is already past its first use.
    const VkImageLayout priorB = DecideRenderTargetPriorLayoutAndUpdate(tracked, slotB, VK_IMAGE_LAYOUT_GENERAL);
    EXPECT_EQ(priorB, VK_IMAGE_LAYOUT_UNDEFINED) << "a fresh handle must not inherit another handle's history";

    // slotA's own history must be unaffected by slotB's insertion.
    tracked[slotA] = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    const VkImageLayout priorA = DecideRenderTargetPriorLayoutAndUpdate(tracked, slotA, VK_IMAGE_LAYOUT_GENERAL);
    EXPECT_EQ(priorA, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
}

TEST(DecideRenderTargetPriorLayoutAndUpdate, UpdatesTrackedStateToTheNewLayout) {
    std::unordered_map<VkImage, VkImageLayout> tracked;
    const VkImage img = FakeImage(1);

    DecideRenderTargetPriorLayoutAndUpdate(tracked, img, VK_IMAGE_LAYOUT_GENERAL);

    ASSERT_NE(tracked.find(img), tracked.end());
    EXPECT_EQ(tracked.at(img), VK_IMAGE_LAYOUT_GENERAL)
        << "the map must be updated to the layout just transitioned to, not left at the old value";
}

TEST(ComputeDispatchAcquirePolicy, OffscreenOnlyPassDoesNotConsumeSwapchainAcquire) {
    EXPECT_FALSE(ComputeDispatchWaitsForSwapchainAcquire(true));
}

TEST(ComputeDispatchAcquirePolicy, SwapchainPassConsumesSwapchainAcquire) {
    EXPECT_TRUE(ComputeDispatchWaitsForSwapchainAcquire(false));
}
