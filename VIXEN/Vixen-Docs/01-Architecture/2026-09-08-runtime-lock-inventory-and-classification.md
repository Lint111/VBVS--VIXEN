# Runtime lock inventory and classification — VIXEN

Date: 2026-09-08. Lane: `lockinventory`. Audited base: `178b838b3b3594b0e3d7102bb50fc8acccb5de29`, branch `lock-inventory`. Audit only; no engine changes or runtime measurements.

## Result and scope

The checked-in engine has **47 distinct explicit host mutex declaration families**, represented in **98 non-test source files** (97 library files and one application file), plus **one GPU slot claim/reclaim protocol** using CAS and a transient exclusion marker. These are inventoried separately: 48 classified exclusion/publication families in total. This is a source inventory, not 47 live mutex instances and not 98 independent serialization points. Template instances, staging buckets and physical queues multiply some families. Two host families are compile-time diagnostics; several others have no located shipping caller. The census includes those facilities so they do not disappear from the architecture review.

**46 host mutex families admit an ownership, delta/log, scheduling or cache-publication candidate; one family preserves required external-resource serialization: the per-physical-Vulkan-queue mutex.** GPU1 additionally admits a partition-by-key candidate. This is a classification of causes, not proof that deleting 46 mutexes is safe, nor a measured speedup claim. A dedicated queue owner can remove the remaining mutex while retaining serialized API access. Optional subsystem activation, callback semantics, resource aliases, capacity admission and GPU completion remain proof obligations.

The host mutex census covers engine-authored C/C++ runtime code under `VIXEN/libraries` and `VIXEN/application`, including inline library APIs and optional runtime diagnostics. Tests, archives, standalone build-time source-generator tools, build locks/box queue and developer tools are excluded; the optional runtime registry-header publisher SH8 is retained and labeled. A broader C/C++ sweep outside those two runtime trees found no additional first-party host lock implementation. A supplemental shader search found GPU1 below; ordinary shader numeric atomics are not counted as exclusion protocols. GPU fences/semaphores/barriers, implicit function-static initialization guards, standard-library/task-scheduler/driver internals and fetched dependency internals are not counted as explicit engine mutex families. In particular, **VMA still uses internal synchronization**; the wrapper count does not inventory its implementation. These boundaries prevent a false claim that the shipping process is otherwise lock-free.

All source paths below are relative to `VIXEN/` at the audited base. A path followed by several line numbers names declaration/acquisition evidence at that revision. The acquisition index at the end accounts for every file in the census, including borrowed locks and inherited members. Contention descriptions are inferred from scopes and callers; “hot” means a frame/item path, not measured waiting. “No located caller” means a source-search reachability limitation, not proof of linker elimination or absence in an external embedding host.

### Method and verification scope

1. Recorded a clean worktree, branch and base SHA before edits. `codegraph explore -p . "runtime mutex shared_mutex lock_guard unique_lock RenderGraph graph construction BuildRenderGraph"` returned exit 1: no index in this worktree. Used ripgrep and bounded batch reads; did not index or inspect another checkout.
2. Read `CLAUDE.md` and local architecture documentation, then inspected declarations, guarded bodies, aliases, callers and guards. Split specialist source review between resource/Vulkan, shader/cache and world/event/kernel facilities; RenderGraph/application and census reconciliation were reviewed together.
3. Scanned 785 non-test C/C++ files under the two runtime trees for qualified mutex types and RAII acquisitions after removing comments/string literals. Supplemented with broad searches for unqualified/custom locks, explicit lock/unlock calls, spin/atomic-flag, once, pthread/Windows and file-lock primitives. No additional explicit host mutual-exclusion primitive family was found. Macro/third-party hidden synchronization remains outside the numerical claim.
4. Verification for this deliverable is source-reference, inventory coverage, arithmetic and document/diff validation. **No product generation/build/test gate is required for this audit.** No build was attempted, no stale binary was used as evidence, and no product baseline failure was asserted. Recovery ladder A/B/C is not applicable: the navigation fallback succeeded and there was no failed required product check. Running a code generator would not validate this source-only inventory.

## Counting and classification

One row means one distinct declared member/static/local lock family. A borrowed `std::mutex*`, a `GetMutex()` reference, each lock guard and each derived cacher acquisition are not new families. Each family receives one primary class, with alternatives and conditions in its row:

- **PARTITION-able (P):** move mutable state into a worker, resource or coordinator owner; preserve aliases and cross-owner handoff.
- **DELTA-able (D):** compute disjoint deltas or producer-owned append records, then merge/apply deterministically. An append log does **not** make callback execution, copies to overlapping destinations or destruction commutative.
- **ORDER-able (O):** explicit mutation/read/lifetime phases or a scheduled coordinator prevent overlap. A CPU barrier alone never proves GPU completion.
- **CACHE/MEMO (C):** precompute or freeze registries, partition caches, publish immutable results; invalidation is an epoch change.
- **GENUINELY-NEEDED (G):** required serialization of an externally shared object. This category preserves the serialization invariant, not a requirement to use `std::mutex`.

| Subsystem | Files with mutex syntax | P | D | O | C | G | Lock families |
|---|---:|---:|---:|---:|---:|---:|---:|
| RenderGraph | 34 | 0 | 3 | 3 | 1 | 0 | 7 |
| Application / body bake | 1 | 0 | 0 | 1 | 0 | 0 | 1 |
| ResourceManagement | 17 | 6 | 4 | 2 | 1 | 0 | 13 |
| VulkanResources | 5 | 0 | 0 | 0 | 1 | 1 | 2 |
| CashSystem | 19 | 1 | 1 | 0 | 4 | 0 | 6 |
| ShaderManagement | 10 | 2 | 1 | 2 | 3 | 0 | 8 |
| SVO | 6 | 1 | 0 | 2 | 1 | 0 | 4 |
| EventBus | 3 | 1 | 2 | 1 | 0 | 0 | 4 |
| GaiaArchetypes | 2 | 0 | 0 | 1 | 0 | 0 | 1 |
| KernelDispatch | 1 | 0 | 1 | 0 | 0 | 0 | 1 |
| **Total** | **98** | **11** | **12** | **12** | **11** | **1** | **47** |

The table above is the **host mutex subtotal**. The supplemental GPU1 claim/reclaim protocol adds one PARTITION-able family, giving **48 classified host/GPU exclusion/publication families: P=12, D=12, O=12, C=11, G=1**. GPU key words are not additional `std::mutex` declarations or files in the 98-file host census. This distinction keeps the starting mutex survey comparable without omitting a real runtime exclusion mechanism.

The dispatch survey's approximate 107 files is not reproduced by this base/scope. Header/source duplication, inherited guards, borrowed queue locks, comments and tests make file-level counts especially unstable. The refined figures above supersede that estimate for this revision. GaiaVoxelWorld has a mutex include but no explicit runtime lock object; the old injection queue was retired (`Vixen-Docs/01-Architecture/Voxel-Mutation-Replacement-2026-09.md:21`).

**Federation accounting:** these 47 families are VIXEN's contribution, including its one `KernelDispatch` adapter error mutex. The kernel and Undertow repositories were not inspected under this worktree-only assignment, so there is **no verified numeric federation grand total**. Do not add the brief's approximate “kernel ~1” to 47: it may describe the same dispatch adapter or a separate implementation. The synthesis/controller must identify that owner before aggregation. The architecture analogy is verified locally below; remote `DeterministicCompactor`, delta transpilation and ownership-row implementations are not presented as inspected evidence.

## Verified kernel analogs used in the tables

| Code | Verified local evidence | What it supports and what it does not |
|---|---|---|
| K1 — wave barriers | `libraries/KernelDispatch/include/KernelDispatch/Dispatcher.h:353`, `:376`, `:383`, `:411`; `libraries/KernelDispatch/src/TaskExecutor.cpp:54`, `:87` | Parallel tasks inside a wave, joined before next wave. `RunSystemWaves` trusts upstream plan/field-manifest proof; it does not independently re-prove an arbitrary caller's plan. |
| K2 — owned task slots / hazards | `libraries/KernelDispatch/include/KernelDispatch/Dispatcher.h:201`, `:217`, `:366` | Per-slot/index RAW/WAW/WAR scheduling and disjoint `(stage,index)` ownership. Transferable to resource rows, per-worker output and mutation/read phases; no cache or GPU lifetime implementation is implied. |
| K3 — proposed deterministic collection | `libraries/KernelDispatch/src/TaskExecutor.cpp:53`, `:62`, `:67`, `:87` | Exception results can occupy owned task slots then be collected after join. This is a proposed replacement for the existing error lock, **not** an already-verified `DeterministicCompactor` implementation. |
| K4 — local adjacent construction proof | `libraries/SVO/src/BulkMaterialization.cpp:190`, `:216`; `libraries/RenderGraph/include/Connection/SdiStageWiring.h:72`, `:185` | Disjoint materialization result slots and deterministic SDI plan traversal already exist locally. These are VIXEN examples, not remote kernel claims. |

Successful native dispatch has no explicit mutex acquisition in its executor, but task bodies, allocation, TBB internals and downstream API calls can still lock. “Lock-free by construction” therefore describes correctly proved task ownership/scheduling, not a formal progress guarantee for the whole runtime.

## Inventory — RenderGraph and application

