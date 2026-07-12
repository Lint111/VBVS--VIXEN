---
title: Kernel Physics Dispatch Contract Spec
aliases: [Physics Dispatch Contract, Kernel Job Contract, VIXEN Physics Job ABI]
tags: [architecture, physics, kernel-framework, dispatch, gpu, parallelism, spec]
created: 2026-07-10
status: technical proposition
related:
  - "[[../03-Research/Voxel-Field-Physics-Research-2026-07]]"
  - "[[RenderGraph-System]]"
  - "[[Vulkan-Pipeline]]"
---

# Kernel Physics Dispatch Contract Spec

> [!summary]
> VIXEN should reuse the **kernel framework's declaration/codegen vocabulary**, but not the Unity
> runtime dispatcher as-is. The right split is: consumer-authored simulation intent becomes a
> generated, language-neutral dispatch artifact; VIXEN validates that artifact and schedules it
> through a native C++/Vulkan/CPU job substrate that understands fields, regions, mips, queues,
> residency, budgets, and deterministic writeback.

## 1. Verified Existing Surfaces

This proposal is grounded in the current repo rather than desired architecture alone.

### 1.1 VIXEN Already Consumes Kernel-Generated Artifacts

`VIXEN/codegen/CMakeLists.txt` already establishes the preferred cross-repo pattern:

- VIXEN owns small consumer schemas under `codegen/*-schemas`.
- The Yeroket kernel-codegen tool is invoked by CMake via `dotnet run --project`.
- Generated artifacts are committed beside their consumers.
- `*_check` targets run in `ALL` and fail on drift; `*_regen` targets regenerate on demand.
- The Windows/WSL bridge is explicit because a WSL-only tool may be visible from Windows but not
  executable by `ninja`/`cmd.exe`.

Current generated artifacts include `OctreeConfig.g.h`, `OctreeConfig.glsl`, `Hud.g.h`,
`Hud.view.rml`, `Hud.blob.g.h`, `hud.viewblob`, and `Hud.view.g.cs`. This proves that VIXEN can
already participate in the kernel "consumer-authored declaration -> committed generated artifact"
workflow without taking a Unity runtime dependency.

### 1.2 RenderGraph Has Useful Scheduling Primitives

The RenderGraph codebase already contains pieces that a physics dispatcher should reuse or extract:

| Current code | Verified capability | Physics relevance |
|---|---|---|
| `TaskQueue<T>` | Priority queue with GPU time/memory budget and strict/lenient overflow modes. | Useful budget concept, but single-threaded and node-local. |
| `VirtualTask` | Atomic work item keyed by `(NodeInstance*, taskIndex)` with phase, priority, dependencies, profile pointers, and error state. | Good task metadata shape, but identity is render-node-specific. |
| `VirtualResourceAccessTracker` | Task-level read/write conflict tracking where read/read is parallel and any write conflicts. | Useful hazard model, but resources are `Resource*`, not field/region/mip identities. |
| `TaskDependencyGraph` | Builds task dependencies and exposes parallel levels. | Reusable DAG/wave idea. |
| `TBBVirtualTaskExecutor` | Executes dependency levels with `tbb::parallel_for_each` and records failure stats. | Good CPU parallel prototype; not a GPU queue scheduler. |
| `WaveScheduler` | Groups nodes into waves based on topology and resource conflicts. | Good wave validation pattern. |
| `AccessKind` / `ResolveAccess` | Single source of truth for Vulkan stage/access/layout mapping. | Strong sync vocabulary seed. Needs extension for fields, queue ownership, and devices. |
| `FrameSyncScheduler` | Builds timeline edges and entry barriers from declared GPU access points. | Good timeline/barrier builder pattern. It is currently node/submit-group shaped. |
| `DispatchPass` / `MultiDispatchNode` | Describes compute pipeline, descriptor sets, push constants, workgroup counts, barriers, group ids. | Useful GPU pass descriptor, but records sequential dispatches into one render node. |
| `TimelineCapacityTracker` | Tracks budgets and measurements across multiple logical GPU queues and CPU threads. | Useful budget feedback model; it is measurement/planning, not actual queue ownership. |

