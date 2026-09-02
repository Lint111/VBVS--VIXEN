// Copyright (C) 2025 Lior Yanai (eLiorg)
// Licensed under the MIT License.
// See LICENSE file in the project root for full license information.
#pragma once

/**
 * @file Abi.h
 * @brief The GENERALIZED, domain-blind dispatcher contract (E7 dispatch D1).
 *
 * This is the Unity-free counterpart of the Yeroket kernel's managed dispatcher vocabulary
 * ($KF/Runtime/Dispatcher/: Backend, DispatcherProfile, SlotRef, KernelStageBase, DispatcherHandle).
 * Per E7 spec 144 ("reuse the verified kernel stage/chain/profile/handle CONCEPTS"), the dispatcher
 * is generalized the same way the transpiler / GpuStruct-emit / AppFlow were: the domain-blind
 * CONTRACT is lifted out, and each runtime is a BACKEND of it. The managed DispatcherSpec (Unity/
 * Burst-in-Unity) is one backend; the native TBB runner in this library (TaskExecutor + Dispatcher,
 * extracted from RenderGraph Tier-A) is another. Same contract, two backends -- NOT a hand-copied
 * twin, NOT a parallel invention.
 *
 * These types mirror the kernel's names and shapes so a transpiled [VMKernel] maps onto ONE
 * generalized Stage, and the backend (Burst-Unity vs native-TBB) is a profile choice. They are plain
 * data + a std::function payload; the library owns NO undertow causal/commit policy (spec :1781) and
 * no gaia/NodeInstance/Resource types.
 *
 * D1 fills only what one CpuTbb per-element stage needs; the shape leaves room for the rest.
 */

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace Vixen::KernelDispatch {

/**
 * @brief Execution backend for a stage -- the generalized superset of the kernel's `Backend` enum.
 *
 * The kernel's managed `Backend {Burst, GPU}` are two points on this axis (Burst == CpuTbb/CpuSimd
 * family, GPU == GpuCompute family). Spec :1780 names the full routing set. D1 implements only
 * CpuTbb; the rest are declared so a DispatcherProfile can route to them once their backends land.
 */
enum class Backend : uint8_t {
    CpuInline,        ///< Run on the calling thread, no parallelism.
    CpuTbb,           ///< TBB task-wave parallelism (the D1 native backend).
    CpuSimd,          ///< (later) SIMD-vectorized fold.
    GpuCompute,       ///< (later) GPU compute dispatch (the kernel's Backend.GPU).
    GpuAsyncCompute,  ///< (later) GPU async-compute queue.
    Transfer,         ///< (later) DMA/transfer engine.
};

/// What happens to a stage's declared outputs (mirrors the kernel's `OutputPolicy`). D1 = None.
enum class OutputPolicy : uint8_t {
    None,  ///< Outputs stay on the writer's backend (D1 default; CpuTbb writes in place).
    All,   ///< (later) All declared outputs land on CPU (GPU writers trigger a readback bridge).
};

/**
 * @brief Admission lane for work that must not consume another lane's worker budget.
 *
 * FrameCompute is the existing bounded TBB wave executor. BlockingIO is a separately budgeted,
 * lazy lane for shader compilation and cache/file I/O. BackgroundGpu is a bounded, non-frame-
 * immediate lane for host-data-friendly work whose result is integrated at an epoch boundary.
 * A task never borrows workers from another lane.
 */
enum class TaskLane : uint8_t {
    FrameCompute,
    BlockingIO,
    BackgroundGpu,
};

/**
 * @brief Worker budget for one admission lane.
 *
 * `workerCount == 0` selects the implementation's bounded default (currently min(4, hardware
 * concurrency) for BlockingIO). Callers that need an explicit budget (tests, hosts with a known
 * I/O ceiling) set a positive count. Budgets are independent; work submitted to one lane never
 * borrows workers from the other.
 */
struct LaneBudget {
    uint32_t workerCount = 0;
};

/**
 * @brief Per-dispatch policy: which backend runs each stage + the output readback policy.
 *
 * The native counterpart of the kernel's `DispatcherProfile {Dictionary<string,Backend> Backends;
 * OutputPolicy Outputs}` -- "one profile per LOD". The same chain can be dispatched against many
 * profiles; the backend is a profile choice, not baked into the stage. D1 uses a single stage keyed
 * by its owner-id string; an absent/empty map means "use the stage's default backend".
 */
struct DispatcherProfile {
    std::unordered_map<std::string, Backend> backends;  ///< stage owner-id -> backend (empty = default)
    OutputPolicy outputs = OutputPolicy::None;
    LaneBudget frameCompute;
    LaneBudget blockingIO;
    LaneBudget backgroundGpu;

    /// Return the configured budget, or `fallback` when the profile requests its bounded default.
    [[nodiscard]] uint32_t WorkerCount(TaskLane lane, uint32_t fallback) const noexcept {
        const auto& budget = lane == TaskLane::BlockingIO ? blockingIO
                           : lane == TaskLane::BackgroundGpu ? backgroundGpu
                           : frameCompute;
        return budget.workerCount == 0 ? fallback : budget.workerCount;
    }

    // BackgroundGpu admission is intentionally a policy surface only in this
    // slice. A consumer must first prove the CapabilityGraph node exists, then
    // submit bounded work with an epoch-boundary result; unknown capability
    // state is fail-closed and never falls back to FrameCompute implicitly.

    /// Backend chosen for `owner`, falling back to `fallback` when the profile has no override.
    [[nodiscard]] Backend BackendFor(const std::string& owner, Backend fallback) const {
        auto it = backends.find(owner);
        return it != backends.end() ? it->second : fallback;
    }
};

/**
 * @brief Backend-abstract handle to a dispatcher slot (mirrors the kernel's `SlotRef<T>`).
 *
 * Slot identity is `id`; the caller's element type is carried out-of-band (D1 stages bind a typed
 * closure, so the contract stays untyped here -- the managed SlotRef<T>'s type param gates a
 * read/write API this D1 slice doesn't yet expose). A stage records read/write slots so a chain
 * planner can derive dependency edges; D1's single stage declares its write slot but forms no edges.
 * `id == 0` is the invalid/default slot (matches the kernel's `IsValid => Id != 0`).
 */
struct SlotRef {
    int32_t id = 0;              ///< Slot identity (0 = invalid/default).
    int32_t length = 0;          ///< Element count.
    std::string debugName;       ///< Diagnostic name, surfaces in validation errors.

    [[nodiscard]] bool IsValid() const { return id != 0; }
    bool operator==(const SlotRef& o) const { return id == o.id; }
};

/**
 * @brief Per-item hazard identity: a single (slot, index) touched by one item (E7 dispatch D2).
 *
 * This is the domain-blind generalization of the render RenderGraph Tier-B hazard identity (its
 * VirtualResourceAccessTracker keyed a hazard on a resource + subresource); here it is an opaque
 * `slot` int plus an `index` int -- NO render/gaia types, the consumer binds the meaning. SlotRef
 * (above) stays as the whole-slot identity a chain planner derives STAGE-granular edges from; this
 * StateAccessKey is finer-grained access WITHIN a slot, so a consumer item can depend only on the
 * producer items that actually touched the indices it reads.
 *
 * A whole-slot access = one key per index in [0, length) (or, equivalently, a stage that does NOT
 * opt into per-item access = today's stage-granular behavior). A SPARSE access = only the specific
 * indices an item touches -- that is where per-item edges produce OBSERVABLE independence over a
 * whole-stage barrier.
 */
struct StateAccessKey {
    int32_t slot = 0;   ///< Opaque slot identity (matches a SlotRef.id; consumer binds meaning).
    int64_t index = 0;  ///< Element index WITHIN the slot the item read/wrote.

    bool operator==(const StateAccessKey& o) const { return slot == o.slot && index == o.index; }
};

/// Hash for StateAccessKey -- enables (slot,index) overlap lookup in unordered containers.
struct StateAccessKeyHash {
    size_t operator()(const StateAccessKey& k) const {
        size_t a = std::hash<int32_t>{}(k.slot);
        size_t b = std::hash<int64_t>{}(k.index);
        return a ^ (b << 16) ^ (b >> 16);
    }
};

/**
 * @brief Records the (slot,index) reads/writes one item performs (E7 dispatch D2).
 *
 * A stage that OPTS IN to per-item scheduling supplies a `perItemAccess(i, recorder)` callback that
 * calls Read/Write for each (slot,index) item i touches. The scheduler then derives per-item edges
 * from key overlap between a producer item's writes and a consumer item's reads/writes. The recorder
 * is a plain accumulator -- no scheduling policy lives here.
 */
struct AccessRecorder {
    std::vector<StateAccessKey> reads;
    std::vector<StateAccessKey> writes;

    void Read(int32_t slot, int64_t index) { reads.push_back({slot, index}); }
    void Write(int32_t slot, int64_t index) { writes.push_back({slot, index}); }
};

/**
 * @brief A unit of dispatchable work: an owner id, slot read/write refs, and a per-element callable.
 *
 * The native counterpart of the kernel's `KernelStageBase` (owner + ReadSlotIds/WriteSlotIds +
 * ScheduleStage). A transpiled [VMKernel] maps onto this the same way it maps onto KernelStageBase:
 * the emitted per-element body becomes `perElement`, and its BufferRef read/write slots become
 * `reads`/`writes`. The library never dereferences `owner` (opaque, domain-blind) -- it is the
 * stage's identity key into DispatcherProfile.backends.
 *
 * D1 minimal: `perElement(i)` is invoked for i in [0, itemCount); the callable captures the data it
 * touches (e.g. the Gaia column). The heavy managed KernelStageBase machinery (GPU shader binding,
 * indirect dispatch, boundary/spatial hashes) is a backend concern, deferred -- not in the contract.
 */
struct Stage {
    std::string owner;                            ///< Opaque stage identity (DispatcherProfile key).
    uint32_t itemCount = 0;                       ///< Per-element item count.
    std::vector<SlotRef> reads;                   ///< Slots this stage reads (for chain edges; D1 unused).
    std::vector<SlotRef> writes;                  ///< Slots this stage writes.
    Backend backend = Backend::CpuTbb;            ///< Default backend when the profile doesn't override.
    std::function<void(uint32_t /*i*/)> perElement;  ///< The emitted per-element work.

    /// OPTIONAL per-item access declaration (E7 dispatch D2). When set, item i records the exact
    /// (slot,index) keys it reads/writes into `rec`, and RunChainPerItem derives PER-ITEM edges from
    /// key overlap. When ABSENT (default), the stage is scheduled whole-slot (stage-granular, exactly
    /// as RunChain does today) -- so existing stages are unchanged and only opted-in stages get
    /// finer edges. This is the minimal honest extension: reads/writes above stay the whole-slot
    /// identity a stage-granular planner uses; this callback is the per-item refinement.
    std::function<void(uint32_t /*i*/, AccessRecorder& /*rec*/)> perItemAccess;

    [[nodiscard]] bool HasPerItemAccess() const { return static_cast<bool>(perItemAccess); }
};

/**
 * @brief Result of a submitted dispatch (mirrors the kernel's `DispatcherHandle` poll/complete shape).
 *
 * D1's native TBB dispatch is synchronous, so `completed` is set at return and `Complete()` is a
 * no-op that returns `succeeded`. The kernel's DispatcherHandle carries the async/readback state a
 * GPU backend needs; that arrives with the GPU backend, not the contract.
 */
struct Handle {
    bool completed = false;   ///< The dispatch ran to completion (kernel: IsCompleted).
    bool succeeded = false;   ///< No per-element callable threw.

    [[nodiscard]] bool IsCompleted() const { return completed; }
    bool Complete() const { return succeeded; }  ///< Block-until-done; no-op for the synchronous CpuTbb backend.
    [[nodiscard]] bool ok() const { return completed && succeeded; }
};

}  // namespace Vixen::KernelDispatch
