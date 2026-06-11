/**
 * @file test_task_queue.cpp
 * @brief Tests for Sprint 6.2 TaskQueue budget-aware system
 *
 * Tests:
 * - Budget enforcement (strict mode)
 * - Lenient mode with warning callbacks
 * - Overflow protection
 * - GetRemainingBudget() API
 * - TaskBudget configuration
 * - Budget presets
 */

#include <gtest/gtest.h>
#include <cstdint>
#include <limits>

#include "Core/TaskQueue.h"
#include "Core/TimelineCapacityTracker.h"
#include "Data/TaskBudget.h"
#include "Data/DispatchPass.h"

using namespace Vixen::RenderGraph;

// ============================================================================
// TEST FIXTURE
// ============================================================================

class TaskQueueTest : public ::testing::Test {
protected:
    using TestQueue = TaskQueue<DispatchPass>;
    using TestSlot = TestQueue::TaskSlot;

    // Helper: Create a minimal valid DispatchPass
    static DispatchPass CreateValidDispatch() {
        DispatchPass pass{};
        pass.pipeline = reinterpret_cast<VkPipeline>(0x1234);
        pass.layout = reinterpret_cast<VkPipelineLayout>(0x5678);
        pass.workGroupCount = {1, 1, 1};
        return pass;
    }

    // Helper: Create a task slot with specified cost
    static TestSlot CreateSlot(uint64_t costNs, uint8_t priority = 128) {
        TestSlot slot;
        slot.data = CreateValidDispatch();
        slot.estimatedCostNs = costNs;
        slot.priority = priority;
        return slot;
    }
};

// ============================================================================
// STRICT MODE TESTS (Task #342)
// ============================================================================

