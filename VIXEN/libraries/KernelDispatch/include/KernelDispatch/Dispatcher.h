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

/// True when two stages have any slot in common between `a`'s writes and `b`'s reads-or-writes --
/// i.e. running B after A is a read-after-write, write-after-write, or write-after-read hazard. The
/// edge is DERIVED from the stages' declared SlotRefs (Abi.h reads/writes), never hard-coded.
inline bool StagesConflict(const Stage& a, const Stage& b) {
    auto touches = [](const std::vector<SlotRef>& lhs, const std::vector<SlotRef>& rhs) {
        for (const auto& l : lhs)
            for (const auto& r : rhs)
                if (l.id == r.id) return true;
        return false;
    };
    // B.reads vs A.writes (RAW), B.writes vs A.writes (WAW), or B.writes vs A.reads (WAR).
    return touches(a.writes, b.reads) || touches(a.writes, b.writes) || touches(a.reads, b.writes);
}

/**
 * @brief Dispatch an ordered CHAIN of per-element stages, deriving dependency edges from slot overlap
 *        (E7 dispatch D1.5 -- the second transpiled stage + the first real cross-stage dependency).
 *
 * For each ordered pair (A before B in `stages`), if StagesConflict(A, B) an edge A->B is added to
 * the EXTRACTED TaskDependencyGraph (AddEdge) -- no hand-rolled scheduler. Each stage is ONE graph
 * node (a whole-stage barrier: every item of a later, dependent stage runs after every item of the
 * one it depends on). The graph's GetParallelLevels() layers the nodes into waves; within each wave
 * the stage's N items are dispatched exactly as RunPerElementStage does. Chained stages therefore run
 * in dependency order, and independent stages share a wave.
 *
 * ponytail: stage-granular ordering (a whole-stage barrier between conflicting stages) is enough for
 * D1.5 -- the RAW column dep is stage->stage, not item->item. Per-item edges (item i of B depends only
 * on item i of A) would let B start before all of A finishes; that finer overlap is D2's hazard model,
 * not needed to prove ordered chaining. UPGRADE PATH: emit per-item edges keyed on (slot,index) when a
 * stage genuinely reads a DIFFERENT index than it writes.
 *
 * @param stages      Ordered stages; earlier-in-vector = earlier in program order for edge derivation.
 * @param profile     Backend/output policy per stage (empty = each stage's default backend).
 * @param workerCount TBB workers for the CpuTbb backend (>=1). Defaults to hardware concurrency.
 */
inline Handle RunChain(const std::vector<Stage>& stages,
                       const DispatcherProfile& profile = {},
                       int workerCount = DefaultWorkerCount()) {
    Handle h;

    // One graph node per stage; its opaque TaskId owner is the stage's string-hash, taskIndex 0.
    // (Distinct from RunPerElementStage's per-item ids -- here a node IS a stage, so items don't
    // collide in the graph.)
    TaskDependencyGraph graph;
    std::vector<TaskId> stageIds;
    stageIds.reserve(stages.size());
    for (const auto& s : stages) {
        TaskId id{std::hash<std::string>{}(s.owner), 0};
        graph.AddTask(id);
        stageIds.push_back(id);
    }
    // Derive edges from slot overlap: A before B in program order, conflict => A must finish first.
    for (size_t a = 0; a < stages.size(); ++a)
        for (size_t b = a + 1; b < stages.size(); ++b)
            if (StagesConflict(stages[a], stages[b]))
                graph.AddEdge(stageIds[a], stageIds[b]);

    // Map each graph node back to its stage, then dispatch stages wave-by-wave in dependency order.
    // A wave may hold several independent stages; run them left-to-right (they don't conflict, so
    // order within a wave is irrelevant to the result). Each stage's items run through the SAME
    // per-element path RunPerElementStage uses.
    bool ok = true;
    for (const auto& wave : graph.GetParallelLevels()) {
        for (const auto& nodeId : wave) {
            // Find the stage whose id matches this node (small chains -- a linear scan is fine).
            for (size_t si = 0; si < stages.size(); ++si) {
                if (stageIds[si] != nodeId) continue;
                Handle sh = RunPerElementStage(stages[si], profile, workerCount);
                ok = ok && sh.ok();
                break;
            }
        }
    }

    h.completed = true;
    h.succeeded = ok;
    return h;
}

}  // namespace Vixen::KernelDispatch
