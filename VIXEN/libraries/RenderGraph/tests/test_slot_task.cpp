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
#include "Core/NodeTypeRegistry.h"
#include "Core/RenderGraph.h"
#include "Core/SlotTask.h"
#include "KernelDispatch/TaskExecutor.h"
#include "Memory/ResourceBudgetManager.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <thread>
#include <vector>

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

TEST_F(SlotTaskManagerTest, ExecuteParallel_IsResultInvariantAtOneTwoAndNWorkers) {
    struct Snapshot {
        uint32_t successCount = 0;
        std::vector<uint64_t> outputs;
        std::vector<uint8_t> statuses;
        uint32_t completedTasks = 0;
        uint32_t failedTasks = 0;
        uint32_t skippedTasks = 0;
        uint64_t totalEstimatedMemory = 0;

        bool operator==(const Snapshot&) const = default;
    };

    constexpr uint64_t memoryPerTask = 64 * 1024;
    constexpr uint64_t memoryBudget = 2 * memoryPerTask;
    const uint32_t nWorkers = std::min(8u, std::max(3u, std::thread::hardware_concurrency()));
    const std::vector<uint32_t> workerCounts{1u, 2u, nWorkers};

    std::optional<Snapshot> reference;
    for (uint32_t workerCount : workerCounts) {
        auto tasks = CreateTasks(24, memoryPerTask);
        std::vector<uint64_t> outputs(tasks.size(), 0);
        std::atomic<uint64_t> inFlightMemory{0};
        std::atomic<uint64_t> peakInFlightMemory{0};

        ResourceBudgetManager budgetManager;
        budgetManager.SetBudget(BudgetResourceType::HostMemory,
            ResourceBudget(memoryBudget, memoryBudget, false));

        const auto task = [&](SlotTaskContext& ctx) {
            const uint64_t inFlight = inFlightMemory.fetch_add(memoryPerTask) + memoryPerTask;
            uint64_t observedPeak = peakInFlightMemory.load();
            while (inFlight > observedPeak &&
                   !peakInFlightMemory.compare_exchange_weak(observedPeak, inFlight)) {}

            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            outputs[ctx.taskIndex] = 0x9e3779b97f4a7c15ull ^
                                     (static_cast<uint64_t>(ctx.taskIndex) * 0x100000001b3ull);
            inFlightMemory.fetch_sub(memoryPerTask);
            return (ctx.taskIndex % 5u) != 3u;
        };

        Snapshot snapshot;
        snapshot.successCount = taskManager.ExecuteParallel(
            tasks, task, &budgetManager, workerCount, {});
        snapshot.outputs = outputs;
        for (const auto& item : tasks) {
            snapshot.statuses.push_back(static_cast<uint8_t>(item.status));
        }
        const auto stats = taskManager.GetLastExecutionStats();
        snapshot.completedTasks = stats.completedTasks;
        snapshot.failedTasks = stats.failedTasks;
        snapshot.skippedTasks = stats.skippedTasks;
        snapshot.totalEstimatedMemory = stats.totalEstimatedMemory;

        EXPECT_LE(peakInFlightMemory.load(), memoryBudget)
            << "workerCount=" << workerCount;
        if (!reference) {
            reference = snapshot;
        } else {
            EXPECT_EQ(snapshot, *reference) << "workerCount=" << workerCount;
        }
    }
}

TEST_F(SlotTaskManagerTest, ExecuteParallel_GraphEpochInvalidationStartsNoTasks) {
    NodeTypeRegistry registry;
    RenderGraph graph(&registry, nullptr, nullptr, nullptr);
    const uint64_t epoch = graph.GetExecutionEpoch();
    const std::stop_token token = graph.GetExecutionStopToken();

    graph.AbortCurrentFrame();

    ASSERT_TRUE(token.stop_requested());
    EXPECT_GT(graph.GetExecutionEpoch(), epoch);

    const uint32_t nWorkers = std::min(8u, std::max(3u, std::thread::hardware_concurrency()));
    for (uint32_t workerCount : {1u, 2u, nWorkers}) {
        auto tasks = CreateTasks(12);
        std::atomic<uint32_t> started{0};
        const uint32_t success = taskManager.ExecuteParallel(
            tasks,
            [&](SlotTaskContext&) {
                ++started;
                return true;
            },
            nullptr,
            workerCount,
            token);

        EXPECT_EQ(success, 0u) << "workerCount=" << workerCount;
        EXPECT_EQ(started.load(), 0u) << "workerCount=" << workerCount;
        EXPECT_TRUE(std::all_of(tasks.begin(), tasks.end(), [](const SlotTaskContext& task) {
            return task.status == TaskStatus::Skipped;
        }));
        EXPECT_EQ(taskManager.GetLastExecutionStats().skippedTasks, tasks.size());
    }
}

