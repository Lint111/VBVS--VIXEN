// Copyright (C) 2025 Lior Yanai (eLiorg)
// Licensed under the GPL-3.0 License.
// See LICENSE file in the project root for full license information.
#pragma once

/**
 * @file TaskDependencyGraph.h
 * @brief Task-level dependency resolution for virtual task scheduling
 *
 * Sprint 6.5: Task-Level Parallelism Architecture
 * Design Element: #38 Timeline Capacity Tracker (Virtual Task Extension)
 *
 * TaskDependencyGraph builds a directed acyclic graph (DAG) of dependencies
 * between VirtualTasks based on resource access patterns. This enables
 * TBBVirtualTaskExecutor to schedule tasks in correct order while maximizing
 * parallelism.
 *
 * Dependency Rules:
 * - If task A writes resource R and task B reads R, A must complete before B
 * - If task A writes resource R and task B writes R, order must be defined
 * - If both tasks only read resource R, no dependency
 *
 * Usage:
 * @code
 * VirtualResourceAccessTracker tracker;
 * tracker.BuildFromTopology(topology);
 *
 * TaskDependencyGraph depGraph;
 * depGraph.Build(tracker, executionOrder);
 *
 * auto sortedTasks = depGraph.TopologicalSort();
 * for (auto& task : sortedTasks) {
 *     executor.Schedule(task);
 * }
 * @endcode
 *
 * @see VirtualResourceAccessTracker for conflict detection
 * @see TBBVirtualTaskExecutor for parallel execution
 */

#include "VirtualTask.h"
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <optional>

namespace Vixen::RenderGraph {

// Forward declarations
class VirtualResourceAccessTracker;
class NodeInstance;
class Resource;

/**
 * @brief Dependency edge between two virtual tasks
 */
struct TaskDependencyEdge {
    VirtualTaskId from;    ///< Task that must complete first
    VirtualTaskId to;      ///< Task that depends on 'from'
    Resource* resource = nullptr;  ///< Resource causing the dependency
    bool isWriteWrite = false;     ///< True if both tasks write (ordering dependency)

    bool operator==(const TaskDependencyEdge& other) const {
        return from == other.from && to == other.to;
    }
};

/**
 * @brief Hash for TaskDependencyEdge
 */
struct TaskDependencyEdgeHash {
    size_t operator()(const TaskDependencyEdge& edge) const {
        VirtualTaskIdHash taskHash;
        size_t h1 = taskHash(edge.from);
        size_t h2 = taskHash(edge.to);
        return h1 ^ (h2 << 1);
    }
};

/**
 * @brief Task-level dependency graph for scheduling
 *
 * Builds and maintains a DAG of dependencies between VirtualTasks.
 * Supports topological sorting for execution order and dependency queries.
 *
 * Thread Safety: NOT thread-safe. Build once, query from single thread.
 */
class TaskDependencyGraph {
public:
    TaskDependencyGraph() = default;
    ~TaskDependencyGraph() = default;

    // Non-copyable, movable
    TaskDependencyGraph(const TaskDependencyGraph&) = delete;
    TaskDependencyGraph& operator=(const TaskDependencyGraph&) = delete;
    TaskDependencyGraph(TaskDependencyGraph&&) noexcept = default;
    TaskDependencyGraph& operator=(TaskDependencyGraph&&) noexcept = default;

    // =========================================================================
    // Building
    // =========================================================================

    /**
     * @brief Build dependency graph from resource access tracker
     *
     * Creates dependency edges based on resource conflicts. Uses the
     * provided execution order to resolve write-write conflicts.
     *
     * @param tracker Resource access tracker with per-task accesses
     * @param executionOrder Original node execution order (for write-write ordering)
     */
    void Build(const VirtualResourceAccessTracker& tracker,
               const std::vector<NodeInstance*>& executionOrder);

    /**
     * @brief Clear all graph data
     */
    void Clear();

    // =========================================================================
    // Dependency Queries
    // =========================================================================

    /**
     * @brief Get all tasks that must complete before a given task
     *
     * @param task Task to query
     * @return Tasks that must complete first
     */
    [[nodiscard]] std::vector<VirtualTaskId> GetDependencies(const VirtualTaskId& task) const;

    /**
     * @brief Get all tasks that depend on a given task
     *
     * @param task Task to query
     * @return Tasks that depend on this task
     */
    [[nodiscard]] std::vector<VirtualTaskId> GetDependents(const VirtualTaskId& task) const;

    /**
     * @brief Check if two tasks can run in parallel
     *
     * Returns true if there is no dependency path between the tasks
     * (neither directly nor transitively).
     *
     * @param taskA First task
     * @param taskB Second task
     * @return true if tasks can run concurrently
     */
    [[nodiscard]] bool CanParallelize(const VirtualTaskId& taskA, const VirtualTaskId& taskB) const;

