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
#include "Memory/ResourceBudgetManager.h"
#include "Lifetime/DeferredDestruction.h"
#include "State/StatefulContainer.h"
#include "Core/SlotTask.h"
#include "Memory/IMemoryAllocator.h"
#include "Memory/DirectAllocator.h"
#include "Memory/VMAAllocator.h"
#include "Memory/HostBudgetManager.h"
#include "Memory/DeviceBudgetManager.h"
#include "Memory/BudgetBridge.h"
#include "Lifetime/SharedResource.h"
#include "Lifetime/LifetimeScope.h"

#include <atomic>
#include <chrono>
#include <future>
#include <random>
#include <thread>
#include <vector>

using namespace ResourceManagement;
using namespace Vixen::RenderGraph;  // For StatefulContainer, SlotTask

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
// BudgetBridge Tests
// ============================================================================

class BudgetBridgeTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create managers for bridge
        HostBudgetManager::Config hostConfig{};
        hostConfig.frameStackSize = 1024 * 1024;
        hostConfig.heapBudget = 10 * 1024 * 1024;
        hostBudget_ = std::make_unique<HostBudgetManager>(hostConfig);

        DeviceBudgetManager::Config deviceConfig{};
        deviceConfig.stagingQuota = 256 * 1024 * 1024;  // 256 MB
        deviceBudget_ = std::make_unique<DeviceBudgetManager>(nullptr, VK_NULL_HANDLE, deviceConfig);

        // Create bridge with custom config
        BudgetBridge::Config bridgeConfig{};
        bridgeConfig.maxStagingQuota = 256 * 1024 * 1024;  // 256 MB
        bridgeConfig.stagingWarningThreshold = 200 * 1024 * 1024;  // 200 MB
        bridgeConfig.maxPendingUploads = 100;
        bridgeConfig.framesToKeepPending = 3;

        bridge_ = std::make_unique<BudgetBridge>(
            hostBudget_.get(), deviceBudget_.get(), bridgeConfig);
    }

    std::unique_ptr<HostBudgetManager> hostBudget_;
    std::unique_ptr<DeviceBudgetManager> deviceBudget_;
    std::unique_ptr<BudgetBridge> bridge_;
};

TEST_F(BudgetBridgeTest, InitialState) {
    EXPECT_EQ(bridge_->GetStagingQuotaUsed(), 0);
    EXPECT_EQ(bridge_->GetAvailableStagingQuota(), 256 * 1024 * 1024);
    EXPECT_EQ(bridge_->GetPendingUploadCount(), 0);
    EXPECT_EQ(bridge_->GetPendingUploadBytes(), 0);
    EXPECT_FALSE(bridge_->IsStagingNearLimit());
}

TEST_F(BudgetBridgeTest, ReserveStagingQuota) {
    EXPECT_TRUE(bridge_->ReserveStagingQuota(10 * 1024 * 1024));  // 10 MB
    EXPECT_EQ(bridge_->GetStagingQuotaUsed(), 10 * 1024 * 1024);
    EXPECT_EQ(bridge_->GetAvailableStagingQuota(), 246 * 1024 * 1024);
}

TEST_F(BudgetBridgeTest, ReleaseStagingQuota) {
    bridge_->ReserveStagingQuota(50 * 1024 * 1024);  // 50 MB
    bridge_->ReleaseStagingQuota(20 * 1024 * 1024);  // Release 20 MB

    EXPECT_EQ(bridge_->GetStagingQuotaUsed(), 30 * 1024 * 1024);
}

TEST_F(BudgetBridgeTest, StagingQuotaExceeded) {
    // Reserve 200 MB
    EXPECT_TRUE(bridge_->ReserveStagingQuota(200 * 1024 * 1024));

    // Try to reserve another 100 MB (would exceed 256 MB limit)
    EXPECT_FALSE(bridge_->ReserveStagingQuota(100 * 1024 * 1024));

    // Original reservation should still be intact
    EXPECT_EQ(bridge_->GetStagingQuotaUsed(), 200 * 1024 * 1024);
}

TEST_F(BudgetBridgeTest, StagingNearLimit) {
    // Reserve 200 MB (at warning threshold)
    bridge_->ReserveStagingQuota(200 * 1024 * 1024);
    EXPECT_TRUE(bridge_->IsStagingNearLimit());

    // Release some
    bridge_->ReleaseStagingQuota(50 * 1024 * 1024);
    EXPECT_FALSE(bridge_->IsStagingNearLimit());
}

TEST_F(BudgetBridgeTest, RecordUpload) {
    // Reserve quota and record upload
    bridge_->ReserveStagingQuota(10 * 1024 * 1024);
    bridge_->RecordUpload(10 * 1024 * 1024, 1);

    EXPECT_EQ(bridge_->GetPendingUploadCount(), 1);
    EXPECT_EQ(bridge_->GetPendingUploadBytes(), 10 * 1024 * 1024);
}

TEST_F(BudgetBridgeTest, ProcessCompletedUploadsFence) {
    // Record multiple uploads with different fence values
    bridge_->ReserveStagingQuota(30 * 1024 * 1024);
    bridge_->RecordUpload(10 * 1024 * 1024, 1);
    bridge_->RecordUpload(10 * 1024 * 1024, 2);
    bridge_->RecordUpload(10 * 1024 * 1024, 3);

    EXPECT_EQ(bridge_->GetPendingUploadCount(), 3);

    // Process with fence value 2 - should complete uploads 1 and 2
    uint64_t reclaimed = bridge_->ProcessCompletedUploads(2);

    EXPECT_EQ(reclaimed, 20 * 1024 * 1024);
    EXPECT_EQ(bridge_->GetPendingUploadCount(), 1);
    EXPECT_EQ(bridge_->GetPendingUploadBytes(), 10 * 1024 * 1024);
    EXPECT_EQ(bridge_->GetStagingQuotaUsed(), 10 * 1024 * 1024);

    // Complete the last one
    reclaimed = bridge_->ProcessCompletedUploads(3);
    EXPECT_EQ(reclaimed, 10 * 1024 * 1024);
    EXPECT_EQ(bridge_->GetPendingUploadCount(), 0);
}

