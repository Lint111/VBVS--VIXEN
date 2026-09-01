// Copyright (C) 2025 Lior Yanai (eLiorg)
// Licensed under the MIT License.
// See LICENSE file in the project root for full license information.

#include "KernelDispatch/TaskExecutor.h"

#include <oneapi/tbb/parallel_for.h>
#include <oneapi/tbb/task_arena.h>
#include <algorithm>
#include <exception>
#include <iterator>
#include <optional>

// Tier-A extraction (E7 dispatch D1): the wave-then-parallel shape is the shared implementation for
// domain-blind task execution. A per-run tbb::task_arena caps concurrency so Run() is deterministic
// across worker counts (spec :1387).

namespace Vixen::KernelDispatch {

VirtualTask* TaskExecutor::FindTask(std::vector<VirtualTask>& tasks, const TaskId& id) const {
    for (auto& t : tasks)
        if (t.id == id) return &t;
    return nullptr;
}

bool TaskExecutor::RunWave(std::vector<VirtualTask>& tasks,
                           const std::vector<TaskId>& wave,
                           std::stop_token stopToken) {
    if (wave.empty()) return true;

    // Each worker writes only its own result slot. Errors are gathered after the wave and sorted by
    // logical task index, so completion timing can never choose the primary error.
    std::vector<std::optional<TaskError>> waveErrors(wave.size());
    const auto runTask = [&](size_t waveIndex) {
        if (stopToken.stop_requested()) return;

        VirtualTask* task = FindTask(tasks, wave[waveIndex]);
        if (!task) return;

        task->state = VirtualTaskState::Running;
        try {
            if (task->execute) task->execute();
            task->MarkCompleted();
        } catch (const std::exception& e) {
            task->MarkFailed(e.what());
            waveErrors[waveIndex] = TaskError{task->id, e.what()};
        } catch (...) {
            task->MarkFailed("Unknown exception");
            waveErrors[waveIndex] = TaskError{task->id, "Unknown exception"};
        }
    };

    // One task in the wave: run inline (matches the render original's single-task fast path).
    if (wave.size() == 1) {
        runTask(0);
    } else {
        tbb::parallel_for(size_t{0}, wave.size(), runTask);
    }

    std::vector<TaskError> orderedErrors;
    orderedErrors.reserve(waveErrors.size());
    for (auto& error : waveErrors) {
        if (error) orderedErrors.push_back(std::move(*error));
    }
    std::sort(orderedErrors.begin(), orderedErrors.end(), [](const TaskError& lhs, const TaskError& rhs) {
        if (lhs.task.taskIndex != rhs.task.taskIndex) {
            return lhs.task.taskIndex < rhs.task.taskIndex;
        }
        return lhs.task.owner < rhs.task.owner;
    });
    errors_.insert(errors_.end(),
                   std::make_move_iterator(orderedErrors.begin()),
                   std::make_move_iterator(orderedErrors.end()));

    return orderedErrors.empty() && !stopToken.stop_requested();
}

bool TaskExecutor::Run(std::vector<VirtualTask>& tasks,
                       const std::vector<std::vector<TaskId>>& waves,
                       int workerCount,
                       std::stop_token stopToken) {
    errors_.clear();
    if (workerCount < 1) workerCount = 1;
    if (stopToken.stop_requested()) return false;

    // A per-run arena caps concurrency: workerCount==1 forces serial execution, N gives N workers.
    // This is the determinism knob the spec's 1/2/N gate exercises.
    tbb::task_arena arena(workerCount);
    bool success = true;
    arena.execute([&] {
        for (const auto& wave : waves) {
            if (stopToken.stop_requested() || !RunWave(tasks, wave, stopToken)) {
                success = false;
                break;
            }
        }
    });
    return success;
}

}  // namespace Vixen::KernelDispatch
