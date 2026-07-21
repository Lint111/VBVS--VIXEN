// Copyright (C) 2025 Lior Yanai (eLiorg)
// Licensed under the MIT License.
// See LICENSE file in the project root for full license information.

#include "KernelDispatch/TaskExecutor.h"

#include <oneapi/tbb/parallel_for_each.h>
#include <oneapi/tbb/task_arena.h>
#include <atomic>
#include <exception>
#include <mutex>

// Tier-A extraction (E7 dispatch D1): the wave-then-parallel_for_each shape is ported from
// RenderGraph/src/Core/TBBVirtualTaskExecutor.cpp (ExecutePhase/ExecuteLevel/ExecuteTask), with the
// NodeInstance::GetExecutionTasks re-fetch removed -- the callable is taken straight from the
// VirtualTask the caller supplied. A per-run tbb::task_arena caps concurrency so Run() is
// deterministic across worker counts (spec :1387).

namespace Vixen::KernelDispatch {

VirtualTask* TaskExecutor::FindTask(std::vector<VirtualTask>& tasks, const TaskId& id) const {
    for (auto& t : tasks)
        if (t.id == id) return &t;
    return nullptr;
}

bool TaskExecutor::RunWave(std::vector<VirtualTask>& tasks, const std::vector<TaskId>& wave) {
    if (wave.empty()) return true;

    std::atomic<bool> anyFailed{false};

    // One task in the wave: run inline (matches the render original's single-task fast path).
    if (wave.size() == 1) {
        if (VirtualTask* t = FindTask(tasks, wave[0])) {
            try {
                if (t->execute) t->execute();
                t->MarkCompleted();
            } catch (const std::exception& e) {
                t->MarkFailed(e.what());
                errors_.push_back({t->id, e.what()});
                anyFailed = true;
            } catch (...) {
                t->MarkFailed("Unknown exception");
                errors_.push_back({t->id, "Unknown exception"});
                anyFailed = true;
            }
        }
        return !anyFailed;
    }

    // Errors are appended from worker threads -- guard the vector. (Small, contended only on failure.)
    // ponytail: a mutex is fine here; the error path is cold. Lock-free queue only if it ever gets hot.
    std::mutex errMutex;
    tbb::parallel_for_each(wave.begin(), wave.end(), [&](const TaskId& id) {
        VirtualTask* t = FindTask(tasks, id);
        if (!t) return;
        try {
            if (t->execute) t->execute();
            t->MarkCompleted();
        } catch (const std::exception& e) {
            t->MarkFailed(e.what());
            std::lock_guard<std::mutex> lk(errMutex);
            errors_.push_back({t->id, e.what()});
            anyFailed = true;
        } catch (...) {
            t->MarkFailed("Unknown exception");
            std::lock_guard<std::mutex> lk(errMutex);
            errors_.push_back({t->id, "Unknown exception"});
            anyFailed = true;
        }
    });

    return !anyFailed;
}

bool TaskExecutor::Run(std::vector<VirtualTask>& tasks,
                       const std::vector<std::vector<TaskId>>& waves,
                       int workerCount) {
    errors_.clear();
    if (workerCount < 1) workerCount = 1;

    // A per-run arena caps concurrency: workerCount==1 forces serial execution, N gives N workers.
    // This is the determinism knob the spec's 1/2/N gate exercises.
    tbb::task_arena arena(workerCount);
    bool success = true;
    arena.execute([&] {
        for (const auto& wave : waves)
            if (!RunWave(tasks, wave)) success = false;  // keep going to collect all errors
    });
    return success;
}

}  // namespace Vixen::KernelDispatch
