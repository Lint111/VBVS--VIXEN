/**
 * @file test_gpu_query_manager.cpp
 * @brief Tests for Sprint 6.3 GPUQueryManager (Phase 0.1)
 *
 * Tests:
 * - Slot allocation and deallocation
 * - Multi-consumer coordination
 * - Per-frame query pool management
 * - Timestamp write tracking
 * - Result retrieval
 * - Edge cases and error handling
 *
 * NOTE: These tests use mock VulkanDevice with null handles.
 * Full integration tests with actual GPU queries are in integration test suite.
 */

#include <gtest/gtest.h>
#include "Core/GPUQueryManager.h"
#include "VulkanDevice.h"
#include <memory>
#include <string>

using namespace Vixen::RenderGraph;
using namespace Vixen::Vulkan::Resources;

// ============================================================================
// TEST FIXTURE
// ============================================================================

class GPUQueryManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create mock VulkanDevice for testing
        VkPhysicalDevice dummyGpu = VK_NULL_HANDLE;
        mockDevice = new VulkanDevice(&dummyGpu);
        mockDevice->device = VK_NULL_HANDLE;  // Null handle OK for structure tests
    }

    void TearDown() override {
        delete mockDevice;
        mockDevice = nullptr;
    }

    VulkanDevice* mockDevice = nullptr;

    static constexpr uint32_t kDefaultFramesInFlight = 3;
    static constexpr uint32_t kDefaultMaxConsumers = 8;
};

// ============================================================================
// CONSTRUCTION AND BASIC API TESTS
// ============================================================================

TEST_F(GPUQueryManagerTest, ConstructionSucceeds) {
    EXPECT_NO_THROW({
        GPUQueryManager manager(mockDevice, kDefaultFramesInFlight, kDefaultMaxConsumers);
    });
}

TEST_F(GPUQueryManagerTest, ConstructionWithNullDeviceThrows) {
    EXPECT_THROW({
        GPUQueryManager manager(nullptr, kDefaultFramesInFlight, kDefaultMaxConsumers);
    }, std::invalid_argument);
}

TEST_F(GPUQueryManagerTest, ConstructionWithZeroFramesThrows) {
    EXPECT_THROW({
        GPUQueryManager manager(mockDevice, 0, kDefaultMaxConsumers);
    }, std::invalid_argument);
}

TEST_F(GPUQueryManagerTest, ConstructionWithZeroMaxConsumersThrows) {
    EXPECT_THROW({
        GPUQueryManager manager(mockDevice, kDefaultFramesInFlight, 0);
    }, std::invalid_argument);
}

TEST_F(GPUQueryManagerTest, GetFrameCountReturnsCorrectValue) {
    GPUQueryManager manager(mockDevice, kDefaultFramesInFlight, kDefaultMaxConsumers);
    EXPECT_EQ(manager.GetFrameCount(), kDefaultFramesInFlight);
}

TEST_F(GPUQueryManagerTest, GetMaxSlotCountReturnsCorrectValue) {
    GPUQueryManager manager(mockDevice, kDefaultFramesInFlight, kDefaultMaxConsumers);
    EXPECT_EQ(manager.GetMaxSlotCount(), kDefaultMaxConsumers);
}

// ============================================================================
// SLOT ALLOCATION TESTS
// ============================================================================

TEST_F(GPUQueryManagerTest, AllocateQuerySlotReturnsValidHandle) {
    GPUQueryManager manager(mockDevice, kDefaultFramesInFlight, kDefaultMaxConsumers);

    auto slot = manager.AllocateQuerySlot("TestConsumer");
    EXPECT_NE(slot, GPUQueryManager::INVALID_SLOT);
    EXPECT_EQ(manager.GetAllocatedSlotCount(), 1u);
}