Important constraint: `VulkanDevice` currently creates **one graphics queue** and exposes it as
`VulkanDevice::queue`. `ComputeDispatchNode` submits through `vkQueueSubmit2` to that queue and
guards submission with `VulkanDevice::SubmitMutex(queue)`. The mutex is correct for Vulkan external
synchronization, but it is not a queue scheduler. Physics needs command/work routing across graphics,
compute, async compute, transfer, and later cross-device domains; RenderGraph's current execution
does not provide that.

### 1.3 Kernel Framework Chain Concepts Worth Reusing

From the kernel framework authoring/chain docs:

- `[VMKernel]` PerElement kernels emit Burst scalar, Burst SIMD4, HLSL `.compute`, bindings, and a
  generated `KernelStage`.
- `[KernelBlackboardLayout]`, `BufferRef`, `AppendRef`, `SpatialHashRef`, `BoundaryHashRef`,
  `TableRef`, `IndirectRef`, and scalar slots define a declarative slot vocabulary.
- `KernelDispatcher.Begin()...BuildSpec().Schedule(profile, pool)` builds a reusable chain spec and
  schedules per-stage backends through `DispatcherProfile`.
- `DispatchFromCount`, `DispatchIndirectFromCount`, `DispatchFromBuffer`, `PublishAppendCount`,
  caller-owned buffer import/export, append buffers, and spatial hashes are already ideated or
  implemented on the Unity side.

Those concepts are valuable. The Unity implementations are not directly portable runtime code:
Burst, `NativeArray`, `ComputeBuffer`, Unity editor domain reload, Unity async readbacks, and Unity
job timing cannot become VIXEN runtime requirements.

## 2. Reuse Decision

| Surface | Recommendation | Reason |
|---|---|---|
| Kernel attributes and schema style | Reuse/extend | The declarative authoring shape is domain-blind enough. |
| Generated artifact workflow | Reuse | VIXEN already uses it with CMake drift guards. |
| Stage/spec/profile/handle vocabulary | Reuse conceptually | It maps well to generated physics chains and runtime handles. |
| Slot kinds: buffer, append, spatial hash, boundary hash, table, indirect, scalar | Reuse conceptually | These are exactly the primitive shapes needed for sparse simulation kernels. |
| CPU/GPU parity requirement | Reuse | Physics cannot tolerate CPU/GPU divergent semantics for deterministic fields. |
| Unity dispatcher runtime | Do not reuse directly | It is shaped around Burst, Unity buffers, and Unity scheduling. |
| Unity backend profile enum | Port into VIXEN terms | VIXEN needs `CpuInline`, `CpuTbb`, `GpuCompute`, `GpuAsyncCompute`, `Transfer`, `ExternalSolver`. |
| RenderGraph node executor | Extract patterns, not dependency | Physics jobs are not render nodes and need independent tick/cadence/history behavior. |
| RenderGraph `AccessKind` mapping | Extract/generalize | Good sync table, but lacks field semantics, region/mip scope, ownership transfer, and device residency. |

The target dependency model is **kernel-generated contracts**:

```text
consumer declaration
  -> kernel/codegen frontend
  -> generated manifest + layouts + optional native/shader kernels
  -> VIXEN artifact loader and validator
  -> VIXEN-native CPU/GPU physics dispatcher
  -> field deltas, history deltas, render-facing summaries
```

## 3. Required Kernel-Level Contract

The artifact must be descriptive enough that VIXEN can decide whether and where a job runs before
loading executable code or touching field data.

### 3.1 Artifact Header

```cpp
struct VxArtifactHeader {
    uint32_t magic;              // 'VXKA' or equivalent
    uint16_t abiMajor;
    uint16_t abiMinor;
    uint64_t producerHash;       // frontend/toolchain identity
    uint64_t sourceHash;         // canonical declaration hash
    uint64_t layoutHash;         // all slot/value layouts
    uint64_t logicHash;          // executable kernel body/package hash
    uint32_t manifestBytes;
    uint32_t codeBlobBytes;
};
```

Required validation:

- ABI major must match exactly.
- ABI minor may be forward-compatible only when all unknown features are optional.
- `layoutHash` must match every generated C++/shader-visible type.
- `sourceHash` must be available for drift/debug reporting.
- Runtime-injected artifacts must declare capabilities before any executable code is accepted.

### 3.2 Slot Manifest

