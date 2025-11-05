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
#include "../include/Core/ResourceBudgetManager.h"
#include "../include/Core/DeferredDestruction.h"
#include "../include/Core/StatefulContainer.h"
#include "../include/Core/SlotTask.h"

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
    // Initial budgets should be zero or default
    auto imageBudget = budgetManager->GetBudget(BudgetResourceType::Image);
    EXPECT_EQ(imageBudget.totalBytes, 0);
    EXPECT_EQ(imageBudget.usedBytes, 0);
}

TEST_F(ResourceBudgetManagerTest, SetBudget) {
    ResourceBudget budget;
    budget.totalBytes = 1024 * 1024 * 100; // 100 MB
    budget.maxCount = 50;

    budgetManager->SetBudget(BudgetResourceType::Image, budget);

    auto retrieved = budgetManager->GetBudget(BudgetResourceType::Image);
    EXPECT_EQ(retrieved.totalBytes, budget.totalBytes);
    EXPECT_EQ(retrieved.maxCount, budget.maxCount);
}

TEST_F(ResourceBudgetManagerTest, TrackUsage) {
    // Set budget
    ResourceBudget budget;
    budget.totalBytes = 1024 * 1024 * 100;
    budget.maxCount = 50;
    budgetManager->SetBudget(BudgetResourceType::Image, budget);

    // Track usage
    BudgetResourceUsage usage;
    usage.bytes = 1024 * 1024 * 10; // 10 MB
    usage.count = 5;

    budgetManager->TrackAllocation(BudgetResourceType::Image, usage);

    auto retrieved = budgetManager->GetBudget(BudgetResourceType::Image);
    EXPECT_EQ(retrieved.usedBytes, usage.bytes);
    EXPECT_EQ(retrieved.usedCount, usage.count);
}

TEST_F(ResourceBudgetManagerTest, BudgetExceeded) {
    // Set small budget
    ResourceBudget budget;
    budget.totalBytes = 1024 * 1024 * 10; // 10 MB
    budget.maxCount = 5;
    budgetManager->SetBudget(BudgetResourceType::Buffer, budget);

    // Try to allocate more
    BudgetResourceUsage usage;
    usage.bytes = 1024 * 1024 * 20; // 20 MB
    usage.count = 3;

    bool withinBudget = budgetManager->CanAllocate(BudgetResourceType::Buffer, usage);
    EXPECT_FALSE(withinBudget); // Should exceed budget
}

TEST_F(ResourceBudgetManagerTest, ReleaseUsage) {
    // Set budget and allocate
    ResourceBudget budget;
    budget.totalBytes = 1024 * 1024 * 100;
    budgetManager->SetBudget(BudgetResourceType::Image, budget);

    BudgetResourceUsage usage;
    usage.bytes = 1024 * 1024 * 10;
    usage.count = 5;
    budgetManager->TrackAllocation(BudgetResourceType::Image, usage);

    // Release some
    BudgetResourceUsage release;
    release.bytes = 1024 * 1024 * 5;
    release.count = 2;
    budgetManager->TrackDeallocation(BudgetResourceType::Image, release);

    auto retrieved = budgetManager->GetBudget(BudgetResourceType::Image);
    EXPECT_EQ(retrieved.usedBytes, usage.bytes - release.bytes);
    EXPECT_EQ(retrieved.usedCount, usage.count - release.count);
}

