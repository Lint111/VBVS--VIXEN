/**
 * @file test_multidispatch_integration.cpp
 * @brief Integration tests for Sprint 6.2 MultiDispatchNode + TaskQueue
 *
 * Tests the integration between MultiDispatchNode and TaskQueue:
 * - Backward compatibility (QueueDispatch zero-cost bypass)
 * - Budget enforcement (TryQueueDispatch strict/lenient modes)
 * - Priority-based execution order
 * - Budget exhaustion handling
 * - Warning callbacks in lenient mode
 *
 * Sprint 6.2 - Task #343: Stress Tests (16h)
 */

#define NOMINMAX  // Prevent Windows macros from breaking std::numeric_limits<T>::max()

#include <gtest/gtest.h>
#include <vulkan/vulkan.h>
#include <vector>
#include <algorithm>

#include "Core/TaskQueue.h"
#include "Data/TaskBudget.h"
#include "Data/DispatchPass.h"

using namespace Vixen::RenderGraph;

// ============================================================================
// TEST FIXTURE
// ============================================================================

/**
 * @brief Integration test fixture simulating MultiDispatchNode usage patterns
 *
 * This fixture tests the TaskQueue<DispatchPass> integration without requiring
 * full Vulkan context. It validates the contract between MultiDispatchNode
 * and TaskQueue.
 */
class MultiDispatchIntegration : public ::testing::Test {
protected:
    using DispatchQueue = TaskQueue<DispatchPass>;
    using TaskSlot = DispatchQueue::TaskSlot;

    // Helper: Create a valid DispatchPass for testing
    static DispatchPass CreateValidDispatch(const char* debugName = "TestPass") {
        DispatchPass pass{};
        pass.pipeline = reinterpret_cast<VkPipeline>(0x1234);
        pass.layout = reinterpret_cast<VkPipelineLayout>(0x5678);
        pass.workGroupCount = {1, 1, 1};
        pass.debugName = debugName;
        return pass;
    }

    // Helper: Simulate QueueDispatch() behavior (zero-cost, no budget check)
    static void SimulateQueueDispatch(DispatchQueue& queue, DispatchPass&& pass) {
        TaskSlot slot;
        slot.data = std::move(pass);
        slot.estimatedCostNs = 0;  // Zero-cost = bypass budget
        slot.priority = 128;        // Default priority
        queue.EnqueueUnchecked(std::move(slot));
    }

    // Helper: Simulate TryQueueDispatch() behavior (budget-aware)
    static bool SimulateTryQueueDispatch(
        DispatchQueue& queue,
        DispatchPass&& pass,
        uint64_t estimatedCostNs,
        uint8_t priority = 128)
    {
        TaskSlot slot;
        slot.data = std::move(pass);
        slot.estimatedCostNs = estimatedCostNs;
        slot.priority = priority;
        return queue.TryEnqueue(std::move(slot));
    }

    // Helper: Execute all queued tasks and collect execution order
    static std::vector<std::string> ExecuteAndCollectOrder(DispatchQueue& queue) {
        std::vector<std::string> executionOrder;

        queue.ExecuteWithMetadata([&](const TaskSlot& slot) {
            executionOrder.push_back(slot.data.debugName);
        });

        return executionOrder;
    }
};

// ============================================================================
// BACKWARD COMPATIBILITY TESTS
// ============================================================================

/**
 * @brief Test: QueueDispatch() bypasses budget enforcement (Sprint 6.1 compatibility)
 *
 * Validates that zero-cost tasks (simulating QueueDispatch()) always succeed
 * regardless of budget, maintaining 100% backward compatibility.
 */
TEST_F(MultiDispatchIntegration, QueueDispatchBackwardCompatibility) {
    DispatchQueue queue;

    // Set strict budget of 1ms
    queue.SetBudget(TaskBudget{1'000'000, BudgetOverflowMode::Strict});

    // Queue 5 dispatches using QueueDispatch() pattern (zero-cost)
    for (int i = 0; i < 5; ++i) {
        std::string name = "Pass" + std::to_string(i);
        SimulateQueueDispatch(queue, CreateValidDispatch(name.c_str()));
    }
    // QueueDispatch always accepts (zero-cost bypass)

    EXPECT_EQ(queue.GetQueuedCount(), 5);
    EXPECT_EQ(queue.GetRemainingBudget(), 1'000'000) << "Zero-cost tasks don't consume budget";
}

