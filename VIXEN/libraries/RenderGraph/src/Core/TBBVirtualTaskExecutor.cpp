// Copyright (C) 2025 Lior Yanai (eLiorg)
// Licensed under the GPL-3.0 License.
// See LICENSE file in the project root for full license information.

#include "Core/TBBVirtualTaskExecutor.h"
#include "Core/NodeInstance.h"
#include "Core/Timer.h"

#include <oneapi/tbb/parallel_for_each.h>
#include <oneapi/tbb/task_group.h>
#include <algorithm>
#include <chrono>
#include <iostream>

namespace Vixen::RenderGraph {

// ============================================================================
// Constructor / Destructor
// ============================================================================

TBBVirtualTaskExecutor::~TBBVirtualTaskExecutor() {
    Clear();
}

TBBVirtualTaskExecutor::TBBVirtualTaskExecutor(TBBVirtualTaskExecutor&& other) noexcept
    : isBuilt_(other.isBuilt_)
    , enabled_(other.enabled_)
    , depGraph_(std::move(other.depGraph_))
    , nodes_(std::move(other.nodes_))
    , tasks_(std::move(other.tasks_))
    , parallelLevels_(std::move(other.parallelLevels_))
    , errors_(std::move(other.errors_))
    , stats_(other.stats_)
{
    other.isBuilt_ = false;
}

TBBVirtualTaskExecutor& TBBVirtualTaskExecutor::operator=(TBBVirtualTaskExecutor&& other) noexcept {
    if (this != &other) {
        Clear();
        isBuilt_ = other.isBuilt_;
        enabled_ = other.enabled_;
        depGraph_ = std::move(other.depGraph_);
        nodes_ = std::move(other.nodes_);
        tasks_ = std::move(other.tasks_);
        parallelLevels_ = std::move(other.parallelLevels_);
        errors_ = std::move(other.errors_);
        stats_ = other.stats_;
        other.isBuilt_ = false;
    }
    return *this;
}

// ============================================================================
// Building
// ============================================================================

void TBBVirtualTaskExecutor::Build(
    const VirtualResourceAccessTracker& tracker,
    const std::vector<NodeInstance*>& executionOrder
) {
    auto startTime = std::chrono::high_resolution_clock::now();

    Clear();
    nodes_ = executionOrder;

    // Build dependency graph
    depGraph_.Build(tracker, executionOrder);

    // Build virtual tasks
    BuildTasks(tracker);

    // Get parallel levels for wave-based execution
    parallelLevels_ = depGraph_.GetParallelLevels();

    // Update statistics
    stats_.totalNodes = nodes_.size();
    stats_.totalTasks = tasks_.size();
    stats_.criticalPathLength = depGraph_.GetCriticalPathLength();
    stats_.maxParallelLevel = depGraph_.GetMaxParallelism();

    // Count opted-in nodes
    stats_.optedInNodes = 0;
    for (NodeInstance* node : nodes_) {
        if (node && node->SupportsTaskParallelism()) {
            ++stats_.optedInNodes;
        }
    }

    auto endTime = std::chrono::high_resolution_clock::now();
    stats_.buildTimeMs = std::chrono::duration<double, std::milli>(endTime - startTime).count();

    isBuilt_ = true;
}

void TBBVirtualTaskExecutor::BuildTasks(const VirtualResourceAccessTracker& tracker) {
    tasks_.clear();

    for (NodeInstance* node : nodes_) {
        if (!node) continue;

        auto nodeTasks = tracker.GetNodeTasks(node);
        for (const auto& taskId : nodeTasks) {
            VirtualTask task;
            task.id = taskId;
            task.estimatedCostNs = node->EstimateTaskCost(taskId.taskIndex);

            // Get dependencies from graph
            task.dependencies = depGraph_.GetDependencies(taskId);

            // Priority based on dependency count (fewer deps = higher priority)
            task.priority = static_cast<uint8_t>(
                std::min(255u, static_cast<uint32_t>(task.dependencies.size()))
            );

            tasks_.push_back(std::move(task));
        }
    }
}

void TBBVirtualTaskExecutor::Clear() {
    depGraph_.Clear();
    nodes_.clear();
    tasks_.clear();
    parallelLevels_.clear();
    errors_.clear();
    ResetStats();
    isBuilt_ = false;
}

// ============================================================================
// Execution
// ============================================================================

bool TBBVirtualTaskExecutor::ExecutePhase(VirtualTaskPhase phase) {
    if (!isBuilt_ || !enabled_) {
        return false;
    }

    ClearErrors();
    auto startTime = std::chrono::high_resolution_clock::now();

    bool success = true;

    // Execute level by level (respects dependencies while maximizing parallelism)
    for (const auto& level : parallelLevels_) {
        if (!ExecuteLevel(level, phase)) {
            success = false;
            // Continue to collect all errors, don't early-exit
        }
    }

    auto endTime = std::chrono::high_resolution_clock::now();
    stats_.executionTimeMs += std::chrono::duration<double, std::milli>(endTime - startTime).count();

    return success;
}

bool TBBVirtualTaskExecutor::ExecuteLevel(
    const std::vector<VirtualTaskId>& level,
    VirtualTaskPhase phase
) {
    if (level.empty()) return true;

    bool allSucceeded = true;

    if (level.size() == 1) {
        // Single task - execute directly
        auto* task = FindTask(level[0]);
        if (task) {
            if (!ExecuteTask(*task, phase)) {
                allSucceeded = false;
            }
            ++stats_.sequentialTasks;
        }
    } else {
        // Multiple tasks - execute in parallel using TBB
        stats_.parallelTasks += level.size();

        std::atomic<bool> anyFailed{false};

        tbb::parallel_for_each(level.begin(), level.end(),
            [this, phase, &anyFailed](const VirtualTaskId& taskId) {
                auto* task = FindTask(taskId);
                if (task) {
                    if (!ExecuteTask(*task, phase)) {
                        anyFailed = true;
                    }
                }
            }
        );

        if (anyFailed) {
            allSucceeded = false;
        }
    }

    return allSucceeded;
}

bool TBBVirtualTaskExecutor::ExecuteTask(VirtualTask& task, VirtualTaskPhase phase) {
    if (!task.id.IsValid() || !task.id.node) {
        return true;  // Skip invalid tasks
    }

    NodeInstance* node = task.id.node;

    // Check if node supports task parallelism
    bool supportsParallel = node->SupportsTaskParallelism();

    // Map VirtualTaskPhase to NodeLifecyclePhase
    NodeLifecyclePhase lifecyclePhase;
    switch (phase) {
        case VirtualTaskPhase::Setup:
            lifecyclePhase = NodeLifecyclePhase::PreSetup;
            break;
        case VirtualTaskPhase::Compile:
            lifecyclePhase = NodeLifecyclePhase::PreCompile;
            break;
        case VirtualTaskPhase::Execute:
            lifecyclePhase = NodeLifecyclePhase::PreExecute;
            break;
        case VirtualTaskPhase::Cleanup:
            lifecyclePhase = NodeLifecyclePhase::PreCleanup;
            break;
        default:
            return true;
    }

    try {
        if (supportsParallel) {
            // Node supports parallelism - use CreateVirtualTask
            auto taskFunc = node->CreateVirtualTask(task.id.taskIndex, lifecyclePhase);
            if (taskFunc) {
                taskFunc();
            }
        } else {
            // Node doesn't support parallelism - only execute for task 0
            // (which runs the entire node's phase)
            if (task.id.taskIndex == 0) {
                auto taskFunc = node->CreateVirtualTask(0, lifecyclePhase);
                if (taskFunc) {
                    taskFunc();
                }
            }
            // Other tasks are no-ops for non-opted nodes
        }

        task.MarkCompleted();
        return true;

    } catch (const std::exception& e) {
        task.MarkFailed(e.what());
        RecordError(task.id, e.what(), phase);
        ++stats_.failedTasks;
        return false;

    } catch (...) {
        task.MarkFailed("Unknown exception");
        RecordError(task.id, "Unknown exception", phase);
        ++stats_.failedTasks;
        return false;
    }
}

bool TBBVirtualTaskExecutor::ExecuteAllPhases() {
    bool success = true;

    if (!ExecutePhase(VirtualTaskPhase::Setup)) success = false;
    if (!ExecutePhase(VirtualTaskPhase::Compile)) success = false;
    if (!ExecutePhase(VirtualTaskPhase::Execute)) success = false;
    if (!ExecutePhase(VirtualTaskPhase::Cleanup)) success = false;

    return success;
}

// ============================================================================
// Error Handling
// ============================================================================

void TBBVirtualTaskExecutor::RecordError(
    const VirtualTaskId& task,
    const std::string& message,
    VirtualTaskPhase phase
) {
    std::lock_guard<std::mutex> lock(errorMutex_);
    errors_.push_back({task, message, phase});
}

// ============================================================================
// Statistics
// ============================================================================

void TBBVirtualTaskExecutor::ResetStats() {
    stats_ = VirtualTaskExecutorStats{};
}

// ============================================================================
// Helpers
// ============================================================================

VirtualTask* TBBVirtualTaskExecutor::FindTask(const VirtualTaskId& id) {
    for (auto& task : tasks_) {
        if (task.id == id) {
            return &task;
        }
    }
    return nullptr;
}

} // namespace Vixen::RenderGraph
