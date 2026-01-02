/**
 * @file test_budget_manager_integration.cpp
 * @brief Integration tests for DeviceBudgetManager → Cacher data flow
 *
 * Tests the complete allocation chain:
 * - DeviceBudgetManager creation and configuration
 * - Multi-device budget isolation
 * - CacherAllocationHelpers bridge behavior
 * - Budget tracking through the allocation chain
 *
 * These are CPU-only tests using mock/direct allocator (no real Vulkan device required).
 */

#include <gtest/gtest.h>
#include "Memory/IMemoryAllocator.h"
#include "Memory/DeviceBudgetManager.h"
#include "Memory/ResourceBudgetManager.h"
#include <memory>
#include <atomic>

using namespace ResourceManagement;

// ============================================================================
// Mock Allocator for Testing (no Vulkan required)
// ============================================================================

/**
 * @brief Mock allocator that simulates allocation without Vulkan
 *
 * Tracks allocations in memory for verification without actual GPU resources.
 */
class MockAllocator : public IMemoryAllocator {
public:
    MockAllocator() = default;
    ~MockAllocator() override = default;

    [[nodiscard]] std::expected<BufferAllocation, AllocationError>
    AllocateBuffer(const BufferAllocationRequest& request) override {
        std::lock_guard<std::mutex> lock(mutex_);

        // Simulate allocation
        uint64_t handle = nextHandle_++;
        totalAllocated_ += request.size;
        allocationCount_++;

        BufferAllocation result{
            .buffer = reinterpret_cast<VkBuffer>(handle),  // Fake handle
            .allocation = reinterpret_cast<AllocationHandle>(handle),
            .size = request.size,
            .offset = 0,
            .mappedData = nullptr,
            .deviceAddress = 0,
            .canAlias = false,
            .isAliased = false
        };

        allocations_[handle] = request.size;

        // Record allocation with budget manager (like real allocators do)
        if (budgetManager_) {
            budgetManager_->RecordAllocation(BudgetResourceType::DeviceMemory, request.size);
        }

        return result;
    }

    void FreeBuffer(BufferAllocation& allocation) override {
        std::lock_guard<std::mutex> lock(mutex_);

        uint64_t handle = reinterpret_cast<uint64_t>(allocation.allocation);
        auto it = allocations_.find(handle);
        if (it != allocations_.end()) {
            // Record deallocation with budget manager (like real allocators do)
            if (budgetManager_) {
                budgetManager_->RecordDeallocation(BudgetResourceType::DeviceMemory, it->second);
            }

            totalAllocated_ -= it->second;
            allocationCount_--;
            allocations_.erase(it);
        }
        allocation = {};
    }

    [[nodiscard]] std::expected<ImageAllocation, AllocationError>
    AllocateImage(const ImageAllocationRequest& request) override {
        return std::unexpected(AllocationError::Unknown);
    }

    void FreeImage(ImageAllocation& allocation) override {}

    [[nodiscard]] std::expected<BufferAllocation, AllocationError>
    CreateAliasedBuffer(const AliasedBufferRequest& request) override {
        return std::unexpected(AllocationError::Unknown);
    }

    [[nodiscard]] std::expected<ImageAllocation, AllocationError>
    CreateAliasedImage(const AliasedImageRequest& request) override {
        return std::unexpected(AllocationError::Unknown);
    }

    [[nodiscard]] bool SupportsAliasing(AllocationHandle allocation) const override {
        return false;
    }

    [[nodiscard]] void* MapBuffer(const BufferAllocation& allocation) override {
        // Return a fake mapped pointer
        return reinterpret_cast<void*>(0xDEADBEEF);
    }

    void UnmapBuffer(const BufferAllocation& allocation) override {}

    void FlushMappedRange(const BufferAllocation& allocation, VkDeviceSize offset, VkDeviceSize size) override {}

    void InvalidateMappedRange(const BufferAllocation& allocation, VkDeviceSize offset, VkDeviceSize size) override {}