```cpp
enum class VxSlotKind : uint8_t {
    Buffer,
    Image,
    FieldLayer,
    AppendBuffer,
    SpatialHash,
    BoundaryHash,
    IndirectArgs,
    Scalar,
    Table
};

enum class VxResidency : uint8_t {
    CpuOnly,
    GpuOnly,
    Mirrored,
    Streamed,
    DeviceLocal,
    CrossDeviceReplicated
};

enum class VxFieldSemantic : uint16_t {
    Occupancy,
    SignedDistance,
    Material,
    Velocity,
    KineticImpulse,
    Pressure,
    Temperature,
    HeatFlux,
    GasComposition,
    GravityVector,
    Damage,
    Stress,
    Strain,
    Phase,
    WeatherState,
    RenderSummary
};

struct VxMipRange {
    uint8_t minMip;
    uint8_t maxMip;
};

struct VxSlotDesc {
    uint32_t slotId;
    const char* name;
    VxSlotKind kind;
    VxResidency residency;
    VxFieldSemantic semantic;
    VxMipRange mipRange;
    uint64_t valueTypeHash;
    uint64_t layoutHash;
    uint64_t maxElementCount;
    uint32_t strideBytes;
    uint32_t accessFlags;        // read/write/readwrite/append/consume/indirect/etc.
    bool persistent;
    bool destructiveWrite;
};
```

Physics-specific additions over the current RenderGraph slot model:

- A slot can identify a semantic field layer, not only a Vulkan handle/resource.
- Region and mip scope are part of the hazard identity.
- Destructive writes are explicit. A heat glow summary write is not the same permission as a
  persistent material deletion or body split.
- Append/spatial/hash slots have declared capacity and overflow behavior.
- Indirect args are first-class so GPU-produced active-region counts can feed later dispatches.

### 3.3 Stage Descriptor

```cpp
enum class VxDispatchDomain : uint16_t {
    CpuInline       = 1 << 0,
    CpuTbb          = 1 << 1,
    CpuSimd         = 1 << 2,
    GpuCompute      = 1 << 3,
    GpuAsyncCompute = 1 << 4,
    Transfer        = 1 << 5,
    ExternalSolver  = 1 << 6
};

enum class VxDispatchShape : uint8_t {
    Linear,
    Sheet,
    Volume,
    RegionList,
    BrickList,
    BodyList,
    ParticleList,
    Indirect
};

struct VxRegionScope {
    uint64_t regionSetId;        // static or dynamic active set
    uint64_t bodySetId;
    VxMipRange mipRange;
    bool mayExpandScope;         // e.g. pressure/heat diffusion to neighbors
};

struct VxScalePolicyRef {
    uint32_t policyId;
    uint8_t requiredMinMip;
    uint8_t requiredMaxMip;
    bool canRunAsSummaryOnly;
    bool requiresConservationReconcile;
};

struct VxStageDesc {
    const char* name;
    uint32_t stableStageId;
    uint32_t supportedDomains;   // VxDispatchDomain bitset
    VxDispatchShape shape;
    uint32_t groupSizeX;
    uint32_t groupSizeY;
    uint32_t groupSizeZ;
    VxRegionScope scope;
    VxScalePolicyRef scalePolicy;
    uint32_t inputSlotIdsOffset;
    uint32_t inputSlotCount;
    uint32_t outputSlotIdsOffset;
    uint32_t outputSlotCount;
    uint64_t estimatedCostNs;
    uint64_t estimatedMemoryBytes;
    uint32_t deterministicFlags;
    uint32_t maxDispatchElements;
};
```

The key contract shift is that a stage is not just "dispatch over N elements." It is a declared
operation over a **field semantic**, a **spatial scope**, a **mip range**, and a **scale policy**.

### 3.4 Scale Policies

Physics jobs must be mip-scale aware. The same declared family can have different legal meanings at
different scales:

```cpp
enum class VxRepresentation : uint8_t {
    Analytic,
    SummaryField,
    SparseMipField,
    DenseLocalBrick,
    ParticleSet,
    BodyGraph,
    ExternalProxy
};

struct VxConservationMask {
    bool mass;
    bool momentum;
    bool energy;
    bool heat;
    bool pressure;
    bool materialVolume;
    bool damage;
};

struct VxScalePolicyDesc {
    uint32_t policyId;
    VxFieldSemantic semantic;
    VxMipRange validMipRange;
    VxRepresentation representation;
    uint32_t solverKind;
    VxConservationMask conservation;
    uint32_t refinementPathId;
    uint32_t transitionRuleId;
};

struct VxScaleTransitionDesc {
    uint32_t transitionRuleId;
    VxMipRange fromMip;
    VxMipRange toMip;
    float enterDistance;
    float exitDistance;          // hysteresis band
    uint32_t blendFieldMask;     // render-facing fields that can cross-fade
    uint32_t promotionRules;     // prewarm, prefetch, conserve, resolve history
};
```

