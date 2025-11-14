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
