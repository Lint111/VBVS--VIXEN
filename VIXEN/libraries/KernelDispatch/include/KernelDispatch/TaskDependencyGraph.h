// Copyright (C) 2025 Lior Yanai (eLiorg)
// Licensed under the MIT License.
// See LICENSE file in the project root for full license information.
#pragma once

/**
 * @file TaskDependencyGraph.h
 * @brief Renderer-independent task DAG (Tier-A extraction, E7 dispatch D1).
 *
 * Extracted from RenderGraph's Core/TaskDependencyGraph.h (spec 14:1783-1786). The render original
 * keyed on NodeInstance*, carried a `Resource*` on each edge, and BUILT the edge set from a
 * VirtualResourceAccessTracker (the render-resource HAZARD model). All of that is RENDER IDENTITY /
 * Tier-B and stays in RenderGraph.
 *
 * What is domain-blind -- and all that this library keeps -- is the DAG itself: opaque-id nodes,
 * `AddEdge(from,to)`, topological sort, parallel-level layering, cycle check. The consumer supplies
 * edges directly (or none). D1's single embarrassingly-parallel per-element stage has NO edges, so
 * GetParallelLevels() yields one wave -- the graph is present for faithful Tier-A extraction and to
 * carry future multi-stage chains, not because D1 needs ordering.
 *
 * The hazard-derived edge construction (VirtualResourceAccessTracker -> StateAccessKey) is D2, a
 * REIMPLEMENTATION, not part of this extraction.
 */

#include "VirtualTask.h"
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Vixen::KernelDispatch {

/**
 * @brief A DAG over opaque TaskIds with topological sort and parallel-level layering.
 *
 * Thread safety: NOT thread-safe. Build once (AddTask/AddEdge), query from a single thread.
 */
class TaskDependencyGraph {
public:
    TaskDependencyGraph() = default;

    TaskDependencyGraph(const TaskDependencyGraph&) = delete;
    TaskDependencyGraph& operator=(const TaskDependencyGraph&) = delete;
    TaskDependencyGraph(TaskDependencyGraph&&) noexcept = default;
    TaskDependencyGraph& operator=(TaskDependencyGraph&&) noexcept = default;

    /// Register a task with no dependencies yet (so isolated tasks appear in the layering).
    void AddTask(const TaskId& task);

    /// Add a dependency edge: `from` must complete before `to`. Both ids are auto-registered.
    void AddEdge(const TaskId& from, const TaskId& to);

    void Clear();

    // --- queries (all domain-blind; ported verbatim from the render original) ---

    [[nodiscard]] std::vector<TaskId> GetDependencies(const TaskId& task) const;
    [[nodiscard]] std::vector<TaskId> GetDependents(const TaskId& task) const;
    [[nodiscard]] bool HasDependency(const TaskId& a, const TaskId& b) const;  ///< a -> b edge exists
    [[nodiscard]] size_t GetDependencyCount(const TaskId& task) const;

    [[nodiscard]] std::vector<TaskId> TopologicalSort() const;

    /// Tasks grouped into waves: every task in a wave has all deps in earlier waves.
    [[nodiscard]] std::vector<std::vector<TaskId>> GetParallelLevels() const;

    [[nodiscard]] size_t GetTaskCount() const { return allTasks_.size(); }
    [[nodiscard]] size_t GetEdgeCount() const { return edgeCount_; }
    [[nodiscard]] bool HasCycle() const;

private:
    std::unordered_map<TaskId, std::vector<TaskId>, TaskIdHash> dependencies_;    ///< task <- its deps
    std::unordered_map<TaskId, std::vector<TaskId>, TaskIdHash> adjacencyList_;   ///< task -> dependents
    std::unordered_set<TaskId, TaskIdHash> allTasks_;
    size_t edgeCount_ = 0;
};

}  // namespace Vixen::KernelDispatch