Example implication: orbital ocean "simulation" may be a render-summary field derived from depth,
clouds, sun exposure, and weather bands. Local ocean near a hovering ship may be a high-resolution
surface/fluid interaction solve with buoyancy, wakes, foam, and arbitrary-gravity flow. The contract
must declare both as valid scale policies, not pretend one solver merely changes cell size.

### 3.5 Chain Descriptor

```cpp
struct VxStageEdge {
    uint32_t fromStageId;
    uint32_t toStageId;
    uint32_t slotId;
    uint32_t dependencyFlags;    // RAW/WAR/WAW, ownership transfer, readback, upload
};

struct VxChainDesc {
    const char* chainName;
    uint32_t chainId;
    uint32_t stageOffset;
    uint32_t stageCount;
    uint32_t edgeOffset;
    uint32_t edgeCount;
    uint32_t slotOffset;
    uint32_t slotCount;
    uint32_t scalePolicyOffset;
    uint32_t scalePolicyCount;
    uint32_t requiredCapabilityOffset;
    uint32_t requiredCapabilityCount;
};
```

`VxChainDesc` is the VIXEN-facing equivalent of the Unity `DispatcherSpec`, but it is data-only.
It is reusable across frames/ticks, while runtime bindings produce a per-run handle.

### 3.6 Consumer Job Injection Descriptor

```cpp
enum class VxBudgetClass : uint8_t {
    Critical,
    Interactive,
    Strategic,
    Background,
    Offline
};

struct VxJobInjectionDesc {
    const char* consumerId;
    const char* packageId;
    uint32_t chainId;
    VxBudgetClass budgetClass;
    double cadenceSeconds;
    uint32_t maxRegionsPerTick;
    uint32_t maxDispatchElementsPerTick;
    uint32_t capabilityFlags;    // destructive writes, persistent deltas, external solver, etc.
    uint32_t saveLoadPolicy;
    uint64_t deterministicSeedPolicyHash;
};
```

This is what lets a spaceship system declare an engine heat job, or a Rust/Python/C# consumer
generate the same artifact shape. VIXEN owns admission control.

## 4. VIXEN Runtime Objects

| Runtime object | Role |
|---|---|
| `PhysicsArtifactPackage` | Owns loaded manifest, generated type layout data, shader/native code blobs, and hashes. |
| `PhysicsJobManifest` | Validated, data-only description of chains, stages, slots, scale policies, and capabilities. |
| `PhysicsJobInstance` | Per-run binding of declared slots to actual field layers, buffers, body sets, and region sets. |
| `PhysicsDispatchGraph` | DAG built from job instances, field hazards, region/mip overlap, explicit edges, budgets, and queues. |
| `PhysicsDispatchHandle` | Completion, errors, timings, readbacks, output deltas, and debug trace for one scheduled run. |
| `FieldResourceRegistry` | Maps `(semantic, region, mip, representation)` to resident CPU/GPU resources. |
| `ScalePolicyRegistry` | Owns legal scale ladders and transition behavior for fluids, integrity, heat, weather, etc. |

The runtime must be independent from RenderGraph nodes. RenderGraph may host adapters that publish
render-facing fields or schedule visible-region updates, but physics dispatch cadence is not the
same thing as frame graph execution.

## 5. Validation Pipeline

Artifact admission should be strict:

1. **Header validation:** ABI, hashes, producer identity, package size, feature flags.
2. **Layout validation:** slot value sizes, alignments, std430/shader layout, generated C++ parity.
3. **Capability validation:** destructive writes, persistent deltas, external solvers, runtime load.
4. **Field validation:** semantic exists, representation exists, mip range is legal, scale policy is
   registered.
5. **Scope validation:** region/body scope is bounded or budget-gated; neighbor expansion is declared.
6. **Hazard validation:** read/write/append/indirect/transfer edges form a legal DAG or explicit loop
   with frame-latency policy.
