// Copyright (C) 2025 Lior Yanai (eLiorg)
// Licensed under the GPL-3.0 License.
// See LICENSE file in the project root for full license information.
#pragma once

/**
 * @file TBBVirtualTaskExecutor.h
 * @brief TBB-based parallel executor for virtual tasks
 *
 * Sprint 6.5: Task-Level Parallelism Architecture
 * Design Element: #38 Timeline Capacity Tracker (Virtual Task Extension)
 *
 * TBBVirtualTaskExecutor schedules VirtualTasks (node, taskIndex pairs)
 * in parallel using Intel TBB's flow_graph. Tasks are scheduled respecting
 * resource dependencies while maximizing parallelism.
 *
 * Key Features:
 * - Builds TBB flow_graph from TaskDependencyGraph
 * - Phase barriers (Setup → Compile → Execute → Cleanup)
 * - Fallback to sequential for non-opted nodes
 * - Exception handling and error collection
 * - Statistics and profiling
 *
 * Usage:
 * @code
 * TBBVirtualTaskExecutor executor;
 *
 * // Build from tracker and nodes
 * executor.Build(virtualAccessTracker, executionOrder);
 *
 * // Execute phases in order
 * executor.ExecutePhase(VirtualTaskPhase::Setup);
 * executor.ExecutePhase(VirtualTaskPhase::Compile);
 * executor.ExecutePhase(VirtualTaskPhase::Execute);
 * executor.ExecutePhase(VirtualTaskPhase::Cleanup);
 * @endcode
 *
 * @see VirtualResourceAccessTracker for resource conflict detection
 * @see TaskDependencyGraph for dependency resolution
 * @see VirtualTask for task definition
 */

#include "VirtualTask.h"
#include "TaskDependencyGraph.h"
#include "VirtualResourceAccessTracker.h"
#include <vector>
#include <memory>
#include <atomic>
#include <mutex>
#include <string>

namespace Vixen::RenderGraph {

// Forward declarations
class NodeInstance;

/**
 * @brief Execution statistics for virtual task executor
 */
struct VirtualTaskExecutorStats {
    size_t totalTasks = 0;           ///< Total virtual tasks
    size_t parallelTasks = 0;        ///< Tasks that ran in parallel
    size_t sequentialTasks = 0;      ///< Tasks that ran sequentially
    size_t optedInNodes = 0;         ///< Nodes with parallelism enabled
    size_t totalNodes = 0;           ///< Total nodes processed
    size_t failedTasks = 0;          ///< Tasks that threw exceptions

    double buildTimeMs = 0.0;        ///< Time to build flow graph
    double executionTimeMs = 0.0;    ///< Total execution time

    size_t maxParallelLevel = 0;     ///< Maximum parallel tasks at any level
    size_t criticalPathLength = 0;   ///< Length of critical path

    /**
     * @brief Get parallelism efficiency (parallel / total)
     */
    [[nodiscard]] float GetParallelismRatio() const {
        return totalTasks > 0 ? static_cast<float>(parallelTasks) / totalTasks : 0.0f;
    }

    /**
     * @brief Get opt-in ratio
     */
    [[nodiscard]] float GetOptInRatio() const {
        return totalNodes > 0 ? static_cast<float>(optedInNodes) / totalNodes : 0.0f;
    }
};

/**
 * @brief Error information for failed tasks
 */
struct VirtualTaskError {
    VirtualTaskId task;
    std::string errorMessage;
    VirtualTaskPhase phase = VirtualTaskPhase::Execute;
};

/**
 * @brief TBB-based parallel executor for virtual tasks
 *
 * Schedules VirtualTasks using TBB flow_graph while respecting
 * resource dependencies from TaskDependencyGraph.
 *
 * Thread Safety: Build is NOT thread-safe. ExecutePhase is thread-safe.
 */
class TBBVirtualTaskExecutor {
public:
    TBBVirtualTaskExecutor() = default;
    ~TBBVirtualTaskExecutor();

