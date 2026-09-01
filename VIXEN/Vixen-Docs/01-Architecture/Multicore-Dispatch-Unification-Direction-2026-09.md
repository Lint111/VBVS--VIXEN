# Multicore Dispatch Unification Direction — September 2026

**Status:** Direction approved; M1 first slice and decision #6's blocking-lane slice implemented and gated on 2026-09-01; M2–M6 remain future milestones.

**Decision:** Make `libraries/KernelDispatch` the one Tier-A task/DAG/executor foundation. Keep RenderGraph as the Tier-B owner of graph semantics, resource hazards, Vulkan synchronization, and node lifecycle. Express render-node work and the useful part of the dormant `InstanceGroup` idea as shared dispatcher stages/partitions instead of maintaining a second node-instance scheduler.

## 1. Scope and prior contracts

This document audits the engine-side concurrency mechanisms, resolves which mechanism is “parallel to the shared TDD solution,” and compares four ways to make VIXEN a multicore framework. It began as the owner-validation checkpoint; §14 records the approved M1 implementation.

It does not restate the contracts already established by:

- [Kernel Physics Dispatch Contract Spec](Kernel-Physics-Dispatch-Contract-Spec-2026-07.md), which defines the domain-blind declaration/codegen vocabulary, field/residency extensions, DAG, backend profiles, deterministic CPU/GPU parity, and native VIXEN ownership.
- [Recipe Bucketed-Dispatch Overhead plan](Recipe-Bucketed-Dispatch-Overhead-Inc3-Plan-2026-07.md) and [Recipe Live-App Bucketed Dispatch plan](Recipe-Live-App-Bucketed-Dispatch-Inc4-Plan-2026-07.md), which establish that grouping GPU dispatch calls is a GPU recording strategy, not a CPU task executor, and that unconditional regroup/readback work can add CPU cost without a clear GPU win.
- [Undertow–VIXEN Federation](Undertow-VIXEN-Federation-2026-08.md), which keeps Undertow managed state authoritative, makes VIXEN a presentation consumer, and records that the installed VIXEN export currently omits `KernelDispatch`.

Terminology used here:

- **Tier A** is domain-blind task identity, dependency edges, wave construction, worker admission, execution, cancellation, and completion.
- **Tier B** is domain-aware compilation: render topology, resource/access meaning, frame cadence, Vulkan barriers and ownership, residency, and lifecycle.
- **Worker-count invariant** means the same observable result for worker counts 1, 2, and N. It does not require the same wall-clock completion order for independent work.
- **Manual instances** are distinct semantic nodes explicitly created by the graph author. An **instance-group shard** is one partition of one logical workload. They are not interchangeable.

## 2. Executive findings

1. `InstanceGroup` was not barely used; it was declaration-only. Before the 2026-09-01 cleanup, its only implementation-tree occurrence was its own header: there was no `.cpp`, no `RenderGraph::CreateInstanceGroup`, no out-of-header method definition, and zero production or test consumers. The header has now been removed; the remaining archive/examples are historical documentation, not build inputs.
2. Before the 2026-09-01 cleanup, VIXEN contained two implementations of the same Tier-A shape. RenderGraph's local `VirtualTask`, `TaskDependencyGraph`, resource-derived waves, and `TBBVirtualTaskExecutor` had zero production activators; they are now deleted. `KernelDispatch` owns the surviving opaque `VirtualTask`, `TaskDependencyGraph`, and `TaskExecutor`. RenderGraph retains its Tier-B `ResourceAccessTracker` and frame-sync scheduler.
3. `KernelDispatch` is the intended shared TDD foundation, but it has zero consumers outside its own library and is omitted from `VIXEN_EXPORTED_TARGETS` (`libraries/KernelDispatch/include/KernelDispatch/Abi.h:7-20`; `cmake/VixenInstall.cmake:46-74`). It is a contract island today, not yet an engine foundation.
4. The live frame is effectively single-core. `Tick()` calls update then render; `VulkanGraphApplication::Render()` calls one `RenderGraph::RenderFrame()`; and the default RenderGraph branch executes `executionOrder` one node at a time (`application/main/source/VulkanApplicationBase.cpp:139-179`; `application/main/source/VulkanGraphApplication.cpp:344-389`; `libraries/RenderGraph/src/Core/RenderGraph.cpp:737-970`).
5. Existing concurrency is fragmented across two TBB DAG executors, two SVO TBB loops, one node-local `std::async` scheduler, cache-wide nested `std::async`, a scene-specific eight-future bake, an unused EventBus thread bridge, and an unused shader worker pool. These islands have different admission, shutdown, exception, ordering, and worker-count rules.
6. Current Vulkan scheduling supports up to four frames in flight and per-node submit groups, but the device creates one queue and every submit/present is externally serialized through a per-queue mutex (`libraries/RenderGraph/include/Data/Nodes/FrameSyncNodeConfig.h:24-25,76`; `libraries/VulkanResources/src/VulkanDevice.cpp:228-233,364-365,388-395`). That is GPU/CPU overlap, not CPU node parallelism or multi-queue execution.
7. The smallest safe bridge was already present conceptually: `NodeInstance` array tasks are render-node workloads that lower directly to the shared task DAG/executor. M1 replaced `SlotTaskManager`'s raw `std::async` execution with that fabric, proving graph-work-through-TDD without touching concurrent Vulkan recording.

## 3. Evidence method and counting rules

The audit searched declaring constructs and direct `std::thread`, `std::jthread`, `std::async`, TBB, and oneTBB calls under `application/` and `libraries/`. A consumer is a non-comment declaration use, construction, activation, or call outside the mechanism's own implementation. Header examples do not count. Test-only constructions are reported separately and do not raise a production consumer count. Counts are syntactic call-site counts, not runtime multiplicities; conditional graph variants can make fewer sites live in one run.

No `std::execution` use was found. Mutexes and atomics that only protect an object are not treated as executors, but queue-submit locking and thread-safe completion paths are included where they constrain the design.

## 4. Concurrency inventory