TEST_F(SlotTaskManagerTest, RenderGraphCompileRotatesExecutionEpoch) {
    NodeTypeRegistry registry;
    RenderGraph graph(&registry, nullptr, nullptr, nullptr);
    const uint64_t epoch = graph.GetExecutionEpoch();
    const std::stop_token oldToken = graph.GetExecutionStopToken();

    ASSERT_NO_THROW(graph.Compile());

    EXPECT_TRUE(oldToken.stop_requested());
    EXPECT_GT(graph.GetExecutionEpoch(), epoch);
    EXPECT_FALSE(graph.GetExecutionStopToken().stop_requested());
}

TEST_F(SlotTaskManagerTest, ExecuteParallel_ExceptionStopsLaterBudgetBatches) {
    auto tasks = CreateTasks(6);
    const uint32_t success = taskManager.ExecuteParallel(
        tasks,
        [](SlotTaskContext& ctx) {
            if (ctx.taskIndex == 1u) {
                throw std::runtime_error("slot task 1 failed");
            }
            return true;
        },
        nullptr,
        2,
        {});

    EXPECT_EQ(success, 1u);
    EXPECT_EQ(tasks[0].status, TaskStatus::Completed);
    EXPECT_EQ(tasks[1].status, TaskStatus::Failed);
    ASSERT_TRUE(tasks[1].errorMessage.has_value());
    EXPECT_EQ(*tasks[1].errorMessage, "slot task 1 failed");
    for (size_t i = 2; i < tasks.size(); ++i) {
        EXPECT_EQ(tasks[i].status, TaskStatus::Skipped) << "taskIndex=" << i;
    }
}

TEST(KernelDispatchTaskExecutorTest, PrimaryErrorIsLowestTaskIndexAndLaterWavesDoNotRun) {
    const uint32_t nWorkers = std::min(8u, std::max(3u, std::thread::hardware_concurrency()));
    for (uint32_t workerCount : {1u, 2u, nWorkers}) {
        std::atomic<uint32_t> laterWaveRuns{0};
        std::vector<Vixen::KernelDispatch::VirtualTask> tasks;
        tasks.push_back({Vixen::KernelDispatch::TaskId{41, 5}, [] { throw std::runtime_error("task 5"); }});
        tasks.push_back({Vixen::KernelDispatch::TaskId{41, 1}, [] {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            throw std::runtime_error("task 1");
        }});
        tasks.push_back({Vixen::KernelDispatch::TaskId{41, 3}, [] { throw std::runtime_error("task 3"); }});
        tasks.push_back({Vixen::KernelDispatch::TaskId{41, 9}, [&] { ++laterWaveRuns; }});

        const std::vector<std::vector<Vixen::KernelDispatch::TaskId>> waves{
            {Vixen::KernelDispatch::TaskId{41, 5}, Vixen::KernelDispatch::TaskId{41, 1},
             Vixen::KernelDispatch::TaskId{41, 3}},
            {Vixen::KernelDispatch::TaskId{41, 9}}
        };

        Vixen::KernelDispatch::TaskExecutor executor;
        EXPECT_FALSE(executor.Run(tasks, waves, static_cast<int>(workerCount), {}));
        ASSERT_EQ(executor.GetErrors().size(), 3u);
        EXPECT_EQ(executor.GetErrors()[0].task.taskIndex, 1u);
        EXPECT_EQ(executor.GetErrors()[1].task.taskIndex, 3u);
        EXPECT_EQ(executor.GetErrors()[2].task.taskIndex, 5u);
        EXPECT_EQ(laterWaveRuns.load(), 0u);
        EXPECT_EQ(tasks[3].state, Vixen::KernelDispatch::VirtualTaskState::Pending);
    }
}

TEST(KernelDispatchTaskExecutorTest, StopTokenPreventsIssuingLaterWavesAtOneTwoAndNWorkers) {
    const uint32_t nWorkers = std::min(8u, std::max(3u, std::thread::hardware_concurrency()));
    for (uint32_t workerCount : {1u, 2u, nWorkers}) {
        std::stop_source source;
        std::atomic<uint32_t> laterWaveRuns{0};
        std::vector<Vixen::KernelDispatch::VirtualTask> tasks;
        tasks.push_back({Vixen::KernelDispatch::TaskId{72, 0}, [&] { source.request_stop(); }});
        tasks.push_back({Vixen::KernelDispatch::TaskId{72, 1}, [&] { ++laterWaveRuns; }});

        const std::vector<std::vector<Vixen::KernelDispatch::TaskId>> waves{
            {Vixen::KernelDispatch::TaskId{72, 0}},
            {Vixen::KernelDispatch::TaskId{72, 1}}
        };

        Vixen::KernelDispatch::TaskExecutor executor;
        EXPECT_FALSE(executor.Run(
            tasks, waves, static_cast<int>(workerCount), source.get_token()));
        EXPECT_TRUE(source.stop_requested());
        EXPECT_EQ(laterWaveRuns.load(), 0u);
        EXPECT_EQ(tasks[0].state, Vixen::KernelDispatch::VirtualTaskState::Completed);
        EXPECT_EQ(tasks[1].state, Vixen::KernelDispatch::VirtualTaskState::Pending);
    }
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