| ID / lock object / declaration | Protects / acquisition evidence | Contention shape | Primary class / removal condition | Kernel analog |
|---|---|---|---|---|
| RG1 `TBBVirtualTaskExecutor::errorMutex_`, `libraries/RenderGraph/include/Core/TBBVirtualTaskExecutor.h:264` | `errors_` append in `src/Core/TBBVirtualTaskExecutor.cpp:277`; called only from task exception handlers at `:245`, `:252`. | Parallel TBB tasks reporting failures; cold error path. No normal-frame acquisition from this mutex. | **DELTA-able** — task-indexed error slots, deterministic collection after level join. Clear/read remain coordinator operations. | K1/K3; same exception-only residual as native executor. |
| RG2 `ITaskProfile::samplesMutex_`, `libraries/RenderGraph/include/Core/ITaskProfile.h:678` | Pending measurements/predictions and calibration updates; same header `:444,463,483,493,511,519,527,625`. Sampler finalization records measurement and prediction separately (`:342`). | Every instrumented task completion, concurrent bundles sharing a profile, frame registry processing and reset. Lock can also process a full batch or trim prediction history. | **DELTA-able** — worker/task samples, sum/max collection and deterministic last-sample key. Preserve temporal order for last-value/EMA consumers and reset epochs; getters/derived profiles need the same publication contract. | K2/K3; collector design is proposed. |
| RG3 `InputNode::eventMutex_`, `libraries/RenderGraph/include/Nodes/InputNode.h:181` | Callback `pendingInput_` push and drain/swap at `src/Nodes/InputNode.cpp:332,383`. | Per input event and each host update. Default host calls are serial: Update drains, then Render pumps GLFW and executes; newly pumped events normally await the next Update. Test injection is separately guarded. | **ORDER-able** — retain main-thread input ownership and an explicit fold/read boundary. Preserve press/release sequence, cursor-at-click semantics and paused accumulation. Moving the pump before the fold would be a separate input-latency change. | K1/K2; events are order-sensitive, not arbitrary commutative deltas. |
| RG4 `WindowNode::eventMutex` (`recursive_mutex`), `libraries/RenderGraph/include/Nodes/WindowNode.h:99` | `pendingEvents` push/swap at `src/Nodes/WindowNode.cpp:151,210,293,300,307,314`; test injection/query at `:320,324`. | GLFW main-thread callbacks, always-running host drain and node execution drain. Per window event/frame, resize bursts; default pump is outside node execution. | **ORDER-able** — one window owner, drain even during pause, publish immutable frame state. A mutex on the queue does not make all GLFW/node operations safe to move to arbitrary workers. | K1/K2. |
| RG5 `LightTreeBufferNode::cutMutex_`, `libraries/RenderGraph/include/Nodes/LightTreeBufferNode.h:73` | CPU light-tree vector replacement (`src/Nodes/LightTreeBufferNode.cpp:36`) vs full GPU-struct copy (`:78`). | Read/copy every Execute; graph-construction setters include `application/main/source/graph/BuildRenderGraph.cpp:4847,5642,6045`; optional mid-run edit-loop replacement at `application/main/source/VulkanGraphApplication.cpp:2309`. Concurrent overlap is unproven. | **ORDER-able** — publish cut at frame boundary; reader holds immutable generation while filling its existing flight-indexed buffer. Partition/snapshot is an alternative. | K1/K2. |
| RG6 `MaskCacheMutex()::m`, `libraries/RenderGraph/src/Ui/UiHitMask.cpp:53` | Path→immutable mask table and failed-load log set; acquisitions `:70,93,100`. | Every image-mask hit lookup; cold image decode outside lock; concurrent misses can duplicate decode then `emplace` selects one stored result. Only image-mask mode activates this cache. | **CACHE/MEMO** — pre-resolve UI asset masks or use owner-local lookup/frozen handle. Existing entries never erased; preserve stable immutable object lifetime. | K2 analogy; no verified kernel cache implementation. |
| RG7 `DescriptorResourceRegistry::mutex_`, `libraries/RenderGraph/include/Debug/DescriptorResourceTracker.h:314` | Debug event vector, scans/dumps/clear; same header `:196,228,240,252,279,301,309`. Immediate console output occurs under lock. | Potential per-descriptor/frame instrumentation contention **only with `VIXEN_DEBUG_DESCRIPTOR_TRACKING=1`**; default is 0 at `:36`. | **DELTA-able** — partitioned diagnostic append buffers, ordered display after merge. Keep tracking-ID event order for mismatch analysis. | K2/K3. Not a default shipping hotspot. |
| APP1 `g_gaiaChunkAllocatorMutex`, `application/main/source/graph/BuildRenderGraph.cpp:5368` | Entire `BakeSdfWorld` + `BuildSdfBodyOctree` calls at `:5382,5575`; source comments identify Gaia's process-wide chunk allocator as the reason. | Eight `std::async` body tasks (`:5550` onward) on Cornell baked-scene artifact **cache miss**, during graph setup. Heavy CPU evaluation and allocation happen inside the guard. Cold-start cost, not each frame. | **ORDER-able** — split pure body evaluation into owned outputs, schedule Gaia structural allocation/apply and destruction with an explicit owner. True allocator partitioning is an alternative only if Gaia's allocator ownership can change; separate `World` objects alone do not suffice. | K1/K2; base-read/compute/apply is a proposed decomposition, not implemented by this guard. |

### RenderGraph: what actually serializes

**There is no graph-construction mutex to eliminate.** `RenderGraph::AddNodeImpl` directly mutates `instances`, `nameToHandle`, `instancesByType`, topology and compilation state (`libraries/RenderGraph/src/Core/RenderGraph.cpp:137`). `ConnectNodes` writes graph wiring/dependencies (`:203`). Compile runs ordered phases (`:502`), then `GeneratePipelines` executes setup/compile and lifecycle callbacks serially (`:1256,1317`). That is existing single-owner execution, not parallel-safe shared mutation. Concurrent calls into these APIs would require an ownership/apply contract.

The remembered 9,918-line monolith is **9,212 lines at this base**. It remains a large construction/application-policy function, but its length does not measure runtime lock contention. SDI's live construction path is `SynthesizeComputeStage` (`libraries/RenderGraph/include/Nodes/SdiStageSynthesis.h:178`) → four node additions and common wiring (`:185`) → `WireStageFromSdi` (`:197`) → `BuildSdiWirePlan` → registry Apply/batch mutations (`libraries/RenderGraph/include/Connection/SdiStageWiring.h:203,205`). The provider registry is an input; its maps iterate deterministically and it is copyable (`SdiStageWiring.h:185`). **The funnel is a single-writer construction boundary; whether it dominates startup is unmeasured.** Pure per-stage plans are a plausible parallel partition, with deterministic handle allocation and ordered application to shared graph containers. Cold SDI header-generation registry locks are a different subsystem (SH8 below).

The highest frame-path concentration is **borrowed queue serialization**, not seven independent graph-wide mutexes. Nineteen node files plus UI upload and two debug readbacks use canonical queue owners; the complete acquisition index below records them. Most obtain a queue mutex through `VulkanDevice::SubmitMutex`, which first locks a device-wide lookup map even on hits. UI paths cache the borrowed pointer. The main renderer defaults to sequential execution (`libraries/RenderGraph/include/Core/RenderGraph.h:1016`); the opt-in TBB path builds levels and runs tasks in parallel (`src/Core/RenderGraph.cpp:845,876`; `src/Core/TBBVirtualTaskExecutor.cpp:148,184`). Queue access is not fully represented by data-flow dependencies (`libraries/VulkanResources/include/VulkanDevice.h:100`). A deterministic graph schedule therefore does not automatically serialize all accesses to a physical queue.

The body-bake guard has an especially misleading nearby comment: `BuildRenderGraph.cpp:5541` claims heavy recipe scanning still overlaps across bodies, but the lock is acquired **before** `BakeSdfWorld` at `:5382` and the light equivalent at `:5575`. Only small region arithmetic precedes the first lock. Thus the guarded bake/build calls serialize across the eight tasks; no source evidence supports parallel inter-body evaluation there. The comment's historical crash account is not a reproduction performed by this audit. This function-local guard also cannot establish process-wide Gaia safety for unrelated allocator users that do not acquire it.

Input scheduling has an analogous precision trap: the ordinary loop calls Update then Render (`application/main/source/VulkanApplicationBase.cpp:141`); input drains in Update (`VulkanGraphApplication.cpp:3471`), whereas GLFW is pumped in Render (`:360`). Ownership is serial, but a newly pumped event ordinarily reaches the fold next tick. Scheduling changes should preserve this or explicitly approve the latency/semantics change, not accidentally attribute it to lock elimination.

## Inventory — remaining subsystems

### ResourceManagement

For compactness in this table, `RM/` means `libraries/ResourceManagement/`; acquisition paths are relative to that directory unless fully qualified. These 13 families concentrate on resource ownership and upload/lifetime handoff, not irreducible allocator-wide exclusion.

