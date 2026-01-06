#pragma once

/**
 * @file TaskQueue.h
 * @brief Budget-aware priority task queue for RenderGraph timeline execution
 *
 * Sprint 6.2: TaskQueue System - Task #339
 * Design Element: #37 TaskQueue System
 *
 * Provides a generic template for budget-constrained task scheduling with
 * priority-based execution ordering. Designed for single-threaded execution
 * within RenderGraph (no mutex required).
 *
 * Key Features:
 * - Budget-aware enqueue (rejects tasks that would exceed frame budget)
 * - Priority-based execution (higher priority = earlier execution)
 * - Stable sorting (preserves insertion order for equal priorities)
 * - O(1) total cost queries (cached, not computed)
 * - Overflow-safe arithmetic
 *
 * @see DispatchPass for primary TTaskData type
 * @see SlotTaskManager for similar budget-aware execution pattern
 */

#include <algorithm>
#include <cstdint>
#include <functional>
#include <limits>
#include <vector>

namespace Vixen::RenderGraph {

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
 * TaskQueue<DispatchPass> queue;
 * queue.SetFrameBudget(16'666'666);  // 16.67ms in nanoseconds
 *
 * TaskQueue<DispatchPass>::TaskSlot slot;
 * slot.data = myDispatch;
 * slot.priority = 128;
 * slot.estimatedCostNs = 100'000;  // 0.1ms
 *
 * if (queue.TryEnqueue(std::move(slot))) {
 *     // Task accepted within budget
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

    TaskQueue() = default;
    ~TaskQueue() = default;

    // Non-copyable (owns task data)
    TaskQueue(const TaskQueue&) = delete;
    TaskQueue& operator=(const TaskQueue&) = delete;

    // Movable
    TaskQueue(TaskQueue&&) noexcept = default;
    TaskQueue& operator=(TaskQueue&&) noexcept = default;

    /**
     * @brief Set the frame budget for this queue
     *
     * @param budgetNs Maximum total GPU time in nanoseconds (default: 16.67ms)
     */
    void SetFrameBudget(uint64_t budgetNs) {
        frameBudgetNs_ = budgetNs;
    }

    /**
     * @brief Get the current frame budget
     * @return Frame budget in nanoseconds
     */
    [[nodiscard]] uint64_t GetFrameBudget() const {
        return frameBudgetNs_;
    }

    /**
     * @brief Attempt to enqueue a task within budget constraints
     *
     * Edge cases handled:
     * - Zero budget: All tasks rejected
     * - Overflow protection: Checked arithmetic prevents wrap-around
     * - Zero-cost tasks: Always accepted if budget > 0
     *
     * @param slot Task slot to enqueue (moved on success)
     * @return true if task accepted, false if would exceed budget
     */
    bool TryEnqueue(TaskSlot&& slot) {
        // Zero budget = reject everything
        if (frameBudgetNs_ == 0) {
            return false;
        }

        // Overflow-safe addition check
        const uint64_t taskCost = slot.estimatedCostNs;
        if (taskCost > std::numeric_limits<uint64_t>::max() - totalEstimatedCostNs_) {
            return false;  // Would overflow
        }

        const uint64_t newTotal = totalEstimatedCostNs_ + taskCost;
        if (newTotal > frameBudgetNs_) {
            return false;  // Would exceed budget
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
     * @return Nanoseconds remaining before budget exhausted
     */
    [[nodiscard]] uint64_t GetRemainingBudget() const {
        if (totalEstimatedCostNs_ >= frameBudgetNs_) {
            return 0;
        }
        return frameBudgetNs_ - totalEstimatedCostNs_;
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
        return totalEstimatedCostNs_ >= frameBudgetNs_;
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
    uint64_t frameBudgetNs_ = 16'666'666;  // Default: 16.67ms (60 FPS target)
    uint32_t nextInsertionOrder_ = 0;
    bool needsSort_ = false;
};

// Forward declaration for explicit instantiation
struct DispatchPass;

}  // namespace Vixen::RenderGraph