| Mechanism | Evidence and consumers | Threading model | Determinism contract | Duplication or disposition relative to shared TDD |
|---|---|---|---|---|
| `KernelDispatch` ABI and dispatcher. | `Abi.h` declares stages, access, profiles, backends, and handles; `Dispatcher.h` implements per-element, stage-chain, per-item-hazard, and precomputed-wave entry points (`libraries/KernelDispatch/include/KernelDispatch/Abi.h:7-20,33-189`; `Dispatcher.h:34-90,93-169,200-419`). There are **zero consumers outside `libraries/KernelDispatch`**. | `CpuInline` uses one worker and `CpuTbb` maps items/waves to a bounded TBB task arena; future GPU/transfer values are declared but not implemented. | The header explicitly requires per-element results to be invariant across worker counts (`Dispatcher.h:17-20,43-47`). Access-derived RAW/WAW/WAR edges order conflicting stages/items. | This is the foundation to extend. Its public dispatcher wrappers are still synchronous, `RunChain` executes independent stages in the same graph wave left-to-right (`Dispatcher.h:150-165`), and blocking/cancellation lanes are missing; those are extension work, not reasons to introduce another executor. |
| `KernelDispatch::TaskDependencyGraph`, `VirtualTask`, and `TaskExecutor`. | The graph is an opaque-ID, domain-blind DAG and exposes parallel levels; the executor runs waves and caps workers (`libraries/KernelDispatch/include/KernelDispatch/TaskDependencyGraph.h:7-22,33-68`; `VirtualTask.h:26-75`; `TaskExecutor.h:7-16,35-67`; `src/TaskExecutor.cpp:27-90`). M1 added RenderGraph as the first external consumer; decision #6 adds the same executor's lazy, independently budgeted blocking lane. | A single caller supplies tasks and waves; each wave uses `tbb::parallel_for` inside one `tbb::task_arena`, while blocking submissions use a bounded FIFO worker lane that never borrows frame-compute workers. | Determinism comes from declared edges, disjoint task writes, stable post-wave error ordering, cooperative epoch cancellation, and `CompletionHandle::SubmissionOrder()` for background results. Queued blocking work is Cancelled before invocation; running I/O/compiler work finishes cooperatively. | Keep this as the sole Tier-A admission foundation. Later task classes/lanes must extend it rather than introduce private pools. |
| RenderGraph virtual-task DAG and `TBBVirtualTaskExecutor`. | The pre-removal census found one default-off executor, zero production activators, and no effective concurrency cap (`SetMaxConcurrency` only logged). The 2026-09-01 cleanup deleted the local task/DAG/tracker/executor types, their RenderGraph configuration APIs, and five legacy test sources. | No current RenderGraph execution path remains; the sequential dependency-order loop is the sole graph frame path. | The removed branch had no worker-count result gate, while the default path's node state, callbacks, graph mutation, Vulkan allocators, and many node implementations remain serially ordered. | **Retired.** `KernelDispatch::VirtualTask`/`TaskDependencyGraph`/`TaskExecutor` is the sole Tier-A executor. RenderGraph's `ResourceAccessTracker` remains Tier-B analysis input for future, explicitly gated lowering. |
| `InstanceGroup` node-cloning API. | The removed header proposed device/workload/budget/memory scaling, `_0.._N` cloning, and distributed work. The pre-removal census found **zero definitions and zero production/test consumers**; the post-removal source census has no live declaring construct. | It was a planned node-cloning scheduler, not executable code. | Its proposed indices were stable only within a frame and its parameter distribution remained unresolved; it defined no worker-count-invariance gate. | **Retired in the 2026-09-01 cleanup.** Preserve only the useful partition-policy intent as `Stage` domain/partition metadata lowered to shared `VirtualTask`s; manual semantic instances remain untouched. |
| Manual multi-instance graph construction. | The main `BuildRenderGraph.cpp` contains **149 `AddNode<T>` call sites**. Repeated semantic types include 24 storage buffers, 23 buffer-sync gatherers, 18 compute stages, 17 shader libraries, and 16 constants. These are compile-time variants, but they demonstrate extensive explicit graph authoring. | Each explicit node follows the graph's normal sequential/default or future wave execution. It is not auto-sharding. | Instance identity, parameters, connections, and ownership are explicit and therefore stable under graph compilation. | Keep. Manual semantic nodes must not be collapsed into workload shards. Only a node's internal indexed domain or an explicitly declared logical group should partition through Tier A. |
| `ITaskProfile` and `TaskProfileRegistry`. | The profile API describes polymorphic pressure valves and names a `BatchDispatchProfile` that would change batch size (`libraries/RenderGraph/include/Core/ITaskProfile.h:7-22,97-106`). There are **11 production profile registrations**, all `SimpleTaskProfile`; no `BatchDispatchProfile` class exists. Registry pressure calls adjust work units (`TaskProfileRegistry.h:278-345`). | It is single-threaded admission/calibration state; it does not execute tasks. `SimpleTaskProfile::OnWorkUnitsChanged` has no workload-specific effect. | Priority selection is deterministic for the registry state, but all live pressure adjustments are behaviorally inert because no live profile connects work units to a real knob. | Move useful cost, priority, backend, and partition-pressure data into the shared dispatcher profile/admission contract. Do not preserve an inert parallel policy layer. A workload-specific valve is valid only when its stage implements the knob. |
| `SlotTaskManager`. | `NodeInstance` owns one manager and has **one production call path** to `ExecuteParallel` (`libraries/RenderGraph/include/Core/NodeInstance.h:1152`; `src/Core/NodeInstance.cpp:400-426`). The implementation batches task contexts and launches one `std::async` per task (`src/Core/SlotTask.cpp:131-255`). | Each call creates futures/threads up to a computed or supplied parallelism, with a barrier after each budget-sized batch. | Results are stored by task index and the caller counts successes, but declared resource hazards, cancellation, and a formal worker-count gate are absent. Correctness relies on the node task function using disjoint state. | Migrate first. Preserve generation, budget, and result APIs; execute the generated indexed work as one shared stage. This deletes a raw-thread island and proves RenderGraph workload homomorphism at low Vulkan risk. |
| `TaskQueue<DispatchPass>` and `MultiDispatchNode`. | `TaskQueue` is explicitly single-threaded, stable-priority, and not mutex-protected (`libraries/RenderGraph/include/Core/TaskQueue.h:3-24,42-47`). It has one instantiated production data type. The main graph creates **one** `MultiDispatchNode`; the application has **two** `QueueDispatch` call sites (`application/main/source/graph/BuildRenderGraph.cpp:1051`; `application/main/source/VulkanGraphApplication.cpp:1083,1265`). | One CPU thread records each group/pass serially into one command buffer, then one queue submission is made. | Stable priority/insertion ordering and ordered `std::map` groups make recording deterministic. Vulkan barriers enforce GPU hazards. | Keep its GPU batching semantics where useful, but do not confuse it with CPU parallelism. Admission should eventually use shared profiles, while command recording can later become shared executor work only after Vulkan ownership rules are met. |
| `LoopManager` and `LoopBridgeNode`. | The application registers **two** loops, physics and simulation; the main graph creates **one** loop bridge (`application/main/source/VulkanGraphApplication.cpp:259-276`; `application/main/source/graph/BuildRenderGraph.cpp:1159`). `RenderFrame` updates all loops once per frame (`libraries/RenderGraph/src/Core/RenderGraph.cpp:770-781`). | A single caller iterates accumulator state; the bridge publishes a reference and `shouldExecute` value (`libraries/RenderGraph/src/Core/LoopManager.cpp:38-130`; `src/Nodes/LoopBridgeNode.cpp:53-70`). `MultipleSteps` currently triggers only once per update. | Fixed-step debt rules are explicit, but cadence depends on input frame delta. There is no task execution or cross-loop concurrency. | Keep as a Tier-B cadence/admission source. When a loop is due, lower its work into shared stages. Do not turn `LoopManager` into another executor. |
| EventBus `WorkerThreadBridge<T>`. | The only occurrences outside its own declaration are documentation and an include from `AsyncShaderBundleBuilder`; there are **zero constructions/consumers** (`libraries/EventBus/include/WorkerThreadBridge.h:74-122`; `libraries/ShaderManagement/include/AsyncShaderBundleBuilder.h:6`). | Each constructed bridge would own one FIFO worker thread, execute serially, then publish results from that worker (`WorkerThreadBridge.h:102-119,145-209`). | FIFO work order is stable, but publication timing relative to main-thread message processing is not. Shutdown stops the loop and can leave queued work unprocessed if `running` becomes false before another iteration. | Delete if the zero-consumer census still holds at implementation time. Future async EventBus work should submit to the shared executor and publish completed results at a deterministic main-thread/frame boundary. |
| `AsyncShaderBundleBuilder`. | The class is retained for its public async API; its private worker pool and work-stealing queues were removed. | It submits one UUID-keyed build to `TaskExecutor`'s blocking lane, preserving started/progress/completed/failed EventBus messages and best-effort cancellation. | `CompletionHandle` gives a stable per-submission order; event publication remains keyed by UUID and `WaitForBuild`/`WaitForAll` retain their readiness semantics. | Keep the public builder as a thin adapter over the shared lane; no private shader pool. |
| CashSystem async save/load. | The prior ten `std::async` launch statements in `MainCacher`/`DeviceRegistry` are gone. RenderGraph's three top-level calls remain API-compatible (`libraries/RenderGraph/src/Core/RenderGraph.cpp:335,388,1302`). | Per-device/per-cacher filesystem and Vulkan cache work is submitted to MainCacher's shared blocking lane; synchronous manifests and unowned registries remain synchronous-by-contract. | Existing boolean aggregation and cacher locks remain; MainCacher drains its lane before teardown. | Keep cache I/O on the dedicated blocking lane, not the finite compute arena, and preserve cache teardown barriers. |
| Scene-specific parallel body bake. | One cache-miss block in `BuildRenderGraph.cpp` launches **eight** `std::async` tasks and gathers futures in canonical body order (`application/main/source/graph/BuildRenderGraph.cpp:5517-5595`). | Eight fresh asynchronous tasks compute independently, while a process-wide Gaia allocator mutex serializes the allocation-sensitive part. | Future gathering and downstream assignment are stable. The comments document a prior allocator corruption and identify unsynchronized logger globals, so correctness depends on the manually scoped global lock and avoiding worker logging. | Migrate after the first slice to shared CPU work with an explicit Gaia-allocator serial resource key. This is evidence that access declarations are needed: raw “independent bodies” reasoning missed hidden singleton state. |
| `SVOBuilder` recursive octant TBB. | `SVOBuilder::subdivideNode` has one `tbb::parallel_for` over eight child slots (`libraries/SVO/src/SVOBuilder.cpp:210-258`). One non-test wrapper, `LaineKarrasBuilder`, constructs and calls it (`libraries/SVO/src/LaineKarrasBuilder.cpp:21,31`). | Recursive build work runs on the ambient TBB scheduler; children use fixed octant indices. | Fixed child slots support stable output topology, but no public worker-count gate covers recursion, progress callbacks, or shared allocators. | Migrate to shared nested-task submission only after preventing TBB arena nesting/oversubscription. Add 1/2/N serialized-octree parity before changing it. |
| `BulkMaterializationQueue`. | `process()` creates a oneTBB arena and parallel-for over a batch (`libraries/SVO/src/BulkMaterialization.cpp:163-255`). The census found **zero production consumers** and test-only queue/process uses. | One caller owns acceptance order; items materialize in parallel into indexed result slots; results are pushed in batch order. | The implementation is structured for stable results and tests compare serial with parallel execution, including cancellation/backpressure paths. | Use as an early shared-executor conformance consumer after `SlotTask`, or remove it if still test-only. It must not retain a private TBB arena beside the shared executor. |
| Main application/frame loop. | There is one serial tick chain and one `RenderFrame` call (`application/main/source/VulkanApplicationBase.cpp:139-179`; `VulkanGraphApplication.cpp:344-389`). `RenderGraph::BuildExecutionOrder` is a placeholder for future batching and parallel groups (`libraries/RenderGraph/src/Core/RenderGraph.cpp:1350-1356`). | Update, graph traversal, node CPU work, command recording, submits, and present are driven by the main thread. The dormant executor is the only alternate traversal. | Sequential node order, post-node callbacks, deferred recompiles, and present order are stable and currently form implicit correctness dependencies beyond declared graph edges. | Preserve a serial-eligibility default. The shared adapter must opt nodes in only when all effects and ownership are declared. Frame pipelining comes after node/wave parity, not before it. |
| Frame-sync schedule, queue submission, fences, and present. | `FrameSyncScheduler` creates one submit group per execution-order node and derives timeline edges/barriers (`libraries/RenderGraph/src/Core/FrameSyncScheduler.cpp:57-145`; `include/Core/FrameSyncSchedule.h:32-56`). The device requests **one** queue and fetches one graphics/present queue (`libraries/VulkanResources/src/VulkanDevice.cpp:228-233,364-371`). The declaring `SubmitMutex` has 27 syntactic runtime call sites across upload/cache/node helpers and protects queue submit/present (`:388-395`; `libraries/RenderGraph/src/Nodes/PresentNode.cpp:67-145`). | GPU work can overlap across four frames in flight, but all queue operations target one externally synchronized queue. Compute-stage, compute-dispatch, blit, and multi-dispatch nodes each issue their own `vkQueueSubmit2`; present follows the graph. Main-graph present does not call `vkDeviceWaitIdle` (`application/main/source/graph/BuildRenderGraph.cpp:1515`). | Timeline semaphores, entry barriers, per-flight fences, and ordered queue operations define GPU visibility. CPU production order is still the sequential graph order. | Keep as Tier B. Parallel CPU recording must feed a single ordered submit lane. Actual async-compute/transfer overlap requires multiple queues plus ownership-transfer policy; a CPU executor alone cannot manufacture it. |
| Test-only thread harnesses. | Direct test threads occur in GaiaArchetypes, GaiaVoxelWorld, ResourceManagement, CashSystem, SVO, and ShaderManagement tests; SVO also has one `std::jthread` cancellation test. They construct no production executor. | They stress shared components or drive queues concurrently. | Each test defines its own assertions; they are evidence about component safety, not an engine scheduling contract. | Retain relevant tests and route new executor tests through explicit 1/2/N profiles. Do not count test harness threads as production consumers. |