| ID / lock object / declaration | Protects / acquisition evidence | Contention shape | Primary class / removal condition | Kernel analog |
|---|---|---|---|---|
| RM1 `DeferredDestructionQueue::mutex_`, `RM/include/Lifetime/DeferredDestruction.h:408` | Ring storage/head/tail/size, growth and destruction statistics. Same header `:100,108,139,169,200,242,260,272,290`; destruction callbacks run outside lock. | Last-reference releasing workers vs frame drain/stats/shutdown; release bursts and frame drain. `include/Lifetime/SharedResource.h:223,317` supplies enqueue paths. | **DELTA-able** — owned retirement append logs then deterministic owner drain. Preserve GPU-safe eligibility and reentrant destructor enqueue. Destruction itself is not commutative. | K1/K2, proposed append/apply. |
| RM2 `BatchedUpdater::mutex_`, `RM/include/Updates/BatchedUpdater.h:205` | All image-indexed update vectors, clear/resize/drain. `src/Updates/BatchedUpdater.cpp:31,47,57,62,75,92,146,151,162`. | Per producer update, potential per-frame drain. `libraries/VulkanResources/src/VulkanDevice.cpp:616,623` routes queue/record; `libraries/CashSystem/src/AccelerationStructureCacher.cpp:69` produces. External `RecordUpdates` caller not located. | **DELTA-able** — worker/frame logs with stable order keys. Equal-priority stable-sort currently preserves arrival order, which is not deterministic between concurrent producers. Resize at quiescence. | K1/K2. |
| RM3 `HostBudgetManager::arenaMutex_`, `RM/include/Memory/HostBudgetManager.h:306` | Persistent-arena reset vs frame-arena resize at `src/Memory/HostBudgetManager.cpp:119,194`. Allocation offsets use CAS, not this lock. | Cold unload/resize; no production callers of these two mutation methods found. Header requires between-frame/lifecycle use (`:229,237`). | **ORDER-able** — quiescent reset/resize epochs. Existing lock does not synchronize allocation or active pointers that bypass it. | K1 ownership epochs. |
| RM4 `ResourceBudgetManager::mutex_` (`shared_mutex`), `RM/include/Memory/ResourceBudgetManager.h:156` | Budget/usage maps, lazy atomic usage-row creation, reset/config. `src/Memory/ResourceBudgetManager.cpp:16,21,26,35,62,72,85,94,102,110,119,125,131,148,173,188,240,248,256`. | Allocating workers/stats readers; `TryAllocate` and `RecordAllocation` exclusively lock even warm rows; frees/read queries shared. | **CACHE/MEMO** — precreate stable immutable row directory, publish row handles; configure/reset at barriers. Usage can be reduced separately. Strict admission cannot be replaced by delayed totals: current check does not reserve before later accounting (`:263`). | K2 stable ownership rows; admission needs a separate capacity contract. |
| RM5 `DirectAllocator::mutex_`, `RM/include/Memory/DirectAllocator.h:93` | Allocation record map, mapped state, stats and budget binding; map/unmap/flush/invalidate under global lock. `src/Memory/DirectAllocator.cpp:21,134,174,293,328,370,397,418,443,460,474,479,539,613,660`. | Direct backend users of otherwise distinct resources; streaming allocation and mapped updates, cold destructor/configuration. Backend activation is deployment-dependent. | **PARTITION-able** — backing-allocation owner and partitioned tracking, separate stats reduction. Buffer aliases must converge on the same backing-memory owner; per-object external lifetime/map rules remain. | K2 plus K1 lifetime. |
| RM6 `VMAAllocator::mutex_`, `RM/include/Memory/VMAAllocator.h:120` | Engine `allocationRecords_` and budget binding, **not VMA's internal allocation lock**. `src/Memory/VMAAllocator.cpp:58,153,199,298,332,373,398,458,463,493,577,628`; VMA calls generally outside guard. | Allocation/free/map callers including staging misses, cold diagnostics. | **PARTITION-able** — carry records with owned allocations; stable owner handles, aliases and retirement. VMA internal synchronization remains independently present. | K2/K1. |
| RM7 `SizeClassBucket::mutex`, `RM/include/Memory/StagingBufferPool.h:243` | One bucket's available-buffer deque and capacity/eviction. `src/Memory/StagingBufferPool.cpp:180,210,241,302,440`. | Acquire/return every staging reuse; hot size classes contend. Eviction/trim can retain lock through buffer destruction. **12 mutex instances per pool**, header `:258`; one family. | **PARTITION-able** — worker/upload-lane pools with owned quota and returned-handle messages. Preserve overall capacity; moving only deques does not resolve records/lifetime. | K2/K1. |
| RM8 `StagingBufferPool::recordsMutex_`, `RM/include/Memory/StagingBufferPool.h:254` | Handle→record map, in-use transitions and duplicate-release checks. `src/Memory/StagingBufferPool.cpp:51,117,150,189,254,327,351,408,431,449,470`. | All size classes on acquire/release/miss/trim/stats; bucket→record nesting at `:189,449`. | **PARTITION-able** — records move with buffer owner, generation-safe handles and retirement. Returned `BufferRecord*` outlives guard (`:351`), so external lifetime already matters. | K2/K1. |
| RM9 `BatchedUploader::cmdBufferMutex_`, `RM/include/Memory/BatchedUploader.h:354` | Available command-buffer FIFO and checkout reset. `src/Memory/BatchedUploader.cpp:385,406`. | Producer-triggered flush vs completion return, every batch; exhaustion may wait for GPU progress. | **PARTITION-able** — per-recording-worker command pools/buffers plus completion recycle. Partition the **VkCommandPool ownership too**, not only the FIFO: current buffers share a pool. | K2; GPU completion is an additional prerequisite. |
| RM10 `BatchedUploader::pendingMutex_`, `RM/include/Memory/BatchedUploader.h:362` | Pending copies/uploads and start timestamp; drain moves vector out. `src/Memory/BatchedUploader.cpp:118,160,213,311,325,546`. | Every upload/flush/query; live producers include `libraries/RenderGraph/src/Nodes/BodyOctreeSceneNode.cpp:1424,1466,1550` and `libraries/CashSystem/src/VoxelSceneCacher.cpp:928`. | **DELTA-able** — worker append records, deterministic batch assembly; order conflicting destination writes. Timestamp read (`:563`) and byte reset (`:221`) already need a coherent publication contract. | K1/K2 proposed append/apply. |
| RM11 `BatchedUploader::submittedMutex_`, `RM/include/Memory/BatchedUploader.h:368` | Submitted FIFO and completion/reclamation; `src/Memory/BatchedUploader.cpp:231,290,525`. Critical section polls GPU, releases staging, updates status, returns commands and destroys fences. | Submit workers vs completion pollers; `libraries/VulkanResources/src/VulkanDevice.cpp:593` polls through `IsUploadComplete`, used by body-octree execution. Broad nested critical section. | **ORDER-able** — one submission/completion owner per queue/timeline. Preserve GPU completion and actual submit order: FIFO enqueue occurs after submit lock release, so current concurrent enqueue order can differ from submit order. | K1 with explicit external completion dependency. |
| RM12 `BatchedUploader::statusMutex_`, `RM/include/Memory/BatchedUploader.h:375` | Upload handle→status map and pruning; `src/Memory/BatchedUploader.cpp:177,570,575`. | Several accesses per upload by producers, completion processing and clients; terminal updates nest under submitted lock. | **PARTITION-able** — stable owner-addressed status slots and generation-safe retirement. Preserve missing/pruned-handle behavior (currently Failed at `:180`). | K2 published result slots. |
| RM13 `BudgetBridge::pendingMutex_`, `RM/include/Memory/BudgetBridge.h:189` | Bounded retirement FIFO, quota reclamation/callbacks; `src/Memory/BudgetBridge.cpp:73,93,131,166`. | Intended producers vs fence/frame completion consumer and stats. No production external `RecordUpload`/`ProcessCompletedUploads` callers found. Callbacks run under lock; header `:164` prohibits reentry. | **DELTA-able** — retirement append records, deterministic fence/frame-key merge and owner apply. Preserve quota/backpressure and oldest-entry completion assumptions. | K1/K2 proposed retirement log. |

The upload subsystem's opportunity is a joint ownership change: worker-owned staging/command resources, stable status slots and one submission/retirement owner. Removing one deque lock while all workers still share its record map or command pool leaves the underlying conflict. Resource-budget registry precreation is a separate, smaller change; strict allocation admission must remain coherent even if telemetry is delayed.

### VulkanResources and all borrowed queue locks

| ID / lock object / declaration | Protects / acquisition evidence | Contention shape | Primary class / removal condition | Kernel analog |
|---|---|---|---|---|
| VK1 `VulkanDevice::submitMutexMapLock_`, `libraries/VulkanResources/include/VulkanDevice.h:438` | Queue→heap-owned mutex directory; `src/VulkanDevice.cpp:389,392`. Entries never erased during device lifetime. | Every `SubmitMutex(queue)` lookup locks, including warm frame submits/present and upload callers. Header's first-use wording does not imply unlocked hits. | **CACHE/MEMO** — immutable queue-owner bindings created at device setup and retained by consumers. Do not weaken VK2. | K2 prebound ownership rows. |
| VK2 elements of `VulkanDevice::submitMutexes_`, `libraries/VulkanResources/include/VulkanDevice.h:439` | Same physical queue's host submit/present/wait-idle operations; one heap mutex per encountered `VkQueue`, created at `src/VulkanDevice.cpp:392`. Complete aliases/acquisitions below. | Many graph tasks, upload producers, UI, texture/AS setup and capture can target the same queue; frame hot path plus long initialization waits. | **GENUINELY-NEEDED serialization** — one physical-queue owner or deterministic submission stage can remove the mutex but must retain exclusive host access. Multiple logical users of one handle must converge; separate physical queues may have independent owners. | K1 can schedule owner calls; no kernel mechanism waives external API requirements. |

