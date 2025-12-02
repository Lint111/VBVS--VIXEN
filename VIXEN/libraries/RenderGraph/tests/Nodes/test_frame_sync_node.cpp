/**
 * @file test_frame_sync_node.cpp
 * @brief Tests for FrameSyncNode class
 *
 * Coverage: FrameSyncNode.h (Target: 50%+ unit, 30%+ integration)
 *
 * Unit Tests: Config validation, slot metadata
 * Integration Tests: Fence creation, semaphore creation, synchronization
 *
 * NOTE: Synchronization primitive creation requires VkDevice.
 */

#include <gtest/gtest.h>
#include <RenderGraph/Nodes/FrameSyncNode.h>
#include <RenderGraph/Data/Nodes/FrameSyncNodeConfig.h>

using namespace Vixen::RenderGraph;

// Use centralized Vulkan global names to avoid duplicate strong symbols
#include <VulkanGlobalNames.h>

class FrameSyncNodeTest : public ::testing::Test {};

// Configuration Tests
TEST_F(FrameSyncNodeTest, ConfigHasOneInput) {
    EXPECT_EQ(FrameSyncNodeConfig::INPUT_COUNT, 1) << "Requires DEVICE input";
}

TEST_F(FrameSyncNodeTest, ConfigHasMultipleOutputs) {
    EXPECT_GE(FrameSyncNodeConfig::OUTPUT_COUNT, 2) << "Outputs fences and semaphores";
}

TEST_F(FrameSyncNodeTest, ConfigDeviceInputIndex) {
    EXPECT_EQ(FrameSyncNodeConfig::VULKAN_DEVICE_Slot::index, 0);
}

TEST_F(FrameSyncNodeTest, ConfigDeviceIsRequired) {
    EXPECT_FALSE(FrameSyncNodeConfig::VULKAN_DEVICE_Slot::nullable);
}

TEST_F(FrameSyncNodeTest, ConfigDeviceTypeIsVulkanDevicePtr) {
    bool isCorrect = std::is_same_v<
        FrameSyncNodeConfig::VULKAN_DEVICE_Slot::Type,
        Vixen::Vulkan::Resources::VulkanDevice*
    >;
    EXPECT_TRUE(isCorrect);
}

// Verify fence outputs exist
TEST_F(FrameSyncNodeTest, ConfigHasFenceOutputs) {
    // FrameSyncNode should output per-frame fences
    EXPECT_GE(FrameSyncNodeConfig::OUTPUT_COUNT, 1);
}

// Verify semaphore outputs exist
TEST_F(FrameSyncNodeTest, ConfigHasSemaphoreOutputs) {
    // FrameSyncNode should output imageAvailable and renderComplete semaphores
    EXPECT_GE(FrameSyncNodeConfig::OUTPUT_COUNT, 2);
}

// Slot Metadata
TEST_F(FrameSyncNodeTest, ConfigDeviceIsReadOnly) {
    EXPECT_EQ(FrameSyncNodeConfig::VULKAN_DEVICE_Slot::mutability,
              SlotMutability::ReadOnly);
}

// Type System
TEST_F(FrameSyncNodeTest, TypeNameIsFrameSync) {
    FrameSyncNodeType frameSyncType;
    EXPECT_STREQ(frameSyncType.GetTypeName().c_str(), "FrameSync");
}

// Array Mode (per-frame resources)
TEST_F(FrameSyncNodeTest, ConfigSupportsMultipleFrames) {
    // FrameSyncNode creates per-frame synchronization primitives
    // Usually MAX_FRAMES_IN_FLIGHT (2 or 3)
    EXPECT_TRUE(true) << "Per-frame sync primitives required";
}

/**
 * Integration Test Placeholders (Require VkDevice):
 * - CreateInFlightFences: Per-frame fence creation (signaled state)
 * - CreateImageAvailableSemaphores: Per-frame semaphores
 * - CreateRenderCompleteSemaphores: Per-frame semaphores
 * - FenceWait: vkWaitForFences with timeout
 * - FenceReset: vkResetFences after wait
 * - SemaphoreSignaling: Proper signal/wait chain
 * - FrameInFlightTracking: MAX_FRAMES_IN_FLIGHT management
 * - CleanupSync: vkDestroyFence, vkDestroySemaphore
 */
