// Copyright (C) 2025 Lior Yanai (eLiorg)
// Licensed under the MIT License.
// See LICENSE file in the project root for full license information.
#pragma once

/**
 * @file TaskExecutor.h
 * @brief Renderer-independent TBB task-wave executor (Tier-A extraction, E7 dispatch D1).
 *
 * Extracted from RenderGraph's Core/TBBVirtualTaskExecutor.h (spec 14:1783-1786). The render
 * original's Build() pulled the whole render stack in: it took a VirtualResourceAccessTracker
 * (Tier-B hazard model) and re-fetched each task's callable via NodeInstance::GetExecutionTasks().
 * That coupling is REMOVED -- the caller hands in a `vector<VirtualTask>` and a wave layering
 * directly. What remains is the essence: `tbb::parallel_for_each` over each wave, exception
 * collection, and (D1's determinism gate, spec :1387) an explicit worker-count control so the same
 * input yields byte-identical output under 1 / 2 / N workers.
 *
 * @see TaskDependencyGraph for producing the waves.
 * @see VirtualTask for the task shape.
 */

#include "TaskDependencyGraph.h"
#include "VirtualTask.h"
#include <stop_token>
#include <string>
#include <vector>

namespace Vixen::KernelDispatch {

/// Error from a failed task.
struct TaskError {
    TaskId task;
    std::string message;
};

/**
 * @brief Runs a set of tasks wave-by-wave; tasks within a wave run in parallel via TBB.
 *
 * `workerCount` selects the TBB arena size for THIS execution: 1 = fully serial, N = N workers.
 * The determinism contract (spec :1387) is that Run() produces identical results for any value --
 * true here because per-element writes are independent (no reducer, no shared accumulator).
 *
 * Thread safety: Run() is single-caller (it may spawn TBB workers internally).
 */
class TaskExecutor {
public:
    /**
     * @brief Execute `tasks` grouped by `waves`, using `workerCount` TBB workers.
     *
     * @param tasks     The work units (by id -> callable).
     * @param waves     Task-id waves from TaskDependencyGraph::GetParallelLevels(); each wave's
     *                  tasks run concurrently, waves run in order.
     * @param workerCount TBB arena concurrency for this run (>=1). 1 forces serial.
     * @param stopToken Cooperative execution cancellation. A stopped token prevents new tasks and
     *                  later waves from being issued; already-running tasks finish normally.
     * @return true if every task completed without throwing and cancellation was not requested.
     */
    bool Run(std::vector<VirtualTask>& tasks,
             const std::vector<std::vector<TaskId>>& waves,
             int workerCount,
             std::stop_token stopToken = {});

    [[nodiscard]] const std::vector<TaskError>& GetErrors() const { return errors_; }
    [[nodiscard]] bool HasErrors() const { return !errors_.empty(); }

private:
    std::vector<TaskError> errors_;

    VirtualTask* FindTask(std::vector<VirtualTask>& tasks, const TaskId& id) const;
    bool RunWave(std::vector<VirtualTask>& tasks,
                 const std::vector<TaskId>& wave,
                 std::stop_token stopToken);
};

}  // namespace Vixen::KernelDispatch