7. **Residency validation:** resources can exist in the requested CPU/GPU/device domain.
8. **Queue validation:** selected backend is supported by the hardware and VIXEN queue topology.
9. **Budget validation:** estimated time, memory, append capacity, readback/upload costs, and max
   dispatch elements fit the declared budget class.
10. **Determinism validation:** seed policy, reduction policy, readback policy, and save/load policy
    are declared for gameplay-relevant jobs.

Reject rather than downgrade silently. If a job can run in summary-only mode, that must be declared
in `VxScalePolicyDesc` or `VxStageDesc`.

## 6. Dispatch Execution Model

Per simulation tick or frame:

```text
collect VIXEN internal jobs
collect validated consumer-injected jobs
collect mip-delta predictor refinement requests
bind fields/resources/body sets/region sets
build field-aware hazard DAG
route stages by backend profile and capacity
partition CPU work into TBB/task waves
record GPU command buffers per queue domain
insert barriers/ownership transfers/readback/upload bridges
submit with timeline semaphores/fences
publish field deltas, history deltas, event queues, timings, and debug traces
```

### 6.1 Hazard Identity

RenderGraph currently tracks hazards by `Resource*`. Physics needs:

```text
HazardKey =
  semantic
  + region key / region set
  + mip range
  + representation
  + body id set when applicable
  + device/residency domain
  + slot kind/access kind
```

Two jobs can write temperature in different regions at the same mip in parallel. A pressure job that
may expand to neighbor regions must declare that expansion or the scheduler cannot prove safety.

### 6.2 Access Kind Extension

Current `AccessKind` is a good Vulkan sync seed. Physics needs an extended semantic layer:

```cpp
enum class VxAccessKind : uint16_t {
    FieldRead,
    FieldWrite,
    FieldReadWrite,
    FieldAppend,
    FieldSummaryRead,
    FieldSummaryWrite,
    BodyStateRead,
    BodyStateWrite,
    HistoryDeltaAppend,
    BufferRead,
    BufferWrite,
    ImageSample,
    ImageStorageWrite,
    IndirectRead,
    IndirectWrite,
    TransferRead,
    TransferWrite,
    ExternalReadWrite
};
```

The backend resolver then maps `VxAccessKind` plus concrete resource class to Vulkan
stage/access/layout, CPU lock/reduction behavior, queue family transfer, and cross-device ownership.

### 6.3 Backend Profiles

VIXEN should port the kernel `DispatcherProfile` idea into native terms:

```cpp
struct VxBackendProfile {
    uint32_t stageId;
    VxDispatchDomain domain;
    uint32_t deviceId;
    uint32_t queueClass;          // graphics, compute, async compute, transfer
    uint32_t priority;
    bool allowReadback;
    bool allowUpload;
    bool allowSummaryFallback;
};
```

Default routing should prefer:

- CPU/TBB for small irregular jobs, topology graph updates, and deterministic reductions.
- GPU compute for dense region lists, field diffusion, contact candidate generation, prefix/sort,
  append/compact, and mip summaries.
- Async compute for background mip predictors, heat/pressure/weather summaries, and strategic
  offscreen simulation where the graphics queue should stay clear.
- Transfer queue for brick streaming, readbacks, and cross-device exchange.
- External solver only when the adapter can declare all field inputs, outputs, and writeback limits.

## 7. What VIXEN Is Missing

The current engine has useful parts, but the following are missing for the physics target:

1. **Simulation job graph independent from RenderGraph nodes.** Existing virtual tasks are keyed by
   `NodeInstance*`; physics needs region/body/field jobs with their own tick cadence.
2. **Field/region/mip resource identity.** Hazard tracking must understand semantic fields and
   overlap at different scales, not only pointer equality.
3. **Artifact ABI loader and validator.** VIXEN can generate headers today, but it does not load or
   validate physics job packages.
4. **Queue-domain scheduler.** Current Vulkan device creation exposes one graphics queue, and compute
   submissions go through that queue plus a submit mutex.
5. **Queue family/device ownership model.** Needed for async compute, transfer queue, multi-GPU, and
   cross-device residency.
6. **GPU-produced work integration.** Indirect dispatch, append counts, active-region compaction, and
   GPU-generated refinement requests must become first-class scheduling inputs.