## 5. Duplication verdict

The owner's phrase “parallel to the shared TDD solution” resolves to two layers:

1. **Direct duplicate:** RenderGraph's former `VirtualTask` + `TaskDependencyGraph` + `TBBVirtualTaskExecutor` duplicated the opaque task, DAG/waves, and TBB execution now extracted into `KernelDispatch`. The local stack was retired in the 2026-09-01 cleanup; no RenderGraph-to-`KernelDispatch` frame adapter was introduced because the old path had no production activators or behavior to preserve.
2. **Planned duplicate:** `InstanceGroup` would create another policy-driven parallel scheduler by cloning nodes according to device/workload/frame budget. Its useful intent belongs in shared dispatcher partition/admission metadata; its node-cloning implementation should never be completed.

The adjacent pieces are not all duplicates:

- RenderGraph resource topology, `AccessKind`, frame-sync scheduling, node lifecycle, and manual instances are Tier-B semantics and must remain.
- `LoopManager` is cadence state and should feed the dispatcher rather than disappear.
- `TaskProfileRegistry` has reusable cost/priority concepts, but its live pressure valves do not control real workloads; those concepts should be folded into the shared profile contract.
- `TaskQueue` and `MultiDispatchNode` express GPU pass ordering/budgeting, not CPU worker scheduling. They may consume shared admission later but are not replacement executors.
- EventBus, shader, cache, bake, SVO, and slot-task mechanisms are concurrency islands. Their owned threads/arenas should be removed or made consumers of the shared foundation.

