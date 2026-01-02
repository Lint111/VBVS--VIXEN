#pragma once

#include "Data/Core/ResourceConfig.h"
#include "Lifetime/SharedResource.h"  // For ResourceScope and SlotScopeToResourceScope
#include <cstdint>
#include <functional>
#include <vector>
#include <optional>

// Forward declaration from ResourceManagement namespace
namespace ResourceManagement {
    class ResourceBudgetManager;
    enum class ResourceScope : uint8_t;
    ResourceScope SlotScopeToResourceScope(uint8_t slotScopeValue);
}

namespace Vixen::RenderGraph {

// Forward declarations
class NodeInstance;
using ResourceManagement::ResourceBudgetManager;
using ResourceManagement::ResourceScope;
using ResourceManagement::SlotScopeToResourceScope;

/**
 * @brief Task execution status
 */
enum class TaskStatus : uint8_t {
    Pending,     // Not yet started
    Running,     // Currently executing
    Completed,   // Finished successfully
    Failed,      // Execution failed
    Skipped      // Skipped due to conditions
};

/**
 * @brief Phase F.2: Slot Task Context
 *
 * Provides per-task execution context for array-based slots.
 * Each task processes one array element independently.
 *
 * Example: TextureLoaderNode with 100 textures
 * - Creates 100 SlotTaskContexts (one per texture)
 * - Each task loads one texture independently
 * - Can execute in parallel based on budget
 */
struct SlotTaskContext {
    NodeInstance* node = nullptr;       // Owning node
    uint32_t taskIndex = 0;             // Index in task array (0-based)
    uint32_t totalTasks = 0;            // Total number of tasks

    // Array element indices this task processes
    uint32_t arrayStartIndex = 0;       // First array element
    uint32_t arrayCount = 1;            // Number of elements (usually 1)

    // Resource requirements (for budget-based scheduling)
    uint64_t estimatedMemoryBytes = 0;  // Estimated memory usage
    uint64_t estimatedTimeMs = 0;       // Estimated execution time

    // Execution state
    TaskStatus status = TaskStatus::Pending;
    std::optional<std::string> errorMessage;

    // Resource scope (from config SlotScope)
    SlotScope resourceScope = SlotScope::NodeLevel;

    // Helper: Is this task for a single array element?
    bool IsSingleElement() const { return arrayCount == 1; }

    // Helper: Get element index for single-element tasks
    uint32_t GetElementIndex() const { return arrayStartIndex; }

    /**
     * @brief Get the memory lifetime scope for this task's resources
     *
     * Maps the task's SlotScope to ResourceScope for memory management.
     * Use this when creating resources to ensure proper lifetime tracking.
     *
     * @return ResourceScope (Transient for task-local, Persistent for shared)
     */
    ResourceScope GetResourceScope() const {
        return SlotScopeToResourceScope(static_cast<uint8_t>(resourceScope));
    }
};

/**
 * @brief Task execution function signature
 *
 * NodeInstance implementations override this to provide task-level processing.
 *
 * @param context Task-specific context
 * @return true if task succeeded, false if failed
 */
using SlotTaskFunction = std::function<bool(SlotTaskContext& context)>;

/**
 * @brief Phase F.2: Slot Task Manager
 *
 * Manages task-based execution for array slots.
 * Coordinates with ResourceBudgetManager for intelligent scheduling.
 *
 * Workflow:
 * 1. Node specifies which slots drive task generation (via SlotScope::InstanceLevel)
 * 2. TaskManager creates SlotTaskContext for each array element
 * 3. Tasks execute sequentially or in parallel based on budget
 * 4. Results aggregated back to node
 */
class SlotTaskManager {
public:
    SlotTaskManager() = default;
    ~SlotTaskManager() = default;

    /**
     * @brief Generate tasks from array slot
     *
     * Creates one task per array element (for SlotScope::InstanceLevel slots).
     *
     * @param node Owning node instance
     * @param slotIndex Input slot index with array data
     * @param resourceScope SlotScope from config (determines task granularity)
     * @return Vector of task contexts
     */
    std::vector<SlotTaskContext> GenerateTasks(
        NodeInstance* node,
        uint32_t slotIndex,
        SlotScope resourceScope
    );

    /**
     * @brief Execute tasks sequentially
     *
     * Runs tasks one at a time in order.
     * Simple and safe for nodes without parallel support.
     *
     * @param tasks Task contexts to execute
     * @param taskFunction Function to execute per task
     * @return Number of successful tasks
     */
    uint32_t ExecuteSequential(
        std::vector<SlotTaskContext>& tasks,
        const SlotTaskFunction& taskFunction
    );

    /**
     * @brief Execute tasks in parallel (budget-aware)
     *
     * Runs tasks concurrently based on available resources.
     * Consults ResourceBudgetManager to determine parallelism level.
     *
     * @param tasks Task contexts to execute
     * @param taskFunction Function to execute per task
     * @param budgetManager Resource budget manager (optional)
     * @param maxParallelism Maximum concurrent tasks (0 = auto)
     * @return Number of successful tasks
     */
    uint32_t ExecuteParallel(
        std::vector<SlotTaskContext>& tasks,
        const SlotTaskFunction& taskFunction,
        ResourceBudgetManager* budgetManager = nullptr,
        uint32_t maxParallelism = 0
    );

    /**
     * @brief Get optimal parallelism level based on budget
     *
     * Analyzes task resource requirements and available budget
     * to determine safe parallel execution count.
     *
     * @param tasks Tasks to analyze
     * @param budgetManager Resource budget manager
     * @return Recommended number of concurrent tasks
     */
    uint32_t CalculateOptimalParallelism(
        const std::vector<SlotTaskContext>& tasks,
        ResourceBudgetManager* budgetManager
    ) const;

    // Statistics (Phase C: Enhanced with budget tracking)
    struct ExecutionStats {
        uint32_t totalTasks = 0;
        uint32_t completedTasks = 0;
        uint32_t failedTasks = 0;
        uint32_t skippedTasks = 0;
        uint64_t totalExecutionTimeMs = 0;

        // Phase C.3: Budget tracking
        uint64_t totalEstimatedMemory = 0;   // Sum of task estimates
        uint64_t totalActualMemory = 0;      // Sum of actual usage (if reported)
        uint32_t tasksOverBudget = 0;        // Tasks that exceeded estimate
        uint32_t tasksThrottled = 0;         // Tasks delayed due to budget
        uint32_t actualParallelism = 0;      // Parallelism level used
    };

    ExecutionStats GetLastExecutionStats() const { return lastStats_; }
    void ResetStats() { lastStats_ = ExecutionStats{}; }

    /**
     * @brief Report actual memory usage for a completed task
     *
     * Call after task completion to track actual vs estimated memory.
     * Used to improve future parallelism calculations.
     *
     * @param taskIndex Index of the completed task
     * @param actualBytes Actual memory used
     */
    void ReportActualMemory(uint32_t taskIndex, uint64_t actualBytes);

    /**
     * @brief Get memory estimation accuracy for last execution
     *
     * @return Ratio of actual/estimated (1.0 = perfect, >1.0 = underestimated)
     */
    float GetEstimationAccuracy() const;

private:
    ExecutionStats lastStats_;

    // Phase C.3: Per-task memory tracking
    std::vector<uint64_t> estimatedMemoryUsage_;  // Estimated per task
    std::vector<uint64_t> actualMemoryUsage_;     // Actual per task (reported)
};

} // namespace Vixen::RenderGraph