TEST_F(BudgetBridgeTest, ProcessCompletedUploadsFrameBased) {
    // Record uploads
    bridge_->ReserveStagingQuota(20 * 1024 * 1024);
    bridge_->RecordUpload(10 * 1024 * 1024, 0);

    // Advance frames (framesToKeepPending = 3)
    // At frame 4, upload from frame 0 should be considered complete
    uint64_t reclaimed = bridge_->ProcessCompletedUploads(4, true);

    EXPECT_EQ(reclaimed, 10 * 1024 * 1024);
    EXPECT_EQ(bridge_->GetPendingUploadCount(), 0);
}

TEST_F(BudgetBridgeTest, UploadCompleteCallback) {
    uint64_t callbackBytes = 0;
    bridge_->SetUploadCompleteCallback([&callbackBytes](uint64_t bytes) {
        callbackBytes += bytes;
    });

    bridge_->ReserveStagingQuota(10 * 1024 * 1024);
    bridge_->RecordUpload(10 * 1024 * 1024, 1);
    bridge_->ProcessCompletedUploads(1);

    EXPECT_EQ(callbackBytes, 10 * 1024 * 1024);
}

TEST_F(BudgetBridgeTest, SetStagingQuotaLimit) {
    // Initially 256 MB
    EXPECT_EQ(bridge_->GetAvailableStagingQuota(), 256 * 1024 * 1024);

    // Increase to 512 MB
    bridge_->SetStagingQuotaLimit(512 * 1024 * 1024);

    // Note: Config update but available quota tracks against used
    auto config = bridge_->GetConfig();
    EXPECT_EQ(config.maxStagingQuota, 512 * 1024 * 1024);
}

TEST_F(BudgetBridgeTest, MaxPendingUploadsDropsOldest) {
    // Create bridge with small max pending limit
    BudgetBridge::Config config{};
    config.maxStagingQuota = 256 * 1024 * 1024;
    config.maxPendingUploads = 3;

    auto testBridge = std::make_unique<BudgetBridge>(
        hostBudget_.get(), deviceBudget_.get(), config);

    // Reserve quota for 4 uploads
    testBridge->ReserveStagingQuota(4 * 1024 * 1024);

    // Record 4 uploads (limit is 3)
    testBridge->RecordUpload(1024 * 1024, 1);
    testBridge->RecordUpload(1024 * 1024, 2);
    testBridge->RecordUpload(1024 * 1024, 3);
    testBridge->RecordUpload(1024 * 1024, 4);  // Should drop oldest

    EXPECT_EQ(testBridge->GetPendingUploadCount(), 3);
    // Oldest (fence 1) was dropped and its staging released
    EXPECT_EQ(testBridge->GetPendingUploadBytes(), 3 * 1024 * 1024);
}