/**
 * @brief Test: Mixed QueueDispatch + TryQueueDispatch behaves correctly
 *
 * Validates that zero-cost (QueueDispatch) and budget-aware (TryQueueDispatch)
 * can coexist in the same queue.
 */
TEST_F(MultiDispatchIntegration, MixedZeroCostAndBudgetAware) {
    DispatchQueue queue;
    queue.SetBudget(TaskBudget{5'000'000, BudgetOverflowMode::Strict});  // 5ms budget

    // Add legacy QueueDispatch pass (zero-cost)
    SimulateQueueDispatch(queue, CreateValidDispatch("Legacy1"));

    // Add budget-aware pass (2ms)
    bool aware1 = SimulateTryQueueDispatch(queue, CreateValidDispatch("Aware1"), 2'000'000);
    EXPECT_TRUE(aware1);

    // Add another legacy pass
    SimulateQueueDispatch(queue, CreateValidDispatch("Legacy2"));

    // Add budget-aware pass (4ms) - should fail (strict mode)
    bool aware2 = SimulateTryQueueDispatch(queue, CreateValidDispatch("Aware2"), 4'000'000);
    EXPECT_FALSE(aware2) << "Budget-aware pass should respect budget (3ms remaining)";

    EXPECT_EQ(queue.GetQueuedCount(), 3);
    EXPECT_EQ(queue.GetRemainingBudget(), 3'000'000);  // 5ms - 2ms = 3ms
}

// ============================================================================
// BUDGET ENFORCEMENT TESTS
// ============================================================================

/**
 * @brief Test: TryQueueDispatch respects strict budget limits
 *
 * Validates that budget-aware enqueue rejects over-budget tasks in strict mode.
 */
TEST_F(MultiDispatchIntegration, StrictBudgetEnforcement) {
    DispatchQueue queue;
    queue.SetBudget(TaskBudget{10'000'000, BudgetOverflowMode::Strict});  // 10ms

    // Enqueue tasks within budget
    EXPECT_TRUE(SimulateTryQueueDispatch(queue, CreateValidDispatch("Pass1"), 3'000'000));  // 3ms
    EXPECT_TRUE(SimulateTryQueueDispatch(queue, CreateValidDispatch("Pass2"), 4'000'000));  // 4ms
    EXPECT_EQ(queue.GetRemainingBudget(), 3'000'000);  // 10 - 3 - 4 = 3ms

    // Attempt to enqueue task exceeding budget
    EXPECT_FALSE(SimulateTryQueueDispatch(queue, CreateValidDispatch("Pass3"), 5'000'000))
        << "5ms task should be rejected (only 3ms remaining)";

    EXPECT_EQ(queue.GetQueuedCount(), 2) << "Rejected task not added to queue";
    EXPECT_EQ(queue.GetRemainingBudget(), 3'000'000) << "Budget unchanged after rejection";
}

/**
 * @brief Test: Lenient mode accepts over-budget tasks with warning
 *
 * Validates that lenient mode allows over-budget tasks and triggers callbacks.
 */
