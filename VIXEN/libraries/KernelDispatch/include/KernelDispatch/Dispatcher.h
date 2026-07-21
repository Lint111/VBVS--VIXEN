// Copyright (C) 2025 Lior Yanai (eLiorg)
// Licensed under the MIT License.
// See LICENSE file in the project root for full license information.
#pragma once

/**
 * @file Dispatcher.h
 * @brief The native CpuTbb BACKEND of the generalized dispatcher contract (E7 dispatch D1).
 *
 * RunPerElementStage is ONE backend implementing the Abi.h contract -- the native, headless
 * counterpart of the kernel's managed DispatcherSpec (Burst-in-Unity backend). It takes a
 * generalized `Stage` + a `DispatcherProfile` (the backend is a profile choice, per spec 144) and,
 * for the CpuTbb backend, runs the per-element callable over N items through the extracted Tier-A
 * executor. This is the "submit ONE immutable single-scope batch to a renderer-independent native
 * runtime ... return deterministic results through a handle" of spec 12.14 #8 (:1414).
 *
 * D1 maps each of the N items to a VirtualTask whose callable is `stage.perElement(i)`. With no
 * dependencies they form a single parallel wave; the executor runs them via tbb::parallel_for_each
 * under the requested worker count. Per-element writes are independent, so the result is identical
 * for any worker count (the spec :1387 determinism gate).
 */

#include "Abi.h"
#include "TaskDependencyGraph.h"
#include "TaskExecutor.h"
#include "VirtualTask.h"
#include <string>
#include <thread>

namespace Vixen::KernelDispatch {

/// Sentinel: use TBB's default (hardware) concurrency for the dispatch.
inline int DefaultWorkerCount() {
    unsigned hc = std::thread::hardware_concurrency();
    return hc == 0 ? 1 : static_cast<int>(hc);
}

/**
 * @brief Run a per-element stage; the profile routes it to a backend. Returns a completion handle.
 *
 * The backend is a profile choice (spec 144): `profile.BackendFor(stage.owner, stage.backend)`
 * selects CpuInline (serial, workerCount forced to 1) vs CpuTbb (task-wave over `workerCount`). Both
 * run the SAME per-element callable through the extracted Tier-A executor -- CpuInline is just
 * CpuTbb with one worker, so they share the path and stay deterministic. GPU backends route
 * elsewhere once they exist (not in D1).
 *
 * @param stage       The generalized per-element stage (owner, itemCount, perElement).
 * @param profile     Backend/output policy (empty = use stage.backend). Defaults to none.
 * @param workerCount TBB workers for the CpuTbb backend (>=1). Defaults to hardware concurrency.
 */
inline Handle RunPerElementStage(const Stage& stage,
                                 const DispatcherProfile& profile = {},
                                 int workerCount = DefaultWorkerCount()) {
    Handle h;

    const Backend backend = profile.BackendFor(stage.owner, stage.backend);
    // D1 backends: CpuInline == 1 worker, CpuTbb == workerCount. (GPU/Transfer are later slices.)
    if (backend == Backend::CpuInline) workerCount = 1;

    // Opaque owner id for the Tier-A TaskId: hash the stage's string identity. The library never
    // interprets it -- it just needs a stable per-stage number for the {owner, taskIndex} key.
    const uint64_t ownerId = std::hash<std::string>{}(stage.owner);

    // One task per item, all in a single wave (no per-element dependencies).
    const uint32_t n = stage.itemCount;
    std::vector<VirtualTask> tasks;
    tasks.reserve(n);
    std::vector<TaskId> wave;
    wave.reserve(n);

    for (uint32_t i = 0; i < n; ++i) {
        TaskId id{ownerId, i};
        VirtualTask t;
        t.id = id;
        // Capture the stage's callable by reference into an index-bound closure. `stage` outlives
        // the synchronous Run() below, so the reference is safe.
        const auto& fn = stage.perElement;
        t.execute = [&fn, i] { if (fn) fn(i); };
        tasks.push_back(std::move(t));
        wave.push_back(id);
    }

    TaskExecutor executor;
    bool ok = executor.Run(tasks, {wave}, workerCount);

    h.completed = true;
    h.succeeded = ok;
    return h;
}

}  // namespace Vixen::KernelDispatch
