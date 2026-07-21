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