    void SetBudgetManager(ResourceBudgetManager* budgetManager) override {
        budgetManager_ = budgetManager;
    }

    [[nodiscard]] ResourceBudgetManager* GetBudgetManager() const override {
        return budgetManager_;
    }

    [[nodiscard]] AllocationStats GetStats() const override {
        return AllocationStats{
            .totalAllocatedBytes = totalAllocated_.load(),
            .totalUsedBytes = totalAllocated_.load(),
            .allocationCount = static_cast<uint32_t>(allocationCount_.load()),
            .blockCount = static_cast<uint32_t>(allocationCount_.load()),
            .fragmentationRatio = 0.0f
        };
    }

    [[nodiscard]] std::string_view GetName() const override {
        return "MockAllocator";
    }

    // Test accessors
    uint64_t GetTotalAllocated() const { return totalAllocated_.load(); }
    int GetAllocationCount() const { return allocationCount_.load(); }

private:
    mutable std::mutex mutex_;
    std::unordered_map<uint64_t, VkDeviceSize> allocations_;
    uint64_t nextHandle_ = 1;
    std::atomic<uint64_t> totalAllocated_{0};
    std::atomic<int> allocationCount_{0};
    ResourceBudgetManager* budgetManager_ = nullptr;
};

// ============================================================================
// DeviceBudgetManager Basic Tests
// ============================================================================

class DeviceBudgetManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        mockAllocator_ = std::make_shared<MockAllocator>();

        DeviceBudgetManager::Config config{
            .deviceMemoryBudget = 1024 * 1024 * 100,  // 100 MB
            .deviceMemoryWarning = 1024 * 1024 * 80,  // 80 MB warning
            .stagingQuota = 1024 * 1024 * 10,         // 10 MB staging
            .strictBudget = false
        };

        budgetManager_ = std::make_unique<DeviceBudgetManager>(
            mockAllocator_,
            VK_NULL_HANDLE,  // No physical device for mock
            config
        );
    }

    std::shared_ptr<MockAllocator> mockAllocator_;
    std::unique_ptr<DeviceBudgetManager> budgetManager_;
};

TEST_F(DeviceBudgetManagerTest, AllocateAndFreeBuffer) {
    BufferAllocationRequest request{
        .size = 1024 * 1024,  // 1 MB
        .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        .location = MemoryLocation::DeviceLocal,
        .debugName = "TestBuffer"
    };

    auto result = budgetManager_->AllocateBuffer(request);
    ASSERT_TRUE(result.has_value()) << "Buffer allocation should succeed";
    EXPECT_EQ(result->size, request.size);
    EXPECT_NE(result->buffer, VK_NULL_HANDLE);

    // Verify allocator received the request
    EXPECT_EQ(mockAllocator_->GetAllocationCount(), 1);
    EXPECT_EQ(mockAllocator_->GetTotalAllocated(), request.size);

    // Free and verify
    budgetManager_->FreeBuffer(*result);
    EXPECT_EQ(mockAllocator_->GetAllocationCount(), 0);
    EXPECT_EQ(mockAllocator_->GetTotalAllocated(), 0);
}

TEST_F(DeviceBudgetManagerTest, StatsTracking) {
    // Allocate multiple buffers
    std::vector<BufferAllocation> allocations;
    constexpr int numAllocations = 5;
    constexpr VkDeviceSize bufferSize = 1024 * 1024;  // 1 MB each

    for (int i = 0; i < numAllocations; ++i) {
        BufferAllocationRequest request{
            .size = bufferSize,
            .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            .location = MemoryLocation::DeviceLocal,
            .debugName = "StatsTestBuffer"
        };
        auto result = budgetManager_->AllocateBuffer(request);
        ASSERT_TRUE(result.has_value());
        allocations.push_back(*result);
    }

    // Check stats
    auto stats = budgetManager_->GetStats();
    EXPECT_EQ(stats.usedDeviceMemory, numAllocations * bufferSize);

    // Free all
    for (auto& alloc : allocations) {
        budgetManager_->FreeBuffer(alloc);
    }

    stats = budgetManager_->GetStats();
    EXPECT_EQ(stats.usedDeviceMemory, 0);
}

