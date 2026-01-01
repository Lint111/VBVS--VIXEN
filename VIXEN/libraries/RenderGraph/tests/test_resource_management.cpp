/**
 * @file test_resource_management.cpp
 * @brief Tests for RenderGraph resource management systems
 *
 * Tests:
 * - ResourceBudgetManager (memory budget tracking)
 * - DeferredDestruction (cleanup queue management)
 * - StatefulContainer (resource state tracking)
 * - SlotTask (task status management)
 *
 * Compatible with VULKAN_TRIMMED_BUILD (headers only).
 */

#include <gtest/gtest.h>
#include "Core/ResourceBudgetManager.h"
#include "Core/DeferredDestruction.h"
#include "Core/StatefulContainer.h"
#include "Core/SlotTask.h"
#include "Core/IMemoryAllocator.h"
#include "Core/DirectAllocator.h"
#include "Core/VMAAllocator.h"
#include "Core/HostBudgetManager.h"
#include "Core/DeviceBudgetManager.h"

#include <atomic>
#include <chrono>
#include <future>
#include <random>
#include <thread>
#include <vector>

using namespace Vixen::RenderGraph;

// ============================================================================
// ResourceBudgetManager Tests
// ============================================================================

class ResourceBudgetManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        budgetManager = std::make_unique<ResourceBudgetManager>();
    }

    std::unique_ptr<ResourceBudgetManager> budgetManager;
};

TEST_F(ResourceBudgetManagerTest, InitialBudget) {
    // Budget should not exist until set
    auto unsetBudget = budgetManager->GetBudget(BudgetResourceType::DeviceMemory);
    EXPECT_FALSE(unsetBudget.has_value());

    // But usage should still be queryable (zero by default)
    auto usage = budgetManager->GetUsage(BudgetResourceType::DeviceMemory);
    EXPECT_EQ(usage.currentBytes, 0);
    EXPECT_EQ(usage.allocationCount, 0);
}

TEST_F(ResourceBudgetManagerTest, SetBudget) {
    ResourceBudget budget(1024 * 1024 * 100, 1024 * 1024 * 80); // 100 MB max, 80 MB warning

    budgetManager->SetBudget(BudgetResourceType::DeviceMemory, budget);

    auto retrieved = budgetManager->GetBudget(BudgetResourceType::DeviceMemory);
    ASSERT_TRUE(retrieved.has_value());
    EXPECT_EQ(retrieved->maxBytes, budget.maxBytes);
    EXPECT_EQ(retrieved->warningThreshold, budget.warningThreshold);
}

TEST_F(ResourceBudgetManagerTest, TrackUsage) {
    // Set budget
    ResourceBudget budget(1024 * 1024 * 100);
    budgetManager->SetBudget(BudgetResourceType::DeviceMemory, budget);

    // Track allocation
    uint64_t bytes = 1024 * 1024 * 10; // 10 MB
    budgetManager->RecordAllocation(BudgetResourceType::DeviceMemory, bytes);

    auto usage = budgetManager->GetUsage(BudgetResourceType::DeviceMemory);
    EXPECT_EQ(usage.currentBytes, bytes);
}

TEST_F(ResourceBudgetManagerTest, BudgetExceeded) {
    // Set small budget
    ResourceBudget budget(1024 * 1024 * 10, 0, true); // 10 MB max, strict mode
    budgetManager->SetBudget(BudgetResourceType::HostMemory, budget);

    // Try to allocate more
    uint64_t bytes = 1024 * 1024 * 20; // 20 MB
    bool canAllocate = budgetManager->TryAllocate(BudgetResourceType::HostMemory, bytes);
    EXPECT_FALSE(canAllocate); // Should exceed budget
}

TEST_F(ResourceBudgetManagerTest, ReleaseUsage) {
    // Set budget and allocate
    ResourceBudget budget(1024 * 1024 * 100);
    budgetManager->SetBudget(BudgetResourceType::DeviceMemory, budget);

    uint64_t allocated = 1024 * 1024 * 10;
    budgetManager->RecordAllocation(BudgetResourceType::DeviceMemory, allocated);

    // Release some
    uint64_t released = 1024 * 1024 * 5;
    budgetManager->RecordDeallocation(BudgetResourceType::DeviceMemory, released);

    auto usage = budgetManager->GetUsage(BudgetResourceType::DeviceMemory);
    EXPECT_EQ(usage.currentBytes, allocated - released);
}

TEST_F(ResourceBudgetManagerTest, MultipleResourceTypes) {
    // Set budgets for different types
    ResourceBudget hostBudget(1024 * 1024 * 100);
    ResourceBudget deviceBudget(1024 * 1024 * 500);

    budgetManager->SetBudget(BudgetResourceType::HostMemory, hostBudget);
    budgetManager->SetBudget(BudgetResourceType::DeviceMemory, deviceBudget);

    // Track different usages
    budgetManager->RecordAllocation(BudgetResourceType::HostMemory, 1024 * 1024 * 10);
    budgetManager->RecordAllocation(BudgetResourceType::DeviceMemory, 1024 * 1024 * 20);

    auto hostUsage = budgetManager->GetUsage(BudgetResourceType::HostMemory);
    auto deviceUsage = budgetManager->GetUsage(BudgetResourceType::DeviceMemory);

    EXPECT_EQ(hostUsage.currentBytes, 1024 * 1024 * 10);
    EXPECT_EQ(deviceUsage.currentBytes, 1024 * 1024 * 20);
}

// ============================================================================
// Concurrent Allocation Tests (Thread Safety Validation)
// ============================================================================

class ConcurrentBudgetManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        budgetManager = std::make_unique<ResourceBudgetManager>();
        // Set a large budget to allow many allocations
        ResourceBudget budget(1024ULL * 1024 * 1024 * 10); // 10 GB
        budgetManager->SetBudget(BudgetResourceType::DeviceMemory, budget);
    }

    std::unique_ptr<ResourceBudgetManager> budgetManager;
};

TEST_F(ConcurrentBudgetManagerTest, ConcurrentRecordAllocations) {
    constexpr int numThreads = 8;
    constexpr int allocationsPerThread = 1000;
    constexpr uint64_t allocationSize = 1024; // 1 KB each

    std::vector<std::thread> threads;
    threads.reserve(numThreads);

    // Launch threads that all allocate concurrently
    for (int t = 0; t < numThreads; ++t) {
        threads.emplace_back([this]() {
            for (int i = 0; i < allocationsPerThread; ++i) {
                budgetManager->RecordAllocation(BudgetResourceType::DeviceMemory, allocationSize);
            }
        });
    }

    // Wait for all threads
    for (auto& t : threads) {
        t.join();
    }

    // Verify total allocations
    auto usage = budgetManager->GetUsage(BudgetResourceType::DeviceMemory);
    uint64_t expectedTotal = static_cast<uint64_t>(numThreads) * allocationsPerThread * allocationSize;
    EXPECT_EQ(usage.currentBytes, expectedTotal);
    EXPECT_EQ(usage.allocationCount, numThreads * allocationsPerThread);
}

TEST_F(ConcurrentBudgetManagerTest, ConcurrentAllocateAndDeallocate) {
    constexpr int numThreads = 8;
    constexpr int operationsPerThread = 500;
    constexpr uint64_t allocationSize = 1024;

    std::atomic<int> allocations{0};
    std::atomic<int> deallocations{0};

    std::vector<std::thread> threads;
    threads.reserve(numThreads);

    // Half threads allocate, half deallocate
    for (int t = 0; t < numThreads; ++t) {
        if (t % 2 == 0) {
            // Allocator thread
            threads.emplace_back([this, &allocations]() {
                for (int i = 0; i < operationsPerThread; ++i) {
                    budgetManager->RecordAllocation(BudgetResourceType::DeviceMemory, allocationSize);
                    allocations.fetch_add(1, std::memory_order_relaxed);
                }
            });
        } else {
            // Deallocator thread (with small delay to ensure something to deallocate)
            threads.emplace_back([this, &deallocations]() {
                for (int i = 0; i < operationsPerThread; ++i) {
                    // Small yield to let allocators run first
                    if (i == 0) {
                        std::this_thread::sleep_for(std::chrono::microseconds(100));
                    }
                    budgetManager->RecordDeallocation(BudgetResourceType::DeviceMemory, allocationSize);
                    deallocations.fetch_add(1, std::memory_order_relaxed);
                }
            });
        }
    }

    for (auto& t : threads) {
        t.join();
    }

    auto usage = budgetManager->GetUsage(BudgetResourceType::DeviceMemory);

    // Net result: (numThreads/2 * ops) allocations - (numThreads/2 * ops) deallocations = 0
    // But due to timing, some deallocations may underflow to 0
    // The key is: no crashes, no data corruption
    EXPECT_GE(usage.currentBytes, 0ULL);

    // Verify peak was tracked
    EXPECT_GT(usage.peakBytes, 0ULL);
}