TEST_F(MultiDispatchIntegration, LenientModeWarningCallback) {
    DispatchQueue queue;
    queue.SetBudget(TaskBudget{10'000'000, BudgetOverflowMode::Lenient});  // 10ms lenient

    // Track warning callbacks
    struct WarningData {
        uint64_t taskCost;
        uint64_t budgetRemaining;
        uint64_t overflow;
        int callCount = 0;
    };
    WarningData warningLog;

    queue.SetWarningCallback([&](uint64_t newTotal, uint64_t budget, uint64_t taskCost) {
        warningLog.taskCost = taskCost;
        warningLog.budgetRemaining = budget;
        warningLog.overflow = newTotal;
        warningLog.callCount++;
    });

    // Enqueue within budget
    EXPECT_TRUE(SimulateTryQueueDispatch(queue, CreateValidDispatch("Pass1"), 6'000'000));
    EXPECT_EQ(warningLog.callCount, 0) << "No warning for within-budget task";

    // Enqueue over-budget task (12ms when only 4ms remaining)
    EXPECT_TRUE(SimulateTryQueueDispatch(queue, CreateValidDispatch("Pass2"), 12'000'000))
        << "Lenient mode accepts over-budget task";

    EXPECT_EQ(warningLog.callCount, 1) << "Warning callback invoked once";
    EXPECT_EQ(warningLog.taskCost, 12'000'000) << "Task cost";
    EXPECT_EQ(warningLog.budgetRemaining, 10'000'000) << "Budget limit";
    EXPECT_EQ(warningLog.overflow, 18'000'000) << "New total (6ms + 12ms)";

    EXPECT_EQ(queue.GetQueuedCount(), 2) << "Both tasks accepted in lenient mode";
}

/**
 * @brief Test: Zero budget in strict mode rejects all tasks
 */
TEST_F(MultiDispatchIntegration, ZeroBudgetStrictRejectsAll) {
    DispatchQueue queue;
    queue.SetBudget(TaskBudget{0, BudgetOverflowMode::Strict});

    // Budget-aware tasks should be rejected
    EXPECT_FALSE(SimulateTryQueueDispatch(queue, CreateValidDispatch("Pass1"), 1'000'000));
    EXPECT_EQ(queue.GetQueuedCount(), 0);

    // Zero-cost tasks (QueueDispatch pattern) still accepted
    SimulateQueueDispatch(queue, CreateValidDispatch("Legacy"));
    EXPECT_EQ(queue.GetQueuedCount(), 1);
}

/**
 * @brief Test: Zero budget in lenient mode accepts with warning
 */
TEST_F(MultiDispatchIntegration, ZeroBudgetLenientAcceptsWithWarning) {
    DispatchQueue queue;
    queue.SetBudget(TaskBudget{0, BudgetOverflowMode::Lenient});

    int warningCount = 0;
    queue.SetWarningCallback([&](uint64_t, uint64_t, uint64_t) {
        warningCount++;
    });

    // Budget-aware tasks accepted with warning
    EXPECT_TRUE(SimulateTryQueueDispatch(queue, CreateValidDispatch("Pass1"), 5'000'000));
    EXPECT_EQ(warningCount, 1);
    EXPECT_EQ(queue.GetQueuedCount(), 1);
}

// ============================================================================
// PRIORITY-BASED EXECUTION TESTS
// ============================================================================

/**
 * @brief Test: Tasks execute in priority order (highest first)
 *
 * Validates stable sort with priority (255=highest, 0=lowest) and insertion
 * order preservation for equal priorities.
 */
TEST_F(MultiDispatchIntegration, PriorityBasedExecutionOrder) {
    DispatchQueue queue;
    queue.SetBudget(BudgetPresets::Unlimited);

    // Enqueue tasks with varying priorities
    SimulateTryQueueDispatch(queue, CreateValidDispatch("Low1"), 1'000'000, 50);    // Priority 50
    SimulateTryQueueDispatch(queue, CreateValidDispatch("High1"), 1'000'000, 200);  // Priority 200
    SimulateTryQueueDispatch(queue, CreateValidDispatch("Med1"), 1'000'000, 128);   // Priority 128
    SimulateTryQueueDispatch(queue, CreateValidDispatch("High2"), 1'000'000, 200);  // Priority 200
    SimulateTryQueueDispatch(queue, CreateValidDispatch("Low2"), 1'000'000, 50);    // Priority 50

    // Execute and collect order
    auto executionOrder = ExecuteAndCollectOrder(queue);

    // Expected: Highest priority first, stable sort preserves insertion order
    ASSERT_EQ(executionOrder.size(), 5);
    EXPECT_EQ(executionOrder[0], "High1") << "First high-priority task";
    EXPECT_EQ(executionOrder[1], "High2") << "Second high-priority task (insertion order)";
    EXPECT_EQ(executionOrder[2], "Med1")  << "Medium-priority task";
    EXPECT_EQ(executionOrder[3], "Low1")  << "First low-priority task";
    EXPECT_EQ(executionOrder[4], "Low2")  << "Second low-priority task (insertion order)";
}

