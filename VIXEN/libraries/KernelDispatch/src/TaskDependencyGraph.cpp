// Copyright (C) 2025 Lior Yanai (eLiorg)
// Licensed under the MIT License.
// See LICENSE file in the project root for full license information.

#include "KernelDispatch/TaskDependencyGraph.h"

#include <algorithm>
#include <queue>

// Tier-A extraction (E7 dispatch D1): the DAG algorithms are ported verbatim from
// RenderGraph/src/Core/TaskDependencyGraph.cpp, with NodeInstance*/Resource*/tracker-driven Build
// removed. Edge construction is now the caller's job (AddTask/AddEdge) -- the hazard-derived edge
// set (VirtualResourceAccessTracker) is Tier-B / D2 and stays in RenderGraph.

namespace Vixen::KernelDispatch {

namespace {

bool TaskIdLess(const TaskId& lhs, const TaskId& rhs) {
    if (lhs.owner != rhs.owner) return lhs.owner < rhs.owner;
    return lhs.taskIndex < rhs.taskIndex;
}

} // namespace

void TaskDependencyGraph::AddTask(const TaskId& task) {
    if (allTasks_.insert(task).second) {
        adjacencyList_[task];  // ensure empty lists exist
        dependencies_[task];
    }
}

void TaskDependencyGraph::AddEdge(const TaskId& from, const TaskId& to) {
    AddTask(from);
    AddTask(to);
    if (HasDependency(from, to)) return;
    adjacencyList_[from].push_back(to);
    dependencies_[to].push_back(from);
    ++edgeCount_;
}

void TaskDependencyGraph::Clear() {
    dependencies_.clear();
    adjacencyList_.clear();
    allTasks_.clear();
    edgeCount_ = 0;
}

std::vector<TaskId> TaskDependencyGraph::GetDependencies(const TaskId& task) const {
    auto it = dependencies_.find(task);
    return it != dependencies_.end() ? it->second : std::vector<TaskId>{};
}

std::vector<TaskId> TaskDependencyGraph::GetDependents(const TaskId& task) const {
    auto it = adjacencyList_.find(task);
    return it != adjacencyList_.end() ? it->second : std::vector<TaskId>{};
}

bool TaskDependencyGraph::HasDependency(const TaskId& a, const TaskId& b) const {
    auto it = dependencies_.find(b);
    if (it == dependencies_.end()) return false;
    return std::find(it->second.begin(), it->second.end(), a) != it->second.end();
}

size_t TaskDependencyGraph::GetDependencyCount(const TaskId& task) const {
    auto it = dependencies_.find(task);
    return it != dependencies_.end() ? it->second.size() : 0;
}

std::vector<TaskId> TaskDependencyGraph::TopologicalSort() const {
    // Kahn's algorithm.
    std::vector<TaskId> result;
    result.reserve(allTasks_.size());

    std::unordered_map<TaskId, size_t, TaskIdHash> inDegree;
    for (const auto& task : allTasks_) inDegree[task] = GetDependencyCount(task);

    std::vector<TaskId> initialReady;
    for (const auto& [task, degree] : inDegree)
        if (degree == 0) initialReady.push_back(task);
    std::sort(initialReady.begin(), initialReady.end(), TaskIdLess);
    std::queue<TaskId> ready;
    for (const auto& task : initialReady) ready.push(task);

    while (!ready.empty()) {
        TaskId current = ready.front();
        ready.pop();
        result.push_back(current);
        auto it = adjacencyList_.find(current);
        if (it != adjacencyList_.end()) {
            std::vector<TaskId> newlyReady;
            for (const auto& dep : it->second)
                if (--inDegree[dep] == 0) newlyReady.push_back(dep);
            std::sort(newlyReady.begin(), newlyReady.end(), TaskIdLess);
            for (const auto& dep : newlyReady) ready.push(dep);
        }
    }
    return result;  // shorter than allTasks_ ==> a cycle
}

std::vector<std::vector<TaskId>> TaskDependencyGraph::GetParallelLevels() const {
    std::vector<std::vector<TaskId>> levels;

    std::unordered_map<TaskId, size_t, TaskIdHash> inDegree;
    std::unordered_set<TaskId, TaskIdHash> processed;
    for (const auto& task : allTasks_) inDegree[task] = GetDependencyCount(task);

    std::vector<TaskId> orderedTasks(allTasks_.begin(), allTasks_.end());
    std::sort(orderedTasks.begin(), orderedTasks.end(), TaskIdLess);

    while (processed.size() < allTasks_.size()) {
        std::vector<TaskId> currentLevel;
        for (const auto& task : orderedTasks)
            if (processed.count(task) == 0 && inDegree[task] == 0)
                currentLevel.push_back(task);

        if (currentLevel.empty()) break;  // cycle or done

        for (const auto& task : currentLevel) {
            processed.insert(task);
            auto it = adjacencyList_.find(task);
            if (it != adjacencyList_.end())
                for (const auto& dep : it->second) --inDegree[dep];
        }
        levels.push_back(std::move(currentLevel));
    }
    return levels;
}

bool TaskDependencyGraph::HasCycle() const {
    return TopologicalSort().size() != allTasks_.size();
}

}  // namespace Vixen::KernelDispatch
