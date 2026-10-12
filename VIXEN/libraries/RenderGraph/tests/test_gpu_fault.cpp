// R516 part 2 (gpuharden): typed GPU faults, fault latching and the recovery-storm guard.
// CPU only: no Vulkan device is created. The device-loss recovery path itself (instance, surface and
// swapchain recreation on a real Dozen removal) is exercised by tools/gpuharden/device_loss_probe.

#include <gtest/gtest.h>

#include "Core/GpuFault.h"
#include "Core/NodeType.h"
#include "Core/RenderGraph.h"

using namespace Vixen::RenderGraph;

TEST(GpuFaultClassify, MapsVulkanResultsToFaultKinds) {
    EXPECT_EQ(ClassifyGpuResult(VK_SUCCESS), GpuFaultKind::None);
    EXPECT_EQ(ClassifyGpuResult(VK_NOT_READY), GpuFaultKind::None);
    EXPECT_EQ(ClassifyGpuResult(VK_SUBOPTIMAL_KHR), GpuFaultKind::None);
    EXPECT_EQ(ClassifyGpuResult(VK_ERROR_DEVICE_LOST), GpuFaultKind::DeviceLost);
    EXPECT_EQ(ClassifyGpuResult(VK_ERROR_OUT_OF_DEVICE_MEMORY), GpuFaultKind::OutOfMemory);
    EXPECT_EQ(ClassifyGpuResult(VK_ERROR_OUT_OF_HOST_MEMORY), GpuFaultKind::OutOfMemory);
    EXPECT_EQ(ClassifyGpuResult(VK_ERROR_SURFACE_LOST_KHR), GpuFaultKind::Other);
    EXPECT_EQ(ClassifyGpuResult(VK_ERROR_UNKNOWN), GpuFaultKind::Other);
}

TEST(GpuFaultClassify, TimeoutIsAWatchdogFaultOnlyFromABoundedWait) {
    EXPECT_EQ(ClassifyGpuResult(VK_TIMEOUT, false), GpuFaultKind::None);
    EXPECT_EQ(ClassifyGpuResult(VK_TIMEOUT, true), GpuFaultKind::WatchdogTimeout);
    EXPECT_EQ(MakeWaitResult(VK_TIMEOUT).fault, GpuFaultKind::WatchdogTimeout);
    EXPECT_TRUE(MakeWaitResult(VK_SUCCESS).Ok());
    EXPECT_FALSE(MakeWaitResult(VK_ERROR_DEVICE_LOST).Ok());
}

TEST(GpuFaultClassify, WaitBoundIsFiniteAndAboveTdr) {
    EXPECT_GT(GpuWaitBoundNs(), 2'000'000'000ull);
    EXPECT_LT(GpuWaitBoundNs(), 3'600'000ull * 1'000'000ull);
}

class GpuFaultGraphTest : public ::testing::Test {
protected:
    NodeTypeRegistry registry;
    RenderGraph graph{&registry, nullptr, nullptr, nullptr};
};

TEST_F(GpuFaultGraphTest, DeviceLostLatchesLossAndAbortsTheFrame) {
    EXPECT_EQ(graph.ReportGpuFault("test", VK_ERROR_DEVICE_LOST), GpuFaultKind::DeviceLost);
    EXPECT_TRUE(graph.IsDeviceLost());
    EXPECT_TRUE(graph.IsFrameAborted());
    EXPECT_EQ(graph.GpuFaultCount(GpuFaultKind::DeviceLost), 1u);
}

TEST_F(GpuFaultGraphTest, BoundedWaitTimeoutIsTreatedAsDeviceLoss) {
    EXPECT_EQ(graph.ReportGpuFault("wait", VK_TIMEOUT, true), GpuFaultKind::WatchdogTimeout);
    EXPECT_TRUE(graph.IsDeviceLost());
    EXPECT_EQ(graph.GpuFaultCount(GpuFaultKind::WatchdogTimeout), 1u);
}

TEST_F(GpuFaultGraphTest, UnboundedTimeoutIsNotAFault) {
    EXPECT_EQ(graph.ReportGpuFault("poll", VK_TIMEOUT, false), GpuFaultKind::None);
    EXPECT_FALSE(graph.IsDeviceLost());
    EXPECT_FALSE(graph.IsFrameAborted());
}

TEST_F(GpuFaultGraphTest, OutOfMemoryFailsTheFrameWithoutLosingTheDevice) {
    EXPECT_EQ(graph.ReportGpuFault("submit", VK_ERROR_OUT_OF_DEVICE_MEMORY), GpuFaultKind::OutOfMemory);
    EXPECT_FALSE(graph.IsDeviceLost());
    EXPECT_TRUE(graph.IsFrameAborted());
    EXPECT_EQ(graph.FrameFault(), VK_ERROR_OUT_OF_DEVICE_MEMORY);
}

TEST_F(GpuFaultGraphTest, FirstFrameFaultWins) {
    (void)graph.ReportGpuFault("a", VK_ERROR_OUT_OF_HOST_MEMORY);
    (void)graph.ReportGpuFault("b", VK_ERROR_SURFACE_LOST_KHR);
    EXPECT_EQ(graph.FrameFault(), VK_ERROR_OUT_OF_HOST_MEMORY);
    EXPECT_EQ(graph.GpuFaultCount(GpuFaultKind::Other), 1u);
}

TEST_F(GpuFaultGraphTest, RecoveryOfAnEmptyGraphSucceedsAndClearsTheLatch) {
    (void)graph.ReportGpuFault("test", VK_ERROR_DEVICE_LOST);
    EXPECT_TRUE(graph.RecoverFromDeviceLoss());
    EXPECT_FALSE(graph.IsDeviceLost());
}

TEST_F(GpuFaultGraphTest, RecoveryStormFailsCleanlyInsteadOfLooping) {
    int recovered = 0;
    bool gaveUp = false;
    for (int i = 0; i < 10 && !gaveUp; ++i) {
        (void)graph.ReportGpuFault("storm", VK_ERROR_DEVICE_LOST);
        if (graph.RecoverFromDeviceLoss()) ++recovered; else gaveUp = true;
    }
    EXPECT_TRUE(gaveUp);
    EXPECT_EQ(recovered, 3);
    EXPECT_TRUE(graph.IsDeviceLost());
    EXPECT_FALSE(graph.RecoverFromDeviceLoss());  // stays failed: no spinning
}

TEST(GpuFaultPolicy, GenuineLossOnTranslatedDeviceDoesNotRecoverInProcess) {
    EXPECT_FALSE(InProcessRecoveryIsSafe(true, true));
    EXPECT_TRUE(InProcessRecoveryIsSafe(true, false));
    EXPECT_TRUE(InProcessRecoveryIsSafe(false, true));   // injected loss never touches the driver
    EXPECT_TRUE(InProcessRecoveryIsSafe(false, false));
}

TEST_F(GpuFaultGraphTest, InjectedLossStaysRecoverableButGenuineLossIsTracked) {
    graph.NotifyDeviceLost("injected");
    EXPECT_TRUE(graph.RecoverFromDeviceLoss());
    (void)graph.ReportGpuFault("real", VK_ERROR_DEVICE_LOST);
    EXPECT_TRUE(graph.RecoverFromDeviceLoss());  // empty graph has no translated device
}