TEST_F(TaskQueueTest, StrictModeRejectsTaskExceedingBudget) {
    TestQueue queue;
    queue.SetFrameBudget(1'000'000);  // 1ms budget

    // First task: 600'000ns (within budget)
    auto slot1 = CreateSlot(600'000);
    EXPECT_TRUE(queue.TryEnqueue(std::move(slot1)));
    EXPECT_EQ(queue.GetQueuedCount(), 1u);
    EXPECT_EQ(queue.GetTotalEstimatedCost(), 600'000u);

    // Second task: 500'000ns (would exceed 1ms budget)
    auto slot2 = CreateSlot(500'000);
    EXPECT_FALSE(queue.TryEnqueue(std::move(slot2)));
    EXPECT_EQ(queue.GetQueuedCount(), 1u);  // Still 1 task
    EXPECT_EQ(queue.GetTotalEstimatedCost(), 600'000u);  // Cost unchanged
}

TEST_F(TaskQueueTest, StrictModeRejectsAllTasksWhenBudgetZero) {
    TestQueue queue;
    queue.SetFrameBudget(0);  // Zero budget

    auto slot = CreateSlot(100);  // Even small task rejected
    EXPECT_FALSE(queue.TryEnqueue(std::move(slot)));
    EXPECT_EQ(queue.GetQueuedCount(), 0u);
}

TEST_F(TaskQueueTest, StrictModeAcceptsZeroCostTasks) {
    TestQueue queue;
    queue.SetFrameBudget(1'000'000);

    // Fill budget to 900'000ns
    auto slot1 = CreateSlot(900'000);
    EXPECT_TRUE(queue.TryEnqueue(std::move(slot1)));

    // Zero-cost task should be accepted even though budget is tight
    auto slot2 = CreateSlot(0);
    EXPECT_TRUE(queue.TryEnqueue(std::move(slot2)));
    EXPECT_EQ(queue.GetQueuedCount(), 2u);
}

TEST_F(TaskQueueTest, StrictModePreventsOverflow) {
    TestQueue queue;
    queue.SetFrameBudget(std::numeric_limits<uint64_t>::max());

    // Fill budget to near-max
    auto slot1 = CreateSlot(std::numeric_limits<uint64_t>::max() - 100);
    EXPECT_TRUE(queue.TryEnqueue(std::move(slot1)));

    // Task that would cause overflow is rejected
    auto slot2 = CreateSlot(200);
    EXPECT_FALSE(queue.TryEnqueue(std::move(slot2)));
    EXPECT_EQ(queue.GetQueuedCount(), 1u);
}

// ============================================================================
// LENIENT MODE TESTS (Task #342)
// ============================================================================

TEST_F(TaskQueueTest, LenientModeAcceptsTaskExceedingBudget) {
    TestQueue queue;
    TaskBudget budget(1'000'000, BudgetOverflowMode::Lenient);
    queue.SetBudget(budget);

    // First task: 600'000ns
    auto slot1 = CreateSlot(600'000);
    EXPECT_TRUE(queue.TryEnqueue(std::move(slot1)));

    // Second task: 500'000ns (exceeds budget but accepted in lenient mode)
    auto slot2 = CreateSlot(500'000);
    EXPECT_TRUE(queue.TryEnqueue(std::move(slot2)));
    EXPECT_EQ(queue.GetQueuedCount(), 2u);
    EXPECT_EQ(queue.GetTotalEstimatedCost(), 1'100'000u);  // Over budget
}

TEST_F(TaskQueueTest, LenientModeCallsWarningCallback) {
    TestQueue queue;
    TaskBudget budget(1'000'000, BudgetOverflowMode::Lenient);
    queue.SetBudget(budget);

    // Set up warning callback
    uint64_t capturedNewTotal = 0;
    uint64_t capturedBudget = 0;
    uint64_t capturedTaskCost = 0;
    int callCount = 0;

    queue.SetWarningCallback([&](uint64_t newTotal, uint64_t budgetNs, uint64_t taskCost) {
        capturedNewTotal = newTotal;
        capturedBudget = budgetNs;
        capturedTaskCost = taskCost;
        ++callCount;
    });

    // Enqueue task within budget (no warning)
    auto slot1 = CreateSlot(600'000);
    EXPECT_TRUE(queue.TryEnqueue(std::move(slot1)));
    EXPECT_EQ(callCount, 0);  // No warning yet

    // Enqueue task that exceeds budget (warning triggered)
    auto slot2 = CreateSlot(500'000);
    EXPECT_TRUE(queue.TryEnqueue(std::move(slot2)));
    EXPECT_EQ(callCount, 1);  // Warning called once
    EXPECT_EQ(capturedNewTotal, 1'100'000u);
    EXPECT_EQ(capturedBudget, 1'000'000u);
    EXPECT_EQ(capturedTaskCost, 500'000u);
}

TEST_F(TaskQueueTest, LenientModeAcceptsTasksWhenBudgetZero) {
    TestQueue queue;
    TaskBudget budget(0, BudgetOverflowMode::Lenient);
    queue.SetBudget(budget);

    int callCount = 0;
    queue.SetWarningCallback([&](uint64_t, uint64_t, uint64_t) {
        ++callCount;
    });

    // Even with zero budget, lenient mode accepts tasks
    auto slot = CreateSlot(100);
    EXPECT_TRUE(queue.TryEnqueue(std::move(slot)));
    EXPECT_EQ(queue.GetQueuedCount(), 1u);
    EXPECT_EQ(callCount, 1);  // Warning called
}

TEST_F(TaskQueueTest, LenientModeHandlesOverflowGracefully) {
    TestQueue queue;
    TaskBudget budget(std::numeric_limits<uint64_t>::max(), BudgetOverflowMode::Lenient);
    queue.SetBudget(budget);

    int callCount = 0;
    queue.SetWarningCallback([&](uint64_t, uint64_t, uint64_t) {
        ++callCount;
    });

    // Fill to near-max
    auto slot1 = CreateSlot(std::numeric_limits<uint64_t>::max() - 100);
    EXPECT_TRUE(queue.TryEnqueue(std::move(slot1)));

    // Task that would overflow is accepted with warning
    auto slot2 = CreateSlot(200);
    EXPECT_TRUE(queue.TryEnqueue(std::move(slot2)));
    EXPECT_EQ(queue.GetQueuedCount(), 2u);
    EXPECT_EQ(callCount, 1);  // Overflow warning
}

// ============================================================================
// BUDGET API TESTS (Task #342)
// ============================================================================

TEST_F(TaskQueueTest, GetRemainingBudgetReturnsCorrectValue) {
    TestQueue queue;
    queue.SetFrameBudget(1'000'000);

    EXPECT_EQ(queue.GetRemainingBudget(), 1'000'000u);

    auto slot1 = CreateSlot(300'000);
    queue.TryEnqueue(std::move(slot1));
    EXPECT_EQ(queue.GetRemainingBudget(), 700'000u);

    auto slot2 = CreateSlot(400'000);
    queue.TryEnqueue(std::move(slot2));
    EXPECT_EQ(queue.GetRemainingBudget(), 300'000u);

    auto slot3 = CreateSlot(300'000);
    queue.TryEnqueue(std::move(slot3));
    EXPECT_EQ(queue.GetRemainingBudget(), 0u);  // Budget exhausted
}

TEST_F(TaskQueueTest, GetRemainingBudgetReturnsZeroWhenOverBudget) {
    TestQueue queue;
    TaskBudget budget(1'000'000, BudgetOverflowMode::Lenient);
    queue.SetBudget(budget);

    auto slot1 = CreateSlot(600'000);
    queue.TryEnqueue(std::move(slot1));

    auto slot2 = CreateSlot(500'000);
    queue.TryEnqueue(std::move(slot2));  // Over budget in lenient mode

    EXPECT_EQ(queue.GetRemainingBudget(), 0u);  // Should return 0, not negative
}

TEST_F(TaskQueueTest, IsBudgetExhaustedDetectsExhaustion) {
    TestQueue queue;
    queue.SetFrameBudget(1'000'000);

    EXPECT_FALSE(queue.IsBudgetExhausted());

    auto slot = CreateSlot(1'000'000);
    queue.TryEnqueue(std::move(slot));

    EXPECT_TRUE(queue.IsBudgetExhausted());
}

TEST_F(TaskQueueTest, GetBudgetReturnsConfiguredBudget) {
    TestQueue queue;
    TaskBudget budget(2'000'000, BudgetOverflowMode::Lenient);
    queue.SetBudget(budget);

    const auto& retrievedBudget = queue.GetBudget();
    EXPECT_EQ(retrievedBudget.gpuTimeBudgetNs, 2'000'000u);
    EXPECT_EQ(retrievedBudget.overflowMode, BudgetOverflowMode::Lenient);
}

// ============================================================================
// TASK BUDGET STRUCTURE TESTS (Task #342)
// ============================================================================

TEST(TaskBudget, DefaultConstructorCreatesUnlimitedBudget) {
    TaskBudget budget;

    EXPECT_EQ(budget.gpuTimeBudgetNs, std::numeric_limits<uint64_t>::max());
    EXPECT_EQ(budget.gpuMemoryBudgetBytes, std::numeric_limits<uint64_t>::max());
    EXPECT_EQ(budget.overflowMode, BudgetOverflowMode::Strict);
    EXPECT_TRUE(budget.IsUnlimited());
    EXPECT_TRUE(budget.IsStrict());
}

TEST(TaskBudget, ConstructorWithTimeAndMode) {
    TaskBudget budget(16'666'666, BudgetOverflowMode::Lenient);

    EXPECT_EQ(budget.gpuTimeBudgetNs, 16'666'666u);
    EXPECT_EQ(budget.overflowMode, BudgetOverflowMode::Lenient);
    EXPECT_FALSE(budget.IsUnlimited());
    EXPECT_TRUE(budget.IsLenient());
    EXPECT_FALSE(budget.IsStrict());
}

TEST(TaskBudget, ConstructorWithAllParameters) {
    TaskBudget budget(1'000'000, 10'000'000, BudgetOverflowMode::Strict);

    EXPECT_EQ(budget.gpuTimeBudgetNs, 1'000'000u);
    EXPECT_EQ(budget.gpuMemoryBudgetBytes, 10'000'000u);
    EXPECT_EQ(budget.overflowMode, BudgetOverflowMode::Strict);
}

TEST(TaskBudget, IsUnlimitedDetectsUnlimitedBudget) {
    TaskBudget unlimited;
    EXPECT_TRUE(unlimited.IsUnlimited());

    TaskBudget limited(1'000'000);
    EXPECT_FALSE(limited.IsUnlimited());
}

TEST(TaskBudget, IsStrictDetectsStrictMode) {
    TaskBudget strict(1'000'000, BudgetOverflowMode::Strict);
    EXPECT_TRUE(strict.IsStrict());
    EXPECT_FALSE(strict.IsLenient());
}

TEST(TaskBudget, IsLenientDetectsLenientMode) {
    TaskBudget lenient(1'000'000, BudgetOverflowMode::Lenient);
    EXPECT_TRUE(lenient.IsLenient());
    EXPECT_FALSE(lenient.IsStrict());
}

// ============================================================================
// BUDGET PRESETS TESTS (Task #342)
// ============================================================================

TEST(BudgetPresets, FPS60StrictHasCorrectValues) {
    const auto& budget = BudgetPresets::FPS60_Strict;

    EXPECT_EQ(budget.gpuTimeBudgetNs, 16'666'666u);  // 16.67ms
    EXPECT_EQ(budget.overflowMode, BudgetOverflowMode::Strict);
}

TEST(BudgetPresets, FPS30StrictHasCorrectValues) {
    const auto& budget = BudgetPresets::FPS30_Strict;

    EXPECT_EQ(budget.gpuTimeBudgetNs, 33'333'333u);  // 33.33ms
    EXPECT_EQ(budget.overflowMode, BudgetOverflowMode::Strict);
}

TEST(BudgetPresets, FPS120StrictHasCorrectValues) {
    const auto& budget = BudgetPresets::FPS120_Strict;

    EXPECT_EQ(budget.gpuTimeBudgetNs, 8'333'333u);  // 8.33ms
    EXPECT_EQ(budget.overflowMode, BudgetOverflowMode::Strict);
}

TEST(BudgetPresets, FPS60LenientHasCorrectValues) {
    const auto& budget = BudgetPresets::FPS60_Lenient;

    EXPECT_EQ(budget.gpuTimeBudgetNs, 16'666'666u);
    EXPECT_EQ(budget.overflowMode, BudgetOverflowMode::Lenient);
}

TEST(BudgetPresets, UnlimitedHasCorrectValues) {
    const auto& budget = BudgetPresets::Unlimited;

    EXPECT_TRUE(budget.IsUnlimited());
    EXPECT_EQ(budget.overflowMode, BudgetOverflowMode::Strict);
}

// ============================================================================
// INTEGRATION TESTS (Task #339 + #342)
// ============================================================================

TEST_F(TaskQueueTest, ClearResetsBudgetTracking) {
    TestQueue queue;
    queue.SetFrameBudget(1'000'000);

    auto slot1 = CreateSlot(600'000);
    queue.TryEnqueue(std::move(slot1));
    EXPECT_EQ(queue.GetRemainingBudget(), 400'000u);

    queue.Clear();
    EXPECT_EQ(queue.GetRemainingBudget(), 1'000'000u);  // Budget reset
    EXPECT_EQ(queue.GetQueuedCount(), 0u);
}

TEST_F(TaskQueueTest, EnqueueUncheckedIgnoresBudget) {
    TestQueue queue;
    queue.SetFrameBudget(100);  // Very small budget

    // EnqueueUnchecked should accept task regardless of budget
    auto slot = CreateSlot(1'000'000);  // Much larger than budget
    queue.EnqueueUnchecked(std::move(slot));

    EXPECT_EQ(queue.GetQueuedCount(), 1u);
    EXPECT_EQ(queue.GetTotalEstimatedCost(), 1'000'000u);
}

TEST_F(TaskQueueTest, ExecuteWithMetadataProvidesCostInformation) {
    TestQueue queue;
    queue.SetFrameBudget(10'000'000);

    auto slot1 = CreateSlot(100'000, 255);
    auto slot2 = CreateSlot(200'000, 128);
    queue.TryEnqueue(std::move(slot1));
    queue.TryEnqueue(std::move(slot2));

    std::vector<uint64_t> executedCosts;
    queue.ExecuteWithMetadata([&](const TestSlot& slot) {
        executedCosts.push_back(slot.estimatedCostNs);
    });

    ASSERT_EQ(executedCosts.size(), 2u);
    EXPECT_EQ(executedCosts[0], 100'000u);  // Priority 255 first
    EXPECT_EQ(executedCosts[1], 200'000u);  // Priority 128 second
}

// ============================================================================
// SPRINT 6.3: CAPACITY TRACKER INTEGRATION (PHASE 2.1)
// ============================================================================

TEST_F(TaskQueueTest, CapacityTrackerLinking) {
    TestQueue queue;
    TimelineCapacityTracker tracker;

    // Initially no tracker
    EXPECT_EQ(queue.GetCapacityTracker(), nullptr);

    // Link tracker
    queue.SetCapacityTracker(&tracker);
    EXPECT_EQ(queue.GetCapacityTracker(), &tracker);

    // Unlink tracker
    queue.SetCapacityTracker(nullptr);
    EXPECT_EQ(queue.GetCapacityTracker(), nullptr);
}

TEST_F(TaskQueueTest, RecordActualCostWithoutTracker) {
    TestQueue queue;
    queue.SetFrameBudget(10'000'000);

    auto slot = CreateSlot(1'000'000);
    queue.TryEnqueue(std::move(slot));

    // Should not crash without tracker
    queue.RecordActualCost(0, 500'000);
}

TEST_F(TaskQueueTest, RecordActualCostWithTracker) {
    TestQueue queue;
    TimelineCapacityTracker tracker;
    queue.SetFrameBudget(16'666'666);
    queue.SetCapacityTracker(&tracker);

    tracker.BeginFrame();

    // Enqueue and record actual cost
    auto slot = CreateSlot(2'000'000);  // Estimated: 2ms
    queue.TryEnqueue(std::move(slot));

    // Record actual execution time
    queue.RecordActualCost(0, 1'500'000);  // Actual: 1.5ms

    // Verify tracker received the measurement
    const auto& timeline = tracker.GetCurrentTimeline();
    EXPECT_EQ(timeline.gpuQueues[0].measuredNs, 1'500'000);
    EXPECT_EQ(timeline.gpuQueues[0].taskCount, 1);
}

TEST_F(TaskQueueTest, RecordActualCostInvalidIndex) {
    TestQueue queue;
    TimelineCapacityTracker tracker;
    queue.SetCapacityTracker(&tracker);

    tracker.BeginFrame();

    // Record with invalid index (no tasks enqueued)
    queue.RecordActualCost(0, 1'000'000);  // Should not crash

    // Verify tracker did not receive invalid measurement
    const auto& timeline = tracker.GetCurrentTimeline();
    EXPECT_EQ(timeline.gpuQueues[0].measuredNs, 0);
}

TEST_F(TaskQueueTest, CanEnqueueWithMeasuredBudgetNoTracker) {
    TestQueue queue;
    queue.SetFrameBudget(10'000'000);  // 10ms budget

    // Without tracker, falls back to estimate-based check
    auto slot1 = CreateSlot(5'000'000);  // 5ms
    EXPECT_TRUE(queue.CanEnqueueWithMeasuredBudget(slot1));

    // Enqueue first task
    queue.TryEnqueue(std::move(slot1));

    // Check second task (would exceed budget)
    auto slot2 = CreateSlot(6'000'000);  // 6ms
    EXPECT_FALSE(queue.CanEnqueueWithMeasuredBudget(slot2));  // 5ms + 6ms > 10ms
}

TEST_F(TaskQueueTest, CanEnqueueWithMeasuredBudgetWithTracker) {
    TestQueue queue;
    TimelineCapacityTracker tracker;
    queue.SetFrameBudget(16'666'666);  // 16.67ms budget
    queue.SetCapacityTracker(&tracker);

    tracker.BeginFrame();

    // Simulate actual GPU usage (8ms consumed)
    tracker.RecordGPUTime(8'000'000);

    // Check if we can enqueue a 5ms task
    auto slot1 = CreateSlot(5'000'000);  // 5ms
    EXPECT_TRUE(queue.CanEnqueueWithMeasuredBudget(slot1));  // 8ms + 5ms = 13ms < 16.67ms

    // Check if we can enqueue a 10ms task
    auto slot2 = CreateSlot(10'000'000);  // 10ms
    EXPECT_FALSE(queue.CanEnqueueWithMeasuredBudget(slot2));  // 8ms + 10ms = 18ms > 16.67ms
}

TEST_F(TaskQueueTest, CanEnqueueWithMeasuredBudgetLenientMode) {
    TestQueue queue;
    TimelineCapacityTracker tracker;

    TaskBudget budget(10'000'000, BudgetOverflowMode::Lenient);
    queue.SetBudget(budget);
    queue.SetCapacityTracker(&tracker);

    tracker.BeginFrame();

    // Consume all budget
    tracker.RecordGPUTime(10'000'000);

    // In lenient mode, should still accept tasks
    auto slot = CreateSlot(5'000'000);
    EXPECT_TRUE(queue.CanEnqueueWithMeasuredBudget(slot));
}

TEST_F(TaskQueueTest, CapacityTrackerFeedbackLoop) {
    TestQueue queue;
    TimelineCapacityTracker tracker;
    queue.SetFrameBudget(16'666'666);
    queue.SetCapacityTracker(&tracker);

    tracker.BeginFrame();

    // Enqueue 3 tasks
    queue.TryEnqueue(CreateSlot(2'000'000, 255));
    queue.TryEnqueue(CreateSlot(3'000'000, 200));
    queue.TryEnqueue(CreateSlot(1'000'000, 100));

    // Execute and record actual costs
    uint32_t slotIndex = 0;
    queue.ExecuteWithMetadata([&](const TestSlot& slot) {
        // Simulate actual execution being slightly different from estimate
        uint64_t actualCost = slot.estimatedCostNs + 100'000;  // +0.1ms overhead
        queue.RecordActualCost(slotIndex++, actualCost);
    });

    // Verify all measurements recorded
    const auto& timeline = tracker.GetCurrentTimeline();
    // Total: (2+0.1) + (3+0.1) + (1+0.1) = 6.3ms
    EXPECT_EQ(timeline.gpuQueues[0].measuredNs, 6'300'000);
    EXPECT_EQ(timeline.gpuQueues[0].taskCount, 3);
}

TEST_F(TaskQueueTest, MeasuredBudgetMoreAccurateThanEstimate) {
    TestQueue queue;
    TimelineCapacityTracker tracker;
    queue.SetFrameBudget(10'000'000);  // 10ms budget
    queue.SetCapacityTracker(&tracker);

    tracker.BeginFrame();

    // Estimate says 8ms used, but actual was only 5ms
    auto slot1 = CreateSlot(8'000'000);  // Estimated: 8ms
    queue.TryEnqueue(std::move(slot1));
    queue.RecordActualCost(0, 5'000'000);  // Actual: 5ms

    // Check if we can enqueue a 4ms task
    auto slot2 = CreateSlot(4'000'000);  // 4ms

    // With measured budget: 5ms + 4ms = 9ms < 10ms ✅
    EXPECT_TRUE(queue.CanEnqueueWithMeasuredBudget(slot2));

    // Without measured budget (estimate-based): 8ms + 4ms = 12ms > 10ms ❌
    EXPECT_FALSE(queue.GetRemainingBudget() >= 4'000'000);
}
