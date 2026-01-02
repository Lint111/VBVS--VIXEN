/**
 * @file test_slot_task.cpp
 * @brief Tests for SlotTaskManager budget-aware execution (Phase C)
 *
 * Tests:
 * - Task generation from slots
 * - Sequential execution
 * - Parallel execution with budget awareness
 * - Dynamic throttling when budget is constrained
 * - Memory estimation tracking
 *
 * Compatible with VULKAN_TRIMMED_BUILD (no GPU required).
 */

#include <gtest/gtest.h>
#include "Core/SlotTask.h"
#include "Memory/ResourceBudgetManager.h"

#include <atomic>
#include <chrono>
#include <thread>

using namespace Vixen::RenderGraph;
using namespace ResourceManagement;

// =============================================================================
// Test Fixtures
// =============================================================================

class SlotTaskManagerTest : public ::testing::Test {
protected:
    SlotTaskManager taskManager;

    // Create N tasks with specified memory estimates
    std::vector<SlotTaskContext> CreateTasks(size_t count, uint64_t memoryPerTask = 0) {
        std::vector<SlotTaskContext> tasks;
        tasks.reserve(count);
        for (size_t i = 0; i < count; ++i) {
            SlotTaskContext task;
            task.taskIndex = static_cast<uint32_t>(i);
            task.totalTasks = static_cast<uint32_t>(count);
            task.arrayStartIndex = static_cast<uint32_t>(i);
            task.arrayCount = 1;
            task.estimatedMemoryBytes = memoryPerTask;
            tasks.push_back(task);
        }
        return tasks;
    }

    // Simple task that always succeeds
    static bool SuccessTask(SlotTaskContext& ctx) {
        return true;
    }

    // Task that fails on specific indices
    static bool FailOnOddTask(SlotTaskContext& ctx) {
        return (ctx.taskIndex % 2) == 0;
    }

    // Task that simulates work
    static bool SimulateWorkTask(SlotTaskContext& ctx) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        return true;
    }
};

// =============================================================================
// Sequential Execution Tests
// =============================================================================

TEST_F(SlotTaskManagerTest, ExecuteSequential_AllSuccess) {
    auto tasks = CreateTasks(10);

    uint32_t success = taskManager.ExecuteSequential(tasks, SuccessTask);

    EXPECT_EQ(success, 10u);
    EXPECT_EQ(taskManager.GetLastExecutionStats().totalTasks, 10u);
    EXPECT_EQ(taskManager.GetLastExecutionStats().completedTasks, 10u);
    EXPECT_EQ(taskManager.GetLastExecutionStats().failedTasks, 0u);
    EXPECT_EQ(taskManager.GetLastExecutionStats().actualParallelism, 1u);
}

TEST_F(SlotTaskManagerTest, ExecuteSequential_SomeFailures) {
    auto tasks = CreateTasks(10);

    uint32_t success = taskManager.ExecuteSequential(tasks, FailOnOddTask);

    EXPECT_EQ(success, 5u);  // Only even indices succeed
    EXPECT_EQ(taskManager.GetLastExecutionStats().completedTasks, 5u);
    EXPECT_EQ(taskManager.GetLastExecutionStats().failedTasks, 5u);
}

TEST_F(SlotTaskManagerTest, ExecuteSequential_EmptyTasks) {
    std::vector<SlotTaskContext> tasks;

    uint32_t success = taskManager.ExecuteSequential(tasks, SuccessTask);

    EXPECT_EQ(success, 0u);
}

TEST_F(SlotTaskManagerTest, ExecuteSequential_NullFunction) {
    auto tasks = CreateTasks(10);

    uint32_t success = taskManager.ExecuteSequential(tasks, nullptr);

    EXPECT_EQ(success, 0u);
}

// =============================================================================
// Parallel Execution Tests
// =============================================================================

TEST_F(SlotTaskManagerTest, ExecuteParallel_AllSuccess) {
    auto tasks = CreateTasks(10);

    uint32_t success = taskManager.ExecuteParallel(tasks, SuccessTask, nullptr, 4);

    EXPECT_EQ(success, 10u);
    EXPECT_EQ(taskManager.GetLastExecutionStats().completedTasks, 10u);
    EXPECT_EQ(taskManager.GetLastExecutionStats().failedTasks, 0u);
}

