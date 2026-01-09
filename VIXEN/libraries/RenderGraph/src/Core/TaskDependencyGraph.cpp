// Copyright (C) 2025 Lior Yanai (eLiorg)
// Licensed under the GPL-3.0 License.
// See LICENSE file in the project root for full license information.

#include "Core/TaskDependencyGraph.h"
#include "Core/VirtualResourceAccessTracker.h"
#include "Core/NodeInstance.h"
#include <algorithm>
#include <queue>
#include <stack>

namespace Vixen::RenderGraph {

// ============================================================================
// Building
// ============================================================================

void TaskDependencyGraph::Build(
    const VirtualResourceAccessTracker& tracker,
    const std::vector<NodeInstance*>& executionOrder
) {
    Clear();

    // Build node order lookup for write-write conflict resolution
    std::unordered_map<NodeInstance*, size_t> nodeOrderMap;
    for (size_t i = 0; i < executionOrder.size(); ++i) {
        nodeOrderMap[executionOrder[i]] = i;
    }

    // Collect all tasks from tracker
    for (NodeInstance* node : executionOrder) {
        auto tasks = tracker.GetNodeTasks(node);
        for (const auto& task : tasks) {
            allTasks_.insert(task);
            // Initialize empty adjacency lists
            adjacencyList_[task];
            dependencies_[task];
        }
    }

    // For each task, find dependencies based on resource access conflicts
    for (const auto& taskA : allTasks_) {
        auto conflicting = tracker.GetConflictingTasks(taskA);

        for (const auto& taskB : conflicting) {
            // Skip if taskB is not in our execution order
            if (allTasks_.count(taskB) == 0) continue;

            // Determine ordering: earlier task is the dependency
            auto orderA = GetNodeOrderIndex(taskA.node, executionOrder);
            auto orderB = GetNodeOrderIndex(taskB.node, executionOrder);

            if (!orderA || !orderB) continue;

            // Compare by (node order, then task index)
            bool aBeforeB = (*orderA < *orderB) ||
                           (*orderA == *orderB && taskA.taskIndex < taskB.taskIndex);

            if (aBeforeB) {
                // taskA must complete before taskB
                // Check if both are writers (write-write conflict)
                bool isWriteWrite = tracker.IsWriter(taskA) && tracker.IsWriter(taskB);

                // Get shared resources to find the conflict cause
                auto shared = tracker.GetSharedResources(taskA, taskB);
                Resource* causingResource = shared.empty() ? nullptr : shared[0];

                // Don't add duplicate edges
                if (!HasDependency(taskA, taskB)) {
                    AddEdge(taskA, taskB, causingResource, isWriteWrite);
                }
            }
        }
    }
}

void TaskDependencyGraph::Clear() {
    dependencies_.clear();
    adjacencyList_.clear();
    edges_.clear();
    allTasks_.clear();
    edgeCount_ = 0;
}

// ============================================================================
// Dependency Queries
// ============================================================================

std::vector<VirtualTaskId> TaskDependencyGraph::GetDependencies(const VirtualTaskId& task) const {
    auto it = dependencies_.find(task);
    return it != dependencies_.end() ? it->second : std::vector<VirtualTaskId>{};
}

std::vector<VirtualTaskId> TaskDependencyGraph::GetDependents(const VirtualTaskId& task) const {
    auto it = adjacencyList_.find(task);
    return it != adjacencyList_.end() ? it->second : std::vector<VirtualTaskId>{};
}

bool TaskDependencyGraph::CanParallelize(const VirtualTaskId& taskA, const VirtualTaskId& taskB) const {
    if (taskA == taskB) return false;

    // Check direct dependency in either direction
    if (HasDependency(taskA, taskB) || HasDependency(taskB, taskA)) {
        return false;
    }

    // Check transitive dependency (path exists)
    if (HasPath(taskA, taskB) || HasPath(taskB, taskA)) {
        return false;
    }

    return true;
}

bool TaskDependencyGraph::HasDependency(const VirtualTaskId& taskA, const VirtualTaskId& taskB) const {
    auto it = dependencies_.find(taskB);
    if (it == dependencies_.end()) return false;

    const auto& deps = it->second;
    return std::find(deps.begin(), deps.end(), taskA) != deps.end();
}

size_t TaskDependencyGraph::GetDependencyCount(const VirtualTaskId& task) const {
    auto it = dependencies_.find(task);
    return it != dependencies_.end() ? it->second.size() : 0;
}

size_t TaskDependencyGraph::GetDependentCount(const VirtualTaskId& task) const {
    auto it = adjacencyList_.find(task);
    return it != adjacencyList_.end() ? it->second.size() : 0;
}

// ============================================================================
// Topological Sort
// ============================================================================

std::vector<VirtualTaskId> TaskDependencyGraph::TopologicalSort() const {
    std::vector<VirtualTaskId> result;
    result.reserve(allTasks_.size());

    // Kahn's algorithm
    std::unordered_map<VirtualTaskId, size_t, VirtualTaskIdHash> inDegree;

    // Initialize in-degrees
    for (const auto& task : allTasks_) {
        inDegree[task] = GetDependencyCount(task);
    }

    // Find all tasks with no dependencies
    std::queue<VirtualTaskId> ready;
    for (const auto& [task, degree] : inDegree) {
        if (degree == 0) {
            ready.push(task);
        }
    }

    while (!ready.empty()) {
        VirtualTaskId current = ready.front();
        ready.pop();
        result.push_back(current);

        // Reduce in-degree of dependents
        auto it = adjacencyList_.find(current);
        if (it != adjacencyList_.end()) {
            for (const auto& dependent : it->second) {
                --inDegree[dependent];
                if (inDegree[dependent] == 0) {
                    ready.push(dependent);
                }
            }
        }
    }

    // If result doesn't contain all tasks, there's a cycle
    // (shouldn't happen if Build() is correct)
    return result;
}

std::vector<VirtualTaskId> TaskDependencyGraph::GetReadyTasks() const {
    std::vector<VirtualTaskId> ready;

    for (const auto& task : allTasks_) {
        if (GetDependencyCount(task) == 0) {
            ready.push_back(task);
        }
    }

    return ready;
}

std::vector<std::vector<VirtualTaskId>> TaskDependencyGraph::GetParallelLevels() const {
    std::vector<std::vector<VirtualTaskId>> levels;

    std::unordered_map<VirtualTaskId, size_t, VirtualTaskIdHash> inDegree;
    std::unordered_set<VirtualTaskId, VirtualTaskIdHash> processed;

    // Initialize in-degrees
    for (const auto& task : allTasks_) {
        inDegree[task] = GetDependencyCount(task);
    }

    while (processed.size() < allTasks_.size()) {
        std::vector<VirtualTaskId> currentLevel;

        // Find all tasks with zero remaining in-degree
        for (const auto& task : allTasks_) {
            if (processed.count(task) == 0 && inDegree[task] == 0) {
                currentLevel.push_back(task);
            }
        }

        if (currentLevel.empty()) {
            // Cycle detected or done
            break;
        }

        // Mark tasks as processed and reduce in-degree of dependents
        for (const auto& task : currentLevel) {
            processed.insert(task);
            auto it = adjacencyList_.find(task);
            if (it != adjacencyList_.end()) {
                for (const auto& dependent : it->second) {
                    --inDegree[dependent];
                }
            }
        }

        levels.push_back(std::move(currentLevel));
    }

    return levels;
}

// ============================================================================
// Statistics
// ============================================================================

size_t TaskDependencyGraph::GetCriticalPathLength() const {
    if (allTasks_.empty()) return 0;

    // Compute longest path using dynamic programming
    std::unordered_map<VirtualTaskId, size_t, VirtualTaskIdHash> longestPath;

    // Initialize all distances to 0
    for (const auto& task : allTasks_) {
        longestPath[task] = 0;
    }

    // Process in topological order
    auto sorted = TopologicalSort();
    for (const auto& task : sorted) {
        auto it = adjacencyList_.find(task);
        if (it != adjacencyList_.end()) {
            for (const auto& dependent : it->second) {
                longestPath[dependent] = std::max(longestPath[dependent],
                                                   longestPath[task] + 1);
            }
        }
    }

    // Find maximum
    size_t maxPath = 0;
    for (const auto& [task, pathLen] : longestPath) {
        maxPath = std::max(maxPath, pathLen);
    }

    return maxPath + 1;  // +1 to count nodes, not edges
}

size_t TaskDependencyGraph::GetMaxParallelism() const {
    auto levels = GetParallelLevels();
    size_t maxSize = 0;
    for (const auto& level : levels) {
        maxSize = std::max(maxSize, level.size());
    }
    return maxSize;
}

bool TaskDependencyGraph::HasCycle() const {
    auto sorted = TopologicalSort();
    return sorted.size() != allTasks_.size();
}

// ============================================================================
// Private Methods
// ============================================================================

void TaskDependencyGraph::AddEdge(
    const VirtualTaskId& from,
    const VirtualTaskId& to,
    Resource* resource,
    bool isWriteWrite
) {
    // Add to adjacency list (from → to)
    adjacencyList_[from].push_back(to);

    // Add to dependencies (to ← from)
    dependencies_[to].push_back(from);

    // Record edge
    edges_.push_back({from, to, resource, isWriteWrite});
    ++edgeCount_;
}

bool TaskDependencyGraph::WouldCreateCycle(const VirtualTaskId& from, const VirtualTaskId& to) const {
    // Adding edge from→to would create cycle if there's already a path to→from
    return HasPath(to, from);
}

bool TaskDependencyGraph::HasPath(const VirtualTaskId& from, const VirtualTaskId& to) const {
    if (from == to) return true;

    // BFS to find path
    std::queue<VirtualTaskId> queue;
    std::unordered_set<VirtualTaskId, VirtualTaskIdHash> visited;

    queue.push(from);
    visited.insert(from);

    while (!queue.empty()) {
        VirtualTaskId current = queue.front();
        queue.pop();

        auto it = adjacencyList_.find(current);
        if (it != adjacencyList_.end()) {
            for (const auto& neighbor : it->second) {
                if (neighbor == to) return true;
                if (visited.count(neighbor) == 0) {
                    visited.insert(neighbor);
                    queue.push(neighbor);
                }
            }
        }
    }

    return false;
}

std::optional<size_t> TaskDependencyGraph::GetNodeOrderIndex(
    NodeInstance* node,
    const std::vector<NodeInstance*>& executionOrder
) const {
    auto it = std::find(executionOrder.begin(), executionOrder.end(), node);
    if (it != executionOrder.end()) {
        return std::distance(executionOrder.begin(), it);
    }
    return std::nullopt;
}

} // namespace Vixen::RenderGraph