The target is therefore **one Tier-A executor, multiple domain compilers and task classes**. “One foundation” does not mean forcing blocking file I/O, CPU compute, Vulkan recording, and queue submission through one indistinguishable worker pool.

## 6. The two-way bridge and the homomorphism

A render-node execute workload and a kernel-dispatch stage have the same executable shape:

| RenderGraph concept | Shared dispatch concept | Required adapter behavior |
|---|---|---|
| A typed input/output slot. | A stable slot/access identity. | Resolve the typed resource, preserve access kind/scope, and emit stable access keys. |
| A node or array-task execute body. | A stage callable over an indexed domain. | Bind one node execute body or one node-owned item function without transferring graph lifecycle ownership. |
| Graph connections and resource hazards. | Explicit DAG edges plus recorded RAW/WAW/WAR access. | Compile existing topology conservatively into shared task edges. |
| A manual semantic instance. | A stable task/stage owner identity. | Keep semantic identity; do not treat repeated node types as shards. |
| An `InstanceGroup` workload. | A domain plus deterministic partition policy. | Produce logical index ranges/tasks, not cloned `_0.._N` node objects. |
| Node setup/compile/cleanup. | Submission preparation and completion hooks. | Keep lifecycle on the graph owner and make only eligible execute work concurrent. |

The bridge has two runtime directions:

### 6.1 Kernel dispatch patterns as render-graph nodes

A generated or native kernel package supplies a stage descriptor: stable identity, typed bindings, indexed domain, access declarations, backend/profile hints, and callable/artifact references. A `KernelStageNode` provider binds RenderGraph slots to that descriptor. A chain may materialize as one composite node initially and as a graph subgraph later. RenderGraph remains responsible for resource lifetime, residency realization, Vulkan barriers, and frame placement.

This makes the kernel framework another node provider without making it the render-graph owner.

### 6.2 Render-graph work through the shared TDD executor

The RenderGraph compiler maps each eligible node execute body or item shard to `KernelDispatch::VirtualTask`. Existing topology and resource-access analysis supplies conservative edges. The shared executor schedules CPU work waves; completions are committed in stable task-ID order at a graph barrier. Serial-only nodes, queue submit, present, and undeclared side effects stay on a serial lane.

The dormant `InstanceGroup` intent becomes a partition descriptor on this path. For a domain of N items, the admission/profile policy chooses stable ranges. Worker count can change execution placement but cannot change range-to-item mapping or observable commit order.

### 6.3 Alternate reading that needs owner confirmation

The owner's “framework file to VIXEN attach back and forth” may mean more than runtime adapters: one generated declarative artifact should be capable of lowering both to a kernel `DispatcherSpec` and to a VIXEN node provider. That artifact bridge is compatible with the runtime bridge and is recommended. The safe order is:

1. Define one generated stage/binding descriptor and hash.
2. Lower it to the native `KernelDispatch::Stage` contract.
3. Generate or register the RenderGraph node-provider adapter from the same descriptor.
4. Keep hand-authored RenderGraph nodes compatible through a runtime adapter even when no generated artifact exists.

If the owner intended only artifact attachment, not execution through one native executor, that reading would not remove RenderGraph's duplicate Tier A and would not satisfy the multicore-framework goal. The recommendation is therefore **both artifact and runtime bridges**.

## 7. Architecture options

Scores use 1 (poor) to 5 (strong). “Cost” is scored higher when migration is cheaper.

| Option | One-foundation integrity | Incremental safety | CPU multicore benefit | Vulkan fit | Determinism confidence | Cost | Total / 30 |
|---|---:|---:|---:|---:|---:|---:|---:|
| A. Adapter-first shared waves. | 5 | 5 | 4 | 5 | 5 | 4 | **28** |
| B. In-node consumers only. | 3 | 5 | 2 | 5 | 4 | 5 | **24** |
| C. KernelDispatch replaces graph compilation wholesale. | 5 | 1 | 5 | 2 | 3 | 1 | **17** |
| D. Frame-pipeline-first coordinator. | 3 | 2 | 4 | 3 | 2 | 2 | **16** |

### Option A — adapter-first shared waves — recommended

RenderGraph keeps compiling topology and resource semantics. An adapter emits shared tasks and edges for eligible CPU work. The same executor first replaces `SlotTask` and direct CPU islands, then node-level waves, then optional parallel command recording. `KernelStageNode` supplies the reverse bridge.

- **Node waves:** Existing graph topology and access tracking produce task edges; a serial eligibility trait defaults every node to serial until audited.
- **Command recording:** Start with CPU derive/upload preparation only. Later allow per-node secondary or independent command-buffer recording with per-worker, per-frame pools and descriptor arenas. Submit order stays graph-owned.
- **Frame pipeline:** Add immutable frame snapshots only after node-wave parity, allowing F+1 CPU preparation while F is submitted/rendering.
- **Other consumers:** Event completion, residency/materialization, SVO derive, shader builds, cache I/O, and shell derive/upload use the same submission API with compute, blocking, serial-resource, and Vulkan-record lanes.
- **Determinism:** Simulation-visible and resource-selection outputs remain worker-count invariant. Independent command recording may vary in CPU completion order, but ordered assembly/submit and rendered output remain invariant.
- **Federation:** Managed code observes the same generated ABI/artifact hashes and frame outputs, never native worker topology. Native packaging eventually exports `KernelDispatch`.
- **Main tradeoff:** The adapter temporarily carries two access vocabularies. Contain it with a single explicit mapping table and contract tests; generalize only semantics proven common.

### Option B — shared executor only inside nodes

Keep sequential graph traversal permanently. Nodes may submit their internal indexed workloads to `KernelDispatch` and wait before returning. `InstanceGroup` becomes an in-node partition policy.

- **Node waves:** None; only item parallelism inside a node.
- **Command recording:** A node may later record parts in parallel, but graph siblings cannot overlap.
- **Frame pipeline:** Limited to explicit async nodes and current frames-in-flight behavior.
- **Other consumers:** Most islands can still converge on the shared executor.
- **Determinism and Vulkan:** This is easy to reason about because every node remains a barrier.
- **Federation:** No managed change.
- **Main tradeoff:** It removes raw threads but leaves RenderGraph's dormant task/DAG duplicate and cannot make the graph itself a multicore framework. It is a useful migration phase, not a sufficient destination.

### Option C — KernelDispatch owns graph compilation wholesale

Translate every RenderGraph node/resource/lifecycle phase into shared stages and delete the RenderGraph dependency and task compilers immediately.

- **Node waves:** Maximum theoretical parallelism from one compiler.
- **Command recording and frame pipeline:** Can be modeled as task classes and frame epochs from the start.
- **Other consumers:** All work is forced into the new model.
- **Determinism:** A single graph is attractive, but the change would simultaneously redefine node state, callbacks, dirty recompilation, device loss, frame sync, and resource lifetime.
- **Vulkan:** `KernelDispatch` currently lacks image layout, queue ownership, swapchain, frame-flight, and node lifecycle semantics that RenderGraph already owns.
- **Federation:** Artifact alignment is direct, but native runtime churn would be large.
- **Main tradeoff:** This confuses domain-blind Tier A with renderer-specific Tier B and puts fundamental building blocks at once-through rewrite risk. Reject.

### Option D — frame-pipeline-first coordinator

Treat update/derive/record/submit/present for multiple frames as top-level dispatcher stages before unifying node execution.

- **Node waves:** The graph may remain sequential within a frame while frames overlap.
- **Command recording:** Parallelism comes from different frames first, with per-flight pools/snapshots.
- **Frame pipeline:** Highest immediate emphasis; CPU F+1 overlaps GPU F and possibly recording F.
- **Other consumers:** Streaming and events can be assigned to frame epochs.
- **Determinism:** Stale-state, publication, rollback, and managed-authority boundaries become immediate problems.
- **Vulkan:** Four flight slots help, but many nodes and caches still contain implicit cross-frame mutable state.
- **Federation:** Managed snapshots and presentation lag policy would become externally observable sooner.
- **Main tradeoff:** It attacks latency overlap before proving safe node effects and one task foundation. Defer until after Option A's node-wave gate.

## 8. Recommended target architecture

### 8.1 Ownership boundary

RenderGraph remains authoritative for:

- node identity, typed slots, connections, setup/compile/execute/cleanup lifecycle;
- resource classes, access semantics, residency realization, image layouts, and frame-local allocation;
- loop/frame cadence, graph recompilation, device-loss recovery, post-node callbacks, and present;
- frame-sync schedules, queue ownership, barriers, semaphores, fences, and submit order.

`KernelDispatch` becomes authoritative for:

- stable task/stage identity and indexed domains;
- dependency DAG representation and wave construction;
- worker pools/arenas, worker-count profiles, priorities, budgets, and admission;
- compute versus blocking/I/O versus serial-resource versus Vulkan-record task lanes;
- cancellation/device epochs, exception capture, completion handles, trace events, and deterministic commit ordering.