TEST_F(SlotTaskManagerTest, ExecuteParallel_SomeFailures) {
    auto tasks = CreateTasks(10);

    uint32_t success = taskManager.ExecuteParallel(tasks, FailOnOddTask, nullptr, 4);

    EXPECT_EQ(success, 5u);
    EXPECT_EQ(taskManager.GetLastExecutionStats().completedTasks, 5u);
    EXPECT_EQ(taskManager.GetLastExecutionStats().failedTasks, 5u);
}

TEST_F(SlotTaskManagerTest, ExecuteParallel_RespectsMaxParallelism) {
    std::atomic<int> concurrentCount{0};
    std::atomic<int> maxConcurrent{0};

    auto trackConcurrency = [&](SlotTaskContext& ctx) {
        int current = ++concurrentCount;

        // Update max if this is higher
        int expected = maxConcurrent.load();
        while (current > expected && !maxConcurrent.compare_exchange_weak(expected, current)) {}

        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        --concurrentCount;
        return true;
    };

    auto tasks = CreateTasks(20);
    taskManager.ExecuteParallel(tasks, trackConcurrency, nullptr, 4);

    // Max concurrent should not exceed 4
    EXPECT_LE(maxConcurrent.load(), 4);
}

// =============================================================================
// Budget-Aware Execution Tests
// =============================================================================

TEST_F(SlotTaskManagerTest, CalculateOptimalParallelism_NoEstimates) {
    auto tasks = CreateTasks(10, 0);  // No memory estimates

    ResourceBudgetManager budgetManager;
    budgetManager.SetBudget(BudgetResourceType::HostMemory,
        ResourceBudget(1024 * 1024, 512 * 1024, false));

    uint32_t parallelism = taskManager.CalculateOptimalParallelism(tasks, &budgetManager);

    // Without estimates, should use hardware concurrency
    EXPECT_GE(parallelism, 1u);
}

TEST_F(SlotTaskManagerTest, CalculateOptimalParallelism_WithEstimates) {
    // 10 tasks, each needing 100KB
    auto tasks = CreateTasks(10, 100 * 1024);

    ResourceBudgetManager budgetManager;
    budgetManager.SetBudget(BudgetResourceType::HostMemory,
        ResourceBudget(300 * 1024, 200 * 1024, false));  // Only 300KB available

    uint32_t parallelism = taskManager.CalculateOptimalParallelism(tasks, &budgetManager);

    // 300KB / 100KB = 3 tasks max
    EXPECT_LE(parallelism, 3u);
}

TEST_F(SlotTaskManagerTest, ExecuteParallel_DynamicThrottling) {
    // 10 tasks, each needing 100KB
    auto tasks = CreateTasks(10, 100 * 1024);

    ResourceBudgetManager budgetManager;
    budgetManager.SetBudget(BudgetResourceType::HostMemory,
        ResourceBudget(200 * 1024, 100 * 1024, false));  // Only 200KB - fits 2 tasks at a time

    uint32_t success = taskManager.ExecuteParallel(tasks, SuccessTask, &budgetManager, 4);

    EXPECT_EQ(success, 10u);

    // Should have throttled since we requested 4 parallel but only 2 fit
    auto stats = taskManager.GetLastExecutionStats();
    // tasksThrottled counts the number of tasks that couldn't run due to budget
    // With 4 requested and 2 max fitting, each batch throttles 2
    EXPECT_GT(stats.tasksThrottled, 0u);
}

TEST_F(SlotTaskManagerTest, ExecuteParallel_NoBudgetManager) {
    auto tasks = CreateTasks(10, 100 * 1024);

    uint32_t success = taskManager.ExecuteParallel(tasks, SuccessTask, nullptr, 4);

    EXPECT_EQ(success, 10u);
    // Without budget manager, no throttling
    EXPECT_EQ(taskManager.GetLastExecutionStats().tasksThrottled, 0u);
}

// =============================================================================
// Memory Tracking Tests (Phase C.3)
// =============================================================================