TEST_F(GPUQueryManagerTest, AllocateMultipleSlotsSucceeds) {
    GPUQueryManager manager(mockDevice, kDefaultFramesInFlight, kDefaultMaxConsumers);

    auto slot1 = manager.AllocateQuerySlot("Consumer1");
    auto slot2 = manager.AllocateQuerySlot("Consumer2");
    auto slot3 = manager.AllocateQuerySlot("Consumer3");

    EXPECT_NE(slot1, GPUQueryManager::INVALID_SLOT);
    EXPECT_NE(slot2, GPUQueryManager::INVALID_SLOT);
    EXPECT_NE(slot3, GPUQueryManager::INVALID_SLOT);

    // Slots should be unique
    EXPECT_NE(slot1, slot2);
    EXPECT_NE(slot2, slot3);
    EXPECT_NE(slot1, slot3);

    EXPECT_EQ(manager.GetAllocatedSlotCount(), 3u);
}

TEST_F(GPUQueryManagerTest, AllocateAllSlotsSucceeds) {
    GPUQueryManager manager(mockDevice, kDefaultFramesInFlight, kDefaultMaxConsumers);

    std::vector<GPUQueryManager::QuerySlotHandle> slots;
    for (uint32_t i = 0; i < kDefaultMaxConsumers; ++i) {
        auto slot = manager.AllocateQuerySlot("Consumer" + std::to_string(i));
        EXPECT_NE(slot, GPUQueryManager::INVALID_SLOT);
        slots.push_back(slot);
    }

    EXPECT_EQ(manager.GetAllocatedSlotCount(), kDefaultMaxConsumers);
}

TEST_F(GPUQueryManagerTest, AllocateBeyondMaxReturnsInvalidSlot) {
    GPUQueryManager manager(mockDevice, kDefaultFramesInFlight, kDefaultMaxConsumers);

    // Allocate all slots
    for (uint32_t i = 0; i < kDefaultMaxConsumers; ++i) {
        manager.AllocateQuerySlot("Consumer" + std::to_string(i));
    }

    // Try to allocate one more - should fail
    auto slot = manager.AllocateQuerySlot("OverflowConsumer");
    EXPECT_EQ(slot, GPUQueryManager::INVALID_SLOT);
    EXPECT_EQ(manager.GetAllocatedSlotCount(), kDefaultMaxConsumers);
}

TEST_F(GPUQueryManagerTest, GetSlotConsumerNameReturnsCorrectName) {
    GPUQueryManager manager(mockDevice, kDefaultFramesInFlight, kDefaultMaxConsumers);

    auto slot = manager.AllocateQuerySlot("ProfilerSystem");
    EXPECT_EQ(manager.GetSlotConsumerName(slot), "ProfilerSystem");
}

TEST_F(GPUQueryManagerTest, GetSlotConsumerNameForInvalidSlotReturnsEmpty) {
    GPUQueryManager manager(mockDevice, kDefaultFramesInFlight, kDefaultMaxConsumers);

    EXPECT_EQ(manager.GetSlotConsumerName(GPUQueryManager::INVALID_SLOT), "");
    EXPECT_EQ(manager.GetSlotConsumerName(999), "");  // Out of range
}

// ============================================================================
// SLOT DEALLOCATION TESTS
// ============================================================================

TEST_F(GPUQueryManagerTest, FreeQuerySlotSucceeds) {
    GPUQueryManager manager(mockDevice, kDefaultFramesInFlight, kDefaultMaxConsumers);

    auto slot = manager.AllocateQuerySlot("TestConsumer");
    EXPECT_EQ(manager.GetAllocatedSlotCount(), 1u);

    manager.FreeQuerySlot(slot);
    EXPECT_EQ(manager.GetAllocatedSlotCount(), 0u);
}

TEST_F(GPUQueryManagerTest, FreeQuerySlotClearsConsumerName) {
    GPUQueryManager manager(mockDevice, kDefaultFramesInFlight, kDefaultMaxConsumers);

    auto slot = manager.AllocateQuerySlot("TestConsumer");
    EXPECT_EQ(manager.GetSlotConsumerName(slot), "TestConsumer");

    manager.FreeQuerySlot(slot);
    EXPECT_EQ(manager.GetSlotConsumerName(slot), "");
}