/**
 * @brief Test: Zero-cost tasks participate in priority ordering
 *
 * Validates that QueueDispatch() tasks (zero-cost) are sorted by priority
 * alongside budget-aware tasks.
 */
TEST_F(MultiDispatchIntegration, ZeroCostTasksRespectPriority) {
    DispatchQueue queue;
    queue.SetBudget(TaskBudget{5'000'000, BudgetOverflowMode::Strict});

    // Mix zero-cost (QueueDispatch) and budget-aware tasks with different priorities
    SimulateQueueDispatch(queue, CreateValidDispatch("Legacy1"));  // Priority 128 (default)
    SimulateTryQueueDispatch(queue, CreateValidDispatch("High"), 2'000'000, 255);
    SimulateQueueDispatch(queue, CreateValidDispatch("Legacy2"));  // Priority 128 (default)
    SimulateTryQueueDispatch(queue, CreateValidDispatch("Low"), 1'000'000, 64);

    auto executionOrder = ExecuteAndCollectOrder(queue);

    ASSERT_EQ(executionOrder.size(), 4);
    EXPECT_EQ(executionOrder[0], "High");     // Priority 255
    EXPECT_EQ(executionOrder[1], "Legacy1");  // Priority 128 (first)
    EXPECT_EQ(executionOrder[2], "Legacy2");  // Priority 128 (second)
    EXPECT_EQ(executionOrder[3], "Low");      // Priority 64
}

// ============================================================================
// BUDGET EXHAUSTION TESTS
// ============================================================================

/**
 * @brief Test: Budget exhaustion prevents further enqueues
 */
TEST_F(MultiDispatchIntegration, BudgetExhaustedRejection) {
    DispatchQueue queue;
    queue.SetBudget(TaskBudget{10'000'000, BudgetOverflowMode::Strict});

    // Fill budget exactly
    EXPECT_TRUE(SimulateTryQueueDispatch(queue, CreateValidDispatch("Pass1"), 6'000'000));
    EXPECT_TRUE(SimulateTryQueueDispatch(queue, CreateValidDispatch("Pass2"), 4'000'000));
    EXPECT_TRUE(queue.IsBudgetExhausted());
    EXPECT_EQ(queue.GetRemainingBudget(), 0);

    // Attempt to enqueue with exhausted budget
    EXPECT_FALSE(SimulateTryQueueDispatch(queue, CreateValidDispatch("Pass3"), 1))
        << "Even 1ns task rejected when budget exhausted";

    EXPECT_EQ(queue.GetQueuedCount(), 2);
}

/**
 * @brief Test: GetRemainingBudget() accurately tracks consumption
 */
TEST_F(MultiDispatchIntegration, RemainingBudgetTracking) {
    DispatchQueue queue;
    queue.SetBudget(TaskBudget{20'000'000, BudgetOverflowMode::Strict});

    EXPECT_EQ(queue.GetRemainingBudget(), 20'000'000);

    SimulateTryQueueDispatch(queue, CreateValidDispatch("Pass1"), 7'000'000);
    EXPECT_EQ(queue.GetRemainingBudget(), 13'000'000);

    SimulateTryQueueDispatch(queue, CreateValidDispatch("Pass2"), 5'000'000);
    EXPECT_EQ(queue.GetRemainingBudget(), 8'000'000);

    // Zero-cost task doesn't affect budget
    SimulateQueueDispatch(queue, CreateValidDispatch("Legacy"));
    EXPECT_EQ(queue.GetRemainingBudget(), 8'000'000);

    SimulateTryQueueDispatch(queue, CreateValidDispatch("Pass3"), 8'000'000);
    EXPECT_EQ(queue.GetRemainingBudget(), 0);
    EXPECT_TRUE(queue.IsBudgetExhausted());
}

// ============================================================================
// CONFIGURATION TESTS
// ============================================================================

/**
 * @brief Test: Budget presets work correctly
 *
 * Validates constexpr presets from TaskBudget.h match expected values.
 */