7. **Append/spatial/boundary hash primitives in VIXEN.** Kernel framework has the vocabulary; VIXEN
   needs native Vulkan buffer/materialization.
8. **Explicit readback/upload bridge model.** Readbacks and uploads need declared latency, budget,
   and determinism costs.
9. **Capability/permission system for consumer jobs.** Runtime or compile-time injection must be
   able to reject destructive writes and over-budget work before execution.
10. **Mip-scale policy registry.** Solvers need legal scale meanings, transition bands, hysteresis,
    conservation rules, and render-summary fallbacks.
11. **Deterministic reduction/replay contract.** Especially for offscreen asteroid impacts, body
    splitting, pressure loss, heat damage, and multiplayer/save-load behavior.
12. **Dispatch graph debugger.** Must answer job -> artifact -> fields -> regions -> barriers ->
    queue/device -> timing -> output deltas.
13. **Cost model beyond nanoseconds.** Need memory bandwidth, append capacity, active region count,
    readback/upload latency, and queue pressure.
14. **Cross-GPU partitioning policy.** Not required for v1 execution, but the ABI must avoid blocking
    explicit ownership and transfer later.

## 8. Extraction Plan

### P0: Contract-Only Schema

- Define `VxArtifactHeader`, `VxSlotDesc`, `VxStageDesc`, `VxChainDesc`, `VxScalePolicyDesc`, and
  `VxJobInjectionDesc`.
- Add generated/hand-authored fixture manifests with no executable kernels.
- Validate ABI, slot layouts, field semantics, scale policies, and hazard keys.

### P1: Field-Aware DAG Prototype

- Build `PhysicsDispatchGraph` from manifests.
- Implement hazard overlap by `(semantic, region, mip, representation)`.
- Reuse RenderGraph's read/write conflict rule and parallel-level idea.
- Output a debug text/JSON graph before executing any work.

### P2: CPU/TBB Execution

- Port the TBB level executor pattern to `PhysicsJobInstance` identities.
- Support region-list and body-list jobs with deterministic reduction stages.
- Record `TimelineCapacityTracker`-style measurements under physics-owned labels.

### P3: Single-Queue GPU Compute Adapter

- Convert a validated stage into a VIXEN `DispatchPass`-like record.
- Submit through the current queue path first, using existing `AccessKind`/barrier machinery where
  possible.
- Keep this intentionally limited: one device, one queue, no runtime injection.

### P4: Append/Indirect/Active-Region Primitives

- Native Vulkan append buffers with count publish.
- Indirect dispatch args generated from active region counts.
- Compact/sort active region lists for sparse fields.
- Bridge readback/upload only through explicit manifest edges.

### P5: Mip-Scale Policies

- Register scale ladders for ocean/water, heat, pressure, and integrity.
- Implement transition bands, hysteresis, summary/fine reconciliation, and debug overlays.
- Let the mip-delta predictor emit `RefinementRequest`s into the same dispatcher.

### P6: Multi-Queue Scheduler

- Extend `VulkanDevice` creation to expose graphics/compute/transfer queue handles where available.
- Add queue-class routing, queue family ownership transfer, timeline semaphores, and per-queue
  capacity tracking.
- Replace "submit mutex as accidental scheduler" with explicit queue submission ownership.

### P7: Consumer Injection

- Compile-time first: linked generated packages only.
- Runtime second: package loading with ABI/capability validation, hard budgets, and debug labels.
- Keep authoring language out of runtime. C#, Rust, Python, or DSL frontends all target the same ABI.

## 9. Design Position

The kernel framework should be treated as a **contract and artifact generator**, not the owner of
VIXEN physics runtime scheduling. This keeps the project aligned with kernel unification while
protecting VIXEN's core needs:

- Vulkan-native queue and resource ownership.
- Field-native simulation rather than mesh/collider authority.
- Mip-aware scale semantics.
- Consumer-declared jobs with engine-owned validation.
- CPU/GPU/cross-device scheduling that can grow beyond Unity's dispatcher assumptions.

The practical first spike is small: generate or hand-author one manifest for a toy heat diffusion
job over sparse regions, validate it, build the hazard DAG, execute a CPU/TBB version, then bind the
same slots to a single Vulkan compute pass. If that shape holds, it becomes the foundation for
voxel body integrity, offscreen collision history, fluid summaries, and consumer-injected systems.