Borrowed pointers are not new locks: `libraries/ResourceManagement/include/Memory/BatchedUploader.h:344` (injected by `libraries/RenderGraph/src/Nodes/DeviceNode.cpp:550`), `libraries/RenderGraph/include/Nodes/UIRenderNode.h:92` (bound at `src/Nodes/UIRenderNode.cpp:106`), `libraries/RenderGraph/include/Ui/VixenRmlRenderInterface.h:87`, and the parameter in `libraries/VulkanResources/include/CommandBufferUtility.h:43`. Optional null pointers mean those helpers rely on caller ownership when no lock is supplied.

VK2 is acquired by the nineteen RenderGraph node files listed in the acquisition index; `libraries/RenderGraph/src/Ui/VixenRmlRenderInterface.cpp:264`; debug captures `libraries/RenderGraph/include/Debug/RenderTargetReadback.h:177,378`; `libraries/CashSystem/src/AccelerationStructureCacher.cpp:473,663`; `libraries/VulkanResources/src/TextureHandling/Loading/TextureLoader.cpp:171`; borrowed command utility `libraries/VulkanResources/src/CommandBufferUtility.cpp:106`; and borrowed uploader `libraries/ResourceManagement/src/Memory/BatchedUploader.cpp:424,475`. The utility (`CommandBufferUtility.cpp:106`) and UI texture upload (`VixenRmlRenderInterface.cpp:264`) hold it across queue submission **and `vkQueueWaitIdle`**, so GPU idle latency can block unrelated host submitters. This is different from just serializing a short submit call.