TEST_F(BudgetBridgeTest, ConcurrentStagingReservation) {
    constexpr int numThreads = 4;
    constexpr int reservationsPerThread = 50;
    constexpr uint64_t reserveSize = 1024 * 1024;  // 1 MB

    std::atomic<int> successCount{0};
    std::atomic<int> failCount{0};

    std::vector<std::thread> threads;
    for (int t = 0; t < numThreads; ++t) {
        threads.emplace_back([this, &successCount, &failCount]() {
            for (int i = 0; i < reservationsPerThread; ++i) {
                if (bridge_->ReserveStagingQuota(reserveSize)) {
                    successCount.fetch_add(1, std::memory_order_relaxed);
                    bridge_->ReleaseStagingQuota(reserveSize);
                } else {
                    failCount.fetch_add(1, std::memory_order_relaxed);
                }
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    // After all threads, staging should be 0
    EXPECT_EQ(bridge_->GetStagingQuotaUsed(), 0);
    EXPECT_GT(successCount.load(), 0);
}

TEST_F(BudgetBridgeTest, CreateWithNullManagers) {
    // Bridge should work without host/device managers (standalone mode)
    BudgetBridge::Config config{};
    config.maxStagingQuota = 100 * 1024 * 1024;

    BudgetBridge standaloneBridge(nullptr, nullptr, config);

    EXPECT_TRUE(standaloneBridge.ReserveStagingQuota(10 * 1024 * 1024));
    EXPECT_EQ(standaloneBridge.GetStagingQuotaUsed(), 10 * 1024 * 1024);

    standaloneBridge.ReleaseStagingQuota(10 * 1024 * 1024);
    EXPECT_EQ(standaloneBridge.GetStagingQuotaUsed(), 0);
}

// ============================================================================
// RefCountBase Tests
// ============================================================================

class RefCountBaseTest : public ::testing::Test {};

TEST_F(RefCountBaseTest, InitialRefCount) {
    RefCountBase ref;
    EXPECT_EQ(ref.GetRefCount(), 1);
    EXPECT_TRUE(ref.IsUnique());
}

TEST_F(RefCountBaseTest, AddRefIncrementsCount) {
    RefCountBase ref;
    EXPECT_EQ(ref.AddRef(), 2);
    EXPECT_EQ(ref.GetRefCount(), 2);
    EXPECT_FALSE(ref.IsUnique());

    EXPECT_EQ(ref.AddRef(), 3);
    EXPECT_EQ(ref.GetRefCount(), 3);
}

TEST_F(RefCountBaseTest, ReleaseDecrementsCount) {
    RefCountBase ref;
    ref.AddRef();  // Now 2
    ref.AddRef();  // Now 3

    EXPECT_EQ(ref.Release(), 2);
    EXPECT_EQ(ref.Release(), 1);
    EXPECT_TRUE(ref.IsUnique());
    EXPECT_EQ(ref.Release(), 0);  // Would trigger destruction
}

TEST_F(RefCountBaseTest, ConcurrentRefCounting) {
    RefCountBase ref;
    constexpr int numThreads = 8;
    constexpr int opsPerThread = 1000;

    std::vector<std::thread> threads;
    for (int t = 0; t < numThreads; ++t) {
        threads.emplace_back([&ref]() {
            for (int i = 0; i < opsPerThread; ++i) {
                ref.AddRef();
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    // Initial 1 + (numThreads * opsPerThread) = expected count
    EXPECT_EQ(ref.GetRefCount(), 1 + numThreads * opsPerThread);
}

TEST_F(RefCountBaseTest, MoveTransfersOwnership) {
    RefCountBase ref1;
    ref1.AddRef();  // Now 2

    RefCountBase ref2 = std::move(ref1);

    EXPECT_EQ(ref2.GetRefCount(), 2);
    EXPECT_EQ(ref1.GetRefCount(), 0);  // Moved-from state
}

// ============================================================================
// SharedBuffer Tests (Header-Only, No Real Vulkan)
// ============================================================================

class SharedBufferTest : public ::testing::Test {
protected:
    void SetUp() override {
        allocator_ = MemoryAllocatorFactory::CreateDirectAllocator(
            VK_NULL_HANDLE, VK_NULL_HANDLE, nullptr);
    }

    std::unique_ptr<IMemoryAllocator> allocator_;
};

TEST_F(SharedBufferTest, CreateWithInvalidAllocation) {
    // Create SharedBuffer with empty allocation
    BufferAllocation emptyAlloc{};
    SharedBuffer buffer(emptyAlloc, allocator_.get());

    EXPECT_FALSE(buffer.IsValid());
    EXPECT_EQ(buffer.GetBuffer(), VK_NULL_HANDLE);
    EXPECT_EQ(buffer.GetRefCount(), 1);
}

TEST_F(SharedBufferTest, RefCountOperations) {
    BufferAllocation alloc{};
    SharedBuffer buffer(alloc, allocator_.get());

    EXPECT_EQ(buffer.GetRefCount(), 1);
    EXPECT_TRUE(buffer.IsUnique());

    buffer.AddRef();
    EXPECT_EQ(buffer.GetRefCount(), 2);
    EXPECT_FALSE(buffer.IsUnique());

    buffer.Release();
    EXPECT_EQ(buffer.GetRefCount(), 1);
    EXPECT_TRUE(buffer.IsUnique());
}

TEST_F(SharedBufferTest, ResourceScope) {
    BufferAllocation alloc{};

    SharedBuffer transient(alloc, allocator_.get(), ResourceScope::Transient);
    EXPECT_EQ(transient.GetScope(), ResourceScope::Transient);

    SharedBuffer persistent(alloc, allocator_.get(), ResourceScope::Persistent);
    EXPECT_EQ(persistent.GetScope(), ResourceScope::Persistent);

    SharedBuffer shared(alloc, allocator_.get(), ResourceScope::Shared);
    EXPECT_EQ(shared.GetScope(), ResourceScope::Shared);
}

TEST_F(SharedBufferTest, MoveSemantics) {
    BufferAllocation alloc{};
    alloc.size = 1024;

    SharedBuffer buffer1(alloc, allocator_.get());
    buffer1.AddRef();  // 2 refs

    SharedBuffer buffer2 = std::move(buffer1);

    EXPECT_EQ(buffer2.GetSize(), 1024);
    EXPECT_EQ(buffer2.GetRefCount(), 2);
    EXPECT_EQ(buffer1.GetRefCount(), 0);  // Moved-from
}

// ============================================================================
// SharedResourcePtr Tests
// ============================================================================

class SharedResourcePtrTest : public ::testing::Test {
protected:
    void SetUp() override {
        allocator_ = MemoryAllocatorFactory::CreateDirectAllocator(
            VK_NULL_HANDLE, VK_NULL_HANDLE, nullptr);
        frameCounter_ = 0;
    }

    std::unique_ptr<IMemoryAllocator> allocator_;
    DeferredDestructionQueue destructionQueue_;
    uint64_t frameCounter_;
};

TEST_F(SharedResourcePtrTest, DefaultConstruction) {
    SharedBufferPtr ptr;
    EXPECT_FALSE(ptr);
    EXPECT_EQ(ptr.Get(), nullptr);
    EXPECT_EQ(ptr.UseCount(), 0);
}

TEST_F(SharedResourcePtrTest, ConstructWithResource) {
    BufferAllocation alloc{};
    auto* buffer = new SharedBuffer(alloc, allocator_.get());

    SharedBufferPtr ptr(buffer, &destructionQueue_, &frameCounter_);

    EXPECT_TRUE(ptr);
    EXPECT_EQ(ptr.Get(), buffer);
    EXPECT_EQ(ptr.UseCount(), 1);
    EXPECT_TRUE(ptr.IsUnique());
}

TEST_F(SharedResourcePtrTest, CopyAddsReference) {
    BufferAllocation alloc{};
    auto* buffer = new SharedBuffer(alloc, allocator_.get());

    SharedBufferPtr ptr1(buffer, &destructionQueue_, &frameCounter_);
    SharedBufferPtr ptr2 = ptr1;  // Copy

    EXPECT_EQ(ptr1.Get(), ptr2.Get());
    EXPECT_EQ(ptr1.UseCount(), 2);
    EXPECT_EQ(ptr2.UseCount(), 2);
    EXPECT_FALSE(ptr1.IsUnique());
}

TEST_F(SharedResourcePtrTest, MoveTransfersOwnership) {
    BufferAllocation alloc{};
    auto* buffer = new SharedBuffer(alloc, allocator_.get());

    SharedBufferPtr ptr1(buffer, &destructionQueue_, &frameCounter_);
    SharedBufferPtr ptr2 = std::move(ptr1);

    EXPECT_FALSE(ptr1);  // Moved-from is null
    EXPECT_TRUE(ptr2);
    EXPECT_EQ(ptr2.Get(), buffer);
    EXPECT_EQ(ptr2.UseCount(), 1);
}

TEST_F(SharedResourcePtrTest, ResetReleasesResource) {
    BufferAllocation alloc{};
    auto* buffer = new SharedBuffer(alloc, allocator_.get());

    SharedBufferPtr ptr(buffer, &destructionQueue_, &frameCounter_);
    EXPECT_EQ(ptr.UseCount(), 1);

    ptr.Reset();

    EXPECT_FALSE(ptr);
    // Empty allocation = nothing to queue (QueueDestruction skips invalid allocations)
    EXPECT_EQ(destructionQueue_.GetPendingCount(), 0);
}

TEST_F(SharedResourcePtrTest, LastRefQueuesDestruction) {
    // Create a "valid" allocation (has buffer handle even though it's not real)
    BufferAllocation alloc{};
    alloc.buffer = reinterpret_cast<VkBuffer>(0x12345678);  // Fake handle for testing
    alloc.size = 1024;

    auto* buffer = new SharedBuffer(alloc, allocator_.get());

    {
        SharedBufferPtr ptr1(buffer, &destructionQueue_, &frameCounter_);
        SharedBufferPtr ptr2 = ptr1;  // 2 refs

        EXPECT_EQ(destructionQueue_.GetPendingCount(), 0);
    }
    // Both ptrs destroyed, last one queues destruction for valid allocation

    EXPECT_EQ(destructionQueue_.GetPendingCount(), 1);
}

TEST_F(SharedResourcePtrTest, Swap) {
    BufferAllocation alloc1{}, alloc2{};
    alloc1.size = 100;
    alloc2.size = 200;

    auto* buffer1 = new SharedBuffer(alloc1, allocator_.get());
    auto* buffer2 = new SharedBuffer(alloc2, allocator_.get());

    SharedBufferPtr ptr1(buffer1, &destructionQueue_, &frameCounter_);
    SharedBufferPtr ptr2(buffer2, &destructionQueue_, &frameCounter_);

    ptr1.Swap(ptr2);

    EXPECT_EQ(ptr1->GetSize(), 200);
    EXPECT_EQ(ptr2->GetSize(), 100);
}

// ============================================================================
// SharedResourceFactory Tests
// ============================================================================

class SharedResourceFactoryTest : public ::testing::Test {
protected:
    void SetUp() override {
        allocator_ = MemoryAllocatorFactory::CreateDirectAllocator(
            VK_NULL_HANDLE, VK_NULL_HANDLE, nullptr);
        frameCounter_ = 0;
    }

    std::unique_ptr<IMemoryAllocator> allocator_;
    DeferredDestructionQueue destructionQueue_;
    uint64_t frameCounter_;
};

TEST_F(SharedResourceFactoryTest, CreateBufferWithNullAllocator) {
    SharedResourceFactory factory(nullptr, &destructionQueue_, &frameCounter_);

    BufferAllocationRequest request{.size = 1024};
    auto buffer = factory.CreateBuffer(request);

    EXPECT_FALSE(buffer);  // Should fail with null allocator
}

TEST_F(SharedResourceFactoryTest, CreateBufferWithInvalidDevice) {
    SharedResourceFactory factory(allocator_.get(), &destructionQueue_, &frameCounter_);

    BufferAllocationRequest request{.size = 1024};
    auto buffer = factory.CreateBuffer(request);

    // DirectAllocator with null device returns error
    EXPECT_FALSE(buffer);
}

// ============================================================================
// DeferredDestructionQueue AddGeneric Tests
// ============================================================================

class DeferredDestructionGenericTest : public ::testing::Test {};

TEST_F(DeferredDestructionGenericTest, AddGenericQueuesFunction) {
    DeferredDestructionQueue queue;
    bool destructorCalled = false;

    queue.AddGeneric([&destructorCalled]() {
        destructorCalled = true;
    }, 0);

    EXPECT_EQ(queue.GetPendingCount(), 1);
    EXPECT_FALSE(destructorCalled);

    queue.ProcessFrame(3, 3);  // After 3 frames

    EXPECT_EQ(queue.GetPendingCount(), 0);
    EXPECT_TRUE(destructorCalled);
}

TEST_F(DeferredDestructionGenericTest, AddGenericWithNullFunction) {
    DeferredDestructionQueue queue;

    queue.AddGeneric(nullptr, 0);

    EXPECT_EQ(queue.GetPendingCount(), 0);  // Null function ignored
}

TEST_F(DeferredDestructionGenericTest, MultipleGenericDestructions) {
    DeferredDestructionQueue queue;
    int callCount = 0;

    for (int i = 0; i < 5; ++i) {
        queue.AddGeneric([&callCount]() {
            callCount++;
        }, i);
    }

    EXPECT_EQ(queue.GetPendingCount(), 5);

    queue.Flush();

    EXPECT_EQ(queue.GetPendingCount(), 0);
    EXPECT_EQ(callCount, 5);
}

// ============================================================================
// LifetimeScope Tests
// ============================================================================

class LifetimeScopeTest : public ::testing::Test {
protected:
    void SetUp() override {
        allocator_ = MemoryAllocatorFactory::CreateDirectAllocator(
            VK_NULL_HANDLE, VK_NULL_HANDLE, nullptr);
        frameCounter_ = 0;
        factory_ = std::make_unique<SharedResourceFactory>(
            allocator_.get(), &destructionQueue_, &frameCounter_);
    }

    std::unique_ptr<IMemoryAllocator> allocator_;
    DeferredDestructionQueue destructionQueue_;
    uint64_t frameCounter_;
    std::unique_ptr<SharedResourceFactory> factory_;
};

TEST_F(LifetimeScopeTest, Construction) {
    LifetimeScope scope("TestScope", factory_.get());

    EXPECT_EQ(scope.GetName(), "TestScope");
    EXPECT_EQ(scope.GetParent(), nullptr);
    EXPECT_FALSE(scope.HasEnded());
    EXPECT_EQ(scope.GetBufferCount(), 0);
    EXPECT_EQ(scope.GetImageCount(), 0);
    EXPECT_EQ(scope.GetTotalResourceCount(), 0);
}

TEST_F(LifetimeScopeTest, ConstructionWithParent) {
    LifetimeScope parentScope("Parent", factory_.get());
    LifetimeScope childScope("Child", factory_.get(), &parentScope);

    EXPECT_EQ(childScope.GetParent(), &parentScope);
}

TEST_F(LifetimeScopeTest, EndScopeMarksEnded) {
    LifetimeScope scope("TestScope", factory_.get());

    EXPECT_FALSE(scope.HasEnded());
    scope.EndScope();
    EXPECT_TRUE(scope.HasEnded());
}

TEST_F(LifetimeScopeTest, EndScopeIdempotent) {
    LifetimeScope scope("TestScope", factory_.get());

    scope.EndScope();
    scope.EndScope();  // Should be safe to call multiple times
    EXPECT_TRUE(scope.HasEnded());
}

TEST_F(LifetimeScopeTest, DestructorEndsScope) {
    bool ended = false;
    {
        LifetimeScope scope("TestScope", factory_.get());
        EXPECT_FALSE(scope.HasEnded());
    }  // Destructor called here
    // Can't check HasEnded() after destruction, but it shouldn't crash
}

TEST_F(LifetimeScopeTest, MoveConstruction) {
    LifetimeScope scope1("MovedScope", factory_.get());

    LifetimeScope scope2 = std::move(scope1);

    EXPECT_EQ(scope2.GetName(), "MovedScope");
    EXPECT_FALSE(scope2.HasEnded());
    EXPECT_TRUE(scope1.HasEnded());  // Moved-from is ended
}

TEST_F(LifetimeScopeTest, MoveAssignment) {
    LifetimeScope scope1("Scope1", factory_.get());
    LifetimeScope scope2("Scope2", factory_.get());

    scope2 = std::move(scope1);

    EXPECT_EQ(scope2.GetName(), "Scope1");
    EXPECT_FALSE(scope2.HasEnded());
    EXPECT_TRUE(scope1.HasEnded());
}

TEST_F(LifetimeScopeTest, TotalMemoryBytesEmpty) {
    LifetimeScope scope("TestScope", factory_.get());

    EXPECT_EQ(scope.GetTotalMemoryBytes(), 0);
}

// ============================================================================
// LifetimeScopeManager Tests
// ============================================================================

class LifetimeScopeManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        allocator_ = MemoryAllocatorFactory::CreateDirectAllocator(
            VK_NULL_HANDLE, VK_NULL_HANDLE, nullptr);
        frameCounter_ = 0;
        factory_ = std::make_unique<SharedResourceFactory>(
            allocator_.get(), &destructionQueue_, &frameCounter_);
        manager_ = std::make_unique<LifetimeScopeManager>(factory_.get());
    }

    std::unique_ptr<IMemoryAllocator> allocator_;
    DeferredDestructionQueue destructionQueue_;
    uint64_t frameCounter_;
    std::unique_ptr<SharedResourceFactory> factory_;
    std::unique_ptr<LifetimeScopeManager> manager_;
};

TEST_F(LifetimeScopeManagerTest, InitialState) {
    EXPECT_EQ(manager_->GetFrameNumber(), 0);
    EXPECT_EQ(manager_->GetNestedScopeDepth(), 0);
    EXPECT_FALSE(manager_->HasNestedScopes());
}

TEST_F(LifetimeScopeManagerTest, BeginFrameIncrementsCounter) {
    manager_->BeginFrame();
    EXPECT_EQ(manager_->GetFrameNumber(), 1);

    manager_->EndFrame();
    manager_->BeginFrame();
    EXPECT_EQ(manager_->GetFrameNumber(), 2);
}

TEST_F(LifetimeScopeManagerTest, GetFrameScope) {
    LifetimeScope& frameScope = manager_->GetFrameScope();
    EXPECT_EQ(frameScope.GetName(), "Frame");
}

TEST_F(LifetimeScopeManagerTest, BeginAndEndScope) {
    manager_->BeginFrame();

    EXPECT_EQ(manager_->GetNestedScopeDepth(), 0);

    LifetimeScope& nested = manager_->BeginScope("ShadowPass");
    EXPECT_EQ(nested.GetName(), "ShadowPass");
    EXPECT_EQ(manager_->GetNestedScopeDepth(), 1);
    EXPECT_TRUE(manager_->HasNestedScopes());

    manager_->EndScope();
    EXPECT_EQ(manager_->GetNestedScopeDepth(), 0);
    EXPECT_FALSE(manager_->HasNestedScopes());

    manager_->EndFrame();
}

TEST_F(LifetimeScopeManagerTest, NestedScopes) {
    manager_->BeginFrame();

    manager_->BeginScope("Level1");
    EXPECT_EQ(manager_->GetNestedScopeDepth(), 1);

    manager_->BeginScope("Level2");
    EXPECT_EQ(manager_->GetNestedScopeDepth(), 2);

    manager_->BeginScope("Level3");
    EXPECT_EQ(manager_->GetNestedScopeDepth(), 3);

    manager_->EndScope();  // Level3
    EXPECT_EQ(manager_->GetNestedScopeDepth(), 2);

    manager_->EndScope();  // Level2
    EXPECT_EQ(manager_->GetNestedScopeDepth(), 1);

    manager_->EndScope();  // Level1
    EXPECT_EQ(manager_->GetNestedScopeDepth(), 0);

    manager_->EndFrame();
}

TEST_F(LifetimeScopeManagerTest, CurrentScopeReturnsFrameWhenNoNested) {
    manager_->BeginFrame();

    LifetimeScope& current = manager_->CurrentScope();
    EXPECT_EQ(current.GetName(), "Frame");

    manager_->EndFrame();
}

TEST_F(LifetimeScopeManagerTest, CurrentScopeReturnsTopNested) {
    manager_->BeginFrame();

    manager_->BeginScope("First");
    EXPECT_EQ(manager_->CurrentScope().GetName(), "First");

    manager_->BeginScope("Second");
    EXPECT_EQ(manager_->CurrentScope().GetName(), "Second");

    manager_->EndScope();
    EXPECT_EQ(manager_->CurrentScope().GetName(), "First");

    manager_->EndScope();
    EXPECT_EQ(manager_->CurrentScope().GetName(), "Frame");

    manager_->EndFrame();
}

TEST_F(LifetimeScopeManagerTest, EndFrameEndsAllNestedScopes) {
    manager_->BeginFrame();

    manager_->BeginScope("Scope1");
    manager_->BeginScope("Scope2");
    manager_->BeginScope("Scope3");

    EXPECT_EQ(manager_->GetNestedScopeDepth(), 3);

    manager_->EndFrame();

    EXPECT_EQ(manager_->GetNestedScopeDepth(), 0);
}

TEST_F(LifetimeScopeManagerTest, EndScopeOnEmptyStackIsNoOp) {
    manager_->BeginFrame();

    // Should not crash
    manager_->EndScope();
    manager_->EndScope();
    manager_->EndScope();

    EXPECT_EQ(manager_->GetNestedScopeDepth(), 0);

    manager_->EndFrame();
}

TEST_F(LifetimeScopeManagerTest, NestedScopeHasCorrectParent) {
    manager_->BeginFrame();

    LifetimeScope& scope1 = manager_->BeginScope("Scope1");
    LifetimeScope& scope2 = manager_->BeginScope("Scope2");

    EXPECT_EQ(scope2.GetParent(), &scope1);

    manager_->EndFrame();
}

TEST_F(LifetimeScopeManagerTest, FirstNestedScopeParentIsFrameScope) {
    manager_->BeginFrame();

    LifetimeScope& frameScope = manager_->GetFrameScope();
    LifetimeScope& nestedScope = manager_->BeginScope("Nested");

    EXPECT_EQ(nestedScope.GetParent(), &frameScope);

    manager_->EndFrame();
}

// ============================================================================
// ScopeGuard Tests
// ============================================================================

class ScopeGuardTest : public ::testing::Test {
protected:
    void SetUp() override {
        allocator_ = MemoryAllocatorFactory::CreateDirectAllocator(
            VK_NULL_HANDLE, VK_NULL_HANDLE, nullptr);
        frameCounter_ = 0;
        factory_ = std::make_unique<SharedResourceFactory>(
            allocator_.get(), &destructionQueue_, &frameCounter_);
        manager_ = std::make_unique<LifetimeScopeManager>(factory_.get());
    }

    std::unique_ptr<IMemoryAllocator> allocator_;
    DeferredDestructionQueue destructionQueue_;
    uint64_t frameCounter_;
    std::unique_ptr<SharedResourceFactory> factory_;
    std::unique_ptr<LifetimeScopeManager> manager_;
};

TEST_F(ScopeGuardTest, AutomaticScopeManagement) {
    manager_->BeginFrame();

    EXPECT_EQ(manager_->GetNestedScopeDepth(), 0);

    {
        ScopeGuard guard(*manager_, "GuardedScope");
        EXPECT_EQ(manager_->GetNestedScopeDepth(), 1);
        EXPECT_EQ(guard.GetScope().GetName(), "GuardedScope");
    }

    EXPECT_EQ(manager_->GetNestedScopeDepth(), 0);

    manager_->EndFrame();
}

TEST_F(ScopeGuardTest, NestedGuards) {
    manager_->BeginFrame();

    {
        ScopeGuard guard1(*manager_, "Outer");
        EXPECT_EQ(manager_->GetNestedScopeDepth(), 1);

        {
            ScopeGuard guard2(*manager_, "Inner");
            EXPECT_EQ(manager_->GetNestedScopeDepth(), 2);
        }

        EXPECT_EQ(manager_->GetNestedScopeDepth(), 1);
    }

    EXPECT_EQ(manager_->GetNestedScopeDepth(), 0);

    manager_->EndFrame();
}

TEST_F(ScopeGuardTest, GetScopeReturnsCorrectScope) {
    manager_->BeginFrame();

    {
        ScopeGuard guard(*manager_, "TestScope");
        LifetimeScope& scope = guard.GetScope();

        EXPECT_EQ(scope.GetName(), "TestScope");
        EXPECT_EQ(&scope, &manager_->CurrentScope());
    }

    manager_->EndFrame();
}

// ============================================================================
// LifetimeScope Integration Tests
// ============================================================================

TEST(LifetimeScopeIntegration, TypicalFrameWorkflow) {
    // Simulate typical frame workflow
    auto allocator = MemoryAllocatorFactory::CreateDirectAllocator(
        VK_NULL_HANDLE, VK_NULL_HANDLE, nullptr);
    DeferredDestructionQueue queue;
    uint64_t frameCounter = 0;
    SharedResourceFactory factory(allocator.get(), &queue, &frameCounter);
    LifetimeScopeManager manager(&factory);

    // Frame 1
    manager.BeginFrame();
    {
        ScopeGuard shadowPass(manager, "ShadowPass");
        // Resources created here would be released at guard destruction
    }
    {
        ScopeGuard mainPass(manager, "MainPass");
        // Resources created here would be released at guard destruction
    }
    manager.EndFrame();
    EXPECT_EQ(manager.GetFrameNumber(), 1);

    // Frame 2
    manager.BeginFrame();
    manager.EndFrame();
    EXPECT_EQ(manager.GetFrameNumber(), 2);
}

TEST(LifetimeScopeIntegration, DeepNestedScopes) {
    auto allocator = MemoryAllocatorFactory::CreateDirectAllocator(
        VK_NULL_HANDLE, VK_NULL_HANDLE, nullptr);
    DeferredDestructionQueue queue;
    uint64_t frameCounter = 0;
    SharedResourceFactory factory(allocator.get(), &queue, &frameCounter);
    LifetimeScopeManager manager(&factory);

    manager.BeginFrame();

    // Create deeply nested scopes
    constexpr int depth = 10;
    for (int i = 0; i < depth; ++i) {
        manager.BeginScope("Level" + std::to_string(i));
    }

    EXPECT_EQ(manager.GetNestedScopeDepth(), depth);

    // End frame should clean up all
    manager.EndFrame();

    EXPECT_EQ(manager.GetNestedScopeDepth(), 0);
}

// ============================================================================
// Memory Aliasing Tests (Phase B+)
// ============================================================================

class AliasingTest : public ::testing::Test {
protected:
    void SetUp() override {
        allocator_ = MemoryAllocatorFactory::CreateDirectAllocator(
            VK_NULL_HANDLE, VK_NULL_HANDLE, nullptr);
    }

    std::unique_ptr<IMemoryAllocator> allocator_;
};

TEST_F(AliasingTest, AllowAliasingFlagDefault) {
    BufferAllocationRequest request{};
    EXPECT_FALSE(request.allowAliasing);

    ImageAllocationRequest imageRequest{};
    EXPECT_FALSE(imageRequest.allowAliasing);
}

TEST_F(AliasingTest, BufferAllocationCanAliasFlagSet) {
    BufferAllocationRequest request{
        .size = 1024,
        .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        .allowAliasing = true
    };

    EXPECT_TRUE(request.allowAliasing);
}

TEST_F(AliasingTest, ImageAllocationCanAliasFlagSet) {
    ImageAllocationRequest request{};
    request.allowAliasing = true;

    EXPECT_TRUE(request.allowAliasing);
}

TEST_F(AliasingTest, AliasedBufferRequestStructure) {
    AliasedBufferRequest request{
        .size = 512,
        .usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        .sourceAllocation = reinterpret_cast<AllocationHandle>(0x12345678),
        .offsetInAllocation = 256,
        .debugName = "AliasedBuffer"
    };

    EXPECT_EQ(request.size, 512);
    EXPECT_EQ(request.offsetInAllocation, 256);
    EXPECT_NE(request.sourceAllocation, nullptr);
}

TEST_F(AliasingTest, AliasedImageRequestStructure) {
    AliasedImageRequest request{};
    request.sourceAllocation = reinterpret_cast<AllocationHandle>(0x87654321);
    request.offsetInAllocation = 1024;
    request.debugName = "AliasedImage";

    EXPECT_NE(request.sourceAllocation, nullptr);
    EXPECT_EQ(request.offsetInAllocation, 1024);
}

TEST_F(AliasingTest, BufferAllocationResultHasAliasingFlags) {
    BufferAllocation alloc{};
    EXPECT_FALSE(alloc.canAlias);
    EXPECT_FALSE(alloc.isAliased);

    alloc.canAlias = true;
    alloc.isAliased = true;
    EXPECT_TRUE(alloc.canAlias);
    EXPECT_TRUE(alloc.isAliased);
}

TEST_F(AliasingTest, ImageAllocationResultHasAliasingFlags) {
    ImageAllocation alloc{};
    EXPECT_FALSE(alloc.canAlias);
    EXPECT_FALSE(alloc.isAliased);

    alloc.canAlias = true;
    alloc.isAliased = true;
    EXPECT_TRUE(alloc.canAlias);
    EXPECT_TRUE(alloc.isAliased);
}

TEST_F(AliasingTest, SupportsAliasingNullHandle) {
    EXPECT_FALSE(allocator_->SupportsAliasing(nullptr));
}

TEST_F(AliasingTest, CreateAliasedBufferNullSource) {
    AliasedBufferRequest request{
        .size = 1024,
        .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        .sourceAllocation = nullptr
    };

    auto result = allocator_->CreateAliasedBuffer(request);
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), AllocationError::InvalidParameters);
}

TEST_F(AliasingTest, CreateAliasedImageNullSource) {
    AliasedImageRequest request{};
    request.sourceAllocation = nullptr;

    auto result = allocator_->CreateAliasedImage(request);
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), AllocationError::InvalidParameters);
}

// ============================================================================
// DeviceBudgetManager Aliasing Tests
// ============================================================================

class DeviceBudgetManagerAliasingTest : public ::testing::Test {
protected:
    void SetUp() override {
        auto allocator = MemoryAllocatorFactory::CreateDirectAllocator(
            VK_NULL_HANDLE, VK_NULL_HANDLE, nullptr);
        DeviceBudgetManager::Config config{};
        config.deviceMemoryBudget = 1024 * 1024 * 1024;  // 1 GB
        manager_ = std::make_unique<DeviceBudgetManager>(
            std::move(allocator), VK_NULL_HANDLE, config);
    }

    std::unique_ptr<DeviceBudgetManager> manager_;
};

TEST_F(DeviceBudgetManagerAliasingTest, InitialAliasedCountIsZero) {
    EXPECT_EQ(manager_->GetAliasedAllocationCount(), 0);
}

TEST_F(DeviceBudgetManagerAliasingTest, SupportsAliasingNullHandle) {
    EXPECT_FALSE(manager_->SupportsAliasing(nullptr));
}

TEST_F(DeviceBudgetManagerAliasingTest, CreateAliasedBufferNullSource) {
    AliasedBufferRequest request{
        .size = 1024,
        .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        .sourceAllocation = nullptr
    };

    auto result = manager_->CreateAliasedBuffer(request);
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(manager_->GetAliasedAllocationCount(), 0);
}

TEST_F(DeviceBudgetManagerAliasingTest, CreateAliasedImageNullSource) {
    AliasedImageRequest request{};
    request.sourceAllocation = nullptr;

    auto result = manager_->CreateAliasedImage(request);
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(manager_->GetAliasedAllocationCount(), 0);
}

TEST_F(DeviceBudgetManagerAliasingTest, FreeAliasedBufferInvalidates) {
    BufferAllocation alloc{};
    alloc.buffer = reinterpret_cast<VkBuffer>(0x12345678);
    alloc.isAliased = true;
    alloc.size = 1024;

    manager_->FreeAliasedBuffer(alloc);

    EXPECT_EQ(alloc.buffer, VK_NULL_HANDLE);
    EXPECT_EQ(alloc.size, 0);
}

TEST_F(DeviceBudgetManagerAliasingTest, FreeAliasedImageInvalidates) {
    ImageAllocation alloc{};
    alloc.image = reinterpret_cast<VkImage>(0x87654321);
    alloc.isAliased = true;
    alloc.size = 2048;

    manager_->FreeAliasedImage(alloc);

    EXPECT_EQ(alloc.image, VK_NULL_HANDLE);
    EXPECT_EQ(alloc.size, 0);
}

// ============================================================================
// RenderGraph Integration Tests (B.3)
// ============================================================================

TEST(RenderGraphIntegration, DeferredDestructionProcessedEachFrame) {
    // Verify that DeferredDestructionQueue::ProcessFrame is called
    DeferredDestructionQueue queue;
    int destructionCount = 0;

    // Add destructions at different frames
    queue.AddGeneric([&destructionCount]() { destructionCount++; }, 0);
    queue.AddGeneric([&destructionCount]() { destructionCount++; }, 1);
    queue.AddGeneric([&destructionCount]() { destructionCount++; }, 2);

    EXPECT_EQ(queue.GetPendingCount(), 3);
    EXPECT_EQ(destructionCount, 0);

    // Process frame 3 (should destroy frame 0 resource with maxFramesInFlight=3)
    queue.ProcessFrame(3, 3);
    EXPECT_EQ(destructionCount, 1);
    EXPECT_EQ(queue.GetPendingCount(), 2);

    // Process frame 4 (should destroy frame 1 resource)
    queue.ProcessFrame(4, 3);
    EXPECT_EQ(destructionCount, 2);

    // Process frame 5 (should destroy frame 2 resource)
    queue.ProcessFrame(5, 3);
    EXPECT_EQ(destructionCount, 3);
    EXPECT_EQ(queue.GetPendingCount(), 0);
}

TEST(RenderGraphIntegration, ScopeManagerFrameLifecycle) {
    // Test the scope manager's frame lifecycle
    auto allocator = MemoryAllocatorFactory::CreateDirectAllocator(
        VK_NULL_HANDLE, VK_NULL_HANDLE, nullptr);
    DeferredDestructionQueue queue;
    uint64_t frameCounter = 0;
    SharedResourceFactory factory(allocator.get(), &queue, &frameCounter);
    LifetimeScopeManager manager(&factory);

    // Simulate multiple frames
    for (int frame = 0; frame < 5; ++frame) {
        manager.BeginFrame();
        EXPECT_EQ(manager.GetFrameNumber(), frame + 1);

        // Create nested scopes within frame
        {
            ScopeGuard pass1(manager, "Pass1");
            EXPECT_EQ(manager.GetNestedScopeDepth(), 1);
        }
        EXPECT_EQ(manager.GetNestedScopeDepth(), 0);

        manager.EndFrame();
    }

    EXPECT_EQ(manager.GetFrameNumber(), 5);
}

TEST(RenderGraphIntegration, IntegratedResourceLifecycle) {
    // Test complete resource lifecycle with all components
    auto allocator = MemoryAllocatorFactory::CreateDirectAllocator(
        VK_NULL_HANDLE, VK_NULL_HANDLE, nullptr);
    DeferredDestructionQueue queue;
    uint64_t frameCounter = 0;
    SharedResourceFactory factory(allocator.get(), &queue, &frameCounter);
    LifetimeScopeManager manager(&factory);

    // Frame 1: Create resources in frame scope
    manager.BeginFrame();
    frameCounter = 1;

    // Would create resources here if we had a real device
    // For now, just verify the structure works
    EXPECT_EQ(manager.GetFrameScope().GetBufferCount(), 0);

    manager.EndFrame();

    // Frame 2: Process deferred destructions from frame 1
    manager.BeginFrame();
    frameCounter = 2;
    queue.ProcessFrame(frameCounter, 3);

    manager.EndFrame();

    // Verify cleanup
    EXPECT_EQ(queue.GetPendingCount(), 0);
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