TEST_F(GPUQueryManagerTest, FreeQuerySlotAllowsReallocation) {
    GPUQueryManager manager(mockDevice, kDefaultFramesInFlight, kDefaultMaxConsumers);

    auto slot1 = manager.AllocateQuerySlot("Consumer1");
    manager.FreeQuerySlot(slot1);

    auto slot2 = manager.AllocateQuerySlot("Consumer2");
    EXPECT_NE(slot2, GPUQueryManager::INVALID_SLOT);
    EXPECT_EQ(manager.GetAllocatedSlotCount(), 1u);
}

TEST_F(GPUQueryManagerTest, FreeInvalidSlotDoesNothing) {
    GPUQueryManager manager(mockDevice, kDefaultFramesInFlight, kDefaultMaxConsumers);

    auto slot = manager.AllocateQuerySlot("TestConsumer");
    EXPECT_EQ(manager.GetAllocatedSlotCount(), 1u);

    manager.FreeQuerySlot(GPUQueryManager::INVALID_SLOT);  // Should not crash
    EXPECT_EQ(manager.GetAllocatedSlotCount(), 1u);  // Count unchanged

    manager.FreeQuerySlot(999);  // Out of range - should not crash
    EXPECT_EQ(manager.GetAllocatedSlotCount(), 1u);  // Count unchanged
}

// ============================================================================
// COMMAND BUFFER RECORDING TESTS (NULL DEVICE)
// ============================================================================

TEST_F(GPUQueryManagerTest, BeginFrameWithInvalidFrameIndexThrows) {
    GPUQueryManager manager(mockDevice, kDefaultFramesInFlight, kDefaultMaxConsumers);
    VkCommandBuffer dummyCmd = VK_NULL_HANDLE;

    EXPECT_THROW({
        manager.BeginFrame(dummyCmd, kDefaultFramesInFlight);  // Index out of range
    }, std::out_of_range);
}

TEST_F(GPUQueryManagerTest, WriteTimestampWithInvalidFrameIndexThrows) {
    GPUQueryManager manager(mockDevice, kDefaultFramesInFlight, kDefaultMaxConsumers);
    auto slot = manager.AllocateQuerySlot("TestConsumer");
    VkCommandBuffer dummyCmd = VK_NULL_HANDLE;

    EXPECT_THROW({
        manager.WriteTimestamp(dummyCmd, kDefaultFramesInFlight, slot, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT);
    }, std::out_of_range);
}

TEST_F(GPUQueryManagerTest, WriteTimestampWithInvalidSlotThrows) {
    GPUQueryManager manager(mockDevice, kDefaultFramesInFlight, kDefaultMaxConsumers);
    VkCommandBuffer dummyCmd = VK_NULL_HANDLE;

    EXPECT_THROW({
        manager.WriteTimestamp(dummyCmd, 0, GPUQueryManager::INVALID_SLOT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT);
    }, std::invalid_argument);
}

TEST_F(GPUQueryManagerTest, WriteTimestampWithUnallocatedSlotThrows) {
    GPUQueryManager manager(mockDevice, kDefaultFramesInFlight, kDefaultMaxConsumers);
    VkCommandBuffer dummyCmd = VK_NULL_HANDLE;

    EXPECT_THROW({
        manager.WriteTimestamp(dummyCmd, 0, 5, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT);  // Slot 5 not allocated
    }, std::invalid_argument);
}

// ============================================================================
// RESULT RETRIEVAL TESTS (NULL DEVICE)
// ============================================================================

TEST_F(GPUQueryManagerTest, ReadAllResultsWithInvalidFrameIndexThrows) {
    GPUQueryManager manager(mockDevice, kDefaultFramesInFlight, kDefaultMaxConsumers);

    EXPECT_THROW({
        manager.ReadAllResults(kDefaultFramesInFlight);
    }, std::out_of_range);
}

