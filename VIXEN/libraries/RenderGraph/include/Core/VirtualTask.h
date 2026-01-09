// Copyright (C) 2025 Lior Yanai (eLiorg)
// Licensed under the GPL-3.0 License.
// See LICENSE file in the project root for full license information.
#pragma once

/**
 * @file VirtualTask.h
 * @brief Task-level parallelism scheduling units for render graphs
 *
 * Sprint 6.5: Task-Level Parallelism Architecture
 * Design Element: #38 Timeline Capacity Tracker (Virtual Task Extension)
 *
 * VirtualTask represents the atomic unit of work that can be scheduled
 * independently by TBBVirtualTaskExecutor. Each VirtualTask corresponds
 * to a (NodeInstance, taskIndex) pair, enabling finer-grained parallelism
 * than node-level scheduling.
 *
 * Key Concepts:
 * - VirtualTaskId: Unique identifier for a task (node + taskIndex)
 * - VirtualTask: Full task with execution function and metadata
 * - NodeLifecyclePhase: Setup, Compile, Execute, Cleanup
 *
 * Usage:
 * @code
 * VirtualTaskId taskA{nodeA, 0};  // NodeA, first bundle
 * VirtualTaskId taskB{nodeA, 1};  // NodeA, second bundle
 *
 * VirtualTask task;
 * task.id = taskA;
 * task.execute = [nodeA]() { nodeA->ExecuteTask(0); };
 * task.profiles = nodeA->GetTaskProfiles(0);  // Cost from profiles
 * @endcode
 *
 * @see VirtualResourceAccessTracker for per-task resource tracking
 * @see TaskDependencyGraph for task-level dependency resolution
 * @see TBBVirtualTaskExecutor for parallel execution
 */

#include <cstdint>
#include <functional>
#include <vector>
#include <string>
#include "ITaskProfile.h"

namespace Vixen::RenderGraph {

// Forward declarations
class NodeInstance;

/**
 * @brief Execution phases for virtual tasks
 *
 * Each phase is executed for all nodes before proceeding to the next.
 * Within a phase, VirtualTasks can execute in parallel if no conflicts.
 *
 * Note: This is distinct from NodeLifecyclePhase in GraphLifecycleHooks.h
 * which has Pre/Post variants for hook callbacks.
 */
enum class VirtualTaskPhase : uint8_t {
    Setup,      ///< Graph-scope initialization (once per compilation)
    Compile,    ///< Resource allocation and pipeline creation
    Execute,    ///< Per-frame execution
    Cleanup     ///< Resource destruction
};

/**
 * @brief Convert virtual task phase to string for debugging
 */
inline const char* ToString(VirtualTaskPhase phase) {
    switch (phase) {
        case VirtualTaskPhase::Setup:   return "Setup";
        case VirtualTaskPhase::Compile: return "Compile";
        case VirtualTaskPhase::Execute: return "Execute";
        case VirtualTaskPhase::Cleanup: return "Cleanup";
        default: return "Unknown";
    }
}

/**
 * @brief Unique identifier for a virtual task
 *
 * A VirtualTaskId represents a specific (NodeInstance, taskIndex) pair.
 * This is the atomic unit for task-level parallelism - each bundle
 * in a multi-bundle node gets its own VirtualTaskId.
 *
 * Example:
 * - NodeA with 3 bundles → VirtualTaskId{NodeA,0}, {NodeA,1}, {NodeA,2}
 * - NodeB with 1 bundle → VirtualTaskId{NodeB,0}
 */
struct VirtualTaskId {
    NodeInstance* node = nullptr;   ///< The owning node
    uint32_t taskIndex = 0;         ///< Index within node's bundles (0 for single-bundle nodes)

    /**
     * @brief Check equality
     */
    bool operator==(const VirtualTaskId& other) const {
        return node == other.node && taskIndex == other.taskIndex;
    }

    /**
     * @brief Check inequality
     */
    bool operator!=(const VirtualTaskId& other) const {
        return !(*this == other);
    }

    /**
     * @brief Check if this is a valid task ID
     */
    [[nodiscard]] bool IsValid() const {
        return node != nullptr;
    }

    /**
     * @brief Create invalid task ID (sentinel value)
     */
    static VirtualTaskId Invalid() {
        return VirtualTaskId{nullptr, UINT32_MAX};
    }
};

/**
 * @brief Hash function for VirtualTaskId
 *
 * Enables use in unordered containers (unordered_map, unordered_set).
 * Uses pointer hash combined with task index.
 */
struct VirtualTaskIdHash {
    size_t operator()(const VirtualTaskId& id) const {
        // Combine node pointer hash with task index
        // Use bit mixing to distribute hash values evenly
        size_t nodeHash = std::hash<NodeInstance*>{}(id.node);
        size_t indexHash = std::hash<uint32_t>{}(id.taskIndex);

        // XOR with shifted index hash for better distribution
        return nodeHash ^ (indexHash << 16) ^ (indexHash >> 16);
    }
};

/**
 * @brief Execution state for a virtual task
 */
enum class VirtualTaskState : uint8_t {
    Pending,    ///< Not yet ready (dependencies not satisfied)
    Ready,      ///< All dependencies satisfied, waiting to execute
    Running,    ///< Currently executing
    Completed,  ///< Successfully completed
    Failed      ///< Execution failed (exception thrown)
};

/**
 * @brief Convert task state to string for debugging
 */
inline const char* ToString(VirtualTaskState state) {
    switch (state) {
        case VirtualTaskState::Pending:   return "Pending";
        case VirtualTaskState::Ready:     return "Ready";
        case VirtualTaskState::Running:   return "Running";
        case VirtualTaskState::Completed: return "Completed";
        case VirtualTaskState::Failed:    return "Failed";
        default: return "Unknown";
    }
}

/**
 * @brief Full virtual task with execution function and metadata
 *
 * VirtualTask is the complete scheduling unit including:
 * - Identity (VirtualTaskId)
 * - Execution function (callable)
 * - Cost estimation for budget-aware scheduling
 * - Dependencies for correct ordering
 * - State tracking
 *
 * Thread Safety: VirtualTask itself is not thread-safe.
 * TBBVirtualTaskExecutor manages concurrent access.
 */
struct VirtualTask {
    VirtualTaskId id;                           ///< Unique identifier