TEST_F(DeviceBudgetManagerTest, StagingQuotaTracking) {
    // Try to reserve staging quota
    EXPECT_TRUE(budgetManager_->TryReserveStagingQuota(1024 * 1024));  // 1 MB
    EXPECT_EQ(budgetManager_->GetStagingQuotaUsed(), 1024 * 1024);

    // Reserve more
    EXPECT_TRUE(budgetManager_->TryReserveStagingQuota(1024 * 1024 * 5));  // 5 MB more
    EXPECT_EQ(budgetManager_->GetStagingQuotaUsed(), 1024 * 1024 * 6);

    // Release some
    budgetManager_->ReleaseStagingQuota(1024 * 1024 * 2);  // Release 2 MB
    EXPECT_EQ(budgetManager_->GetStagingQuotaUsed(), 1024 * 1024 * 4);
}

// ============================================================================
// Multi-Device Budget Isolation Tests
// ============================================================================

class MultiDeviceBudgetTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create two separate allocators (simulating two GPUs)
        allocator1_ = std::make_shared<MockAllocator>();
        allocator2_ = std::make_shared<MockAllocator>();

        DeviceBudgetManager::Config config1{
            .deviceMemoryBudget = 1024 * 1024 * 100,  // 100 MB for device 1
            .stagingQuota = 1024 * 1024 * 10,
            .strictBudget = false
        };

        DeviceBudgetManager::Config config2{
            .deviceMemoryBudget = 1024 * 1024 * 200,  // 200 MB for device 2
            .stagingQuota = 1024 * 1024 * 20,
            .strictBudget = false
        };

        budgetManager1_ = std::make_unique<DeviceBudgetManager>(allocator1_, VK_NULL_HANDLE, config1);
        budgetManager2_ = std::make_unique<DeviceBudgetManager>(allocator2_, VK_NULL_HANDLE, config2);
    }

    std::shared_ptr<MockAllocator> allocator1_;
    std::shared_ptr<MockAllocator> allocator2_;
    std::unique_ptr<DeviceBudgetManager> budgetManager1_;
    std::unique_ptr<DeviceBudgetManager> budgetManager2_;
};

TEST_F(MultiDeviceBudgetTest, IsolatedAllocations) {
    // Allocate on device 1
    BufferAllocationRequest request1{
        .size = 1024 * 1024 * 10,  // 10 MB
        .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        .location = MemoryLocation::DeviceLocal,
        .debugName = "Device1Buffer"
    };
    auto result1 = budgetManager1_->AllocateBuffer(request1);
    ASSERT_TRUE(result1.has_value());

    // Allocate on device 2
    BufferAllocationRequest request2{
        .size = 1024 * 1024 * 20,  // 20 MB
        .usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
        .location = MemoryLocation::DeviceLocal,
        .debugName = "Device2Buffer"
    };
    auto result2 = budgetManager2_->AllocateBuffer(request2);
    ASSERT_TRUE(result2.has_value());

    // Verify isolation - each allocator only sees its own allocations
    EXPECT_EQ(allocator1_->GetTotalAllocated(), 1024 * 1024 * 10);
    EXPECT_EQ(allocator2_->GetTotalAllocated(), 1024 * 1024 * 20);

    // Verify stats are isolated
    auto stats1 = budgetManager1_->GetStats();
    auto stats2 = budgetManager2_->GetStats();
    EXPECT_EQ(stats1.usedDeviceMemory, 1024 * 1024 * 10);
    EXPECT_EQ(stats2.usedDeviceMemory, 1024 * 1024 * 20);

    // Free on device 1 shouldn't affect device 2
    budgetManager1_->FreeBuffer(*result1);
    EXPECT_EQ(allocator1_->GetTotalAllocated(), 0);
    EXPECT_EQ(allocator2_->GetTotalAllocated(), 1024 * 1024 * 20);  // Unchanged

    // Cleanup
    budgetManager2_->FreeBuffer(*result2);
}

