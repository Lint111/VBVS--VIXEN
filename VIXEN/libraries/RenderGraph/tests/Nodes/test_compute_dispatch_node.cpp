#include <gtest/gtest.h>
#include "Nodes/ComputeDispatchNode.h"
#include "Core/RenderGraph.h"
#include "Core/NodeTypeRegistry.h"

#include "Nodes/Common/PresentationSynchronization.h"
#include "Nodes/RenderTargetNode.h"
#include "Nodes/SwapChainNode.h"

using namespace Vixen::RenderGraph;
using Vixen::Vulkan::Resources::RenderTargetData;

TEST(DecideRenderTargetPriorLayoutAndUpdate, FirstUseOfAHandleIsUndefined) {
    RenderTargetData target;
    target.buffers.resize(1);
    EXPECT_EQ(DecideRenderTargetPriorLayoutAndUpdate(target, 0, VK_IMAGE_LAYOUT_GENERAL),
              VK_IMAGE_LAYOUT_UNDEFINED);
}

TEST(DecideRenderTargetPriorLayoutAndUpdate, SecondUseReportsTheActualTrackedLayoutNotAGuess) {
    RenderTargetData target;
    target.buffers.resize(1);
    DecideRenderTargetPriorLayoutAndUpdate(target, 0, VK_IMAGE_LAYOUT_GENERAL);
    // A different writer can leave a different layout on the same physical buffer.
    target.SetImageLayout(0, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    EXPECT_EQ(DecideRenderTargetPriorLayoutAndUpdate(target, 0, VK_IMAGE_LAYOUT_GENERAL),
              VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
}

TEST(DecideRenderTargetPriorLayoutAndUpdate, DistinctRingSlotsAreTrackedIndependently) {
    RenderTargetData target;
    target.buffers.resize(2);
    DecideRenderTargetPriorLayoutAndUpdate(target, 0, VK_IMAGE_LAYOUT_GENERAL);
    target.SetImageLayout(0, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    EXPECT_EQ(DecideRenderTargetPriorLayoutAndUpdate(target, 1, VK_IMAGE_LAYOUT_GENERAL),
              VK_IMAGE_LAYOUT_UNDEFINED);
    EXPECT_EQ(target.GetImageLayout(0), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
}

TEST(DecideRenderTargetPriorLayoutAndUpdate, UpdatesTrackedStateToTheNewLayout) {
    RenderTargetData target;
    target.buffers.resize(1);
    DecideRenderTargetPriorLayoutAndUpdate(target, 0, VK_IMAGE_LAYOUT_GENERAL);
    EXPECT_EQ(target.GetCurrentLayout(), VK_IMAGE_LAYOUT_GENERAL);
}

TEST(RenderTargetLayout, RecreatedBufferDoesNotInheritTheOldAllocationLayout) {
    RenderTargetData target;
    target.buffers.resize(1);
    target.SetImageLayout(0, VK_IMAGE_LAYOUT_GENERAL);
    target.buffers.clear();
    target.buffers.resize(1);
    EXPECT_EQ(target.GetCurrentLayout(), VK_IMAGE_LAYOUT_UNDEFINED);
}

TEST(RenderTargetLayout, RenderPassFinalLayoutComesFromTheCompiledPass) {
    NodeTypeRegistry registry;
    Vixen::RenderGraph::RenderGraph graph(&registry);
    const auto pass = reinterpret_cast<VkRenderPass>(uintptr_t{1});
    EXPECT_THROW(graph.GetRenderPassFinalLayout(pass), std::runtime_error);
    graph.RegisterRenderPassFinalLayout(pass, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    EXPECT_EQ(graph.GetRenderPassFinalLayout(pass), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    graph.RegisterRenderPassFinalLayout(pass, VK_IMAGE_LAYOUT_GENERAL);
    EXPECT_EQ(graph.GetRenderPassFinalLayout(pass), VK_IMAGE_LAYOUT_GENERAL);
    graph.Clear();
    EXPECT_THROW(graph.GetRenderPassFinalLayout(pass), std::runtime_error);
}

// Baked-Perf M6 Task 6.1 (audit E2): ComputeDispatchWaitsForSwapchainAcquire decides whether
// this dispatch's own submit consumes the WSI acquire semaphore, or leaves it for whichever
// later submit is the real first swapchain touch (BlitNode, on the split baked path).
TEST(ComputeDispatchAcquirePolicy, OffscreenOnlyPassDoesNotConsumeSwapchainAcquire) {
    EXPECT_FALSE(ComputeDispatchWaitsForSwapchainAcquire(/*writesNoImage=*/true))
        << "a dispatch that writes no presentable image (writesNoImage) must not wait the acquire "
           "-- it never touches the swapchain image, so the wait belongs to whichever later submit "
           "does (BlitNode)";
}

TEST(ComputeDispatchAcquirePolicy, SwapchainWritingPassConsumesSwapchainAcquire) {
    EXPECT_TRUE(ComputeDispatchWaitsForSwapchainAcquire(/*writesNoImage=*/false))
        << "a dispatch that DOES write the swapchain/render-target image (voxel-only or "
           "self-blitting variants) is the first real swapchain touch and must still consume "
           "the acquire wait itself, unchanged from before Task 6.1";
}


static VkSemaphore FakeSemaphore(uintptr_t id) { return reinterpret_cast<VkSemaphore>(id); }

TEST(TargetSynchronization, OffscreenNeverUsesWsiHandoffsEvenIfArraysAreProvided) {
    RenderTargetData target;
    const auto result = ResolveTargetSemaphoreHandoffs(&target, {FakeSemaphore(1)}, {FakeSemaphore(2)}, 0, 0, true, true);
    EXPECT_EQ(result.acquire, VK_NULL_HANDLE);
    EXPECT_EQ(result.present, VK_NULL_HANDLE);
    EXPECT_NO_THROW(ResolveTargetSemaphoreHandoffs(&target, {}, {}, 3, 4, true, true));
}

TEST(TargetSynchronization, WindowIndexesAcquireByFrameAndPresentByImage) {
    SwapChainPublicVariables target{};
    const auto result = ResolveTargetSemaphoreHandoffs(&target,
        {FakeSemaphore(1), FakeSemaphore(2)}, {FakeSemaphore(3), FakeSemaphore(4), FakeSemaphore(5)}, 1, 2, true, true);
    EXPECT_EQ(result.acquire, FakeSemaphore(2));
    EXPECT_EQ(result.present, FakeSemaphore(5));
}

TEST(TargetSynchronization, MissingWindowAcquireIsAnError) {
    SwapChainPublicVariables target{};
    EXPECT_THROW(ResolveTargetSemaphoreHandoffs(&target, {}, {FakeSemaphore(1)}, 0, 0, true, true), std::runtime_error);
    EXPECT_THROW(ResolveTargetSemaphoreHandoffs(&target, {VK_NULL_HANDLE}, {}, 0, 0, true, false), std::runtime_error);
    EXPECT_THROW(ResolveTargetSemaphoreHandoffs(&target, {FakeSemaphore(1)}, {}, 1, 0, true, false), std::runtime_error);
}

TEST(TargetSynchronization, MissingWindowPresentIsAnError) {
    SwapChainPublicVariables target{};
    EXPECT_THROW(ResolveTargetSemaphoreHandoffs(&target, {FakeSemaphore(1)}, {}, 0, 0, true, true), std::runtime_error);
    EXPECT_THROW(ResolveTargetSemaphoreHandoffs(&target, {}, {VK_NULL_HANDLE}, 0, 0, false, true), std::runtime_error);
    EXPECT_THROW(ResolveTargetSemaphoreHandoffs(&target, {}, {FakeSemaphore(1)}, 0, 1, false, true), std::runtime_error);
}

TEST(TargetSynchronization, ProducerDoesNotRequireUnusedWindowHandoffs) {
    SwapChainPublicVariables target{};
    EXPECT_NO_THROW(ResolveTargetSemaphoreHandoffs(&target, {}, {}, 0, 0, false, false));
    EXPECT_NO_THROW(ResolveTargetSemaphoreHandoffs(nullptr, {}, {}, 0, 0, false, false));
    EXPECT_THROW(ResolveTargetSemaphoreHandoffs(nullptr, {}, {}, 0, 0, true, false), std::runtime_error);
}

TEST(TargetSynchronization, NodeWiringAndRuntimeUseTheSameCapability) {
    RenderTargetNodeType offscreenType;
    SwapChainNodeType windowType;
    RenderTargetData offscreen;
    SwapChainPublicVariables window{};
    const auto offscreenContract = offscreenType.GetPresentationTargetContract();
    const auto windowContract = windowType.GetPresentationTargetContract();
    ASSERT_TRUE(offscreenContract);
    ASSERT_TRUE(windowContract);
    EXPECT_EQ(offscreenContract->synchronization, offscreen.GetSynchronization());
    EXPECT_EQ(windowContract->synchronization, window.GetSynchronization());
    EXPECT_FALSE(offscreenContract->renderComplete);
    EXPECT_TRUE(windowContract->renderComplete);
    EXPECT_EQ(offscreenContract->target.index, RenderTargetNodeConfig::RENDER_TARGET.index);
    EXPECT_EQ(windowContract->target.index, SwapChainNodeConfig::SWAPCHAIN_PUBLIC.index);
}
