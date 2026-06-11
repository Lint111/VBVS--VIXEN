#pragma once

/**
 * @file TaskQueue.h
 * @brief Budget-aware priority task queue for RenderGraph timeline execution
 *
 * Sprint 6.2: TaskQueue System - Tasks #339, #342
 * Design Element: #37 TaskQueue System
 *
 * Provides a generic template for budget-constrained task scheduling with
 * priority-based execution ordering. Designed for single-threaded execution
 * within RenderGraph (no mutex required).
 *
 * Key Features:
 * - Budget-aware enqueue (rejects tasks that would exceed frame budget)
 * - Strict/lenient overflow modes (reject vs warn+accept)
 * - Priority-based execution (higher priority = earlier execution)
 * - Stable sorting (preserves insertion order for equal priorities)
 * - O(1) total cost queries (cached, not computed)
 * - Overflow-safe arithmetic
 *
 * @see DispatchPass for primary TTaskData type
 * @see TaskBudget for budget configuration
 * @see SlotTaskManager for similar budget-aware execution pattern
 */

#include <algorithm>
#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <string>
#include <vector>

#include "Data/TaskBudget.h"

namespace Vixen::RenderGraph {

// Forward declarations
class TimelineCapacityTracker;

/**
 * @brief Budget-aware priority task queue
 *
 * Single-threaded task queue with priority scheduling and budget enforcement.
 * Tasks are enqueued with cost estimates and executed in priority order.
 *
 * @tparam TTaskData Type of data stored per task (e.g., DispatchPass)
 *
 * Usage:
 * @code
 * // Option 1: Simple budget (strict mode by default)
 * TaskQueue<DispatchPass> queue;
 * queue.SetFrameBudget(16'666'666);  // 16.67ms in nanoseconds
 *
 * // Option 2: Full TaskBudget configuration
 * TaskBudget budget(16'666'666, BudgetOverflowMode::Lenient);
 * queue.SetBudget(budget);
 *
 * // Option 3: Use presets
 * queue.SetBudget(BudgetPresets::FPS60_Strict);
 *
 * TaskQueue<DispatchPass>::TaskSlot slot;
 * slot.data = myDispatch;
 * slot.priority = 128;
 * slot.estimatedCostNs = 100'000;  // 0.1ms
 *
 * if (queue.TryEnqueue(std::move(slot))) {
 *     // Task accepted within budget (or lenient mode)
 * }
 *
 * queue.Execute([&](const DispatchPass& pass) {
 *     RecordDispatch(cmdBuffer, pass);
 * });
 * @endcode
 */
template<typename TTaskData>
class TaskQueue {
public:
    /**
     * @brief Task slot containing data, priority, and cost estimates
     */
    struct TaskSlot {
        TTaskData data;                      ///< User task data
        uint8_t priority = 0;                ///< Execution priority (0=lowest, 255=highest)
        uint64_t estimatedCostNs = 0;        ///< GPU time estimate in nanoseconds
        uint64_t estimatedMemoryBytes = 0;   ///< Memory estimate (reserved for Phase 2)
        uint32_t insertionOrder = 0;         ///< Internal: for stable sort tie-breaking
    };

    /**
     * @brief Warning callback signature for lenient mode overflow
     *
     * Called when task exceeds budget in lenient mode.
     * Parameters: (newTotalCostNs, budgetNs, taskCostNs)
     */
    using WarningCallback = std::function<void(uint64_t, uint64_t, uint64_t)>;

    TaskQueue() = default;
    ~TaskQueue() = default;

    // Non-copyable (owns task data)
    TaskQueue(const TaskQueue&) = delete;
    TaskQueue& operator=(const TaskQueue&) = delete;

    // Movable
    TaskQueue(TaskQueue&&) noexcept = default;
    TaskQueue& operator=(TaskQueue&&) noexcept = default;

    /**
     * @brief Set budget configuration
     *
     * @param budget Budget constraints and overflow policy
     */
    void SetBudget(const TaskBudget& budget) {
        budget_ = budget;
    }

    /**
     * @brief Set the frame budget for this queue (strict mode)
     *
     * Convenience method that creates a TaskBudget with strict overflow mode.
     *
     * @param budgetNs Maximum total GPU time in nanoseconds (default: 16.67ms)
     */
    void SetFrameBudget(uint64_t budgetNs) {
        budget_.gpuTimeBudgetNs = budgetNs;
        budget_.overflowMode = BudgetOverflowMode::Strict;
    }

    /**
     * @brief Get the current budget configuration
     * @return TaskBudget structure with all budget parameters
     */
    [[nodiscard]] const TaskBudget& GetBudget() const {
        return budget_;
    }