    /**
     * @brief Check if taskA must complete before taskB
     *
     * @param taskA Potential predecessor
     * @param taskB Potential successor
     * @return true if taskA → taskB dependency exists
     */
    [[nodiscard]] bool HasDependency(const VirtualTaskId& taskA, const VirtualTaskId& taskB) const;

    /**
     * @brief Get count of dependencies for a task
     *
     * @param task Task to query
     * @return Number of tasks this task depends on
     */
    [[nodiscard]] size_t GetDependencyCount(const VirtualTaskId& task) const;

    /**
     * @brief Get count of dependents for a task
     *
     * @param task Task to query
     * @return Number of tasks that depend on this task
     */
    [[nodiscard]] size_t GetDependentCount(const VirtualTaskId& task) const;

    // =========================================================================
    // Topological Sort
    // =========================================================================

    /**
     * @brief Get tasks in topological order
     *
     * Returns all tasks sorted such that for every dependency edge (A→B),
     * A appears before B in the result.
     *
     * @return Tasks in valid execution order
     */
    [[nodiscard]] std::vector<VirtualTaskId> TopologicalSort() const;

    /**
     * @brief Get tasks that have no dependencies (ready to execute)
     *
     * These are the "root" tasks that can start immediately.
     *
     * @return Tasks with zero dependencies
     */
    [[nodiscard]] std::vector<VirtualTaskId> GetReadyTasks() const;

    /**
     * @brief Get parallel levels (tasks that can run at same time)
     *
     * Groups tasks into levels where all tasks at the same level can
     * run concurrently (all their dependencies are at earlier levels).
     *
     * @return Vector of levels, each level is a vector of parallel tasks
     */
    [[nodiscard]] std::vector<std::vector<VirtualTaskId>> GetParallelLevels() const;

    // =========================================================================
    // Statistics
    // =========================================================================

    /**
     * @brief Get total number of tasks in graph
     */
    [[nodiscard]] size_t GetTaskCount() const { return adjacencyList_.size(); }

    /**
     * @brief Get total number of dependency edges
     */
    [[nodiscard]] size_t GetEdgeCount() const { return edgeCount_; }

    /**
     * @brief Get all edges in the graph
     */
    [[nodiscard]] const std::vector<TaskDependencyEdge>& GetAllEdges() const { return edges_; }

    /**
     * @brief Get the critical path length (longest dependency chain)
     *
     * @return Length of longest path in the DAG
     */
    [[nodiscard]] size_t GetCriticalPathLength() const;

    /**
     * @brief Get maximum parallelism potential
     *
     * Returns the size of the largest level (max tasks that can run concurrently).
     *
     * @return Maximum concurrent tasks possible
     */
    [[nodiscard]] size_t GetMaxParallelism() const;

    /**
     * @brief Check if the graph has a cycle (should never happen)
     *
     * @return true if cycle detected (indicates bug)
     */
    [[nodiscard]] bool HasCycle() const;

private:
    /// Task → tasks it depends on (incoming edges)
    std::unordered_map<VirtualTaskId, std::vector<VirtualTaskId>, VirtualTaskIdHash> dependencies_;

    /// Task → tasks that depend on it (outgoing edges)
    std::unordered_map<VirtualTaskId, std::vector<VirtualTaskId>, VirtualTaskIdHash> adjacencyList_;

    /// All edges
    std::vector<TaskDependencyEdge> edges_;

    /// Edge count
    size_t edgeCount_ = 0;

    /// All tasks in the graph
    std::unordered_set<VirtualTaskId, VirtualTaskIdHash> allTasks_;

    /**
     * @brief Add a dependency edge
     *
     * @param from Task that must complete first
     * @param to Task that depends on 'from'
     * @param resource Resource causing the dependency
     * @param isWriteWrite True if both tasks write
     */
    void AddEdge(const VirtualTaskId& from, const VirtualTaskId& to,
                 Resource* resource, bool isWriteWrite);

    /**
     * @brief Check if adding an edge would create a cycle
     */
    [[nodiscard]] bool WouldCreateCycle(const VirtualTaskId& from, const VirtualTaskId& to) const;

    /**
     * @brief Check if there's a path from 'from' to 'to'
     */
    [[nodiscard]] bool HasPath(const VirtualTaskId& from, const VirtualTaskId& to) const;

    /**
     * @brief Get node execution order index
     */
    [[nodiscard]] std::optional<size_t> GetNodeOrderIndex(
        NodeInstance* node,
        const std::vector<NodeInstance*>& executionOrder) const;
};

} // namespace Vixen::RenderGraph