The boundary is compiled, not inferred at runtime: RenderGraph emits the task plan once per graph generation, then binds per-frame resources/snapshots.

### 8.2 Task classes and Vulkan rules

1. **Pure CPU/derive tasks** may execute freely when all reads/writes are declared and outputs are task-owned.
2. **Blocking/I/O tasks** use a separately budgeted lane so cache or file waits cannot starve frame compute workers.
3. **Serial-resource tasks** carry explicit keys such as the Gaia global chunk allocator or a non-thread-safe compiler/cache. Tasks with the same key are ordered without a hand-written global mutex at each call site.
4. **Vulkan record tasks** require one command pool per worker per frame flight, one descriptor arena per worker/frame or an externally synchronized allocator, immutable pipeline/layout handles, and task-owned command buffers. Vulkan command pools and descriptor pools are externally synchronized objects.
5. **Vulkan submit/present tasks** remain on one queue-owner lane. Current VIXEN has one queue, so parallel recording must assemble results in graph order and submit under the existing queue mutex.
6. **Multi-queue tasks** are a later capability. They require separate compute/transfer queues, queue-family ownership transfers, timeline policy, and a profitability gate; they cannot be enabled merely because the CPU executor has workers.

Secondary command buffers are useful for compatible render-pass/dynamic-rendering regions, but they are not the first target. Compute and transfer nodes may instead record task-owned primary command buffers if their submit grouping remains valid. The choice belongs to a later Vulkan recording design measured against driver overhead.

### 8.3 Determinism gate

The following remain strictly worker-count invariant:

- managed- or simulation-visible outputs and all data later projected to Undertow;
- resource/residency selection, logical instance/shard membership, and stable task IDs;
- reductions, cache keys, artifact hashes, graph edge/order decisions, and completion publication;
- exception selection when multiple tasks fail, using the lowest stable task ID as the primary error;
- frame snapshot commit and node state transitions.

Parallel work may relax byte-identical execution traces only for:

- completion timing among independent background shader/cache requests, provided publication is keyed and committed at a deterministic boundary; and
- command-buffer recording order among independent nodes, provided command assembly/submission follows the same graph order and rendered/readback outputs are equivalent.

Reductions must use fixed partitions and a fixed merge tree. Dynamic work stealing may change which worker runs a task, never which items belong to a logical partition or the order in which results become authoritative.

### 8.4 Frame pipelining

After node-wave parity, introduce immutable `FrameSnapshot[F]` inputs and versioned task outputs. The allowed overlap is:

1. GPU executes/submits frame F.
2. CPU prepares pure render data for F+1 from an immutable managed/render snapshot.
3. The graph commits F+1 outputs at a stable barrier only if their graph/device epoch still matches.

Simulation update remains authoritative and ordered. Device loss, graph recompilation, or scene epoch changes cancel unpublished work. No worker writes live node state directly across frame epochs.

### 8.5 Federation visibility

The managed side should not observe worker count, TBB, queue choice, or native task completion order. It observes:

- the same dispatcher schema/artifact hash and declared backend capabilities;
- deterministic projected frame/simulation data;
- explicit capability/failure status when a requested backend is unavailable.

The generated descriptor can be shared by the managed `DispatcherSpec`/Burst lowering and native VIXEN node provider. The actual VIXEN worker plan remains native. Once the ABI and consumer exist, add `KernelDispatch` to the install/export surface with a package-level ABI/hash gate; do not export the current zero-consumer library prematurely as a stable SDK promise.

## 9. Decommission order

| Order | Mechanism | Action and exit condition |
|---:|---|---|
| 1 | `SlotTaskManager` raw `std::async`. | Preserve its API while routing work through shared stages. Delete the async launch after budget/result/1-2-N parity passes. |
| 2 | RenderGraph `TBBVirtualTaskExecutor` Tier A. | **Retired in the 2026-09-01 cleanup** after the zero-activator census. `KernelDispatch::TaskExecutor` is the sole Tier-A executor; any future RenderGraph lowering requires an explicit behavior/parity gate. |
| 3 | Unused `WorkerThreadBridge` and `AsyncShaderBundleBuilder`. | Re-run the declaring-construct census; delete if still zero-consumer. If a new consumer appeared, migrate it before deletion. |
| 4 | `BulkMaterializationQueue` private TBB arena. | Make it a shared-executor conformance consumer or remove the test-only production class. |
| 5 | Scene-bake and CashSystem `std::async`. | Submit compute and blocking work to the correct lanes; model Gaia as a serial-resource key; preserve cache teardown barriers. |
| 6 | `SVOBuilder` direct TBB recursion. | Migrate after serialized-octree 1/2/N parity and nested-executor behavior are proven. |
| 7 | `InstanceGroup` node-cloning API. | **Retired in the 2026-09-01 cleanup.** Its useful scaling/partition intent remains a shared stage-domain concern; manual semantic instances remain unchanged. |
| 8 | Inert RenderGraph pressure/profile layer. | Move live cost/priority/backend/partition knobs to shared profiles; retain renderer-specific measurement only where it drives an implemented action. |

`LoopManager`, manual graph instances, RenderGraph access/frame-sync compilers, `TaskQueue`'s GPU ordering role, and the queue-submit mutex do not belong on the deletion list.

## 10. Migration sequence

### M0 — Freeze and witness the contract

- Add no parallel production behavior.
- Define stable task IDs, task classes, cancellation/device epochs, deterministic error choice, and completion trace format.
- Record current sequential node order, post-node callback order, submissions, and observable outputs as the comparison oracle.
- Add worker-count 1/2/N tests to `KernelDispatch`, including independent stages in one wave; close the current `RunChain` inter-stage parallelism gap without changing results.

### M1 — First implementation slice: SlotTask shared-dispatch cutover

Name: **RenderGraph array-task → shared TDD execution**.

`NodeInstance` keeps generating `SlotTaskContext`s. The compatibility adapter lowers each budget-admitted batch to `KernelDispatch::VirtualTask`s, registers them with `TaskDependencyGraph`, and runs the resulting wave through `TaskExecutor`. No Vulkan recording or graph-sibling concurrency is enabled.

The gate is:

- identical task output/status and success counts for worker counts 1, 2, and N;
- identical budget admission and stable task-index result order;
- deterministic exception selection and no task running after cancellation/graph epoch invalidation;
- current slot-task tests pass, plus a race-sanitized disjoint-output test where supported;
- no `std::async` remains in `SlotTask.cpp`; and
- the direct declaring-construct census demonstrates the first real consumer outside `KernelDispatch`.

This is the smallest slice that both removes an island and proves a render-node workload can execute through the shared foundation.

### M2 — CPU-only graph waves

- Compile existing topology/access data into shared tasks for an allowlist of audited pure CPU nodes.
- Keep every Vulkan-recording, submit, present, callback-sensitive, graph-mutating, and unknown node serial.
- Run sequential and shared modes against the same frame snapshot; compare output hashes, node states, callbacks, errors, and submit trace.
- Remove RenderGraph's duplicate Tier-A executor after the adapter is the only parallel path.

### M3 — Concurrency-island migration

- Move bulk materialization, scene bake, cache I/O, shader compilation if retained, EventBus completion, and SVO derive/upload to shared lanes.
- Add explicit serial-resource keys for hidden singleton/global constraints.
- Enforce one process-wide worker/admission budget to prevent nested TBB and `std::async` oversubscription.

### M4 — Kernel-as-node provider

- Land the shared generated descriptor and `KernelStageNode` binding adapter.
- Prove one native/generated CPU kernel both as a headless dispatch and as a RenderGraph node with identical output and artifact hash.
- Keep managed observation limited to the existing contract and results.

### M5 — Parallel Vulkan recording

- Add per-worker/per-frame command and descriptor ownership.
- Enable only audited record-only nodes; queue submission and present remain serial and ordered.
- Measure CPU frame time and driver overhead; retain the path only with a demonstrated win and clean validation-layer output.

### M6 — Frame pipeline, then multi-queue

- Add immutable versioned frame snapshots and F/F+1 overlap.
- Only after that, evaluate separate compute/transfer queues, ownership transfers, and async-compute profitability.
- Do not couple multi-queue enablement to CPU worker count or resurrect `InstanceGroup` device-queue cloning.

## 11. Risks to fundamental building blocks

| Risk | Why it is fundamental | Containment |
|---|---|---|
| Hidden node side effects. | Sequential traversal currently orders callbacks, logger access, dirty recompiles, node state, and globals even when resource slots do not show a dependency. | Default every node to serial; require an explicit execution-safety declaration; add debug access witnesses and stable completion commit. |
| Incomplete access vocabulary. | Render resources include images, ranges, mips, descriptors, residency, indirect/append state, and queue ownership beyond simple slot IDs. | Keep RenderGraph Tier B conservative; map access explicitly; extend shared keys only from proven domain requirements in the kernel dispatch spec. |
| Vulkan externally synchronized objects. | Sharing command/descriptor pools or queues across workers is invalid without ownership/locks. | Use per-worker/per-frame pools and arenas; keep a single queue-owner submit lane; run validation layers and ownership stress gates. |
| Device loss or graph recompilation during work. | Tasks may hold pointers to destroyed nodes/resources or publish stale outputs. | Attach graph/device epoch tokens, cooperative cancellation, teardown joins, and commit-time epoch validation. |
| Worker oversubscription and blocking starvation. | Existing TBB arenas, raw futures, recursive SVO tasks, shader threads, and file I/O can multiply threads and stall the frame. | One admission authority with separate bounded compute and blocking lanes; prohibit nested private arenas after migration. |
| Worker-count-dependent results. | Managed authority, caches, hashes, and visual stability rely on repeatable data. | Fixed logical partitions, stable task IDs, ordered reductions/commits, and mandatory 1/2/N gates for authoritative outputs. |
| Hidden third-party singletons. | The Gaia allocator corruption proves apparently independent worlds can share process-global mutable state. | Declare serial-resource keys, audit third-party allocation/compiler/logger state, and use sanitizer/live stress gates before eligibility. |
| Frame-pipeline stale state. | Multiple frames can observe mutable cameras, residency, UI, or managed projections at different epochs. | Immutable frame snapshots, versioned outputs, explicit presentation latency, and cancellation on authoritative-state changes. |
| Error-order drift. | Parallel failures can make user-visible diagnostics and recovery nondeterministic. | Collect all failures, choose the primary by stable task ID, and commit error/device-loss transitions on the graph thread. |
| ABI/export drift. | The federation twins managed and native descriptors, while `KernelDispatch` is currently not installed. | One generated descriptor hash, native/managed conformance tests, and export only after a real external consumer and compatibility policy exist. |
| Parallelism without benefit. | Recipe bucket work showed that more dispatch organization can add CPU cost without a statistically clear win. | Require measured CPU-frame improvement for each widened eligibility class; retain serial fallback and remove unprofitable complexity. |

## 12. Recommendation

Adopt **Option A, adapter-first shared waves**, with the following non-negotiable boundaries:

1. `KernelDispatch` owns the only Tier-A task/DAG/executor implementation.
2. RenderGraph retains Tier-B topology, resource/Vulkan semantics, lifecycle, and ordered submit/present.
3. The `InstanceGroup` node-cloning API is retired. Its partition/scaling intent becomes shared stage-domain policy; manual semantic instances remain untouched.
4. The bridge is two-way: kernel descriptors can materialize as nodes, and eligible node/shard work executes through the shared executor.
5. The first slice is the `SlotTask` cutover, not graph-wide parallel execution or Vulkan recording.
6. Every authoritative workload is gated at worker counts 1, 2, and N; completion timing may vary, authoritative commit order may not.
7. Blocking I/O, serial third-party state, Vulkan recording, and queue submission are separate task classes on one admission foundation, not one undifferentiated pool.

This direction turns the existing contract into an actual second consumer, retires the parallel engine island instead of growing it, and preserves the render graph's fundamental responsibilities.

## 13. Owner validation — resolved

The owner approved all eight decisions on 2026-09-01. Ruling 0ea strengthened decisions 1 and 7: the graph ultimately lowers to the Tier-A fabric, the sequential driver is retired in later milestones, loops become real multi-rate domains, UI/AppFlow remain independent, and app responsiveness under render load is the arc acceptance criterion. M1 was the first implementation slice; decision #6 separately authorizes the shader/cache blocking-lane migration recorded below.