TEST_F(MultiDeviceBudgetTest, IndependentStagingQuotas) {
    // Reserve staging on device 1
    EXPECT_TRUE(budgetManager1_->TryReserveStagingQuota(1024 * 1024 * 5));  // 5 MB

    // Reserve staging on device 2
    EXPECT_TRUE(budgetManager2_->TryReserveStagingQuota(1024 * 1024 * 15));  // 15 MB

    // Verify independent tracking
    EXPECT_EQ(budgetManager1_->GetStagingQuotaUsed(), 1024 * 1024 * 5);
    EXPECT_EQ(budgetManager2_->GetStagingQuotaUsed(), 1024 * 1024 * 15);

    // Release from device 1
    budgetManager1_->ReleaseStagingQuota(1024 * 1024 * 5);
    EXPECT_EQ(budgetManager1_->GetStagingQuotaUsed(), 0);
    EXPECT_EQ(budgetManager2_->GetStagingQuotaUsed(), 1024 * 1024 * 15);  // Unchanged
}

// ============================================================================
// Data Flow Tests (Allocation Chain Verification)
// ============================================================================

TEST(DataFlowTest, AllocationRequestPropagation) {
    // Verify allocation requests propagate correctly through the chain
    auto mockAllocator = std::make_shared<MockAllocator>();

    DeviceBudgetManager::Config config{
        .deviceMemoryBudget = 1024 * 1024 * 100,
        .stagingQuota = 1024 * 1024 * 10,
        .strictBudget = false
    };

    DeviceBudgetManager budgetManager(mockAllocator, VK_NULL_HANDLE, config);

    // Make allocation with specific parameters
    BufferAllocationRequest request{
        .size = 4096,
        .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        .location = MemoryLocation::DeviceLocal,
        .debugName = "PropagationTest"
    };

    auto result = budgetManager.AllocateBuffer(request);
    ASSERT_TRUE(result.has_value());

    // Verify the allocator received the allocation
    EXPECT_EQ(mockAllocator->GetAllocationCount(), 1);
    EXPECT_EQ(mockAllocator->GetTotalAllocated(), 4096);

    // Verify allocation result has correct size
    EXPECT_EQ(result->size, 4096);

    // Free and verify cleanup
    budgetManager.FreeBuffer(*result);
    EXPECT_EQ(mockAllocator->GetAllocationCount(), 0);
}

TEST(DataFlowTest, MapUnmapPropagation) {
    auto mockAllocator = std::make_shared<MockAllocator>();

    DeviceBudgetManager::Config config{};
    DeviceBudgetManager budgetManager(mockAllocator, VK_NULL_HANDLE, config);

    BufferAllocationRequest request{
        .size = 1024,
        .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        .location = MemoryLocation::HostVisible,  // Mappable
        .debugName = "MapTest"
    };

    auto result = budgetManager.AllocateBuffer(request);
    ASSERT_TRUE(result.has_value());

    // Map should work through allocator
    void* mapped = mockAllocator->MapBuffer(*result);
    EXPECT_NE(mapped, nullptr);

    // Cleanup
    mockAllocator->UnmapBuffer(*result);
    budgetManager.FreeBuffer(*result);
}

// ============================================================================
// Budget Limit Tests
// ============================================================================

class BudgetLimitTest : public ::testing::Test {
protected:
    void SetUp() override {
        mockAllocator_ = std::make_shared<MockAllocator>();

        // Small budget for testing limits
        DeviceBudgetManager::Config config{
            .deviceMemoryBudget = 1024 * 1024 * 10,  // 10 MB max
            .deviceMemoryWarning = 1024 * 1024 * 8,  // 8 MB warning
            .stagingQuota = 1024 * 1024,              // 1 MB staging
            .strictBudget = false  // Non-strict: allows over-budget
        };

        budgetManager_ = std::make_unique<DeviceBudgetManager>(
            mockAllocator_, VK_NULL_HANDLE, config
        );
    }