TEST_F(GPUQueryManagerTest, TryReadTimestampsWithInvalidFrameIndexReturnsFalse) {
    GPUQueryManager manager(mockDevice, kDefaultFramesInFlight, kDefaultMaxConsumers);
    auto slot = manager.AllocateQuerySlot("TestConsumer");

    EXPECT_FALSE(manager.TryReadTimestamps(kDefaultFramesInFlight, slot));
}

TEST_F(GPUQueryManagerTest, TryReadTimestampsWithInvalidSlotReturnsFalse) {
    GPUQueryManager manager(mockDevice, kDefaultFramesInFlight, kDefaultMaxConsumers);

    EXPECT_FALSE(manager.TryReadTimestamps(0, GPUQueryManager::INVALID_SLOT));
    EXPECT_FALSE(manager.TryReadTimestamps(0, 999));  // Out of range
}

TEST_F(GPUQueryManagerTest, TryReadTimestampsWithUnallocatedSlotReturnsFalse) {
    GPUQueryManager manager(mockDevice, kDefaultFramesInFlight, kDefaultMaxConsumers);

    EXPECT_FALSE(manager.TryReadTimestamps(0, 5));  // Slot 5 not allocated
}

TEST_F(GPUQueryManagerTest, GetElapsedNsWithInvalidFrameIndexReturnsZero) {
    GPUQueryManager manager(mockDevice, kDefaultFramesInFlight, kDefaultMaxConsumers);
    auto slot = manager.AllocateQuerySlot("TestConsumer");

    EXPECT_EQ(manager.GetElapsedNs(kDefaultFramesInFlight, slot), 0u);
}

TEST_F(GPUQueryManagerTest, GetElapsedNsWithInvalidSlotReturnsZero) {
    GPUQueryManager manager(mockDevice, kDefaultFramesInFlight, kDefaultMaxConsumers);

    EXPECT_EQ(manager.GetElapsedNs(0, GPUQueryManager::INVALID_SLOT), 0u);
    EXPECT_EQ(manager.GetElapsedNs(0, 999), 0u);  // Out of range
}

TEST_F(GPUQueryManagerTest, GetElapsedMsWithInvalidFrameIndexReturnsZero) {
    GPUQueryManager manager(mockDevice, kDefaultFramesInFlight, kDefaultMaxConsumers);
    auto slot = manager.AllocateQuerySlot("TestConsumer");

    EXPECT_EQ(manager.GetElapsedMs(kDefaultFramesInFlight, slot), 0.0f);
}

TEST_F(GPUQueryManagerTest, GetElapsedMsWithInvalidSlotReturnsZero) {
    GPUQueryManager manager(mockDevice, kDefaultFramesInFlight, kDefaultMaxConsumers);

    EXPECT_EQ(manager.GetElapsedMs(0, GPUQueryManager::INVALID_SLOT), 0.0f);
    EXPECT_EQ(manager.GetElapsedMs(0, 999), 0.0f);  // Out of range
}

// ============================================================================
// RESOURCE RELEASE TESTS
// ============================================================================

TEST_F(GPUQueryManagerTest, ReleaseGPUResourcesSucceeds) {
    GPUQueryManager manager(mockDevice, kDefaultFramesInFlight, kDefaultMaxConsumers);

    EXPECT_NO_THROW({
        manager.ReleaseGPUResources();
    });
}

TEST_F(GPUQueryManagerTest, ReleaseGPUResourcesCanBeCalledMultipleTimes) {
    GPUQueryManager manager(mockDevice, kDefaultFramesInFlight, kDefaultMaxConsumers);

    manager.ReleaseGPUResources();
    EXPECT_NO_THROW({
        manager.ReleaseGPUResources();  // Second call should not crash
    });
}