    /// Execution function - captures node and taskIndex in closure
    std::function<void()> execute;

    uint8_t priority = 128;                     ///< Execution priority (0=highest, 255=lowest)

    /// Tasks that must complete before this one
    std::vector<VirtualTaskId> dependencies;

    /// Task profiles for timing/calibration (non-owning, owned by registry)
    /// Multiple profiles enable composable sub-task measurement
    std::vector<ITaskProfile*> profiles;

    /// Set to true when node-level code has already profiled this task
    /// Executor skips profiling if this is true (avoids double-timing)
    bool profiled = false;

    VirtualTaskState state = VirtualTaskState::Pending;

    /// Error message if state == Failed
    std::string errorMessage;

    // =========================================================================
    // Convenience Methods
    // =========================================================================

    /**
     * @brief Check if task is ready to execute
     */
    [[nodiscard]] bool IsReady() const {
        return state == VirtualTaskState::Ready;
    }

    /**
     * @brief Check if task has completed (success or failure)
     */
    [[nodiscard]] bool IsComplete() const {
        return state == VirtualTaskState::Completed || state == VirtualTaskState::Failed;
    }

    /**
     * @brief Check if task failed
     */
    [[nodiscard]] bool IsFailed() const {
        return state == VirtualTaskState::Failed;
    }

    /**
     * @brief Check if task has dependencies
     */
    [[nodiscard]] bool HasDependencies() const {
        return !dependencies.empty();
    }

    /**
     * @brief Get dependency count
     */
    [[nodiscard]] size_t GetDependencyCount() const {
        return dependencies.size();
    }

    /**
     * @brief Mark task as ready
     */
    void MarkReady() {
        state = VirtualTaskState::Ready;
    }

    /**
     * @brief Mark task as running
     */
    void MarkRunning() {
        state = VirtualTaskState::Running;
    }

    /**
     * @brief Mark task as completed
     */
    void MarkCompleted() {
        state = VirtualTaskState::Completed;
    }

    /**
     * @brief Mark task as failed with error message
     */
    void MarkFailed(const std::string& error) {
        state = VirtualTaskState::Failed;
        errorMessage = error;
    }

    // =========================================================================
    // Profile Methods
    // =========================================================================

    /**
     * @brief Check if task has profiles attached
     */
    [[nodiscard]] bool HasProfiles() const {
        return !profiles.empty();
    }

    /**
     * @brief Start timing on all attached profiles
     *
     * Sets profiled=true so executor knows not to double-time.
     */
    void BeginProfiling() {
        profiled = true;
        for (ITaskProfile* profile : profiles) {
            if (profile) profile->Begin();
        }
    }

    /**
     * @brief End timing on all attached profiles
     */
    void EndProfiling() {
        for (ITaskProfile* profile : profiles) {
            if (profile) profile->End();
        }
    }

    /**
     * @brief Check if this task was already profiled by node code
     */
    [[nodiscard]] bool WasProfiled() const { return profiled; }

    /**
     * @brief Get total estimated cost from attached profiles
     *
     * Sums estimates from all attached profiles. Returns 0 if no profiles.
     * Use NodeInstance::EstimateTaskCost() to get profile-based estimates.
     */
    [[nodiscard]] uint64_t GetEstimatedCostFromProfiles() const {
        uint64_t totalCost = 0;
        for (const ITaskProfile* profile : profiles) {
            if (profile) {
                totalCost += profile->GetEstimatedCostNs();
            }
        }
        return totalCost;
    }
};

/**
 * @brief Statistics for virtual task execution
 */
struct VirtualTaskStats {
    size_t totalTasks = 0;              ///< Total virtual tasks created
    size_t completedTasks = 0;          ///< Successfully completed tasks
    size_t failedTasks = 0;             ///< Failed tasks
    size_t parallelTasks = 0;           ///< Tasks that ran in parallel
    size_t serializedTasks = 0;         ///< Tasks forced sequential (conflicts)

    double totalExecutionMs = 0.0;      ///< Total execution time
    double avgTaskDurationMs = 0.0;     ///< Average task duration
    double maxTaskDurationMs = 0.0;     ///< Longest task duration

    size_t dependencyEdges = 0;         ///< Total dependency edges
    float avgDependenciesPerTask = 0.0f; ///< Average dependencies per task

    /**
     * @brief Get parallelism factor (parallel / total)
     */
    [[nodiscard]] float GetParallelismFactor() const {
        return totalTasks > 0 ? static_cast<float>(parallelTasks) / totalTasks : 0.0f;
    }

    /**
     * @brief Get success rate
     */
    [[nodiscard]] float GetSuccessRate() const {
        size_t total = completedTasks + failedTasks;
        return total > 0 ? static_cast<float>(completedTasks) / total : 1.0f;
    }
};

} // namespace Vixen::RenderGraph