TEST_F(ResourceBudgetManagerTest, MultipleResourceTypes) {
    // Set budgets for different types
    ResourceBudget imageBudget{1024 * 1024 * 100, 0, 50, 0};
    ResourceBudget bufferBudget{1024 * 1024 * 50, 0, 100, 0};

    budgetManager->SetBudget(BudgetResourceType::Image, imageBudget);
    budgetManager->SetBudget(BudgetResourceType::Buffer, bufferBudget);

    // Track different usages
    budgetManager->TrackAllocation(BudgetResourceType::Image, {1024 * 1024 * 10, 5});
    budgetManager->TrackAllocation(BudgetResourceType::Buffer, {1024 * 1024 * 20, 10});

    auto imageRetrieved = budgetManager->GetBudget(BudgetResourceType::Image);
    auto bufferRetrieved = budgetManager->GetBudget(BudgetResourceType::Buffer);

    EXPECT_EQ(imageRetrieved.usedBytes, 1024 * 1024 * 10);
    EXPECT_EQ(bufferRetrieved.usedBytes, 1024 * 1024 * 20);
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

TEST_F(DeferredDestructionTest, DeferDestruction) {
    destructionCounter = 0;

    // Create pending destruction
    PendingDestruction pending;
    pending.frameDelay = 3;
    pending.destructor = [this]() { destructionCounter++; };

    destructionQueue->Defer(std::move(pending));

    // Should not be destroyed immediately
    EXPECT_EQ(destructionCounter, 0);
}

TEST_F(DeferredDestructionTest, ProcessAfterDelay) {
    destructionCounter = 0;

    PendingDestruction pending;
    pending.frameDelay = 2;
    pending.destructor = [this]() { destructionCounter++; };

    destructionQueue->Defer(std::move(pending));

    // Advance frames
    destructionQueue->ProcessFrame(); // Frame 1
    EXPECT_EQ(destructionCounter, 0);

    destructionQueue->ProcessFrame(); // Frame 2
    EXPECT_EQ(destructionCounter, 0);

    destructionQueue->ProcessFrame(); // Frame 3 - should destroy
    EXPECT_EQ(destructionCounter, 1);
}

TEST_F(DeferredDestructionTest, MultipleDestructions) {
    destructionCounter = 0;

    // Queue multiple destructions
    for (int i = 0; i < 5; ++i) {
        PendingDestruction pending;
        pending.frameDelay = 2;
        pending.destructor = [this]() { destructionCounter++; };
        destructionQueue->Defer(std::move(pending));
    }

    destructionQueue->ProcessFrame();
    destructionQueue->ProcessFrame();
    destructionQueue->ProcessFrame();

    EXPECT_EQ(destructionCounter, 5);
}

TEST_F(DeferredDestructionTest, DifferentDelays) {
    destructionCounter = 0;

    // Queue destructions with different delays
    PendingDestruction pending1;
    pending1.frameDelay = 1;
    pending1.destructor = [this]() { destructionCounter++; };

    PendingDestruction pending2;
    pending2.frameDelay = 3;
    pending2.destructor = [this]() { destructionCounter++; };

    destructionQueue->Defer(std::move(pending1));
    destructionQueue->Defer(std::move(pending2));

    destructionQueue->ProcessFrame(); // Frame 1
    EXPECT_EQ(destructionCounter, 0);

    destructionQueue->ProcessFrame(); // Frame 2 - first should destroy
    EXPECT_EQ(destructionCounter, 1);

    destructionQueue->ProcessFrame(); // Frame 3
    EXPECT_EQ(destructionCounter, 1);

    destructionQueue->ProcessFrame(); // Frame 4 - second should destroy
    EXPECT_EQ(destructionCounter, 2);
}

TEST_F(DeferredDestructionTest, ImmediateDestruction) {
    destructionCounter = 0;

    PendingDestruction pending;
    pending.frameDelay = 0; // Immediate
    pending.destructor = [this]() { destructionCounter++; };

    destructionQueue->Defer(std::move(pending));
    destructionQueue->ProcessFrame();

    EXPECT_EQ(destructionCounter, 1);
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

    void SetUp() override {
        container = std::make_unique<StatefulContainer<TestResource>>();
    }

    std::unique_ptr<StatefulContainer<TestResource>> container;
};

TEST_F(StatefulContainerTest, InitialState) {
    TestResource resource{42, true};
    container->Set(std::move(resource), ResourceState::Created);

    EXPECT_EQ(container->GetState(), ResourceState::Created);
    EXPECT_TRUE(container->IsInState(ResourceState::Created));
}

TEST_F(StatefulContainerTest, StateTransitions) {
    TestResource resource{42, true};
    container->Set(std::move(resource), ResourceState::Created);

    container->TransitionTo(ResourceState::Initialized);
    EXPECT_EQ(container->GetState(), ResourceState::Initialized);

    container->TransitionTo(ResourceState::Ready);
    EXPECT_EQ(container->GetState(), ResourceState::Ready);

    container->TransitionTo(ResourceState::InUse);
    EXPECT_EQ(container->GetState(), ResourceState::InUse);
}

TEST_F(StatefulContainerTest, AccessResource) {
    TestResource resource{42, true};
    container->Set(std::move(resource), ResourceState::Ready);

    const auto& retrieved = container->Get();
    EXPECT_EQ(retrieved.value, 42);
    EXPECT_TRUE(retrieved.valid);
}

TEST_F(StatefulContainerTest, ModifyResource) {
    TestResource resource{42, true};
    container->Set(std::move(resource), ResourceState::Ready);

    auto& retrieved = container->GetMutable();
    retrieved.value = 100;

    EXPECT_EQ(container->Get().value, 100);
}

TEST_F(StatefulContainerTest, StateHistory) {
    TestResource resource{42, true};
    container->Set(std::move(resource), ResourceState::Created);

    container->TransitionTo(ResourceState::Initialized);
    container->TransitionTo(ResourceState::Ready);

    // Should be able to check previous states
    EXPECT_FALSE(container->IsInState(ResourceState::Created));
    EXPECT_TRUE(container->IsInState(ResourceState::Ready));
}

// ============================================================================
// SlotTask Tests
// ============================================================================

class SlotTaskTest : public ::testing::Test {
protected:
    void SetUp() override {
        task = std::make_unique<SlotTask>("TestTask");
    }

    std::unique_ptr<SlotTask> task;
};

TEST_F(SlotTaskTest, InitialStatus) {
    EXPECT_EQ(task->GetStatus(), TaskStatus::Pending);
    EXPECT_FALSE(task->IsComplete());
    EXPECT_FALSE(task->HasFailed());
}

TEST_F(SlotTaskTest, StartTask) {
    task->Start();

    EXPECT_EQ(task->GetStatus(), TaskStatus::Running);
    EXPECT_FALSE(task->IsComplete());
}

TEST_F(SlotTaskTest, CompleteTask) {
    task->Start();
    task->Complete();

    EXPECT_EQ(task->GetStatus(), TaskStatus::Completed);
    EXPECT_TRUE(task->IsComplete());
    EXPECT_FALSE(task->HasFailed());
}

TEST_F(SlotTaskTest, FailTask) {
    task->Start();
    task->Fail("Test error");

    EXPECT_EQ(task->GetStatus(), TaskStatus::Failed);
    EXPECT_FALSE(task->IsComplete());
    EXPECT_TRUE(task->HasFailed());
}

TEST_F(SlotTaskTest, TaskName) {
    EXPECT_EQ(task->GetName(), "TestTask");
}

TEST_F(SlotTaskTest, TaskProgress) {
    task->Start();
    task->SetProgress(0.5f);

    EXPECT_FLOAT_EQ(task->GetProgress(), 0.5f);
}

TEST_F(SlotTaskTest, CancelTask) {
    task->Start();
    task->Cancel();

    EXPECT_EQ(task->GetStatus(), TaskStatus::Canceled);
    EXPECT_FALSE(task->IsComplete());
}

TEST_F(SlotTaskTest, MultipleTaskSequence) {
    // Create multiple tasks
    SlotTask task1("Task1");
    SlotTask task2("Task2");
    SlotTask task3("Task3");

    task1.Start();
    task1.Complete();
    EXPECT_TRUE(task1.IsComplete());

    task2.Start();
    task2.Complete();
    EXPECT_TRUE(task2.IsComplete());

    task3.Start();
    task3.Fail("Intentional failure");
    EXPECT_TRUE(task3.HasFailed());
}

// ============================================================================
// Integration Test: Resource Lifecycle
// ============================================================================

TEST(ResourceManagementIntegration, CompleteResourceLifecycle) {
    // Simulate complete resource lifecycle with all management systems

    // 1. Budget check
    ResourceBudgetManager budgetMgr;
    ResourceBudget budget{1024 * 1024 * 100, 0, 50, 0};
    budgetMgr.SetBudget(BudgetResourceType::Image, budget);

    BudgetResourceUsage usage{1024 * 1024 * 10, 5};
    EXPECT_TRUE(budgetMgr.CanAllocate(BudgetResourceType::Image, usage));

    // 2. Track allocation
    budgetMgr.TrackAllocation(BudgetResourceType::Image, usage);

    // 3. Resource state management
    struct TestResource { int id = 123; };
    StatefulContainer<TestResource> container;
    container.Set(TestResource{123}, ResourceState::Created);
    container.TransitionTo(ResourceState::Initialized);
    container.TransitionTo(ResourceState::Ready);

    // 4. Task tracking
    SlotTask task("AllocateImage");
    task.Start();
    task.SetProgress(0.5f);
    task.Complete();
    EXPECT_TRUE(task.IsComplete());

    // 5. Deferred cleanup
    DeferredDestructionQueue destructionQueue;
    bool destroyed = false;
    PendingDestruction pending;
    pending.frameDelay = 2;
    pending.destructor = [&destroyed]() { destroyed = true; };
    destructionQueue.Defer(std::move(pending));

    // Process frames
    destructionQueue.ProcessFrame();
    destructionQueue.ProcessFrame();
    destructionQueue.ProcessFrame();
    EXPECT_TRUE(destroyed);

    // 6. Release budget
    budgetMgr.TrackDeallocation(BudgetResourceType::Image, usage);
    auto finalBudget = budgetMgr.GetBudget(BudgetResourceType::Image);
    EXPECT_EQ(finalBudget.usedBytes, 0);
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
