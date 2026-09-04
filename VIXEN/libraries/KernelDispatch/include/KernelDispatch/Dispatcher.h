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
#include <stop_token>
#include <thread>
#include <unordered_map>
#include <vector>

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
                                 int workerCount = DefaultWorkerCount(),
                                 std::stop_token stopToken = {}) {
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
    bool ok = executor.Run(tasks, {wave}, workerCount, stopToken);

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
 * not needed to prove ordered chaining. The per-item edge model keyed on (slot,index) -- for a stage
 * that genuinely reads a DIFFERENT index than it writes -- is delivered by RunChainPerItem below (D2).
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

// ---------------------------------------------------------------------------------------------------
// E7 dispatch D2 -- PER-ITEM (slot,index) hazard model + WaveScheduler over the extracted graph.
// ---------------------------------------------------------------------------------------------------

/// The (stage, item) node id used by the per-item scheduler: owner = stage-owner hash, taskIndex =
/// the item index. A whole-slot (non-opted-in) stage collapses to a SINGLE node with taskIndex 0,
/// exactly the RunChain node id -- so the two paths agree for non-opted-in stages.
inline TaskId PerItemNodeId(const Stage& s, uint32_t item) {
    return TaskId{std::hash<std::string>{}(s.owner), item};
}

/// Record item i's accesses for an opted-in stage (empty if the stage has no per-item callback).
inline AccessRecorder RecordItemAccess(const Stage& s, uint32_t i) {
    AccessRecorder rec;
    if (s.perItemAccess) s.perItemAccess(i, rec);
    return rec;
}

/// True if any of `producerWrites` collides (same slot+index) with `consumerReadsOrWrites` -- a
/// RAW/WAW hazard at index granularity. WAR is handled by the caller passing producer READS.
inline bool KeysOverlap(const std::vector<StateAccessKey>& lhs, const std::vector<StateAccessKey>& rhs) {
    // Small per-item access sets (a handful of keys) -- a nested scan is cheaper than building a set.
    for (const auto& l : lhs)
        for (const auto& r : rhs)
            if (l == r) return true;
    return false;
}

/**
 * @brief Dispatch a chain with PER-ITEM (slot,index) hazard edges for stages that opt in (D2).
 *
 * This lifts RunChain's stage-granular ceiling: a stage may declare, per item, the exact (slot,index)
 * keys it reads/writes (Stage.perItemAccess). The scheduler then builds a graph whose NODES are
 * (stage,item) pairs for opted-in stages (one node per stage for whole-slot stages), derives edges
 * from StateAccessKey OVERLAP at index granularity (RAW/WAW/WAR), and layers them into waves via the
 * SAME extracted TaskDependencyGraph (AddEdge + GetParallelLevels) RunChain uses -- NO hand-rolled
 * scheduler. A consumer item then depends ONLY on the producer items that actually touched the
 * indices it reads, so an item reading only UNtouched indices runs in an earlier/parallel wave than a
 * whole-stage barrier would ever allow.
 *
 * Edge rules for an ordered pair (A before B in `stages`):
 *   - BOTH opt in:            A-item p -> B-item c iff p's writes overlap c's reads-or-writes (RAW/WAW)
 *                             OR p's reads overlap c's writes (WAR), by (slot,index) key.
 *   - one/both whole-slot:    fall back to stage-granular StagesConflict (a whole-slot stage touches
 *                             every index, so it is the conservative-correct whole-stage barrier) --
 *                             every node of the dependent stage waits for every node of the other.
 * This keeps RunChain's whole-stage behavior byte-behavior-identical for non-opted-in stages
 * (BACKWARD COMPATIBLE): a chain with no perItemAccess anywhere produces exactly RunChain's edges.
 *
 * @param stages      Ordered stages; earlier-in-vector = earlier program order for edge derivation.
 * @param profile     Backend/output policy per stage (empty = each stage's default backend).
 * @param workerCount TBB workers for the CpuTbb backend (>=1). Defaults to hardware concurrency.
 * @param outLevels   OPTIONAL: the wave layering (GetParallelLevels) the scheduler produced, so a
 *                    caller can WITNESS that per-item edges placed independent consumer items in an
 *                    earlier wave than the dependent ones. Node ids are PerItemNodeId(stage,item).
 */
inline Handle RunChainPerItem(const std::vector<Stage>& stages,
                              const DispatcherProfile& profile = {},
                              int workerCount = DefaultWorkerCount(),
                              std::vector<std::vector<TaskId>>* outLevels = nullptr) {
    Handle h;

    // Precompute per-item accesses for every opted-in stage (whole-slot stages record nothing).
    // access[si][i] = item i's recorded (slot,index) reads/writes; empty for whole-slot stages.
    std::vector<std::vector<AccessRecorder>> access(stages.size());
    for (size_t si = 0; si < stages.size(); ++si) {
        if (!stages[si].HasPerItemAccess()) continue;
        access[si].reserve(stages[si].itemCount);
        for (uint32_t i = 0; i < stages[si].itemCount; ++i)
            access[si].push_back(RecordItemAccess(stages[si], i));
    }

    // Build the node set: one node per item for opted-in stages, one node per whole-slot stage.
    TaskDependencyGraph graph;
    for (size_t si = 0; si < stages.size(); ++si) {
        if (stages[si].HasPerItemAccess())
            for (uint32_t i = 0; i < stages[si].itemCount; ++i)
                graph.AddTask(PerItemNodeId(stages[si], i));
        else
            graph.AddTask(PerItemNodeId(stages[si], 0));  // whole-slot: single node, taskIndex 0
    }

    // Derive edges for each ordered pair (A before B).
    for (size_t a = 0; a < stages.size(); ++a) {
        for (size_t b = a + 1; b < stages.size(); ++b) {
            const Stage& A = stages[a];
            const Stage& B = stages[b];
            const bool bothPerItem = A.HasPerItemAccess() && B.HasPerItemAccess();
            if (bothPerItem) {
                // Per-item edges: only the producer items whose (slot,index) writes/reads collide with
                // a consumer item's reads/writes create an edge. Items with no overlap stay unordered.
                for (uint32_t p = 0; p < A.itemCount; ++p) {
                    const AccessRecorder& pa = access[a][p];
                    for (uint32_t c = 0; c < B.itemCount; ++c) {
                        const AccessRecorder& ca = access[b][c];
                        const bool raw_waw = KeysOverlap(pa.writes, ca.reads) ||
                                             KeysOverlap(pa.writes, ca.writes);
                        const bool war = KeysOverlap(pa.reads, ca.writes);
                        if (raw_waw || war)
                            graph.AddEdge(PerItemNodeId(A, p), PerItemNodeId(B, c));
                    }
                }
            } else if (StagesConflict(A, B)) {
                // At least one stage is whole-slot: fall back to a stage-granular barrier -- every
                // node of B waits for every node of A (conservative-correct; a whole-slot stage
                // touches all indices). This is exactly RunChain's edge for such a pair.
                auto nodesOf = [](const Stage& s) {
                    std::vector<TaskId> ns;
                    if (s.HasPerItemAccess())
                        for (uint32_t i = 0; i < s.itemCount; ++i) ns.push_back(PerItemNodeId(s, i));
                    else
                        ns.push_back(PerItemNodeId(s, 0));
                    return ns;
                };
                for (const auto& an : nodesOf(A))
                    for (const auto& bn : nodesOf(B))
                        graph.AddEdge(an, bn);
            }
        }
    }

    const std::vector<std::vector<TaskId>> levels = graph.GetParallelLevels();
    if (outLevels) *outLevels = levels;

    // Map each node id back to its (stage,item) work and execute wave-by-wave through the SAME
    // Tier-A executor. A whole-slot stage node runs ALL its items (like RunPerElementStage); an
    // opted-in stage node runs exactly its one item. Waves run in dependency order; within a wave
    // the executor runs nodes in parallel under `workerCount`.
    bool ok = true;
    // owner-hash -> stage index, for O(1) node->stage lookup.
    std::unordered_map<uint64_t, size_t> ownerToStage;
    for (size_t si = 0; si < stages.size(); ++si)
        ownerToStage[std::hash<std::string>{}(stages[si].owner)] = si;

    for (const auto& wave : levels) {
        std::vector<VirtualTask> tasks;
        tasks.reserve(wave.size());
        for (const auto& nodeId : wave) {
            auto it = ownerToStage.find(nodeId.owner);
            if (it == ownerToStage.end()) continue;  // defensive; every node came from a stage
            const Stage& s = stages[it->second];
            const Backend backend = profile.BackendFor(s.owner, s.backend);
            (void)backend;  // CpuInline vs CpuTbb only changes worker count, handled below per-wave
            VirtualTask t;
            t.id = nodeId;
            if (s.HasPerItemAccess()) {
                const uint32_t item = nodeId.taskIndex;
                const auto& fn = s.perElement;
                t.execute = [&fn, item] { if (fn) fn(item); };
            } else {
                const auto& fn = s.perElement;
                const uint32_t n = s.itemCount;
                t.execute = [&fn, n] { if (fn) for (uint32_t i = 0; i < n; ++i) fn(i); };
            }
            tasks.push_back(std::move(t));
        }
        // CpuInline forces serial: if ANY stage in this wave is routed CpuInline, run 1 worker.
        int waveWorkers = workerCount;
        for (const auto& nodeId : wave) {
            auto it = ownerToStage.find(nodeId.owner);
            if (it != ownerToStage.end() &&
                profile.BackendFor(stages[it->second].owner, stages[it->second].backend) ==
                    Backend::CpuInline) {
                waveWorkers = 1;
                break;
            }
        }
        TaskExecutor executor;
        ok = ok && executor.Run(tasks, {wave}, waveWorkers);
    }

    h.completed = true;
    h.succeeded = ok;
    return h;
}

// ---------------------------------------------------------------------------------------------------
// terra-jobsim slice 3 -- access-DAG WAVES over a PRE-COMPUTED partition (NativeDispatchPlan.g.h).
// ---------------------------------------------------------------------------------------------------

/**
 * @brief Dispatch stages grouped into PRE-COMPUTED waves, running each wave's stages CONCURRENTLY and
 *        waves themselves in sequence (a wave is a full barrier for the next wave's start).
 *
 * Unlike RunChain/RunChainPerItem, which DERIVE the wave partition from pairwise StagesConflict scans
 * over the stage list, this takes a partition some upstream authority already proved disjoint --
 * terra-jobsim slice 3's NativeDispatchPlanEntry rows, which combine the solved tick schedule with the
 * field-level GaiaFieldAccessManifest. The caller is responsible for grouping `stages` into `waves`
 * (each inner vector = one wave's stage indices into `stages`, in ascending wave order) using that
 * proof; this function does NOT re-derive or re-verify disjointness -- it is the executor half of the
 * design, not the planner half. Two stages in the SAME wave run concurrently; this function does not
 * defend against a caller passing conflicting stages in one wave (the plan/manifest is the safety
 * proof; a caller bypassing it gets whatever a data race gets).
 *
 * Each wave's stages run through the SAME Tier-A executor as RunPerElementStage (one VirtualTask per
 * item, all stages' items pooled into ONE wave dispatch) so 1/2/N worker counts stay deterministic for
 * the same reason RunPerElementStage is: every item owns a disjoint (stage,index) key, so no item's
 * result depends on interleaving.
 *
 * @param stages      The stage list; `waves[i]` holds INDICES into this vector.
 * @param waves       Wave groups, ascending order; stages within one wave may run concurrently.
 * @param profile     Backend/output policy per stage (empty = each stage's default backend).
 * @param workerCount TBB workers for the CpuTbb backend (>=1). Defaults to hardware concurrency.
 */
inline Handle RunSystemWaves(const std::vector<Stage>& stages,
                             const std::vector<std::vector<size_t>>& waves,
                             const DispatcherProfile& profile = {},
                             int workerCount = DefaultWorkerCount()) {
    Handle h;
    bool ok = true;

    for (const auto& waveStageIndices : waves) {
        std::vector<VirtualTask> tasks;
        std::vector<TaskId> waveIds;
        // CpuInline forces the whole wave serial: if ANY member stage is routed CpuInline, 1 worker --
        // matching RunChainPerItem's per-wave rule (a wave is the barrier granularity here too).
        int waveWorkers = workerCount;
        for (size_t si : waveStageIndices)
            if (profile.BackendFor(stages[si].owner, stages[si].backend) == Backend::CpuInline) {
                waveWorkers = 1;
                break;
            }

        for (size_t si : waveStageIndices) {
            const Stage& s = stages[si];
            const uint64_t ownerId = std::hash<std::string>{}(s.owner);
            for (uint32_t i = 0; i < s.itemCount; ++i) {
                TaskId id{ownerId, i};
                VirtualTask t;
                t.id = id;
                const auto& fn = s.perElement;
                t.execute = [&fn, i] { if (fn) fn(i); };
                tasks.push_back(std::move(t));
                waveIds.push_back(id);
            }
        }
        if (tasks.empty()) continue;

        TaskExecutor executor;
        ok = ok && executor.Run(tasks, {waveIds}, waveWorkers);
    }

    h.completed = true;
    h.succeeded = ok;
    return h;
}

}  // namespace Vixen::KernelDispatch