The current Vulkan reference requires external queue host synchronization unless the queue was created with the specific internally-synchronized flag; VIXEN initializes `VkDeviceQueueCreateInfo` with zero flags (`libraries/VulkanResources/src/VulkanDevice.cpp:32`) and contains no use of that extension flag. GPU semaphore ordering does not replace host access serialization. [Vulkan queue-submit host synchronization](https://docs.vulkan.org/refpages/latest/refpages/source/vkQueueSubmit2.html). Command pool ownership also matters when recording concurrently. [Vulkan synchronization rules](https://docs.vulkan.org/spec/latest/chapters/synchronization.html).

The fetched VMA version is pinned to v3.1.0 (`dependencies/CMakeLists.txt:311`). Wrapper creation sets budget/device-address flags without `VMA_ALLOCATOR_CREATE_EXTERNALLY_SYNCHRONIZED_BIT` (`libraries/ResourceManagement/src/Memory/VMAAllocator.cpp:39`). Therefore removing RM6 does not remove VMA's internal synchronization; the general VMA contract treats these as distinct responsibilities. [VMA threading considerations](https://gpuopen-librariesandsdks.github.io/VulkanMemoryAllocator/html/general_considerations.html). Dependency internals are an explicitly uncounted follow-up scope, not additional guessed GENUINELY-NEEDED rows.

### CashSystem

`C/` below means `libraries/CashSystem/`. The inherited lock is counted once as a declaration family, not separately for every resource type/device instance. CashSystem contributes six families, while its inherited and borrowed acquisitions span 19 files.

| ID / lock object / declaration | Protects / acquisition evidence | Contention shape | Primary class / removal condition | Kernel analog |
|---|---|---|---|---|
| C1 `TypedCacher::m_lock` (`shared_mutex`), `C/include/TypedCacher.h:195` | Entries/pending futures, one resource publication per key; base acquisitions `:75,92,129,134,145,155,160`; derived file mapping in acquisition index. | Every cache hit takes shared lock; concurrent creation vs publication, serialization and teardown. Base releases before Create/wait; several overrides do not. | **CACHE/MEMO** — per-key construction owner and immutable published entries; stable snapshot lifetime and retirement. Preserve one-publisher behavior, exception completion and invalidation. | K2 plus K1 publication; no verified kernel cache implementation. |
| C2 `TypedCacher::m_debugMutex`, `C/include/TypedCacher.h:207` | Key→creation-content-hash collision map; acquisition `:228`. | Every base `GetOrCreate` collision check in **`#ifdef _DEBUG` only**, guard at `:202`; collision logging while locked. | **CACHE/MEMO** — prevalidated/frozen hashes or partition diagnostics. Excluded from ordinary Release burden. | K2 analogy. |
| C3 `MainCacher::m_globalRegistryMutex`, `C/include/MainCacher.h:762` | Factories, names, dependency metadata and global cacher instances; header `:114,159,174,233,241,288,308,316,325,361,370,540,577,664,714`; `src/main_cacher.cpp:16,136`. | Global **per MainCacher instance**, not process singleton. Exclusive global cacher hit at `:241`; creation/init/deserialization and persistence waits may retain lock. | **CACHE/MEMO** — freeze factories, eagerly publish stable independent cachers; immutable lookups, staged lifecycle. | K1/K2 initialization/publication. |
| C4 `MainCacher::m_deviceRegistriesMutex`, `C/include/MainCacher.h:758` | Device→registry table, device cachers/lifetime; header `:193,341,369,386,419,554,576`; `src/main_cacher.cpp:86,112`. | Every device-registry request takes exclusive lock even on hit; multiple devices, graph resources, async saves and device teardown contend. | **PARTITION-able** — stable per-device owner and immutable outer directory with retirement barrier. | K2 per-device rows plus K1. |
| C5 `RecipeContentCacher::m_memberLock`, `C/include/RecipeContentCacher.h:100` | Deduplicated family member recipe IDs after base lookup; header `:71`; live call `application/main/source/VulkanGraphApplication.cpp:4102`. | Each recipe registration across all families; setup/content registration, not proven frame hot path. | **DELTA-able** — `(contentHash,recipeId)` membership intents and deterministic set-union/apply. Define canonical vector order and representative `firstRecipeId`; first arrival is currently order-dependent. | K2/K3 proposed deterministic collection; remote delta-transpiler not inspected. |
| C6 `TypeRegistry::m_mutex` (`shared_mutex`), `C/include/TypeRegistry.h:163` | Factory/name/cacher maps; header `:50,75,106,116,126,134,144,158`. | Shared getters, exclusive GetOrCreate even on hit, factory under lock. **No runtime instantiation located**; `include/DeviceIdentifier.h:218` describes placeholder future integration. | **CACHE/MEMO** — freeze registration/eager publication if integrated. Not a proven shipping acquisition. | K1/K2. |

Fifteen concrete cacher types inherit C1 (including `RecipeContentCacher`, which uses the base acquisition). The fourteen acquisition-bearing derived source files in the index all map to C1, except the two acceleration-structure queue acquisitions, which map to VK2. C2 is shared in declaration shape but separately instantiated with each relevant debug cacher. Cross-resource publication/lifetime is more valuable to consolidate than replacing fourteen guard spellings independently.

### ShaderManagement

`SH/` means `libraries/ShaderManagement/`. Eight families span ten files. Shader creation/recompile is cold relative to steady frames, but can be a material startup/hot-reload path; five of these families have no located application instantiation.

| ID / lock object / declaration | Protects / acquisition evidence | Contention shape | Primary class / removal condition | Kernel analog |
|---|---|---|---|---|
| SH1 `ShaderCacheManager::cacheMutex`, `SH/include/ShaderCacheManager.h:178` | SPIR-V disk files, metadata/config and stats; `src/ShaderCacheManager.cpp:39,108,149,160,178,196,245,250,255,264,269,277`. | Exclusive scope includes hit file reads, writes/validation and maintenance scans; unrelated shader keys serialize. Live app instance at `application/main/include/VulkanGraphApplication.h:326`, nine builder cache bindings. | **CACHE/MEMO** — immutable content-addressed artifacts and per-key publisher, statistics deltas and staged eviction. Filesystem access is not proof of irreducible global exclusion. | K1/K2 analogy. |
| SH2 `ShaderLibrary::libraryMutex`, `SH/include/ShaderLibrary.h:246` | Definitions/IDs, compiled programs, status/jobs, swap/watch state; `src/ShaderLibrary.cpp:41,71,97,124,129,135,141,167,191,212,223,229,242,257,270,282,298,311,316,321,351,391,413,418,423`. | Intended polling/hot-reload vs loading; compilation/reflection/scans and getters serialize. **No app/RenderGraph object located**; background compiler described as stub. | **ORDER-able** — parallel immutable program outputs, ordered generation swap and lifetime reclamation. Existing nested nonrecursive acquisitions need correction before treating this as a concurrent baseline. | K1/K2. |
| SH3 `AsyncShaderBundleBuilder::ThreadLocalQueue::mutex`, `SH/include/AsyncShaderBundleBuilder.h:293` | Per-worker task queue; `src/AsyncShaderBundleBuilder.cpp:183,201,222,248`. | Submitters, owner and all work-stealing workers contend; wake predicate scans all queues. Per job/scheduling cycle; no app instantiation located. One mutex per worker, one family. | **PARTITION-able** — stable ownership and producer mailboxes or sanctioned arena; stealing currently breaks single-owner queue assumptions. | K2 dispatch ownership. |
| SH4 `AsyncShaderBundleBuilder::cvMutex_`, `SH/include/AsyncShaderBundleBuilder.h:302` | Condition-variable sleep/wake protocol; `src/AsyncShaderBundleBuilder.cpp:216`, predicate separately locks queues. | Idle workers serialize predicate scans; up to 100 ms timeout, no shader execution inside guard. No app instantiation located. | **ORDER-able** — explicit dispatch epochs with correct wake/notification mechanism. A phase barrier alone cannot replace asynchronous sleeping or prevent lost wakeups. | K1 analogy only; no verified kernel sleep-protocol counterpart. |
| SH5 `AsyncShaderBundleBuilder::buildsMutex_`, `SH/include/AsyncShaderBundleBuilder.h:307` | Active-build handle map/lifetime; `src/AsyncShaderBundleBuilder.cpp:51,63,81,106,129,135,148,172,272`; cancellation/completion fields separately atomic. | Submit/cancel/status/cleanup and worker cancellation lookup; waits poll with lock every 10 ms. No app instantiation located. | **PARTITION-able** — coordinator-owned stable job slots; worker retains handle, snapshot status, explicit retirement. | K2 output/job rows. |
| SH6 `ShaderLogger::mutex_`, `SH/include/ShaderLogger.h:273` | Callback replacement and complete-message callback/default sink; header `:172,209`, optional callback invoked under lock. | Every unfiltered shader log across compilation workers; live log calls at `src/ShaderBundleBuilder.cpp:517,577`. No app callback installation located; default sink unless a host supplies one. Cold compilation but rate-dependent cost. | **DELTA-able** — worker log records, deterministic drain, frozen/published callback configuration. Sink remains one owner; arbitrary callback side effects are not commutative. | K2/K3 append/merge proposal. |
| SH7 `s_initMutex`, `SH/src/ShaderCompiler.cpp:15` | Process-wide glslang initialization flag and `InitializeProcess`; acquisition `:26`. | Every compiler constructor, including after initialization; cold builder setup. | **CACHE/MEMO** — initialize process before compiler workers, publish ready lifetime. Replacing with call_once still introduces implicit once synchronization. | K1 initialization-before-dispatch. |
| SH8 `SdiRegistryManager::mutex_` (`recursive_mutex`), `SH/include/SdiRegistryManager.h:266` | UUID/alias maps, dirty count/timestamp and registry header/metadata publication; `src/SdiRegistryManager.cpp:79,129,162,169,182,217,232,247,258,265,438,443,472,496,521`. | Optional builder callback at `src/ShaderBundleBuilder.cpp:754`; registration holds guard across recursive regeneration and file writes. **No app instance or frame-retrieval edge located.** | **CACHE/MEMO** — batch registrations, deterministic aliases, one immutable generated snapshot. This is optional SDI header generation/publication, distinct from graph's live SDI provider wiring. | K1/K2 publication analogy. |

Actual cache bindings are `application/main/source/graph/BuildRenderGraph.cpp:1940,2054,2080,2099,2113,2201,2227,2269,2332`: nine, not the application header comment's four. Optional library API implementations remain in this conservative inventory even where shipping reachability is unestablished; source compilation into a static library does not prove final executable use.

### SVO, EventBus, GaiaArchetypes and native dispatch

| ID / lock object / declaration | Protects / acquisition evidence | Contention shape | Primary class / removal condition | Kernel analog |
|---|---|---|---|---|
| SVO1 `BulkMaterializationQueue::m_processMutex`, `libraries/SVO/include/BulkMaterialization.h:120` | Batch coordinator, acceptance-order publication and output capacity reservation; `src/BulkMaterialization.cpp:169`. | Competing process callers serialize for entire TBB batch; within-batch result slots parallel. **No production caller located.** | **ORDER-able** — scheduled single coordinator dispatch stage; retain capacity reservation, close/cancel terminal results and batch order. | K1/K4. |
| SVO2 `BulkMaterializationQueue::m_mutex`, `libraries/SVO/include/BulkMaterialization.h:121` | Pending/ready deques, capacity checks, accepted/in-flight/terminal counters and closed state; `src/BulkMaterialization.cpp:151,173,245,259,269,274`. | Producers vs dispatcher/consumers/status/close, per enqueue/pop and batch boundaries. Expensive work outside this guard. No production caller located. | **PARTITION-able** — coordinator owns capacity and queues, producers own request slots, workers own result slots. Exact backpressure/close/drain semantics remain. | K2/K4. |
| SVO3 `LaineKarrasOctree::m_renderLock` (`shared_mutex`), `libraries/SVO/include/LaineKarrasOctree.h:330` | Octree rebuild/update/remove state and explicit render lease; `src/SVORebuild.cpp:231,768,833,867,871`. | Full rebuild holds exclusive guard; active bake/cache consumers. Render-lease acquisition only found in examples/tests. **`lockForRendering()` uses exclusive `.lock()`, not `.lock_shared()`.** | **ORDER-able** — mutate/publish/read generations, lifetime-safe immutable snapshots; per-octree/region rebuild partition optional. Ordinary readers cannot be assumed covered by an unused lease. | K1/K2. |
| SVO4 `VoxelDataCache::GetMutex()::mutex`, `libraries/SVO/src/SceneGenerator.cpp:125` | Global voxel cache map and most stats; acquisitions `:164,202,216`. Header `libraries/SVO/include/SceneGenerator.h:475` is accessor, not new object. | Every lookup; miss holds mutex over generator construction, full Generate and logging. No external non-test caller located. | **CACHE/MEMO** — precompute/key partitions/immutable publish; returned raw pointers need lifetime against Clear. | K2 analogy; no verified kernel cache. |
| EB1 `MessageBus::queueMutex`, `libraries/EventBus/include/MessageBus.h:257` | Message queue push/swap/clear/reserve; `src/MessageBus.cpp:125,161,259,275,347,363,368`. | **Hot per publication**, multi-producers vs frame consumer. Queue swap is short; dispatch occurs outside queue mutex. Live graph drains `libraries/RenderGraph/src/Core/RenderGraph.cpp:684,1580`. | **DELTA-able** — producer append logs merged at dispatch boundary. Preserve producer sequence and define cross-producer ordering; payload callbacks are not commutative. | K1/K2 proposed logs; remote delta transpiler unverified. |
| EB2 `MessageBus::subscriptionMutex`, `libraries/EventBus/include/MessageBus.h:261` | Owning subscription storage, lookup pointers and IDs; `src/MessageBus.cpp:20,48,81,114,194`; handler call at `:242` remains guarded. | **Every dispatched message**, arbitrary callbacks, registration/teardown; immediate dispatch on caller thread. `libraries/AppFlow/src/AppFlowRuntime.cpp:17` uses immediate publish. | **ORDER-able** — stage registry edits, immutable subscriber snapshots per dispatch epoch, callback lifetime and side-effect ordering. Snapshotting alone cannot make handlers safe to run concurrently. | K1/K2. |
| EB3 `MessageBus::statsMutex`, `libraries/EventBus/include/MessageBus.h:274` | Counts/type histogram, queue high-water/snapshot and warning stats; `src/MessageBus.cpp:131,149,184,253,269,280,285,328`. | **Hot per publish/dispatch**, all producers plus query/reset; separate from queue lock. | **DELTA-able** — partitioned counters/histograms and sum/max reduction. Define reset epoch and queue-size snapshot semantics explicitly. | K2/K3 proposed fold. |
| EB4 `WorkerThreadBridge<T>::workMutex`, `libraries/EventBus/include/WorkerThreadBridge.h:211` | Queue push/pop/size and CV predicate; same header `:135,147,158`, wait `:159`. | Submitters vs one owned worker (`:106`), every work item; execution outside guard (`:179`). **No instantiated runtime bridge located.** | **PARTITION-able** — producer mailboxes + one consumer or sanctioned arena. Preserve sleep/wakeup and shutdown protocol. | K2; native executor's arena avoids this private-worker queue shape. |
| GA1 `RelationshipObserver::m_mutex`, `libraries/GaiaArchetypes/include/RelationshipObserver.h:349` | Callback registry/IDs, deferred operations and callback execution; `src/RelationshipObserver.cpp:20,41,62,83,101,126,166,199,305,352,377,402`. | Per notification/enqueue/registration, handlers while locked. Volume library registers hooks at `src/VoxelVolumeArchetype.cpp:23`; no production construction of that pathway located. | **ORDER-able** — structural-commit/callback phases and immutable registry; defer records into owned logs later. Lock does **not** cover actual Gaia structural writes at `RelationshipObserver.cpp:122,155,211`. | K1/K2. |
| KD1 `TaskExecutor::RunWave::errMutex`, `libraries/KernelDispatch/src/TaskExecutor.cpp:53` | Error-vector append in parallel catch paths `:62,67`. | **Cold exceptions only**; successful multi-task wave constructs but does not acquire. Single-task exception path (`:33`) does not need lock. | **DELTA-able** — per-task error slots then deterministic task-order collection after join; current vector order follows nondeterministic acquisition order. | K1/K2/K3; this is the residual exception-path lock itself. |

SVO's live rebuild boundary is more relevant to existing shipping paths than its unwired batch queue or legacy voxel memoizer. The replacement document explicitly leaves delta-log caller wiring and generation swap/retirement unfinished (`Vixen-Docs/01-Architecture/Voxel-Mutation-Replacement-2026-09.md:58,74,75`). EventBus has the clearest repeated active locking in this group. RelationshipObserver and WorkerThreadBridge remain future activation risks, not demonstrated frame bottlenecks.

### Supplemental GPU exclusion/publication protocol

| ID / lock-like state / declaration | Protects / acquisition evidence | Contention shape | Primary class / removal condition | Kernel analog |
|---|---|---|---|---|
| GPU1 `HitAccumTable::accumEntries[s].keyLo/keyHi` slot claim and transient reclaim marker, `shaders/HitAccumulationCommon.glsl:48`, `shaders/HitAccumulate.comp:62` | `ClaimAccumSlot`: claim empty keyLo with CAS at `HitAccumulate.comp:104`; publish representative/keyHi at `:110`; stale epoch CAS to `kTransient` at `:132`, exclusively reset sums/representative then publish keyHi at `:139`. Contenders bounded-spin 16 reads at `:125`, then probe onward. | Many hit-record GPU invocations contend by hashed recipe/cell/mip key each engaged frame, including collisions and epoch reclaim. Hit accumulation itself is opt-in (`application/main/source/graph/BuildRenderGraph.cpp:790`). **HitAccumClear is separately opt-in/off by default** (`:814,837`); lazy reclaim remains the ordinary path when accumulation is enabled. No GPU wait-time measurement here. | **PARTITION-able** — group records by canonical key then assign one cell/key owner to initialization, representative choice and deterministic reduction. Numeric fixed-point sums fit DELTA-able work, but shared table claim/publication is a separate invariant. Preserve bounded capacity/probing/drop behavior or explicitly redesign it; a CAS spelling alone is not proof of harmless telemetry or formal lock-freedom. | K2 owned output rows; K1 phase separation. GPU grouping/reduction is proposed, not implemented by native scheduling alone. |

GPU1 is a lock-like exclusion/publication protocol rather than an indefinitely blocking mutex: losers have bounded wait/probe/fail-soft behavior. Its two key fields form one per-slot protocol, not two independent mutex families. The table-wide clear pass (`shaders/HitAccumClear.comp:44`) is an optional schedule-based alternative to **stale-epoch reclaim**, not elimination of same-frame competing claims. Ordinary atomicAdd sums, counter resets and overflow flags found elsewhere in the shader sweep are not separate mutexes. This supplemental search did not constitute a complete GPU memory-model correctness proof.

The clear pass is **not a validated drop-in remedy**: builder comments (`application/main/source/graph/BuildRenderGraph.cpp:804`) describe why it was disabled, including later in-flight clearing of a sampled epoch. It resets keys only; the fresh-claim branch does not itself zero all totals. Optional workgroup premerge (`shaders/HitAccumulate.comp:146`) reduces numerical atomic additions **after** slot claiming and therefore does not eliminate this protocol. Synthesis must treat GPU visibility, representative choice and epoch retirement as explicit proof obligations.

## Highest-leverage candidates for the synthesis lane

Priority is inferred from fan-in, critical-section breadth and active call paths; no lock wait-time ranking was measured. Each line is a coordinated ownership change, not permission to remove the named guards independently.

| Priority | Families / opportunity | Why this is high leverage | Required proof before elimination |
|---|---|---|---|
| 1 | **VK1/VK2 — bind physical queues once, record independently, submit through one owner** | Reaches nineteen graph node files plus upload/UI/cache helpers; warm lookup locks add an avoidable device-wide bottleneck before queue serialization. Supports batching while independent workers record. | All submit/present/idle paths must share actual queue identity and lifetime. Model host queue access separately from GPU data hazards; preserve timeline/signal/fence ownership. |
| 2 | **RM7–RM12 — upload/staging partitions and completion owner** | Staging bucket/global-map, command FIFO, pending list, submitted FIFO and status map currently lock multiple times per upload. Completion nests resource returns and status writes. | Worker-owned command **pools**, backing-allocation aliases, deterministic conflicting-copy order, capacity limits, GPU completion before reuse and status-handle retirement. |
| 3 | **EB2 — subscriber snapshots and dispatch phases** | Every message executes arbitrary callback code under a registry mutex, widening the critical section with user work and creating reentrancy hazards. | Subscription edits/lifetime at epoch boundaries, explicit callback-side-effect order and immediate-publish semantics. |
| 4 | **EB1/EB3, then RG2 — append/event/telemetry records** | Hot per-publication queue+statistics locks and per-task measurement/prediction locks are direct fan-in points. | Stable producer/task keys, queue capacity, epoch reset and loss/overflow policy; event ordering and temporal statistics cannot be replaced with unordered sums. |
| 5 | **C1/C3/C4, SH1 — immutable resource/cache publication** | Common base affects fifteen resource types; registry hits repeatedly lock; disk hits retain global I/O exclusion. Startup/streaming/reload rather than necessarily every frame. | One publisher per key, pending exception/future completion, snapshots for persistence and lifecycle/alias-safe reclamation. Consolidate override protocols before increasing concurrency. |
| 6 | **RM4 — prebound budget rows** | Stable map lookup currently takes exclusive locks on allocation paths despite atomic counters. Smaller scoped structural opportunity. | Strict capacity admission/reservation versus later telemetry must be explicitly distinguished; freeze directory without freezing legitimate budget policy changes. |
| 7 | **APP1 — split pure body evaluation from Gaia structural apply** | Eight apparent parallel body jobs actually serialize their heavy bake/build; strong cold-cache startup partition opportunity. | Revalidate Gaia allocator ownership, including destruction and other worlds; evaluate into owned plain outputs, deterministically apply structure. Cached startup bypasses this work entirely. |
| 8 | **SVO3 — publish immutable octree generations** | Rebuild/update/remove hold a large exclusive state guard; separating construction from read generation makes parallel worlds/chunks more viable. | Real reader lifetime, all mutation paths and GPU generation retirement. Current explicit rendering lease is exclusive and not wired to located production readers. |
| 9 | **RM1/RM13 — deterministic retirement records** | Last-reference releases can become short owned appends; one lifecycle owner can drain. RM1 has active enqueue paths; RM13 is currently an unwired follow-on, so do not prioritize it from declaration count alone. | GPU/frame eligibility, bounded queues, quota return and reentrant destructor/callback behavior. |
| 10 | **GPU1 — key-owned hit-cell aggregation** | Many GPU hit records claim/reclaim a shared table; bounded transient waiting and probing are real runtime exclusion, separate from commutative numeric atomics. Activation is feature-dependent. | Deterministic representative selection, grouping cost, capacity/overflow behavior, integer sum semantics and GPU publication/phase visibility. Profile before selecting grouping/sort/compaction architecture. |

RG3/RG4/RG5 are comparatively small ownership cleanups once the host update/read phases are explicit. RG7/C2 are diagnostic-only and should not displace measured shipping work. SH2–SH5/SH8 and SVO1/SVO2/SVO4/EB4/GA1 need activation evidence before being called runtime hotspots. Kernel's error collection (KD1) is a useful small pattern demonstration, but not a throughput priority on successful dispatch.

## Findings, uncertainty and limits

### Correctness constraints exposed by the inventory

1. **A mutex census is not a thread-safety proof.** `libraries/RenderGraph/src/Nodes/SkyProjectionNode.cpp:565` calls `fpQueueSubmit2_` without a canonical queue guard. Default sequential graph execution can mask this; source evidence alone does not prove conflicting live calls in one run. A queue-owner design must include this unguarded path, not only the guard index. `PresentNode.cpp:144` also has device-idle handling outside its present guard; quiescence must be established engine-wide. No engine fix was attempted.
2. **Same-key cache waiting can deadlock publication.** Six derived C1 paths call `future.get()` while retaining shared ownership: `libraries/CashSystem/src/pipeline_cacher.cpp:108`, `shader_module_cacher.cpp:54`, `pipeline_layout_cacher.cpp:23`, `SamplerCacher.cpp:63`, `MeshCacher.cpp:69`, `RenderPassCacher.cpp:56`. The base creator needs exclusive ownership at `libraries/CashSystem/include/TypedCacher.h:117` before fulfilling the pending result. This is a source-established wait cycle under overlap, not a runtime reproduction. Base GetOrCreate already releases before its own wait. Unifying publication is a prerequisite to safely expanding parallel resource creation.
3. **Callback locks permit self-deadlock.** EB2 holds a nonrecursive mutex while invoking handlers (`libraries/EventBus/src/MessageBus.cpp:194,242`); Subscribe, Unsubscribe or PublishImmediate from such a callback reacquires it. Ordinary queued Publish uses different locks and does not inherently create this cycle. GA1 and SH6 similarly call arbitrary code under their registry/output mutexes. Snapshot publication still needs a side-effect/lifetime contract.
4. **Dormant shader APIs are not a proven concurrent baseline.** SH2 nests nonrecursive acquisitions: UpdateProgram→HasProgram (`libraries/ShaderManagement/src/ShaderLibrary.cpp:71,124`), RemoveProgram→CancelCompilation (`:97,391`), GetCompiledProgramByName→GetCompiledProgram (`:242,223`), file watching→CompileProgramAsync (`:321,167`). Returned pointers can outlive lock scope. No located shipping instance; record this as activation risk, not measured frame failure.
5. **Lifetime/configuration barriers matter beyond maps.** MainCacher has global→device registration order (`libraries/CashSystem/include/MainCacher.h:174,193`) and device→global ClearAll (`:369,370`); persistence holds locks across work and waits (`:664`; `libraries/CashSystem/src/VoxelSceneCacher.cpp:202`). Phase separation must cover save/clear/device destruction, not just fast lookup. SH8 public generation/query methods include caller-lock assumptions (`libraries/ShaderManagement/src/SdiRegistryManager.cpp:292,433`).
6. **Some state is inconsistently covered already.** EventBus filter counters written under subscription guard (`libraries/EventBus/src/MessageBus.cpp:228,231,235`) are queried/reset under stats guard. Voxel memoizer flags/stats have unguarded accesses (`libraries/SVO/src/SceneGenerator.cpp:150,158,211,225`). Host arena allocation bypasses reset guard, observer mutex excludes Gaia mutation, and uploader state transitions span separate locks/atomics. Removing guards must not preserve these gaps by assuming the existing code supplies a complete invariant.
7. **`.session.lock` is a dirty marker, not mutual exclusion.** `libraries/RenderGraph/src/Core/RenderGraph.cpp:1272` checks/overwrites a cache marker, later removed on successful save (`:344`). It does not obtain an OS lock and cannot establish multi-process cache exclusion. It is excluded from the 47; deterministic cache publication must separately address process/namespace ownership if multiple hosts share a cache directory.

These source findings are present at the audited base and were not introduced by the document. They are not “pre-existing red” build/test classifications: no failing product command or reproduced concurrency run is claimed.

### Explicit unresolved questions for synthesis/owner

- **No engine-owned explicit family is left without a proposed root-cause class.** Classification confidence is about mechanism, not measured benefit or a completed lock-free design. The conditional O class for SH4 requires a real asynchronous wakeup contract; deciding between persistent async workers and plan-dispatched jobs belongs to synthesis.
- **External internals are unclassified/unquantified:** fetched Gaia allocator implementation, VMA v3.1.0's internal locks, TBB/glslang/GLFW/standard-library and Vulkan-driver internals. The brief's goal of making locks unnecessary across the whole process cannot be certified from first-party source. VMA flags and the documented Gaia guard cause are verified to the extent stated; detailed dependency lock families require a separate authorized inventory, not guessed counts.
- **Federation total and remote kernel analogs remain unverified.** Need repository/base identities and proof that a cited native-dispatch mutex is or is not KD1 before aggregation. DeterministicCompactor and ratified delta-transpiler implementation citations must be supplied by the kernel/synthesis lane; K1/K2 are the local verified evidence.
- **Shipping activation and timings need tracing.** Resolve which optional facilities actually instantiate in target modes, then record lock owner, acquisition count, wait time, hold time, task/worker, resource/queue identity and phase. This is a follow-up measurement prescription; no instrumentation was added and no timing numbers were inferred from file counts.
- **Semantic choices need explicit contracts:** cross-producer event ordering; immediate callback recursion; recipe representative/order; overlapping upload writes; strict quota reservation; snapshot/generation retention; asynchronous wakeup and cancellation. Per-row removal candidates are contingent on these, not authorization to silently redesign them.

### Atomics and non-mutex synchronization

The same scoped census finds **10 files with `std::atomic` syntax and no explicit mutex syntax**, not the starting estimate of ~17. Files using both remain in the lock inventory. Atomics can use hidden locks on some types/targets; no target-specific `is_lock_free` proof or formal nonblocking progress audit was performed.

| Atomic-only source | Role / evidence |
|---|---|
| `libraries/RenderGraph/include/Core/RenderGraph.h:984` | External cleanup ID counter. |
| `libraries/RenderGraph/include/Core/TaskProfileRegistry.h:721` | Pending increase/decrease flags (`:722` also). |
| `libraries/RenderGraph/src/Core/FailScenario.cpp:43` | Optional validation-error counter. |
| `libraries/RenderGraph/src/Core/NodeInstance.cpp:24` | Node instance ID allocation. |
| `libraries/RenderGraph/src/Ui/BlobView.cpp:58` | UI view ID allocation. |
| `libraries/ResourceManagement/include/Lifetime/SharedResource.h:112` | Reference count; eventual retirement can acquire RM1. |
| `libraries/ResourceManagement/include/Memory/DeviceBudgetManager.h:331` | Staging quota and alias accounting (`:334`). |
| `libraries/SVO/include/SVOBuilder.h:266` | Build progress. |
| `libraries/logger/Logger.h:77` | Logger threshold/output flags (`:78`). |
| `libraries/logger/Logger.cpp:10` | Definitions of logger atomic flags (`:13`). Does not imply its log vector is thread-safe. |

Condition-variable mutexes are explicitly EB4 and SH4, with SH4 additionally consulting SH3 queues. GPU timeline/binary semaphores, fences, command barriers, queue/device-idle waits and host atomics enforce different relationships; replacing a mutex must retain the applicable relationship. Implicit C++ static initialization can also synchronize cold construction even in files without an explicit lock.

## CONSOLIDATION ISSUES (non-facade deltas this lane needed)

- **None introduced.** This lane needed no hand-written runtime workaround, build/tooling patch, generator edit, setup shim or facade-adjacent implementation. It used the authorized no-index source-navigation fallback and produced this document only. Existing ownership/publication holes discovered by the audit are enumerated above; they are findings for future lanes, not incidental code deltas made here.

## Acquisition and alias coverage index

The index lists all 98 files from the mutex-token census and their owning family IDs. Numbers are **mutex-type/RAII syntax lines**, not acquisition counts: signatures, declarations, a default `unique_lock` and its assignment can each appear. Explicit octree `.lock()`/`.unlock()` at `libraries/SVO/src/SVORebuild.cpp:867,871` are additional acquisitions/releases already attributed to SVO3. Debug/test-only branches inside runtime files are retained and qualified in the inventory.

| Source file | Syntax lines at base | Owner families |
|---|---|---|
| `libraries/CashSystem/include/MainCacher.h` | 114,159,174,193,233,241,288,308,316,325,341,361,369,370,386,419,540,554,576,577,664,714,758,762 | C3/C4 |
| `libraries/CashSystem/include/RecipeContentCacher.h` | 71,100 | C5 |
| `libraries/CashSystem/include/TypeRegistry.h` | 50,75,106,116,126,134,144,158,163 | C6 |
| `libraries/CashSystem/include/TypedCacher.h` | 75,92,129,134,145,155,160,195,207,228 | C1/C2 |
| `libraries/CashSystem/src/AccelerationStructureCacher.cpp` | 77,187,473,663 | C1; VK2 at 473,663 |
| `libraries/CashSystem/src/ComputePipelineCacher.cpp` | 93,208 | C1 |
| `libraries/CashSystem/src/DescriptorSetLayoutCacher.cpp` | 18,94 | C1 |
| `libraries/CashSystem/src/MeshCacher.cpp` | 24,60,206 | C1 |
| `libraries/CashSystem/src/RenderPassCacher.cpp` | 22,47,193 | C1 |
| `libraries/CashSystem/src/SamplerCacher.cpp` | 23,54,149,270 | C1 |
| `libraries/CashSystem/src/TextureCacher.cpp` | 241,362 | C1 |
| `libraries/CashSystem/src/VoxelAABBCacher.cpp` | 75 | C1 |
| `libraries/CashSystem/src/VoxelSceneCacher.cpp` | 167,202,309 | C1 |
| `libraries/CashSystem/src/descriptor_cacher.cpp` | 170 | C1 |
| `libraries/CashSystem/src/main_cacher.cpp` | 16,86,112,136 | C3/C4 |
| `libraries/CashSystem/src/pipeline_cacher.cpp` | 57,99,374,409,550 | C1 |
| `libraries/CashSystem/src/pipeline_layout_cacher.cpp` | 14,102 | C1 |
| `libraries/CashSystem/src/shader_compilation_cacher.cpp` | 68,218 | C1 |
| `libraries/CashSystem/src/shader_module_cacher.cpp` | 45,131,174,316,344,509 | C1 |
| `libraries/EventBus/include/MessageBus.h` | 257,261,274 | EB1/EB2/EB3 |
| `libraries/EventBus/include/WorkerThreadBridge.h` | 135,147,158,211 | EB4 |
| `libraries/EventBus/src/MessageBus.cpp` | 20,48,81,114,125,131,149,161,184,194,253,259,269,275,280,285,328,347,363,368 | EB1/EB2/EB3 |
| `libraries/GaiaArchetypes/include/RelationshipObserver.h` | 349 | GA1 |
| `libraries/GaiaArchetypes/src/RelationshipObserver.cpp` | 20,41,62,83,101,126,166,199,305,352,377,402 | GA1 |
| `libraries/KernelDispatch/src/TaskExecutor.cpp` | 53,62,67 | KD1 |
| `libraries/RenderGraph/include/Core/ITaskProfile.h` | 444,463,483,493,511,519,527,625,678 | RG2 |
| `libraries/RenderGraph/include/Core/TBBVirtualTaskExecutor.h` | 264 | RG1 |
| `libraries/RenderGraph/include/Debug/DescriptorResourceTracker.h` | 196,228,240,252,279,301,309,314 | RG7 |
| `libraries/RenderGraph/include/Debug/RenderTargetReadback.h` | 177,378 | VK2 borrowed |
| `libraries/RenderGraph/include/Nodes/InputNode.h` | 181 | RG3 |
| `libraries/RenderGraph/include/Nodes/LightTreeBufferNode.h` | 73 | RG5 |
| `libraries/RenderGraph/include/Nodes/UIRenderNode.h` | 92 | VK2 borrowed |
| `libraries/RenderGraph/include/Nodes/WindowNode.h` | 99 | RG4 |
| `libraries/RenderGraph/include/Ui/VixenRmlRenderInterface.h` | 30,87 | VK2 borrowed |
| `libraries/RenderGraph/src/Core/TBBVirtualTaskExecutor.cpp` | 277 | RG1 |
| `libraries/RenderGraph/src/Nodes/AccumulationHistoryNode.cpp` | 247 | VK2 borrowed |
| `libraries/RenderGraph/src/Nodes/BlitNode.cpp` | 252 | VK2 borrowed |
| `libraries/RenderGraph/src/Nodes/BodyOctreeSceneNode.cpp` | 1693 | VK2 borrowed |
| `libraries/RenderGraph/src/Nodes/ComputeDispatchNode.cpp` | 358 | VK2 borrowed |
| `libraries/RenderGraph/src/Nodes/ComputeStageNode.cpp` | 285 | VK2 borrowed |
| `libraries/RenderGraph/src/Nodes/DepthBufferNode.cpp` | 245 | VK2 borrowed |
| `libraries/RenderGraph/src/Nodes/DepthTargetNode.cpp` | 218 | VK2 borrowed |
| `libraries/RenderGraph/src/Nodes/GeometryRenderNode.cpp` | 255 | VK2 borrowed |
| `libraries/RenderGraph/src/Nodes/InputNode.cpp` | 332,383 | RG3 |
| `libraries/RenderGraph/src/Nodes/LightTreeBufferNode.cpp` | 36,78 | RG5 |
| `libraries/RenderGraph/src/Nodes/MultiDispatchNode.cpp` | 466 | VK2 borrowed |
| `libraries/RenderGraph/src/Nodes/PassGroupNode.cpp` | 199 | VK2 borrowed |
| `libraries/RenderGraph/src/Nodes/PickIdTargetNode.cpp` | 273 | VK2 borrowed |
| `libraries/RenderGraph/src/Nodes/PresentNode.cpp` | 134 | VK2 borrowed |
| `libraries/RenderGraph/src/Nodes/ProbeAtlasNode.cpp` | 254 | VK2 borrowed |
| `libraries/RenderGraph/src/Nodes/ShellRevalidateNode.cpp` | 380 | VK2 borrowed |
| `libraries/RenderGraph/src/Nodes/SkySphereNode.cpp` | 245 | VK2 borrowed |
| `libraries/RenderGraph/src/Nodes/TraceRaysNode.cpp` | 375 | VK2 borrowed |
| `libraries/RenderGraph/src/Nodes/UIRenderNode.cpp` | 374,375 | VK2 borrowed |
| `libraries/RenderGraph/src/Nodes/VoxelSelectionProviderNode.cpp` | 329 | VK2 borrowed |
| `libraries/RenderGraph/src/Nodes/WindowNode.cpp` | 151,210,293,300,307,314,320,324 | RG4 |
| `libraries/RenderGraph/src/Nodes/WorldPosHistoryNode.cpp` | 238 | VK2 borrowed |
| `libraries/RenderGraph/src/Ui/UiHitMask.cpp` | 52,53,70,93,100 | RG6 |
| `libraries/RenderGraph/src/Ui/VixenRmlRenderInterface.cpp` | 56,263,264 | VK2 borrowed |
| `libraries/ResourceManagement/include/Lifetime/DeferredDestruction.h` | 100,108,139,169,200,242,260,272,290,408 | RM1 |
| `libraries/ResourceManagement/include/Memory/BatchedUploader.h` | 145,155,344,354,362,368,375 | RM9/RM10/RM11/RM12; VK2 borrowed |
| `libraries/ResourceManagement/include/Memory/BudgetBridge.h` | 189 | RM13 |
| `libraries/ResourceManagement/include/Memory/DirectAllocator.h` | 93 | RM5 |
| `libraries/ResourceManagement/include/Memory/HostBudgetManager.h` | 306 | RM3 |
| `libraries/ResourceManagement/include/Memory/ResourceBudgetManager.h` | 156 | RM4 |
| `libraries/ResourceManagement/include/Memory/StagingBufferPool.h` | 243,254 | RM7/RM8 |
| `libraries/ResourceManagement/include/Memory/VMAAllocator.h` | 120 | RM6 |
| `libraries/ResourceManagement/include/Updates/BatchedUpdater.h` | 205 | RM2 |
| `libraries/ResourceManagement/src/Memory/BatchedUploader.cpp` | 19,118,160,177,213,231,290,311,325,385,406,423,424,474,475,525,546,570,575 | RM9/RM10/RM11/RM12; VK2 borrowed |
| `libraries/ResourceManagement/src/Memory/BudgetBridge.cpp` | 73,93,131,166 | RM13 |
| `libraries/ResourceManagement/src/Memory/DirectAllocator.cpp` | 21,134,174,293,328,370,397,418,443,460,474,479,539,613,660 | RM5 |
| `libraries/ResourceManagement/src/Memory/HostBudgetManager.cpp` | 119,194 | RM3 |
| `libraries/ResourceManagement/src/Memory/ResourceBudgetManager.cpp` | 16,21,26,35,62,72,85,94,102,110,119,125,131,148,173,188,240,248,256 | RM4 |
| `libraries/ResourceManagement/src/Memory/StagingBufferPool.cpp` | 51,117,150,180,189,210,241,254,302,327,351,408,431,440,449,470 | RM7/RM8 |
| `libraries/ResourceManagement/src/Memory/VMAAllocator.cpp` | 58,153,199,298,332,373,398,458,463,493,577,628 | RM6 |
| `libraries/ResourceManagement/src/Updates/BatchedUpdater.cpp` | 31,47,57,62,75,92,146,151,162 | RM2 |
| `libraries/SVO/include/BulkMaterialization.h` | 120,121 | SVO1/SVO2 |
| `libraries/SVO/include/LaineKarrasOctree.h` | 330 | SVO3 |
| `libraries/SVO/include/SceneGenerator.h` | 475 | SVO4 |
| `libraries/SVO/src/BulkMaterialization.cpp` | 151,169,173,245,259,269,274 | SVO1/SVO2 |
| `libraries/SVO/src/SVORebuild.cpp` | 231,768,833 | SVO3 |
| `libraries/SVO/src/SceneGenerator.cpp` | 124,125,164,202,216 | SVO4 |
| `libraries/ShaderManagement/include/AsyncShaderBundleBuilder.h` | 293,302,307 | SH3/SH4/SH5 |
| `libraries/ShaderManagement/include/SdiRegistryManager.h` | 266 | SH8 |
| `libraries/ShaderManagement/include/ShaderCacheManager.h` | 178 | SH1 |
| `libraries/ShaderManagement/include/ShaderLibrary.h` | 246 | SH2 |
| `libraries/ShaderManagement/include/ShaderLogger.h` | 172,209,273 | SH6 |
| `libraries/ShaderManagement/src/AsyncShaderBundleBuilder.cpp` | 51,63,81,106,129,135,148,172,183,201,216,222,248,272 | SH3/SH4/SH5 |
| `libraries/ShaderManagement/src/SdiRegistryManager.cpp` | 79,129,162,169,182,217,232,247,258,265,438,443,472,496,521 | SH8 |
| `libraries/ShaderManagement/src/ShaderCacheManager.cpp` | 39,108,149,160,178,196,245,250,255,264,269,277 | SH1 |
| `libraries/ShaderManagement/src/ShaderCompiler.cpp` | 15,26 | SH7 |
| `libraries/ShaderManagement/src/ShaderLibrary.cpp` | 41,71,97,124,129,135,141,167,191,212,223,229,242,257,270,282,298,311,316,321,351,391,413,418,423 | SH2 |
| `libraries/VulkanResources/include/CommandBufferUtility.h` | 43 | VK2 borrowed |
| `libraries/VulkanResources/include/VulkanDevice.h` | 112,438,439 | VK1/VK2 |
| `libraries/VulkanResources/src/CommandBufferUtility.cpp` | 98,104,106 | VK2 borrowed |
| `libraries/VulkanResources/src/TextureHandling/Loading/TextureLoader.cpp` | 171 | VK2 borrowed |
| `libraries/VulkanResources/src/VulkanDevice.cpp` | 388,389,392 | VK1/VK2 |
| `application/main/source/graph/BuildRenderGraph.cpp` | 5368,5382,5575 | APP1 |

### Reproduce the host file census

Run from the engine repository root. This is an artifact/source inspection, not a product build or test. The lexical census is a discovery/checking aid; the per-object classification comes from the source review and alias index, not this regular expression. GPU1 is independently indexed in its supplemental row.

```python
from pathlib import Path
import re
from collections import Counter

extensions = {'.h', '.hpp', '.hxx', '.cpp', '.c', '.cc', '.cxx', '.inl', '.ipp', '.ixx'}
lex = re.compile(r'//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'')
mutex = re.compile(r'\bstd::(?:mutex|recursive_mutex|shared_mutex|shared_timed_mutex|'
                   r'timed_mutex|lock_guard|scoped_lock|unique_lock|shared_lock)\b')
counts, atomic_only, scanned = Counter(), [], 0
for base in (Path('VIXEN/libraries'), Path('VIXEN/application')):
    for path in sorted(base.rglob('*')):
        if path.suffix not in extensions or 'tests' in path.parts:
            continue
        scanned += 1
        source = lex.sub(lambda match: '\n' * match[0].count('\n'), path.read_text())
        if mutex.search(source):
            scope = path.parts[2] if path.parts[1] == 'libraries' else 'application'
            counts[scope] += 1
        elif re.search(r'\bstd::atomic(?:\b|_)', source):
            atomic_only.append(str(path))
print(scanned, sum(counts.values()), dict(sorted(counts.items())), len(atomic_only))
```

At the audited base: **785 scanned, 98 mutex-syntax files, 10 atomic-only files**, with subsystem counts matching the summary. Document validation also reconciled 47 host IDs/classes plus GPU1, the 98-file alias index, referenced file/line bounds and the doc-only diff. Independent source review corrected input tick order, a live light-cut setter, SDI call direction and two shader/cache citations before commit. No code generation, build, test, benchmark or runtime contention result is claimed.