TEST_F(GPUQueryManagerTest, IsTimestampSupportedReturnsFalseAfterRelease) {
    GPUQueryManager manager(mockDevice, kDefaultFramesInFlight, kDefaultMaxConsumers);

    manager.ReleaseGPUResources();
    EXPECT_FALSE(manager.IsTimestampSupported());
}

// ============================================================================
// MOVE SEMANTICS TESTS
// ============================================================================

TEST_F(GPUQueryManagerTest, MoveConstructionSucceeds) {
    GPUQueryManager manager1(mockDevice, kDefaultFramesInFlight, kDefaultMaxConsumers);
    auto slot = manager1.AllocateQuerySlot("TestConsumer");
    EXPECT_EQ(manager1.GetAllocatedSlotCount(), 1u);

    GPUQueryManager manager2(std::move(manager1));
    EXPECT_EQ(manager2.GetAllocatedSlotCount(), 1u);
    EXPECT_EQ(manager2.GetSlotConsumerName(slot), "TestConsumer");
}

TEST_F(GPUQueryManagerTest, MoveAssignmentSucceeds) {
    GPUQueryManager manager1(mockDevice, kDefaultFramesInFlight, kDefaultMaxConsumers);
    auto slot = manager1.AllocateQuerySlot("TestConsumer");
    EXPECT_EQ(manager1.GetAllocatedSlotCount(), 1u);

    GPUQueryManager manager2(mockDevice, 2, 4);  // Different config
    manager2 = std::move(manager1);

    EXPECT_EQ(manager2.GetAllocatedSlotCount(), 1u);
    EXPECT_EQ(manager2.GetSlotConsumerName(slot), "TestConsumer");
    EXPECT_EQ(manager2.GetFrameCount(), kDefaultFramesInFlight);  // Moved config
}

// ============================================================================
// MULTI-CONSUMER COORDINATION TESTS
// ============================================================================

TEST_F(GPUQueryManagerTest, MultipleConsumersCanAllocateSeparateSlots) {
    GPUQueryManager manager(mockDevice, kDefaultFramesInFlight, kDefaultMaxConsumers);

    auto profilerSlot = manager.AllocateQuerySlot("ProfilerSystem");
    auto trackerSlot = manager.AllocateQuerySlot("CapacityTracker");
    auto loggerSlot = manager.AllocateQuerySlot("PerformanceLogger");

    EXPECT_NE(profilerSlot, GPUQueryManager::INVALID_SLOT);
    EXPECT_NE(trackerSlot, GPUQueryManager::INVALID_SLOT);
    EXPECT_NE(loggerSlot, GPUQueryManager::INVALID_SLOT);

    // All slots should be unique
    EXPECT_NE(profilerSlot, trackerSlot);
    EXPECT_NE(trackerSlot, loggerSlot);
    EXPECT_NE(profilerSlot, loggerSlot);

    EXPECT_EQ(manager.GetAllocatedSlotCount(), 3u);
}

TEST_F(GPUQueryManagerTest, ConsumerNamesAreMaintainedIndependently) {
    GPUQueryManager manager(mockDevice, kDefaultFramesInFlight, kDefaultMaxConsumers);

    auto slot1 = manager.AllocateQuerySlot("Consumer1");
    auto slot2 = manager.AllocateQuerySlot("Consumer2");
    auto slot3 = manager.AllocateQuerySlot("Consumer3");

    EXPECT_EQ(manager.GetSlotConsumerName(slot1), "Consumer1");
    EXPECT_EQ(manager.GetSlotConsumerName(slot2), "Consumer2");
    EXPECT_EQ(manager.GetSlotConsumerName(slot3), "Consumer3");

    // Free middle slot
    manager.FreeQuerySlot(slot2);

    // Other slots should remain unchanged
    EXPECT_EQ(manager.GetSlotConsumerName(slot1), "Consumer1");
    EXPECT_EQ(manager.GetSlotConsumerName(slot2), "");  // Freed
    EXPECT_EQ(manager.GetSlotConsumerName(slot3), "Consumer3");
}