TEST_F(SlotTaskManagerTest, ReportActualMemory_TracksUsage) {
    auto tasks = CreateTasks(5, 100);  // Estimate 100 bytes each

    taskManager.ExecuteSequential(tasks, SuccessTask);

    // Report actual usage for each task
    for (uint32_t i = 0; i < 5; ++i) {
        taskManager.ReportActualMemory(i, 120);  // Actual was 120, not 100
    }

    auto stats = taskManager.GetLastExecutionStats();
    EXPECT_EQ(stats.totalEstimatedMemory, 500u);  // 5 * 100
    EXPECT_EQ(stats.totalActualMemory, 600u);     // 5 * 120
    EXPECT_EQ(stats.tasksOverBudget, 5u);         // All exceeded estimate
}

TEST_F(SlotTaskManagerTest, GetEstimationAccuracy_CalculatesRatio) {
    auto tasks = CreateTasks(4, 100);  // Estimate 100 each = 400 total

    taskManager.ExecuteSequential(tasks, SuccessTask);

    // Report actual: 50 + 100 + 150 + 100 = 400 (perfect overall)
    taskManager.ReportActualMemory(0, 50);
    taskManager.ReportActualMemory(1, 100);
    taskManager.ReportActualMemory(2, 150);
    taskManager.ReportActualMemory(3, 100);

    float accuracy = taskManager.GetEstimationAccuracy();
    EXPECT_NEAR(accuracy, 1.0f, 0.001f);  // 400/400 = 1.0
}

TEST_F(SlotTaskManagerTest, GetEstimationAccuracy_Underestimated) {
    auto tasks = CreateTasks(2, 100);  // Estimate 200 total

    taskManager.ExecuteSequential(tasks, SuccessTask);

    // Actual: 200 + 200 = 400 (double the estimate)
    taskManager.ReportActualMemory(0, 200);
    taskManager.ReportActualMemory(1, 200);

    float accuracy = taskManager.GetEstimationAccuracy();
    EXPECT_NEAR(accuracy, 2.0f, 0.001f);  // 400/200 = 2.0 (underestimated)
}

TEST_F(SlotTaskManagerTest, GetEstimationAccuracy_NoEstimates) {
    auto tasks = CreateTasks(2, 0);  // No estimates

    taskManager.ExecuteSequential(tasks, SuccessTask);

    float accuracy = taskManager.GetEstimationAccuracy();
    EXPECT_NEAR(accuracy, 1.0f, 0.001f);  // No estimates = perfect (vacuous)
}

// =============================================================================
// Stats Reset Tests
// =============================================================================

TEST_F(SlotTaskManagerTest, ResetStats_ClearsAll) {
    auto tasks = CreateTasks(5, 100);
    taskManager.ExecuteSequential(tasks, SuccessTask);

    // Stats should have values
    EXPECT_GT(taskManager.GetLastExecutionStats().totalTasks, 0u);

    taskManager.ResetStats();

    // Stats should be zeroed
    auto stats = taskManager.GetLastExecutionStats();
    EXPECT_EQ(stats.totalTasks, 0u);
    EXPECT_EQ(stats.completedTasks, 0u);
    EXPECT_EQ(stats.failedTasks, 0u);
    EXPECT_EQ(stats.totalEstimatedMemory, 0u);
}

// =============================================================================
// Task Context Tests
// =============================================================================

TEST_F(SlotTaskManagerTest, TaskContext_GetResourceScope) {
    SlotTaskContext task;

    task.resourceScope = SlotScope::NodeLevel;
    EXPECT_EQ(task.GetResourceScope(), ResourceScope::Persistent);

    task.resourceScope = SlotScope::TaskLevel;
    EXPECT_EQ(task.GetResourceScope(), ResourceScope::Transient);

    task.resourceScope = SlotScope::InstanceLevel;
    EXPECT_EQ(task.GetResourceScope(), ResourceScope::Transient);
}

TEST_F(SlotTaskManagerTest, TaskContext_SingleElementHelper) {
    SlotTaskContext single;
    single.arrayCount = 1;
    single.arrayStartIndex = 5;

    EXPECT_TRUE(single.IsSingleElement());
    EXPECT_EQ(single.GetElementIndex(), 5u);

    SlotTaskContext batch;
    batch.arrayCount = 3;

    EXPECT_FALSE(batch.IsSingleElement());
}