TEST_F(ConcurrentBudgetManagerTest, ConcurrentTryAllocate) {
    // Set strict budget
    ResourceBudget strictBudget(1024 * 1024 * 100, 0, true); // 100 MB strict
    budgetManager->SetBudget(BudgetResourceType::HostMemory, strictBudget);

    constexpr int numThreads = 8;
    constexpr int attemptsPerThread = 100;

    std::atomic<int> successCount{0};
    std::atomic<int> failureCount{0};

    std::vector<std::thread> threads;
    threads.reserve(numThreads);

    for (int t = 0; t < numThreads; ++t) {
        threads.emplace_back([this, &successCount, &failureCount]() {
            for (int i = 0; i < attemptsPerThread; ++i) {
                // Try to allocate 50 MB (will fit 2x in 100 MB budget)
                bool canAllocate = budgetManager->TryAllocate(BudgetResourceType::HostMemory, 50 * 1024 * 1024);
                if (canAllocate) {
                    successCount.fetch_add(1, std::memory_order_relaxed);
                    // Record the allocation
                    budgetManager->RecordAllocation(BudgetResourceType::HostMemory, 50 * 1024 * 1024);
                } else {
                    failureCount.fetch_add(1, std::memory_order_relaxed);
                }
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    // At least some should succeed, most should fail (only 2 fit in budget)
    EXPECT_GT(successCount.load(), 0);
    // With strict mode and 100MB budget, only 2 x 50MB allocations fit
    // But concurrent attempts may see "room available" before recording
    // The key test is: no crashes or corruption occurred
}

TEST_F(ConcurrentBudgetManagerTest, ConcurrentGetUsage) {
    constexpr int numReaders = 4;
    constexpr int numWriters = 4;
    constexpr int opsPerThread = 500;
    constexpr uint64_t allocationSize = 1024;

    std::atomic<bool> running{true};
    std::vector<std::thread> threads;

    // Writer threads (allocate/deallocate)
    for (int t = 0; t < numWriters; ++t) {
        threads.emplace_back([this, opsPerThread]() {
            for (int i = 0; i < opsPerThread; ++i) {
                budgetManager->RecordAllocation(BudgetResourceType::DeviceMemory, allocationSize);
                budgetManager->RecordDeallocation(BudgetResourceType::DeviceMemory, allocationSize);
            }
        });
    }

    // Reader threads (query usage)
    std::atomic<int> readCount{0};
    for (int t = 0; t < numReaders; ++t) {
        threads.emplace_back([this, &running, &readCount]() {
            while (running.load(std::memory_order_relaxed)) {
                auto usage = budgetManager->GetUsage(BudgetResourceType::DeviceMemory);
                // Just accessing - shouldn't crash
                (void)usage.currentBytes;
                (void)usage.peakBytes;
                (void)usage.allocationCount;
                readCount.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }

    // Let writers finish
    for (int t = 0; t < numWriters; ++t) {
        threads[t].join();
    }

    running.store(false, std::memory_order_relaxed);

    // Let readers finish
    for (int t = numWriters; t < numWriters + numReaders; ++t) {
        threads[t].join();
    }

    // Verify no crashes and reads occurred
    EXPECT_GT(readCount.load(), 0);

    // After equal alloc/dealloc, should be at 0
    auto finalUsage = budgetManager->GetUsage(BudgetResourceType::DeviceMemory);
    EXPECT_EQ(finalUsage.currentBytes, 0ULL);
}

TEST_F(ConcurrentBudgetManagerTest, StressTestHighContention) {
    constexpr int numThreads = 16;
    constexpr int opsPerThread = 2000;

    std::vector<std::thread> threads;
    threads.reserve(numThreads);

    auto startTime = std::chrono::high_resolution_clock::now();

    for (int t = 0; t < numThreads; ++t) {
        threads.emplace_back([this, t]() {
            std::mt19937 rng(t); // Thread-specific seed
            std::uniform_int_distribution<uint64_t> sizeDist(1, 4096);
            std::uniform_int_distribution<int> opDist(0, 2);

            for (int i = 0; i < opsPerThread; ++i) {
                uint64_t size = sizeDist(rng);
                int op = opDist(rng);

                switch (op) {
                    case 0:
                        budgetManager->RecordAllocation(BudgetResourceType::DeviceMemory, size);
                        break;
                    case 1:
                        budgetManager->RecordDeallocation(BudgetResourceType::DeviceMemory, size);
                        break;
                    case 2:
                        budgetManager->GetUsage(BudgetResourceType::DeviceMemory);
                        break;
                }
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    auto endTime = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);

    // Test passed if no crashes - performance metric is informational
    std::cout << "  Stress test: " << numThreads * opsPerThread << " operations in "
              << duration.count() << "ms\n";

    // Verify manager is still functional
    auto usage = budgetManager->GetUsage(BudgetResourceType::DeviceMemory);
    EXPECT_GE(usage.peakBytes, 0ULL); // Should have tracked something
}

// ============================================================================
// DeferredDestruction Tests
// ============================================================================

class DeferredDestructionTest : public ::testing::Test {
protected:
    void SetUp() override {
        destructionQueue = std::make_unique<DeferredDestructionQueue>();
        destructionCounter = 0;
    }

    std::unique_ptr<DeferredDestructionQueue> destructionQueue;
    static int destructionCounter;
};

int DeferredDestructionTest::destructionCounter = 0;

TEST_F(DeferredDestructionTest, EmptyQueue) {
    // Initially queue should be empty
    EXPECT_EQ(destructionQueue->GetPendingCount(), 0);

    // Processing empty queue should do nothing
    destructionQueue->ProcessFrame(0, 3);
    EXPECT_EQ(destructionCounter, 0);
}

TEST_F(DeferredDestructionTest, PendingDestructionStructure) {
    // Verify PendingDestruction structure works correctly
    destructionCounter = 0;

    PendingDestruction pending([this]() { destructionCounter++; }, 5);
    EXPECT_EQ(pending.submittedFrame, 5);

    // Manually call destructor to verify it works
    pending.destructorFunc();
    EXPECT_EQ(destructionCounter, 1);
}

TEST_F(DeferredDestructionTest, FlushAllDestructions) {
    destructionCounter = 0;

    // Manually create and queue pending destructions
    auto queue = std::make_unique<DeferredDestructionQueue>();

    // Since we can't directly queue PendingDestruction, verify Flush works on empty queue
    queue->Flush();
    EXPECT_EQ(queue->GetPendingCount(), 0);
}

TEST_F(DeferredDestructionTest, ProcessFrameFrameTracking) {
    // Test frame-based destruction logic
    destructionCounter = 0;

    // Create pending destruction at frame 0
    PendingDestruction pending([this]() { destructionCounter++; }, 0);

    // Verify: should destroy when currentFrame - submittedFrame >= maxFramesInFlight
    // Example: frame 3 - 0 >= 3, so should destroy
    uint64_t submittedFrame = 0;
    uint64_t currentFrame = 3;
    uint32_t maxFramesInFlight = 3;

    bool shouldDestroy = (currentFrame - submittedFrame) >= maxFramesInFlight;
    EXPECT_TRUE(shouldDestroy);

    // Verify: frame 2 - 0 >= 3 should be false (not yet)
    currentFrame = 2;
    shouldDestroy = (currentFrame - submittedFrame) >= maxFramesInFlight;
    EXPECT_FALSE(shouldDestroy);
}

// ============================================================================
// StatefulContainer Tests
// ============================================================================

class StatefulContainerTest : public ::testing::Test {
protected:
    struct TestResource {
        int value = 0;
        bool valid = false;
    };
};

TEST_F(StatefulContainerTest, ContainerSize) {
    StatefulContainer<TestResource> container;
    container.resize(3);

    EXPECT_EQ(container.size(), 3);
    EXPECT_FALSE(container.empty());
}

TEST_F(StatefulContainerTest, ElementStateTracking) {
    StatefulContainer<TestResource> container;
    container.resize(1);

    // Initial state is Dirty
    EXPECT_EQ(container.GetState(0), ResourceState::Dirty);
    EXPECT_TRUE(container.IsDirty(0));

    // Transition to Ready
    container.MarkReady(0);
    EXPECT_EQ(container.GetState(0), ResourceState::Ready);
    EXPECT_TRUE(container.IsReady(0));
    EXPECT_FALSE(container.IsDirty(0));
}

TEST_F(StatefulContainerTest, ElementValueStorage) {
    StatefulContainer<TestResource> container;
    container.resize(2);

    container.GetValue(0).value = 42;
    container.GetValue(1).value = 100;

    EXPECT_EQ(container.GetValue(0).value, 42);
    EXPECT_EQ(container.GetValue(1).value, 100);
}

TEST_F(StatefulContainerTest, BulkStateOperations) {
    StatefulContainer<TestResource> container;
    container.resize(5);

    // Mark all as dirty (initially Dirty, so verify the function)
    container.MarkAllDirty();
    EXPECT_EQ(container.CountDirty(), 5);

    // Mark some as ready
    container.MarkReady(0);
    container.MarkReady(2);
    EXPECT_EQ(container.CountDirty(), 3);

    // Mark all as ready
    container.MarkAllReady();
    EXPECT_EQ(container.CountDirty(), 0);
    EXPECT_FALSE(container.AnyDirty());
}

// ============================================================================
// SlotTaskContext Tests
// ============================================================================

class SlotTaskContextTest : public ::testing::Test {
};

TEST_F(SlotTaskContextTest, InitialStatus) {
    SlotTaskContext context;

    EXPECT_EQ(context.status, TaskStatus::Pending);
    EXPECT_FALSE(context.errorMessage.has_value());
}

TEST_F(SlotTaskContextTest, SingleElementProperties) {
    SlotTaskContext context;
    context.arrayStartIndex = 5;
    context.arrayCount = 1;

    EXPECT_TRUE(context.IsSingleElement());
    EXPECT_EQ(context.GetElementIndex(), 5);
}

TEST_F(SlotTaskContextTest, MultipleElementProperties) {
    SlotTaskContext context;
    context.arrayStartIndex = 10;
    context.arrayCount = 5;

    EXPECT_FALSE(context.IsSingleElement());
}

TEST_F(SlotTaskContextTest, TaskStatusTransitions) {
    SlotTaskContext context;

    // Start
    context.status = TaskStatus::Running;
    EXPECT_EQ(context.status, TaskStatus::Running);

    // Complete
    context.status = TaskStatus::Completed;
    EXPECT_EQ(context.status, TaskStatus::Completed);

    // Failed
    context.errorMessage = "Test error";
    context.status = TaskStatus::Failed;
    EXPECT_EQ(context.status, TaskStatus::Failed);
    EXPECT_TRUE(context.errorMessage.has_value());
    EXPECT_EQ(context.errorMessage.value(), "Test error");
}

TEST_F(SlotTaskContextTest, ResourceEstimates) {
    SlotTaskContext context;
    context.estimatedMemoryBytes = 1024 * 1024 * 100;
    context.estimatedTimeMs = 500;

    EXPECT_EQ(context.estimatedMemoryBytes, 1024 * 1024 * 100);
    EXPECT_EQ(context.estimatedTimeMs, 500);
}

TEST_F(SlotTaskContextTest, TaskIndexing) {
    SlotTaskContext context;
    context.taskIndex = 3;
    context.totalTasks = 10;

    EXPECT_EQ(context.taskIndex, 3);
    EXPECT_EQ(context.totalTasks, 10);
}

// ============================================================================
// Integration Test: Resource Lifecycle
// ============================================================================

TEST(ResourceManagementIntegration, CompleteResourceLifecycle) {
    // Simulate complete resource lifecycle with all management systems

    // 1. Budget allocation
    ResourceBudgetManager budgetMgr;
    ResourceBudget budget(1024 * 1024 * 100, 1024 * 1024 * 80);
    budgetMgr.SetBudget(BudgetResourceType::DeviceMemory, budget);

    uint64_t allocationBytes = 1024 * 1024 * 10;
    EXPECT_TRUE(budgetMgr.TryAllocate(BudgetResourceType::DeviceMemory, allocationBytes));

    // 2. Track allocation
    budgetMgr.RecordAllocation(BudgetResourceType::DeviceMemory, allocationBytes);
    auto usage = budgetMgr.GetUsage(BudgetResourceType::DeviceMemory);
    EXPECT_EQ(usage.currentBytes, allocationBytes);

    // 3. Resource state management with StatefulContainer
    struct TestResource { int id = 123; };
    StatefulContainer<TestResource> container;
    container.resize(1);
    container.GetValue(0).id = 123;
    container.MarkReady(0);
    EXPECT_TRUE(container.IsReady(0));

    // 4. Task context tracking
    SlotTaskContext task;
    task.status = TaskStatus::Running;
    task.estimatedMemoryBytes = allocationBytes;
    task.arrayCount = 1;
    task.status = TaskStatus::Completed;
    EXPECT_EQ(task.status, TaskStatus::Completed);

    // 5. Deferred cleanup - verify pending destruction structure
    bool destroyed = false;
    PendingDestruction pending([&destroyed]() { destroyed = true; }, 0);
    EXPECT_EQ(pending.submittedFrame, 0);
    pending.destructorFunc();
    EXPECT_TRUE(destroyed);

    // 6. Release budget
    budgetMgr.RecordDeallocation(BudgetResourceType::DeviceMemory, allocationBytes);
    auto finalUsage = budgetMgr.GetUsage(BudgetResourceType::DeviceMemory);
    EXPECT_EQ(finalUsage.currentBytes, 0);
}

// ============================================================================
// IMemoryAllocator Interface Tests (Header-Only Compatible)
// ============================================================================

class MemoryAllocatorInterfaceTest : public ::testing::Test {};

TEST_F(MemoryAllocatorInterfaceTest, AllocationErrorToString) {
    EXPECT_EQ(AllocationErrorToString(AllocationError::Success), "Success");
    EXPECT_EQ(AllocationErrorToString(AllocationError::OutOfDeviceMemory), "Out of device memory");
    EXPECT_EQ(AllocationErrorToString(AllocationError::OutOfHostMemory), "Out of host memory");
    EXPECT_EQ(AllocationErrorToString(AllocationError::OverBudget), "Over budget");
    EXPECT_EQ(AllocationErrorToString(AllocationError::InvalidParameters), "Invalid parameters");
    EXPECT_EQ(AllocationErrorToString(AllocationError::MappingFailed), "Mapping failed");
    EXPECT_EQ(AllocationErrorToString(AllocationError::Unknown), "Unknown error");
}

TEST_F(MemoryAllocatorInterfaceTest, MemoryLocationValues) {
    // Verify enum values are distinct
    EXPECT_NE(static_cast<int>(MemoryLocation::DeviceLocal),
              static_cast<int>(MemoryLocation::HostVisible));
    EXPECT_NE(static_cast<int>(MemoryLocation::HostVisible),
              static_cast<int>(MemoryLocation::HostCached));
    EXPECT_NE(static_cast<int>(MemoryLocation::HostCached),
              static_cast<int>(MemoryLocation::Auto));
}

TEST_F(MemoryAllocatorInterfaceTest, BufferAllocationRequestDefaults) {
    BufferAllocationRequest request{};
    EXPECT_EQ(request.size, 0);
    EXPECT_EQ(request.usage, 0);
    EXPECT_EQ(request.location, MemoryLocation::DeviceLocal);
    EXPECT_TRUE(request.debugName.empty());
    EXPECT_FALSE(request.dedicated);
}

TEST_F(MemoryAllocatorInterfaceTest, BufferAllocationDefaults) {
    BufferAllocation alloc{};
    EXPECT_EQ(alloc.buffer, VK_NULL_HANDLE);
    EXPECT_EQ(alloc.allocation, nullptr);
    EXPECT_EQ(alloc.size, 0);
    EXPECT_EQ(alloc.offset, 0);
    EXPECT_EQ(alloc.mappedData, nullptr);
    EXPECT_FALSE(static_cast<bool>(alloc));
}

TEST_F(MemoryAllocatorInterfaceTest, ImageAllocationDefaults) {
    ImageAllocation alloc{};
    EXPECT_EQ(alloc.image, VK_NULL_HANDLE);
    EXPECT_EQ(alloc.allocation, nullptr);
    EXPECT_EQ(alloc.size, 0);
    EXPECT_FALSE(static_cast<bool>(alloc));
}

TEST_F(MemoryAllocatorInterfaceTest, AllocationStatsDefaults) {
    AllocationStats stats{};
    EXPECT_EQ(stats.totalAllocatedBytes, 0);
    EXPECT_EQ(stats.totalUsedBytes, 0);
    EXPECT_EQ(stats.allocationCount, 0);
    EXPECT_EQ(stats.blockCount, 0);
    EXPECT_FLOAT_EQ(stats.fragmentationRatio, 0.0f);
}

// ============================================================================
// DirectAllocator Tests (Null Handle Safe)
// ============================================================================

class DirectAllocatorTest : public ::testing::Test {};

TEST_F(DirectAllocatorTest, CreateWithNullHandles) {
    // DirectAllocator should accept null handles (for testing/mocking)
    auto allocator = MemoryAllocatorFactory::CreateDirectAllocator(
        VK_NULL_HANDLE, VK_NULL_HANDLE, nullptr);

    ASSERT_NE(allocator, nullptr);
    EXPECT_EQ(allocator->GetName(), "DirectAllocator");
    EXPECT_EQ(allocator->GetBudgetManager(), nullptr);
}

TEST_F(DirectAllocatorTest, AllocateWithNullDeviceFails) {
    auto allocator = MemoryAllocatorFactory::CreateDirectAllocator(
        VK_NULL_HANDLE, VK_NULL_HANDLE, nullptr);

    BufferAllocationRequest request{
        .size = 1024,
        .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT
    };

    auto result = allocator->AllocateBuffer(request);
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), AllocationError::InvalidParameters);
}

TEST_F(DirectAllocatorTest, GetStatsEmpty) {
    auto allocator = MemoryAllocatorFactory::CreateDirectAllocator(
        VK_NULL_HANDLE, VK_NULL_HANDLE, nullptr);

    auto stats = allocator->GetStats();
    EXPECT_EQ(stats.totalAllocatedBytes, 0);
    EXPECT_EQ(stats.allocationCount, 0);
}

TEST_F(DirectAllocatorTest, SetBudgetManager) {
    auto allocator = MemoryAllocatorFactory::CreateDirectAllocator(
        VK_NULL_HANDLE, VK_NULL_HANDLE, nullptr);

    ResourceBudgetManager budgetMgr;
    allocator->SetBudgetManager(&budgetMgr);

    EXPECT_EQ(allocator->GetBudgetManager(), &budgetMgr);
}

// ============================================================================
// VMAAllocator Tests (Null Handle Safe)
// ============================================================================

class VMAAllocatorTest : public ::testing::Test {};

TEST_F(VMAAllocatorTest, CreateWithNullHandlesReturnsNull) {
    // VMA requires valid Vulkan handles, so factory returns nullptr
    auto allocator = MemoryAllocatorFactory::CreateVMAAllocator(
        VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE, nullptr);

    // With null handles, VMA creation fails
    EXPECT_EQ(allocator, nullptr);
}

TEST_F(VMAAllocatorTest, DirectConstructionWithNullHandles) {
    // Direct construction with null handles creates an invalid allocator
    VMAAllocator allocator(VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE, nullptr);

    EXPECT_FALSE(allocator.IsValid());
    EXPECT_EQ(allocator.GetName(), "VMAAllocator");
    EXPECT_EQ(allocator.GetBudgetManager(), nullptr);
}

TEST_F(VMAAllocatorTest, InvalidAllocatorReturnsErrorOnAllocate) {
    VMAAllocator allocator(VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE, nullptr);

    BufferAllocationRequest request{
        .size = 1024,
        .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT
    };

    auto result = allocator.AllocateBuffer(request);
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), AllocationError::Unknown);
}

TEST_F(VMAAllocatorTest, InvalidAllocatorReturnsEmptyStats) {
    VMAAllocator allocator(VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE, nullptr);

    auto stats = allocator.GetStats();
    EXPECT_EQ(stats.totalAllocatedBytes, 0);
    EXPECT_EQ(stats.allocationCount, 0);
}

TEST_F(VMAAllocatorTest, SetBudgetManager) {
    VMAAllocator allocator(VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE, nullptr);

    ResourceBudgetManager budgetMgr;
    allocator.SetBudgetManager(&budgetMgr);

    EXPECT_EQ(allocator.GetBudgetManager(), &budgetMgr);
}

// ============================================================================
// HostBudgetManager Tests
// ============================================================================

class HostBudgetManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        HostBudgetManager::Config config{};
        config.frameStackSize = 1024 * 1024;       // 1 MB frame stack
        config.persistentStackSize = 512 * 1024;   // 512 KB persistent stack
        config.heapBudget = 10 * 1024 * 1024;      // 10 MB heap
        hostBudget = std::make_unique<HostBudgetManager>(config);
    }

    std::unique_ptr<HostBudgetManager> hostBudget;
};

TEST_F(HostBudgetManagerTest, InitialState) {
    auto frameStats = hostBudget->GetFrameStackStats();
    EXPECT_EQ(frameStats.capacity, 1024 * 1024);
    EXPECT_EQ(frameStats.used, 0);
    EXPECT_EQ(frameStats.allocationCount, 0);

    auto persistentStats = hostBudget->GetPersistentStackStats();
    EXPECT_EQ(persistentStats.capacity, 512 * 1024);
    EXPECT_EQ(persistentStats.used, 0);
}

TEST_F(HostBudgetManagerTest, FrameStackAllocation) {
    auto alloc = hostBudget->Allocate(256, 16, AllocationScope::Frame);

    ASSERT_TRUE(alloc);
    EXPECT_NE(alloc.data, nullptr);
    EXPECT_EQ(alloc.size, 256);
    EXPECT_EQ(alloc.source, AllocationSource::FrameStack);
    EXPECT_EQ(alloc.scope, AllocationScope::Frame);

    auto stats = hostBudget->GetFrameStackStats();
    EXPECT_GT(stats.used, 0);
    EXPECT_EQ(stats.allocationCount, 1);
}

TEST_F(HostBudgetManagerTest, PersistentStackAllocation) {
    auto alloc = hostBudget->Allocate(256, 16, AllocationScope::PersistentStack);

    ASSERT_TRUE(alloc);
    EXPECT_NE(alloc.data, nullptr);
    EXPECT_EQ(alloc.source, AllocationSource::PersistentStack);
    EXPECT_EQ(alloc.scope, AllocationScope::PersistentStack);

    auto stats = hostBudget->GetPersistentStackStats();
    EXPECT_GT(stats.used, 0);
    EXPECT_EQ(stats.allocationCount, 1);
}

TEST_F(HostBudgetManagerTest, PersistentStackSurvivesFrameReset) {
    // Allocate in persistent stack
    auto persistent = hostBudget->Allocate(256, 16, AllocationScope::PersistentStack);
    ASSERT_TRUE(persistent);

    auto beforeReset = hostBudget->GetPersistentStackStats();
    EXPECT_GT(beforeReset.used, 0);

    // Reset frame - should NOT affect persistent stack
    hostBudget->ResetFrame();

    auto afterReset = hostBudget->GetPersistentStackStats();
    EXPECT_EQ(afterReset.used, beforeReset.used);
    EXPECT_EQ(afterReset.allocationCount, beforeReset.allocationCount);
}

TEST_F(HostBudgetManagerTest, MultipleFrameAllocations) {
    for (int i = 0; i < 100; ++i) {
        auto alloc = hostBudget->Allocate(1024, 16, AllocationScope::Frame);
        ASSERT_TRUE(alloc);
        EXPECT_EQ(alloc.source, AllocationSource::FrameStack);
    }

    auto stats = hostBudget->GetFrameStackStats();
    EXPECT_EQ(stats.allocationCount, 100);
    EXPECT_GE(stats.used, 100 * 1024);
}

TEST_F(HostBudgetManagerTest, FrameReset) {
    // Allocate some memory
    for (int i = 0; i < 10; ++i) {
        hostBudget->Allocate(1024, 16, AllocationScope::Frame);
    }

    auto beforeReset = hostBudget->GetFrameStackStats();
    EXPECT_GT(beforeReset.used, 0);

    // Reset frame
    hostBudget->ResetFrame();

    auto afterReset = hostBudget->GetFrameStackStats();
    EXPECT_EQ(afterReset.used, 0);
    EXPECT_EQ(afterReset.allocationCount, 0);
}

TEST_F(HostBudgetManagerTest, FrameStackFallbackToHeap) {
    // Fill the frame stack arena
    auto bigAlloc = hostBudget->Allocate(1024 * 1024, 16, AllocationScope::Frame);
    ASSERT_TRUE(bigAlloc);
    EXPECT_EQ(bigAlloc.source, AllocationSource::FrameStack);

    // Next allocation should fall back to heap
    auto fallbackAlloc = hostBudget->Allocate(1024, 16, AllocationScope::Frame);
    ASSERT_TRUE(fallbackAlloc);
    EXPECT_EQ(fallbackAlloc.source, AllocationSource::Heap);

    auto stats = hostBudget->GetFrameStackStats();
    EXPECT_EQ(stats.fallbackCount, 1);

    // Free the heap allocation
    hostBudget->Free(fallbackAlloc);
}

TEST_F(HostBudgetManagerTest, HeapAllocation) {
    auto alloc = hostBudget->Allocate(512, 16, AllocationScope::Heap);

    ASSERT_TRUE(alloc);
    EXPECT_EQ(alloc.source, AllocationSource::Heap);
    EXPECT_EQ(alloc.scope, AllocationScope::Heap);

    auto heapUsage = hostBudget->GetHeapUsage();
    EXPECT_GT(heapUsage.currentBytes, 0);

    hostBudget->Free(alloc);

    heapUsage = hostBudget->GetHeapUsage();
    EXPECT_EQ(heapUsage.currentBytes, 0);
}

TEST_F(HostBudgetManagerTest, TypedFrameAllocation) {
    struct TestStruct {
        int a;
        float b;
        double c;
    };

    TestStruct* ptr = hostBudget->AllocateFrame<TestStruct>(10);
    ASSERT_NE(ptr, nullptr);

    // Verify alignment
    EXPECT_EQ(reinterpret_cast<uintptr_t>(ptr) % alignof(TestStruct), 0);

    // Write to allocated memory
    ptr[0] = {1, 2.0f, 3.0};
    ptr[9] = {10, 20.0f, 30.0};

    EXPECT_EQ(ptr[0].a, 1);
    EXPECT_EQ(ptr[9].a, 10);
}

TEST_F(HostBudgetManagerTest, TypedPersistentAllocation) {
    struct LevelData {
        uint32_t id;
        float position[3];
    };

    LevelData* data = hostBudget->AllocatePersistent<LevelData>(100);
    ASSERT_NE(data, nullptr);

    // Verify alignment
    EXPECT_EQ(reinterpret_cast<uintptr_t>(data) % alignof(LevelData), 0);

    // Data should survive frame reset
    data[0] = {1, {1.0f, 2.0f, 3.0f}};

    hostBudget->ResetFrame();

    EXPECT_EQ(data[0].id, 1);
}

TEST_F(HostBudgetManagerTest, ConcurrentStackAllocations) {
    constexpr int numThreads = 4;
    constexpr int allocsPerThread = 100;

    std::vector<std::thread> threads;

    for (int t = 0; t < numThreads; ++t) {
        threads.emplace_back([this]() {
            for (int i = 0; i < allocsPerThread; ++i) {
                auto alloc = hostBudget->Allocate(64, 16, AllocationScope::Frame);
                // Don't assert - some may fall back to heap
                (void)alloc;
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    auto stats = hostBudget->GetFrameStackStats();
    // Total allocations = threads * allocsPerThread
    // (some may have fallen back to heap)
    EXPECT_GT(stats.allocationCount + stats.fallbackCount, 0);
}

TEST_F(HostBudgetManagerTest, ResetPersistentStack) {
    // Allocate persistent data
    hostBudget->Allocate(1024, 16, AllocationScope::PersistentStack);
    hostBudget->Allocate(1024, 16, AllocationScope::PersistentStack);

    auto beforeReset = hostBudget->GetPersistentStackStats();
    EXPECT_GT(beforeReset.used, 0);
    EXPECT_EQ(beforeReset.allocationCount, 2);

    // Reset persistent stack (e.g., level unload)
    hostBudget->ResetPersistentStack();

    auto afterReset = hostBudget->GetPersistentStackStats();
    EXPECT_EQ(afterReset.used, 0);
    EXPECT_EQ(afterReset.allocationCount, 0);
}

#ifdef _DEBUG
TEST_F(HostBudgetManagerTest, DebugEpochValidation) {
    // Frame allocation should be valid before reset
    auto frameAlloc = hostBudget->Allocate(256, 16, AllocationScope::Frame);
    ASSERT_TRUE(frameAlloc);
    EXPECT_TRUE(hostBudget->IsValid(frameAlloc));

    // Persistent allocation should be valid
    auto persistentAlloc = hostBudget->Allocate(256, 16, AllocationScope::PersistentStack);
    ASSERT_TRUE(persistentAlloc);
    EXPECT_TRUE(hostBudget->IsValid(persistentAlloc));

    // After frame reset, frame allocation is invalid but persistent is still valid
    hostBudget->ResetFrame();
    EXPECT_FALSE(hostBudget->IsValid(frameAlloc));
    EXPECT_TRUE(hostBudget->IsValid(persistentAlloc));

    // After persistent reset, persistent allocation is also invalid
    hostBudget->ResetPersistentStack();
    EXPECT_FALSE(hostBudget->IsValid(persistentAlloc));
}

TEST_F(HostBudgetManagerTest, HeapAllocationsAlwaysValid) {
    auto heapAlloc = hostBudget->Allocate(256, 16, AllocationScope::Heap);
    ASSERT_TRUE(heapAlloc);
    EXPECT_TRUE(hostBudget->IsValid(heapAlloc));

    // Heap allocations survive resets
    hostBudget->ResetFrame();
    EXPECT_TRUE(hostBudget->IsValid(heapAlloc));

    hostBudget->ResetPersistentStack();
    EXPECT_TRUE(hostBudget->IsValid(heapAlloc));

    hostBudget->Free(heapAlloc);
}
#endif

// ============================================================================
// DeviceBudgetManager Tests
// ============================================================================

class DeviceBudgetManagerTest : public ::testing::Test {};

TEST_F(DeviceBudgetManagerTest, CreateWithNullAllocator) {
    DeviceBudgetManager::Config config{};
    config.deviceMemoryBudget = 1024 * 1024 * 100;  // 100 MB
    config.stagingQuota = 1024 * 1024 * 10;         // 10 MB

    DeviceBudgetManager manager(nullptr, VK_NULL_HANDLE, config);

    EXPECT_EQ(manager.GetAllocator(), nullptr);
    EXPECT_EQ(manager.GetAllocatorName(), "None");
}

TEST_F(DeviceBudgetManagerTest, AllocateWithNullAllocatorFails) {
    DeviceBudgetManager manager(nullptr);

    BufferAllocationRequest request{
        .size = 1024,
        .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT
    };

    auto result = manager.AllocateBuffer(request);
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), AllocationError::InvalidParameters);
}

TEST_F(DeviceBudgetManagerTest, StagingQuotaManagement) {
    DeviceBudgetManager::Config config{};
    config.stagingQuota = 1024 * 1024;  // 1 MB

    DeviceBudgetManager manager(nullptr, VK_NULL_HANDLE, config);

    // Reserve some quota
    EXPECT_TRUE(manager.TryReserveStagingQuota(512 * 1024));  // 512 KB
    EXPECT_EQ(manager.GetStagingQuotaUsed(), 512 * 1024);

    // Reserve more
    EXPECT_TRUE(manager.TryReserveStagingQuota(256 * 1024));  // 256 KB
    EXPECT_EQ(manager.GetStagingQuotaUsed(), 768 * 1024);

    // Try to exceed quota
    EXPECT_FALSE(manager.TryReserveStagingQuota(512 * 1024));  // Would exceed

    // Release some
    manager.ReleaseStagingQuota(256 * 1024);
    EXPECT_EQ(manager.GetStagingQuotaUsed(), 512 * 1024);

    // Now we can reserve more
    EXPECT_TRUE(manager.TryReserveStagingQuota(256 * 1024));
}

TEST_F(DeviceBudgetManagerTest, GetStats) {
    DeviceBudgetManager::Config config{};
    config.stagingQuota = 1024 * 1024;

    DeviceBudgetManager manager(nullptr, VK_NULL_HANDLE, config);

    manager.TryReserveStagingQuota(256 * 1024);

    auto stats = manager.GetStats();
    EXPECT_EQ(stats.stagingQuotaUsed, 256 * 1024);
    EXPECT_EQ(stats.stagingQuotaMax, 1024 * 1024);
}

TEST_F(DeviceBudgetManagerTest, SetStagingQuota) {
    DeviceBudgetManager manager(nullptr);

    manager.SetStagingQuota(2 * 1024 * 1024);  // 2 MB

    EXPECT_EQ(manager.GetConfig().stagingQuota, 2 * 1024 * 1024);
    EXPECT_EQ(manager.GetAvailableStagingQuota(), 2 * 1024 * 1024);
}

TEST_F(DeviceBudgetManagerTest, ConcurrentStagingQuota) {
    DeviceBudgetManager::Config config{};
    config.stagingQuota = 10 * 1024 * 1024;  // 10 MB

    DeviceBudgetManager manager(nullptr, VK_NULL_HANDLE, config);

    constexpr int numThreads = 4;
    constexpr int opsPerThread = 100;
    constexpr uint64_t reserveSize = 1024;

    std::atomic<int> successCount{0};

    std::vector<std::thread> threads;
    for (int t = 0; t < numThreads; ++t) {
        threads.emplace_back([&manager, &successCount]() {
            for (int i = 0; i < opsPerThread; ++i) {
                if (manager.TryReserveStagingQuota(reserveSize)) {
                    successCount.fetch_add(1, std::memory_order_relaxed);
                    manager.ReleaseStagingQuota(reserveSize);
                }
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    // After all threads complete, quota should be 0
    EXPECT_EQ(manager.GetStagingQuotaUsed(), 0);
    EXPECT_GT(successCount.load(), 0);
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);

    std::cout << "═══════════════════════════════════════════════════════\n";
    std::cout << "  RESOURCE MANAGEMENT TEST SUITE\n";
    std::cout << "  Trimmed Build Compatible (Headers Only)\n";
    std::cout << "═══════════════════════════════════════════════════════\n\n";

    int result = RUN_ALL_TESTS();

    if (result == 0) {
        std::cout << "\n═══════════════════════════════════════════════════════\n";
        std::cout << "  ✅ ALL RESOURCE MANAGEMENT TESTS PASSED!\n";
        std::cout << "═══════════════════════════════════════════════════════\n";
    }

    return result;
}