    /**
     * @brief Get the current frame budget (time component only)
     * @return Frame budget in nanoseconds
     */
    [[nodiscard]] uint64_t GetFrameBudget() const {
        return budget_.gpuTimeBudgetNs;
    }

    /**
     * @brief Set warning callback for lenient mode overflow
     *
     * Called when task exceeds budget in lenient mode.
     * Use for logging or telemetry.
     *
     * @param callback Function to call on overflow (nullptr to disable)
     */
    void SetWarningCallback(WarningCallback callback) {
        warningCallback_ = std::move(callback);
    }

    /**
     * @brief Attempt to enqueue a task within budget constraints
     *
     * Behavior depends on overflow mode:
     * - **Strict**: Rejects tasks that would exceed budget (returns false)
     * - **Lenient**: Accepts all tasks, calls warning callback on overflow (returns true)
     *
     * Edge cases handled:
     * - Zero budget: All tasks rejected (strict), accepted with warning (lenient)
     * - Overflow protection: Checked arithmetic prevents wrap-around
     * - Zero-cost tasks: Always accepted regardless of budget
     *
     * @param slot Task slot to enqueue (moved on success)
     * @return true if task accepted, false if rejected (strict mode only)
     */
    bool TryEnqueue(TaskSlot&& slot) {
        const uint64_t budgetNs = budget_.gpuTimeBudgetNs;
        const uint64_t taskCost = slot.estimatedCostNs;

        // Zero budget handling
        if (budgetNs == 0) {
            if (budget_.IsStrict()) {
                return false;  // Strict: reject
            } else {
                // Lenient: accept with warning
                if (warningCallback_) {
                    warningCallback_(taskCost, 0, taskCost);
                }
                EnqueueUnchecked(std::move(slot));
                return true;
            }
        }

        // Overflow-safe addition check
        if (taskCost > std::numeric_limits<uint64_t>::max() - totalEstimatedCostNs_) {
            if (budget_.IsStrict()) {
                return false;  // Would overflow - reject in strict mode
            } else {
                // Lenient: clamp to max and accept with warning
                if (warningCallback_) {
                    warningCallback_(std::numeric_limits<uint64_t>::max(), budgetNs, taskCost);
                }
                EnqueueUnchecked(std::move(slot));
                return true;
            }
        }

        const uint64_t newTotal = totalEstimatedCostNs_ + taskCost;

        // Budget exceeded check
        if (newTotal > budgetNs) {
            if (budget_.IsStrict()) {
                return false;  // Strict: reject
            } else {
                // Lenient: accept with warning
                if (warningCallback_) {
                    warningCallback_(newTotal, budgetNs, taskCost);
                }
                // Fall through to accept task
            }
        }

        // Accept task
        slot.insertionOrder = nextInsertionOrder_++;
        slots_.push_back(std::move(slot));
        totalEstimatedCostNs_ = newTotal;
        ++activeCount_;
        needsSort_ = true;

        return true;
    }

    /**
     * @brief Enqueue a task without budget checking
     *
     * Use when budget enforcement is handled externally or for
     * mandatory tasks that must execute regardless of budget.
     *
     * @param slot Task slot to enqueue (moved)
     */
    void EnqueueUnchecked(TaskSlot&& slot) {
        slot.insertionOrder = nextInsertionOrder_++;

        // Overflow-safe addition (clamp to max)
        const uint64_t taskCost = slot.estimatedCostNs;
        if (taskCost <= std::numeric_limits<uint64_t>::max() - totalEstimatedCostNs_) {
            totalEstimatedCostNs_ += taskCost;
        } else {
            totalEstimatedCostNs_ = std::numeric_limits<uint64_t>::max();
        }

        slots_.push_back(std::move(slot));
        ++activeCount_;
        needsSort_ = true;
    }

    /**
     * @brief Execute all queued tasks in priority order
     *
     * Tasks execute from highest priority (255) to lowest (0).
     * Equal priorities maintain insertion order (stable sort).
     *
     * Safe to call on empty queue (no-op).
     *
     * @param executor Function called for each task's data
     */
    void Execute(const std::function<void(const TTaskData&)>& executor) {
        if (slots_.empty()) {
            return;  // Empty queue = no-op
        }

        SortIfNeeded();

        for (const auto& slot : slots_) {
            executor(slot.data);
        }
    }

    /**
     * @brief Execute tasks and pass slot metadata
     *
     * Extended executor that receives the full TaskSlot for
     * access to priority and cost information.
     *
     * @param executor Function called with full slot data
     */
    void ExecuteWithMetadata(const std::function<void(const TaskSlot&)>& executor) {
        if (slots_.empty()) {
            return;
        }

        SortIfNeeded();

        for (const auto& slot : slots_) {
            executor(slot);
        }
    }