    // Non-copyable, movable
    TBBVirtualTaskExecutor(const TBBVirtualTaskExecutor&) = delete;
    TBBVirtualTaskExecutor& operator=(const TBBVirtualTaskExecutor&) = delete;
    TBBVirtualTaskExecutor(TBBVirtualTaskExecutor&&) noexcept;
    TBBVirtualTaskExecutor& operator=(TBBVirtualTaskExecutor&&) noexcept;

    // =========================================================================
    // Building
    // =========================================================================

    /**
     * @brief Build executor from resource tracker and node list
     *
     * Creates TBB flow_graph structures for all phases.
     *
     * @param tracker Virtual resource access tracker
     * @param executionOrder Original node execution order
     */
    void Build(
        const VirtualResourceAccessTracker& tracker,
        const std::vector<NodeInstance*>& executionOrder
    );

    /**
     * @brief Clear all execution state
     */
    void Clear();

    /**
     * @brief Check if executor is ready
     */
    [[nodiscard]] bool IsBuilt() const { return isBuilt_; }

    // =========================================================================
    // Execution
    // =========================================================================

    /**
     * @brief Execute all tasks for a given phase
     *
     * Runs all virtual tasks in parallel according to dependency graph.
     * Blocks until all tasks complete.
     *
     * @param phase Phase to execute
     * @return true if all tasks succeeded
     */
    bool ExecutePhase(VirtualTaskPhase phase);

    /**
     * @brief Execute all phases in order
     *
     * Convenience method that calls ExecutePhase for each phase.
     *
     * @return true if all phases succeeded
     */
    bool ExecuteAllPhases();

    /**
     * @brief Check if execution is enabled
     */
    [[nodiscard]] bool IsEnabled() const { return enabled_; }

    /**
     * @brief Enable/disable virtual task execution
     *
     * When disabled, falls back to TBBGraphExecutor behavior.
     */
    void SetEnabled(bool enabled) { enabled_ = enabled; }

    // =========================================================================
    // Error Handling
    // =========================================================================

    /**
     * @brief Get errors from last execution
     */
    [[nodiscard]] const std::vector<VirtualTaskError>& GetErrors() const { return errors_; }

    /**
     * @brief Check if last execution had errors
     */
    [[nodiscard]] bool HasErrors() const { return !errors_.empty(); }

    /**
     * @brief Clear error list
     */
    void ClearErrors() { errors_.clear(); }

    // =========================================================================
    // Statistics
    // =========================================================================

    /**
     * @brief Get execution statistics
     */
    [[nodiscard]] const VirtualTaskExecutorStats& GetStats() const { return stats_; }

    /**
     * @brief Reset statistics
     */
    void ResetStats();

    /**
     * @brief Get the dependency graph
     */
    [[nodiscard]] const TaskDependencyGraph& GetDependencyGraph() const { return depGraph_; }

private:
    // State
    bool isBuilt_ = false;
    bool enabled_ = true;

    // Components
    TaskDependencyGraph depGraph_;
    std::vector<NodeInstance*> nodes_;
    std::vector<VirtualTask> tasks_;

    // Parallel levels (tasks grouped by execution wave)
    std::vector<std::vector<VirtualTaskId>> parallelLevels_;

    // Error tracking
    std::vector<VirtualTaskError> errors_;
    std::mutex errorMutex_;

    // Statistics
    VirtualTaskExecutorStats stats_;

    /**
     * @brief Build virtual tasks from tracker
     */
    void BuildTasks(const VirtualResourceAccessTracker& tracker);

    /**
     * @brief Execute a single task
     */
    bool ExecuteTask(VirtualTask& task, VirtualTaskPhase phase);

    /**
     * @brief Record a task error
     */
    void RecordError(const VirtualTaskId& task, const std::string& message, VirtualTaskPhase phase);

    /**
     * @brief Execute tasks in a level in parallel
     */
    bool ExecuteLevel(const std::vector<VirtualTaskId>& level, VirtualTaskPhase phase);

    /**
     * @brief Find task by ID
     */
    VirtualTask* FindTask(const VirtualTaskId& id);
};

} // namespace Vixen::RenderGraph