1. **Should `KernelDispatch` become the sole Tier-A task/DAG/executor, while RenderGraph remains the Tier-B graph/Vulkan compiler?** Recommended answer: **Yes.**
2. **Should the unimplemented `InstanceGroup` node-cloning API be retired in favor of stable stage-domain partition metadata, while manual semantic node instances remain unchanged?** Recommended answer: **Yes.**
3. **Does the requested two-way bridge include both runtime execution adapters and one generated descriptor that can lower to managed `DispatcherSpec` and a VIXEN node provider?** Recommended answer: **Yes, both; descriptor first, adapters second.**
4. **Is the first implementation slice approved as `SlotTaskManager` → shared `KernelDispatch` execution with no graph-sibling or Vulkan-recording parallelism?** Recommended answer: **Yes.**
5. **Is the determinism boundary approved as strict 1/2/N invariance for authoritative data and ordered commits, with only independent completion/recording timing allowed to vary?** Recommended answer: **Yes.**
6. **Should shader/cache I/O use separately budgeted blocking lanes under the same admission foundation rather than consuming finite frame-compute workers?** Recommended answer: **Yes.**
7. **Should parallel command-buffer recording and frame pipelining remain later, separately measured milestones after CPU-only node-wave parity?** Recommended answer: **Yes.**
8. **Should `KernelDispatch` remain absent from the installed SDK until the bridge has a real consumer and an ABI/hash compatibility gate?** Recommended answer: **Yes.**

**Checkpoint result:** Satisfied. The owner separately ruled the execution-control seam as standard `std::stop_token`, with a RenderGraph-owned epoch/source and deterministic TaskExecutor error ordering.

## 14. M1 implementation record — 2026-09-01

Delivered:

- `SlotTaskManager::ExecuteParallel` no longer launches `std::async`; it preserves stable budget batches while lowering each admitted batch to the shared `TaskDependencyGraph`/`TaskExecutor` fabric.
- `TaskExecutor::Run` and `SlotTaskManager::ExecuteParallel` accept `std::stop_token`. RenderGraph owns the stop source and monotonically increasing execution epoch; frame begin/abort, compile/recompile, device-loss recovery, and cleanup rotate or invalidate it at Tier-B lifecycle boundaries.
- Task errors are staged per wave and committed in ascending task-index order (owner ID breaks ties). A failed or cancelled wave issues no later wave, and a failed slot-task batch skips all later budget batches.
- `RenderGraphCore` now links `KernelDispatch`, establishing the first production consumer outside the dispatcher library. Authored/modder vocabulary and the installed SDK export remain unchanged.

Gate evidence, run through the shared box queue after the final lifecycle review:

- worker counts exercised inside every run: **1 / 2 / 8**;
- run 1: `[==========] 24 tests from 2 test suites ran. (93 ms total)` and `[  PASSED  ] 24 tests.`;
- run 2: `[==========] 24 tests from 2 test suites ran. (103 ms total)` and `[  PASSED  ] 24 tests.`;
- run 3: `[==========] 24 tests from 2 test suites ran. (90 ms total)` and `[  PASSED  ] 24 tests.`;
- the invariant snapshot covers byte/result output, task statuses, success/failure/skipped counts, stable task-index placement, estimated-memory totals, and the budget ceiling;
- the cancellation gate proves an invalidated graph token starts zero slot tasks at 1/2/8 workers; the compile gate proves the old token stops and the new epoch is live;
- the executor gate proves primary-error order `1, 3, 5`, prevents the later wave, and proves a token fired in wave 0 prevents wave 1 at 1/2/8 workers;
- the test writes disjoint byte-addressable result cells. The WSL/Ninja preset has no ThreadSanitizer configuration, so no project-supported race-sanitized variant was available;
- declaring-construct census: zero `std::async` occurrences remain in `SlotTask.cpp`; RenderGraph is now a direct production `KernelDispatch` consumer.

Not delivered in M1 or the decision #6 slice: graph-sibling scheduling, parallel Vulkan command recording, frame pipelining, the two-way kernel/node bridge, concurrency-island retirement beyond `SlotTaskManager`, managed/modder vocabulary changes, SDK installation/export changes, or any M2–M6 work. The separate 2026-09-01 cleanup subsequently retired the dead `InstanceGroup` API and the duplicate RenderGraph TBB executor.

## 15. Decision #6 implementation record — 2026-09-01

Delivered:

- `DispatcherProfile` now carries independent `frameCompute` and `blockingIO` worker budgets. `TaskExecutor` keeps frame waves in their TBB arena and lazily owns a bounded FIFO blocking lane under the same admission foundation.
- `CompletionHandle` is the explicit ordering contract: handles receive monotonically increasing `SubmissionOrder()` values and callers retain/read them in submission order. Queued stop-token cancellation produces `Cancelled` without invoking the callable; an already-running callable is allowed to finish.
- `AsyncShaderBundleBuilder` now submits compilation/I/O work to the blocking lane while preserving UUID tracking, EventBus readiness messages, cancellation flags, and wait/cleanup APIs.
- MainCacher and MainCacher-owned DeviceRegistry replace ten ad-hoc `std::async` launches with lane batches. Manifest reads/writes and unowned registries remain synchronous-by-contract and are not presented as asynchronous work.
- The RenderGraph starvation test saturates a one-worker blocking lane while a four-task frame wave completes; the queued-I/O test proves stop-token cancellation and submission-order handles.

Timing evidence for the shader-build path (same one-stage compute shader, captured from the queue-owned logs):

- Before migration, original private-thread-pool probe: `Test #311 ... Passed 0.24 sec`, ctest total `0.37 sec`, process wall `0.59 sec` (`/tmp/undertow-box-logs/1788276854-test-shaderlanes:timing-before.log`).
- After migration, blocking-lane readiness/event probe: `Test #312 ... Passed 0.21 sec`, ctest total `0.33 sec`, process wall `0.53 sec` (`/tmp/undertow-box-logs/1788277002-test-shaderlanes:timing-after.log`).
- The comparison shows no new stall on this path; queue admission time is outside the timed subprocess and is reported separately by the box log.