    /**
     * @brief Clear all queued tasks and reset state
     *
     * Idempotent: safe to call multiple times.
     */
    void Clear() {
        slots_.clear();
        activeCount_ = 0;
        totalEstimatedCostNs_ = 0;
        nextInsertionOrder_ = 0;
        needsSort_ = false;
    }

    /**
     * @brief Get number of queued tasks
     * @return Task count
     */
    [[nodiscard]] uint32_t GetQueuedCount() const {
        return activeCount_;
    }

    /**
     * @brief Get total estimated cost of all queued tasks
     *
     * O(1) operation - cached value, not computed.
     *
     * @return Total estimated GPU time in nanoseconds
     */
    [[nodiscard]] uint64_t GetTotalEstimatedCost() const {
        return totalEstimatedCostNs_;
    }

    /**
     * @brief Get remaining budget capacity
     * @return Nanoseconds remaining before budget exhausted (0 if over budget)
     */
    [[nodiscard]] uint64_t GetRemainingBudget() const {
        const uint64_t budgetNs = budget_.gpuTimeBudgetNs;
        if (totalEstimatedCostNs_ >= budgetNs) {
            return 0;
        }
        return budgetNs - totalEstimatedCostNs_;
    }

    /**
     * @brief Check if queue is empty
     * @return true if no tasks queued
     */
    [[nodiscard]] bool IsEmpty() const {
        return activeCount_ == 0;
    }

    /**
     * @brief Check if budget is exhausted
     * @return true if total cost >= frame budget
     */
    [[nodiscard]] bool IsBudgetExhausted() const {
        return totalEstimatedCostNs_ >= budget_.gpuTimeBudgetNs;
    }

    /**
     * @brief Reserve capacity for expected task count
     *
     * Optimization to avoid reallocations during enqueue.
     *
     * @param capacity Expected number of tasks
     */
    void Reserve(size_t capacity) {
        slots_.reserve(capacity);
    }

    // =========================================================================
    // Sprint 6.3: Capacity Tracker Integration (Phase 2.1)
    // =========================================================================

    /**
     * @brief Link capacity tracker for feedback loop
     *
     * Enables recording of actual task execution times for adaptive scheduling.
     * TaskQueue delegates measurement recording to the tracker after execution.
     *
     * @param tracker Pointer to TimelineCapacityTracker (nullptr to disable)
     */
    void SetCapacityTracker(TimelineCapacityTracker* tracker) {
        capacityTracker_ = tracker;
    }

    /**
     * @brief Get linked capacity tracker
     * @return Pointer to tracker (nullptr if not set)
     */
    [[nodiscard]] TimelineCapacityTracker* GetCapacityTracker() const {
        return capacityTracker_;
    }

    /**
     * @brief Record actual execution cost for a task (feedback loop)
     *
     * Called after task execution with measured GPU/CPU time.
     * Updates capacity tracker for learning and adaptive scheduling.
     *
     * @param slotIndex Index of executed slot (0-based)
     * @param actualNs Measured execution time in nanoseconds
     */
    void RecordActualCost(uint32_t slotIndex, uint64_t actualNs);

    /**
     * @brief Check if task fits in measured remaining capacity
     *
     * Uses TimelineCapacityTracker's actual remaining budget instead of
     * estimated budget. More accurate than TryEnqueue() if tracker is linked.
     *
     * Falls back to estimate-based check if no tracker is linked.
     *
     * @param slot Task slot to check
     * @return true if task fits in actual remaining capacity
     */
    [[nodiscard]] bool CanEnqueueWithMeasuredBudget(const TaskSlot& slot) const;

private:
    /**
     * @brief Sort tasks by priority (descending), stable for equal priorities
     */
    void SortIfNeeded() {
        if (!needsSort_) {
            return;
        }

        // Stable sort: higher priority first, insertion order preserved for ties
        std::stable_sort(slots_.begin(), slots_.end(),
            [](const TaskSlot& a, const TaskSlot& b) {
                if (a.priority != b.priority) {
                    return a.priority > b.priority;  // Higher priority first
                }
                return a.insertionOrder < b.insertionOrder;  // Earlier insertion first
            });

        needsSort_ = false;
    }

    std::vector<TaskSlot> slots_;
    uint32_t activeCount_ = 0;
    uint64_t totalEstimatedCostNs_ = 0;
    TaskBudget budget_{16'666'666, BudgetOverflowMode::Strict};  // Default: 60 FPS strict
    uint32_t nextInsertionOrder_ = 0;
    bool needsSort_ = false;
    WarningCallback warningCallback_;  // Optional: for lenient mode warnings

    // Sprint 6.3: Capacity tracker integration (Phase 2.1)
    TimelineCapacityTracker* capacityTracker_ = nullptr;  // Optional: for feedback loop
};

// Forward declaration for explicit instantiation
struct DispatchPass;

}  // namespace Vixen::RenderGraph
