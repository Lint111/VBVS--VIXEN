#pragma once

/**
 * @file TaskBudget.h
 * @brief Budget configuration for TaskQueue resource constraints
 *
 * Sprint 6.2: TaskQueue System - Task #342
 * Design Element: #37 TaskQueue System
 *
 * Defines budget constraints and overflow behavior for TaskQueue operations.
 * Supports GPU time budgets (Phase 1) with extensibility for memory budgets (Phase 2).
 *
 * @see TaskQueue for usage context
 */

#include <cstdint>
#include <limits>

namespace Vixen::RenderGraph {

/**
 * @brief Overflow behavior when task would exceed budget
 */
enum class BudgetOverflowMode : uint8_t {
    /**
     * @brief Reject tasks that exceed budget
     *
     * TryEnqueue() returns false, task is not queued.
     * Use for hard real-time constraints (e.g., 60 FPS guarantee).
     */
    Strict = 0,

    /**
     * @brief Accept tasks but log warnings
     *
     * TryEnqueue() returns true, task is queued, warning logged.
     * Use for soft constraints where frame drops are acceptable.
     */
    Lenient = 1
};

/**
 * @brief Budget constraints for TaskQueue scheduling
 *
 * Encapsulates frame budget limits and overflow policy.
 * Immutable after construction for thread-safety (future proofing).
 *
 * Usage:
 * @code
 * // 60 FPS target with strict enforcement
 * TaskBudget budget60fps(16'666'666, BudgetOverflowMode::Strict);
 *
 * // 30 FPS target with lenient overflow
 * TaskBudget budget30fps(33'333'333, BudgetOverflowMode::Lenient);
 *
 * // Unlimited budget (accepts all tasks)
 * TaskBudget unlimitedBudget;
 * @endcode
 */
struct TaskBudget {
    /**
     * @brief Maximum GPU time per frame in nanoseconds
     *
     * Default: uint64_t::max() (effectively unlimited)
     * Common values:
     * - 60 FPS: 16'666'666 ns (16.67ms)
     * - 30 FPS: 33'333'333 ns (33.33ms)
     * - 120 FPS: 8'333'333 ns (8.33ms)
     */
    uint64_t gpuTimeBudgetNs = std::numeric_limits<uint64_t>::max();

    /**
     * @brief Maximum GPU memory per frame in bytes
     *
     * Reserved for Phase 2 (currently unused).
     * Default: uint64_t::max() (effectively unlimited)
     */
    uint64_t gpuMemoryBudgetBytes = std::numeric_limits<uint64_t>::max();

    /**
     * @brief Overflow handling policy
     *
     * Default: Strict (reject tasks that would exceed budget)
     */
    BudgetOverflowMode overflowMode = BudgetOverflowMode::Strict;

    /**
     * @brief Default constructor: unlimited budget
     */
    TaskBudget() = default;

    /**
     * @brief Construct with GPU time budget and overflow mode
     *
     * @param gpuTimeBudgetNs_  GPU time limit in nanoseconds
     * @param overflowMode_     Overflow handling policy
     */
    explicit constexpr TaskBudget(
        uint64_t gpuTimeBudgetNs_,
        BudgetOverflowMode overflowMode_ = BudgetOverflowMode::Strict)
        : gpuTimeBudgetNs(gpuTimeBudgetNs_)
        , overflowMode(overflowMode_)
    {}

    /**
     * @brief Construct with full budget parameters
     *
     * @param gpuTimeBudgetNs_      GPU time limit in nanoseconds
     * @param gpuMemoryBudgetBytes_ GPU memory limit in bytes (Phase 2)
     * @param overflowMode_         Overflow handling policy
     */
    constexpr TaskBudget(
        uint64_t gpuTimeBudgetNs_,
        uint64_t gpuMemoryBudgetBytes_,
        BudgetOverflowMode overflowMode_)
        : gpuTimeBudgetNs(gpuTimeBudgetNs_)
        , gpuMemoryBudgetBytes(gpuMemoryBudgetBytes_)
        , overflowMode(overflowMode_)
    {}

    /**
     * @brief Check if budget is effectively unlimited
     *
     * @return true if no practical budget constraints
     */
    [[nodiscard]] bool IsUnlimited() const {
        return gpuTimeBudgetNs == std::numeric_limits<uint64_t>::max();
    }

    /**
     * @brief Check if strict overflow mode is enabled
     *
     * @return true if tasks exceeding budget should be rejected
     */
    [[nodiscard]] bool IsStrict() const {
        return overflowMode == BudgetOverflowMode::Strict;
    }

    /**
     * @brief Check if lenient overflow mode is enabled
     *
     * @return true if tasks exceeding budget should be accepted with warning
     */
    [[nodiscard]] bool IsLenient() const {
        return overflowMode == BudgetOverflowMode::Lenient;
    }
};

/**
 * @brief Common budget presets for convenience
 */
namespace BudgetPresets {
    /// @brief 60 FPS target (16.67ms) with strict enforcement
    inline constexpr TaskBudget FPS60_Strict{16'666'666, BudgetOverflowMode::Strict};

    /// @brief 30 FPS target (33.33ms) with strict enforcement
    inline constexpr TaskBudget FPS30_Strict{33'333'333, BudgetOverflowMode::Strict};

    /// @brief 120 FPS target (8.33ms) with strict enforcement
    inline constexpr TaskBudget FPS120_Strict{8'333'333, BudgetOverflowMode::Strict};

    /// @brief 60 FPS target with lenient overflow (allows frame drops)
    inline constexpr TaskBudget FPS60_Lenient{16'666'666, BudgetOverflowMode::Lenient};

    /// @brief Unlimited budget (accepts all tasks)
    inline constexpr TaskBudget Unlimited{};
}

}  // namespace Vixen::RenderGraph