TEST_F(MultiDispatchIntegration, BudgetPresetsCorrect) {
    DispatchQueue queue;

    // FPS60 strict = 16.67ms
    queue.SetBudget(BudgetPresets::FPS60_Strict);
    EXPECT_EQ(queue.GetBudget().gpuTimeBudgetNs, 16'666'666);
    EXPECT_TRUE(queue.GetBudget().IsStrict());

    // FPS60 lenient = 16.67ms
    queue.SetBudget(BudgetPresets::FPS60_Lenient);
    EXPECT_EQ(queue.GetBudget().gpuTimeBudgetNs, 16'666'666);
    EXPECT_TRUE(queue.GetBudget().IsLenient());
}

/**
 * @brief Test: SetBudget mid-frame changes budget immediately
 *
 * Documents behavior: Budget changes apply to next TryEnqueue(), not retroactively.
 */
TEST_F(MultiDispatchIntegration, SetBudgetMidFrameBehavior) {
    DispatchQueue queue;
    queue.SetBudget(TaskBudget{10'000'000, BudgetOverflowMode::Strict});

    // Enqueue 6ms task
    EXPECT_TRUE(SimulateTryQueueDispatch(queue, CreateValidDispatch("Pass1"), 6'000'000));
    EXPECT_EQ(queue.GetRemainingBudget(), 4'000'000);

    // Change budget mid-frame to 5ms (less than already consumed)
    queue.SetBudget(TaskBudget{5'000'000, BudgetOverflowMode::Strict});

    // Budget calculation: 5ms - 6ms = 0 (clamped, exhausted)
    // Note: This is documented behavior - budget changes affect all queued tasks
    EXPECT_TRUE(queue.IsBudgetExhausted()) << "Budget exhausted after reduction";

    // Future enqueues rejected
    EXPECT_FALSE(SimulateTryQueueDispatch(queue, CreateValidDispatch("Pass2"), 1'000'000));
}

// ============================================================================
// EDGE CASE TESTS
// ============================================================================

/**
 * @brief Test: Empty queue execution is safe
 */
TEST_F(MultiDispatchIntegration, EmptyQueueExecutionSafe) {
    DispatchQueue queue;

    auto executionOrder = ExecuteAndCollectOrder(queue);
    EXPECT_EQ(executionOrder.size(), 0);
    EXPECT_EQ(queue.GetQueuedCount(), 0);
}

/**
 * @brief Test: Execution does not clear queue (manual Clear() required)
 */
TEST_F(MultiDispatchIntegration, ExecutionDoesNotAutoClear) {
    DispatchQueue queue;
    queue.SetBudget(BudgetPresets::Unlimited);

    SimulateTryQueueDispatch(queue, CreateValidDispatch("Pass1"), 1'000'000);
    SimulateTryQueueDispatch(queue, CreateValidDispatch("Pass2"), 1'000'000);
    EXPECT_EQ(queue.GetQueuedCount(), 2);

    ExecuteAndCollectOrder(queue);

    EXPECT_EQ(queue.GetQueuedCount(), 2) << "Queue not auto-cleared by Execute";

    // Manual clear required
    queue.Clear();
    EXPECT_EQ(queue.GetQueuedCount(), 0) << "Queue cleared after explicit Clear()";
}

/**
 * @brief Test: Multiple warning callbacks in lenient mode
 *
 * Documents behavior: Callback fires once per over-budget task.
 */
TEST_F(MultiDispatchIntegration, MultipleWarningsInLenientMode) {
    DispatchQueue queue;
    queue.SetBudget(TaskBudget{5'000'000, BudgetOverflowMode::Lenient});

    int warningCount = 0;
    queue.SetWarningCallback([&](uint64_t, uint64_t, uint64_t) {
        warningCount++;
    });

    // Enqueue 5 over-budget tasks
    for (int i = 0; i < 5; ++i) {
        std::string name = "Pass" + std::to_string(i);
        EXPECT_TRUE(SimulateTryQueueDispatch(queue, CreateValidDispatch(name.c_str()), 10'000'000));
    }

    EXPECT_EQ(warningCount, 5) << "Callback fires once per over-budget task";
}
