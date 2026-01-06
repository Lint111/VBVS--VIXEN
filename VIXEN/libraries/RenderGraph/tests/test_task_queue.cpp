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