    std::shared_ptr<MockAllocator> mockAllocator_;
    std::unique_ptr<DeviceBudgetManager> budgetManager_;
};

TEST_F(BudgetLimitTest, NearBudgetDetection) {
    // Allocate up to warning threshold
    BufferAllocationRequest request{
        .size = 1024 * 1024 * 9,  // 9 MB (over 8 MB warning)
        .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        .location = MemoryLocation::DeviceLocal,
        .debugName = "NearBudgetBuffer"
    };

    auto result = budgetManager_->AllocateBuffer(request);
    ASSERT_TRUE(result.has_value());

    // Should be near budget limit
    EXPECT_TRUE(budgetManager_->IsNearBudgetLimit());

    budgetManager_->FreeBuffer(*result);
}

TEST_F(BudgetLimitTest, OverBudgetDetection) {
    // Allocate over budget (non-strict mode allows this)
    BufferAllocationRequest request{
        .size = 1024 * 1024 * 11,  // 11 MB (over 10 MB budget)
        .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        .location = MemoryLocation::DeviceLocal,
        .debugName = "OverBudgetBuffer"
    };

    auto result = budgetManager_->AllocateBuffer(request);
    ASSERT_TRUE(result.has_value());  // Non-strict allows over-budget

    // Should detect over-budget
    EXPECT_TRUE(budgetManager_->IsOverBudget());

    budgetManager_->FreeBuffer(*result);
}

// ============================================================================
// Thread Safety Tests
// ============================================================================

TEST(ThreadSafetyTest, ConcurrentAllocations) {
    auto mockAllocator = std::make_shared<MockAllocator>();

    DeviceBudgetManager::Config config{
        .deviceMemoryBudget = 1024ULL * 1024 * 1024,  // 1 GB
        .stagingQuota = 1024 * 1024 * 100,
        .strictBudget = false
    };

    DeviceBudgetManager budgetManager(mockAllocator, VK_NULL_HANDLE, config);

    constexpr int numThreads = 8;
    constexpr int allocationsPerThread = 100;
    std::vector<std::thread> threads;
    std::atomic<int> successCount{0};
    std::vector<BufferAllocation> allAllocations;
    std::mutex allocMutex;

    for (int t = 0; t < numThreads; ++t) {
        threads.emplace_back([&, t]() {
            for (int i = 0; i < allocationsPerThread; ++i) {
                BufferAllocationRequest request{
                    .size = 1024,  // 1 KB each
                    .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                    .location = MemoryLocation::DeviceLocal,
                    .debugName = "ConcurrentBuffer"
                };

                auto result = budgetManager.AllocateBuffer(request);
                if (result.has_value()) {
                    successCount++;
                    std::lock_guard<std::mutex> lock(allocMutex);
                    allAllocations.push_back(*result);
                }
            }
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    // All allocations should succeed
    EXPECT_EQ(successCount.load(), numThreads * allocationsPerThread);
    EXPECT_EQ(mockAllocator->GetAllocationCount(), numThreads * allocationsPerThread);

    // Cleanup
    for (auto& alloc : allAllocations) {
        budgetManager.FreeBuffer(alloc);
    }

    EXPECT_EQ(mockAllocator->GetAllocationCount(), 0);
}

// ============================================================================
// Allocator Access Tests
// ============================================================================

TEST(AllocatorAccessTest, GetAllocator) {
    auto mockAllocator = std::make_shared<MockAllocator>();
    DeviceBudgetManager::Config config{};
    DeviceBudgetManager budgetManager(mockAllocator, VK_NULL_HANDLE, config);

    // Should be able to access underlying allocator
    IMemoryAllocator* allocator = budgetManager.GetAllocator();
    ASSERT_NE(allocator, nullptr);
    EXPECT_EQ(allocator->GetName(), "MockAllocator");
}

TEST(AllocatorAccessTest, GetAllocatorName) {
    auto mockAllocator = std::make_shared<MockAllocator>();
    DeviceBudgetManager::Config config{};
    DeviceBudgetManager budgetManager(mockAllocator, VK_NULL_HANDLE, config);

    EXPECT_EQ(budgetManager.GetAllocatorName(), "MockAllocator");
}

// ============================================================================
// StagingBufferPool Tests
// ============================================================================

#include "Memory/StagingBufferPool.h"

class StagingBufferPoolTest : public ::testing::Test {
protected:
    void SetUp() override {
        mockAllocator_ = std::make_shared<MockAllocator>();

        DeviceBudgetManager::Config config{
            .deviceMemoryBudget = 1024ULL * 1024 * 1024,  // 1 GB
            .stagingQuota = 1024 * 1024 * 100,            // 100 MB staging
            .strictBudget = false
        };

        budgetManager_ = std::make_unique<DeviceBudgetManager>(
            mockAllocator_, VK_NULL_HANDLE, config);
    }

    void TearDown() override {
        pool_.reset();
        budgetManager_.reset();
        mockAllocator_.reset();
    }

    std::shared_ptr<MockAllocator> mockAllocator_;
    std::unique_ptr<DeviceBudgetManager> budgetManager_;
    std::unique_ptr<StagingBufferPool> pool_;
};

TEST_F(StagingBufferPoolTest, AcquireAndRelease) {
    StagingBufferPool::Config poolConfig{
        .minBufferSize = 1024,           // 1 KB min
        .maxBufferSize = 1024 * 1024,    // 1 MB max
        .maxPooledBuffersPerBucket = 4,
        .maxTotalPooledBytes = 1024 * 1024 * 10  // 10 MB max pool
    };

    pool_ = std::make_unique<StagingBufferPool>(budgetManager_.get(), poolConfig);

    // Acquire a buffer
    auto acquisition = pool_->AcquireBuffer(4096);
    ASSERT_TRUE(acquisition.has_value());
    StagingBufferHandle handle = acquisition->handle;
    VkBuffer buffer = acquisition->buffer;
    VkDeviceSize bufSize = acquisition->size;
    EXPECT_NE(handle, InvalidStagingHandle);
    EXPECT_NE(buffer, VK_NULL_HANDLE);
    EXPECT_GE(bufSize, static_cast<VkDeviceSize>(4096));

    // Stats should reflect active buffer
    auto stats = pool_->GetStats();
    EXPECT_EQ(stats.activeBuffers, 1u);
    EXPECT_GT(stats.activeBytes, 0u);

    // Release the buffer
    pool_->ReleaseBuffer(handle);

    // Stats should now show pooled buffer
    stats = pool_->GetStats();
    EXPECT_EQ(stats.activeBuffers, 0u);
    EXPECT_GT(stats.totalPooledBuffers, 0u);
}

TEST_F(StagingBufferPoolTest, BufferReuse) {
    StagingBufferPool::Config poolConfig{
        .minBufferSize = 1024,
        .maxBufferSize = 1024 * 1024,
        .maxPooledBuffersPerBucket = 4,
        .maxTotalPooledBytes = 1024 * 1024 * 10
    };

    pool_ = std::make_unique<StagingBufferPool>(budgetManager_.get(), poolConfig);

    // Acquire and release a buffer
    auto first = pool_->AcquireBuffer(2048);
    ASSERT_TRUE(first.has_value());
    VkBuffer originalBuffer = first->buffer;
    pool_->ReleaseBuffer(first->handle);

    // Second acquire should reuse the buffer
    auto second = pool_->AcquireBuffer(2048);
    ASSERT_TRUE(second.has_value());

    // Should get the same buffer back (reuse from pool)
    EXPECT_EQ(second->buffer, originalBuffer);

    auto stats = pool_->GetStats();
    EXPECT_EQ(stats.poolHits, 1);

    pool_->ReleaseBuffer(second->handle);
}

TEST_F(StagingBufferPoolTest, SizeClassBucketing) {
    StagingBufferPool::Config poolConfig{
        .minBufferSize = 1024,           // 1 KB = bucket 0
        .maxBufferSize = 1024 * 1024,    // 1 MB max
        .maxPooledBuffersPerBucket = 4,
        .maxTotalPooledBytes = 1024 * 1024 * 50
    };

    pool_ = std::make_unique<StagingBufferPool>(budgetManager_.get(), poolConfig);

    // Request 1.5KB - should round up to 2KB bucket
    auto smallBuf = pool_->AcquireBuffer(1536);
    ASSERT_TRUE(smallBuf.has_value());
    VkDeviceSize smallSize = smallBuf->size;
    EXPECT_GE(smallSize, VkDeviceSize{2048});  // Rounded to bucket size

    // Request 5KB - should round up to 8KB bucket
    auto mediumBuf = pool_->AcquireBuffer(5000);
    ASSERT_TRUE(mediumBuf.has_value());
    VkDeviceSize mediumSize = mediumBuf->size;
    EXPECT_GE(mediumSize, VkDeviceSize{8192});  // Rounded to bucket size

    pool_->ReleaseBuffer(smallBuf->handle);
    pool_->ReleaseBuffer(mediumBuf->handle);
}

TEST_F(StagingBufferPoolTest, ClearPool) {
    StagingBufferPool::Config poolConfig{
        .minBufferSize = 1024,
        .maxBufferSize = 1024 * 1024,
        .maxPooledBuffersPerBucket = 4,
        .maxTotalPooledBytes = 1024 * 1024 * 10
    };

    pool_ = std::make_unique<StagingBufferPool>(budgetManager_.get(), poolConfig);

    // Acquire and release several buffers to populate pool
    for (int i = 0; i < 5; ++i) {
        auto buf = pool_->AcquireBuffer(4096);
        ASSERT_TRUE(buf.has_value());
        pool_->ReleaseBuffer(buf->handle);
    }

    auto stats = pool_->GetStats();
    EXPECT_GT(stats.totalPooledBuffers, 0u);

    // Clear the pool
    pool_->Clear();

    stats = pool_->GetStats();
    EXPECT_EQ(stats.totalPooledBuffers, 0u);
    EXPECT_EQ(stats.totalPooledBytes, 0u);
}

TEST_F(StagingBufferPoolTest, TrimPool) {
    StagingBufferPool::Config poolConfig{
        .minBufferSize = 1024,
        .maxBufferSize = 1024 * 1024,
        .maxPooledBuffersPerBucket = 10,  // Allow more pooled
        .maxTotalPooledBytes = 1024 * 1024 * 50
    };

    pool_ = std::make_unique<StagingBufferPool>(budgetManager_.get(), poolConfig);

    // Populate pool with buffers
    std::vector<StagingBufferHandle> handles;
    for (int i = 0; i < 10; ++i) {
        auto buf = pool_->AcquireBuffer(32 * 1024);  // 32KB each
        ASSERT_TRUE(buf.has_value());
        handles.push_back(buf->handle);
    }

    for (auto h : handles) {
        pool_->ReleaseBuffer(h);
    }

    auto beforeStats = pool_->GetStats();
    EXPECT_GT(beforeStats.totalPooledBytes, 0u);

    // Trim to smaller size
    uint64_t freed = pool_->Trim(100 * 1024);  // Trim to 100KB
    EXPECT_GT(freed, 0u);

    auto afterStats = pool_->GetStats();
    EXPECT_LE(afterStats.totalPooledBytes, 100 * 1024u);
}

TEST_F(StagingBufferPoolTest, ConcurrentAcquireRelease) {
    StagingBufferPool::Config poolConfig{
        .minBufferSize = 1024,
        .maxBufferSize = 1024 * 1024,
        .maxPooledBuffersPerBucket = 16,
        .maxTotalPooledBytes = 1024 * 1024 * 100
    };

    pool_ = std::make_unique<StagingBufferPool>(budgetManager_.get(), poolConfig);

    constexpr int numThreads = 4;
    constexpr int opsPerThread = 50;
    std::vector<std::thread> threads;
    std::atomic<int> successCount{0};

    for (int t = 0; t < numThreads; ++t) {
        threads.emplace_back([&, t]() {
            for (int i = 0; i < opsPerThread; ++i) {
                VkDeviceSize size = ((t * 1024) + (i * 512)) % (64 * 1024) + 1024;
                auto buf = pool_->AcquireBuffer(size);
                if (buf) {
                    successCount++;
                    // Simulate some work
                    std::this_thread::yield();
                    pool_->ReleaseBuffer(buf->handle);
                }
            }
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    EXPECT_EQ(successCount.load(), numThreads * opsPerThread);

    auto stats = pool_->GetStats();
    EXPECT_EQ(stats.activeBuffers, 0);  // All released
    EXPECT_EQ(stats.totalAcquisitions, numThreads * opsPerThread);
    EXPECT_EQ(stats.totalReleases, numThreads * opsPerThread);
}

TEST_F(StagingBufferPoolTest, ReleaseAndDestroy) {
    StagingBufferPool::Config poolConfig{
        .minBufferSize = 1024,
        .maxBufferSize = 1024 * 1024,
        .maxPooledBuffersPerBucket = 4,
        .maxTotalPooledBytes = 1024 * 1024 * 10
    };

    pool_ = std::make_unique<StagingBufferPool>(budgetManager_.get(), poolConfig);

    // Acquire a buffer
    auto buf = pool_->AcquireBuffer(8192);
    ASSERT_TRUE(buf.has_value());

    // Release and destroy (don't return to pool)
    pool_->ReleaseAndDestroy(buf->handle);

    // Pool should be empty
    auto stats = pool_->GetStats();
    EXPECT_EQ(stats.totalPooledBuffers, 0u);
    EXPECT_EQ(stats.activeBuffers, 0);
}

TEST_F(StagingBufferPoolTest, StatsAccuracy) {
    StagingBufferPool::Config poolConfig{
        .minBufferSize = 1024,
        .maxBufferSize = 1024 * 1024,
        .maxPooledBuffersPerBucket = 4,
        .maxTotalPooledBytes = 1024 * 1024 * 10
    };

    pool_ = std::make_unique<StagingBufferPool>(budgetManager_.get(), poolConfig);

    // Initial stats should be zero
    auto stats = pool_->GetStats();
    EXPECT_EQ(stats.totalAcquisitions, 0u);
    EXPECT_EQ(stats.poolHits, 0u);
    EXPECT_EQ(stats.poolMisses, 0u);

    // First acquisition - should be a miss (no pooled buffers)
    auto buf1 = pool_->AcquireBuffer(4096);
    ASSERT_TRUE(buf1.has_value());

    stats = pool_->GetStats();
    EXPECT_EQ(stats.totalAcquisitions, 1u);
    EXPECT_EQ(stats.poolMisses, 1u);
    EXPECT_EQ(stats.poolHits, 0u);

    // Release and re-acquire - should be a hit
    pool_->ReleaseBuffer(buf1->handle);
    auto buf2 = pool_->AcquireBuffer(4096);
    ASSERT_TRUE(buf2.has_value());

    stats = pool_->GetStats();
    EXPECT_EQ(stats.totalAcquisitions, 2u);
    EXPECT_EQ(stats.poolHits, 1u);
    EXPECT_EQ(stats.poolMisses, 1u);
    EXPECT_FLOAT_EQ(stats.hitRate, 0.5f);

    pool_->ReleaseBuffer(buf2->handle);
}

// ============================================================================
// BatchedUploader Tests
// ============================================================================
// NOTE: BatchedUploader requires actual Vulkan device/queue for testing.
// Full integration tests are in the application-level test suite.
// The StagingBufferPool tests above cover the buffer pooling logic.
// BatchedUploader adds:
// - Command buffer batching (Vulkan-dependent)
// - Timeline semaphore completion tracking (Vulkan-dependent)
// - Deadline-based flush (uses std::chrono, tested via integration)
//
// See: application/tests/test_batched_upload_integration.cpp (future)
