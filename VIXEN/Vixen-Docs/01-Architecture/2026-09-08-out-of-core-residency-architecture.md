# Out-of-core residency: residency as a dispatch dimension (one federation architecture)

Date: 2026-09-08. Lane: `oocaudit` (branch `ooc-audit` off engine main `d0646590`, whose tip is the
runtime lock inventory). Status: AUDIT + DESIGN — doc-only, no source changed, nothing minted (`0cz`).
Owner direction honored: bare-minimum hot working set, everything else cold-persisted, load/unload as
off-tick parallel tasks, one unified design across undertow + kernel + VIXEN, CPU-first,
budget-parameterized so one architecture serves a 4 GB and a 64 GB box.

Evidence is MEASURED from this checkout (`VIXEN/…` paths), the kernel checkout
`/home/liory/Github/Yeroket-Fantasy` and its `waveassembler`/`nscopelevels` lane worktrees, and the
undertow checkout `/home/liory/Github/undertow` and its `nscopeapi`/`deltatranspile` lane worktrees.
The wave-assembler, N-scope and delta-transpilation designs are unratified inputs and are cited as such.
Lock classes (P/D/O/C/G) and family ids (C1, RM4, SVO3, …) are the inventory's
(`Vixen-Docs/01-Architecture/2026-09-08-runtime-lock-inventory-and-classification.md`).

---

## 0. Headline

**Out-of-core is not a new subsystem. It is a fourth column on the rows the dispatch already
schedules.** The wave assembler decides, every tick and off the critical path, WHICH rows run at N+1
(`active`) and HOW BIG they are (`itemCount`). Residency adds WHERE THEIR BYTES ARE — and that
answer is derivable from the same input: a row can only run over data that is hot, and data is
only worth keeping hot while some row that runs over it is active or about to be. Everything else
follows:

| Question | Answer, in the vocabulary that already exists |
|---|---|
| What is the working set? | The union of the declared access manifests of the rows in the assembler's active set for N+1, restricted to their active scopes, plus a declared prefetch horizon (§3). |
| What is the residency unit? | The N-scope **scope address** (undertow rows) / the **`TierAddress`** region (VIXEN payloads) — the same `(scopeAddress, rowKey)` key the delta envelope reserves (delta audit §4, Q2). |
| Where do load/unload run? | On the assembler's host arena, as tasks spawned at step 1 of its frame protocol and joined by a plain generation flip at the boundary; never as a wait the executor can observe (§4). |
| What keeps a mid-load reference safe? | Generation-stamped page ranges with fence-retired reclaim — VIXEN already has the CPU ledger (`WholesaleCapacityArena`) and the two-slot GPU pattern (shell cache); the assembler's `slots[2]` is the same shape (§4.3). |
| What is cold storage? | The sealed per-scope snapshot plus its delta tail — the same seal the assembler reads as `DeltaSealSummary`, persisted through the existing addressed-node codec widened by the scope address (§5). |
| What is the budget knob? | Per-tier byte budgets bound to the existing `ResourceBudgetManager` rows; eviction walks a declared ladder (hot→warm→cold→virtual) with a per-kind policy; admission reserves before it loads; a scope that cannot be admitted is not "missing", it runs at its coarser level (§6). |
| Where is the manager? | There is none. A per-declaration residency row (constexpr, emitted beside the dispatch candidate) + a per-scope `ResidencyState` (the `WholesaleAvailability` struct generalized) + two emitted hooks (`Seal`, `Reconstruct`) composed by the assembler's boundary step (§2.4). |

Two corrections to the brief's framing, stated up front because the design depends on them:

1. **A load never blocks the boundary.** The assembler's overrun policy (its §4.4, "wait") is right
   for assembly (microseconds of bit-ops) and wrong for I/O. A scope whose load has not completed
   at the boundary is simply **not active at N+1** — `activeNext = demanded ∩ ready` — and, because
   N-scope declares `OnMissing = Downgrade`, it runs at its coarser level instead. This is the
   `Delayed` feedback class the assembler already assigns to activation changes, applied to bytes.
2. **Render residency and sim residency are two instances of one state machine, kept apart by
   ruling R4** (`undertow/docs/design/2026-08-31-bridge-provisions.md:45`, "voxel residency is
   never an input to simulation"). VIXEN's resident payloads are an evictable cache over sim truth
   (the lazy-procedural doc's standing rule); undertow's resident scopes decide which LEVEL runs, a
   declared, deterministic function of demand records and budget. Neither side ever exposes an
   "is it loaded?" bit to a system body (§7.3, owner decision D9).

---

## 1. Current state (MEASURED) — what streams, what is hardcoded-resident, where the tick stalls

### 1.1 VIXEN: a bandwidth-reduction system, not an out-of-core system

There is **no per-region streaming, no eviction and no I/O thread anywhere in the engine**. What
ships is a per-tree *binary* residency gate over a fully CPU-resident, whole-pool-built octree:

| Payload | Today | Evidence |
|---|---|---|
| Octree nodes, materials, config | Fully resident CPU + GPU, uploaded whole at Compile via raw `vkMapMemory`/`memcpy` | `libraries/SVO/include/ResidencyDefault.h:13-18` ("channelPool, nodes, mips, lookup tables, and shell-cache slots still upload whole at Compile — their laziness is a future increment's paged pool"); `Vixen-Docs/Deep-Field-Residency-Unification-2026-08.md:49-62` |
| Brick pool (`concatenated_.bricks`) | Whole-pool binary gate: one bool per tree, async upload, **de-residency is a documented no-op** | `libraries/RenderGraph/src/Nodes/BodyOctreeSceneNode.cpp:1408-1417` ("stop requesting is sufficient; there is no brick data to un-write") |
| channelPool + brickLookup, mipPool, tierRefTable, occupancyGrid | Wholesale admission S1–S5 shipped: demand-classified, hysteresis, ready-bit publication; **demotion clears the ready bit and retains bytes** (no reclaim) | `libraries/SVO/include/WholesaleAvailability.h:19-24` (`desiredRegime, committedRegime, generation, pendingMask, readyMask`), `:63-98` (`AdvanceWholesaleAvailability` 2-up/4-down hysteresis, `PublishWholesaleReady`); `Vixen-Docs/Deep-Field-Wholesale-Admission-2026-08.md:79-82`; undertow `docs/design/TDD.md:668` (−98.6% boot transfer) |
| Shell cache | Two distinct GPU slots, partial dirty-brick revalidate per frame into the write slot | `BodyOctreeSceneNode.cpp:537-566` |
| CPU-side `ConcatenatedOctrees` vectors | **Fully resident for process lifetime** — the pool the GPU gates are a cache of is itself never paged | `libraries/SVO/include/ShellOctreeGpu.h:404-425` (`nodes, bricks, materials, channelPool, brickGridLookup, mipPool, mipAnisoPool` as `std::vector<uint8_t>`) |
| GaiaVoxelWorld | One ECS entity per voxel, one `unordered_map` index, an unbounded never-evicted block-query cache | `libraries/GaiaVoxelWorld/src/GaiaVoxelWorld.cpp:28`; `include/GaiaVoxelWorld.h:604` |
| CashSystem entries (18 cachers) | Fully resident, no LRU, no byte budget; budget explicitly delegated away and dropped | `libraries/CashSystem/include/TypedCacher.h:66-168`, `:252-256`; `include/MainCacher.h:601` |
| Persistent GPU caches | Disk (`cache/devices/<id>/*.bin`, magic+version, reject-and-regenerate), loaded once at `GeneratePipelines` — `LoadAllAsync` **immediately `.get()`'d** | `libraries/RenderGraph/src/Core/RenderGraph.cpp:1302-1304`; `libraries/CashSystem/src/VoxelSceneCacher.cpp:194-297` |
| `SVOStreaming.h` (`ISVOStreamingManager`, `StreamingConfig{maxResidentBricks=4096, maxGPUMemoryMB=512, persistentLevels=3, ioThreads=4}`, LRU config) | **Header only — no `.cpp`, no CMake entry, no caller.** A designed-but-unbuilt *manager* | `libraries/SVO/include/SVOStreaming.h:17-38`, `:98-125`, `:139-244`; confirmed dead by `libraries/SVO/include/FrustumCull.h:6-8` and `Deep-Field-Residency-Unification-2026-08.md:237-244` |
| `WholesaleCapacityArena` | CPU-only free-list ledger with `Acquire / Retire(range, safeFrame) / Reclaim(completedFrame)`; no GPU backing yet ("the renderer *may* use the same offsets when this ledger *is* backed by segmented Vulkan buffers") | `libraries/SVO/include/WholesaleCapacityArena.h:11-14`, `:18-49` |

**Where residency demand is computed today:** `VulkanGraphApplication::UpdateBodySceneResidency`
(`application/main/source/VulkanGraphApplication.cpp:4190`), on the main thread every tick the camera
moves (`:4245-4251` change-detect), per instance: padded frustum (`libraries/SVO/include/FrustumCull.h:38`
`kResidencyFrustumHysteresisDeg = 5.0f`) → `ClassifyCellFootprintRegime == Surface`
(`libraries/SVO/include/ResidencyTrigger.h:60-84`) → occlusion by resident trees → **OR-reduced into one
bool** → `bodyScene->RequestBrickResidency(anyInstanceWantsBricks)` (`:4354`). The request is stashed
and serviced inside `ExecuteImpl` (`BodyOctreeSceneNode.cpp:497-500`); the upload is queued, not
waited (`UploadBrickPool`, `:1424-1437`: `device->Upload(...); device->FlushUploads();`), and a
two-phase non-blocking poll advances `brickResident` only once the bytes are visible
(`PollBrickUploadCompletion`, `:1515-1572`). **This queue-then-poll-then-publish state machine is the
proof the anti-stutter shape already works in this engine; the design below generalizes it from
one bool per tree to one row per (kind, scope).**

**On-tick stall sites (the stutter risks), ranked.** All on the frame thread:

| # | Site | Evidence | What it is under §4 |
|---|---|---|---|
| 1 | `ShellRevalidateNode::ExecuteImpl` → `vkQueueWaitIdle` on every Execute | `libraries/RenderGraph/src/Nodes/ShellRevalidateNode.cpp:384` (holds VK2 across the idle wait) | A hot→hot in-place revalidate that must become a generation write into the back slot + timeline poll (the shell cache already has two slots) |
| 2 | `Rematerialize()` → `vkDeviceWaitIdle` + full `EnsureOctreesBuilt()` re-bake + re-concat, entered from `ExecuteImpl` on any recipe edit | `BodyOctreeSceneNode.cpp:487-495`, `:1248-1258` | An unload(gen g) + load(gen g+1) pair with a flip; the device wait exists only because the buffers are reused in place |
| 3 | `EnsureRtQueryTlasBuilt` → `vkQueueWaitIdle`, re-fired whenever `(instanceCount_, concatenated_.count)` changes | `BodyOctreeSceneNode.cpp:1697`, called from `ExecuteImpl:643` | A derived-payload load task (AS is "residency-proportional", `undertow/docs/design/2026-09-02-rtperf.md:359`) |
| 4 | `EnsureRingAllocated` → `vkDeviceWaitIdle` on capacity overflow | `BodyOctreeSceneNode.cpp:1221` | Budget exhaustion handled by a synchronous resize — the "resize-as-last-resort" the lazy-procedural doc accepts (`§4.3`); under §6 admission it is a demotion, not a wait |
| 5 | `VoxelSelectionProviderNode::ExecuteImpl` → `vkWaitForFences(…, UINT64_MAX)` | `VoxelSelectionProviderNode.cpp:337` | Readback poll |
| 6 | `TypedCacher::GetOrCreate` miss runs `Create(ci)` **on the calling thread**; `VoxelSceneCacher::Create` ends in `m_device->WaitAllUploads()` | `libraries/CashSystem/include/TypedCacher.h:108-118`; `src/VoxelSceneCacher.cpp:936` | A cold→hot asset load executed synchronously at the demand site (§6.4) |
| 7 | `UpdateBodySceneResidency` per-instance loop + `SortInstancesFrontToBack` | `VulkanGraphApplication.cpp:4300-4362` | Demand evaluation that belongs in the assembler's boundary step 6 (O(changed), sealed inputs) |

Disk I/O that is verified **off** the frame thread today: `MainCacher::LoadAllAsync` at graph
generation (`RenderGraph.cpp:1302`), bake-artifact load/store inside `BuildRenderGraph()`
(`application/main/source/graph/BuildRenderGraph.cpp:5484`, `:5701`), SPIR-V reads at Compile.
Init-time cost, not tick cost.

**Threading substrate:** TBB is used for *width inside a blocking call*, never for offload —
`libraries/SVO/src/BulkMaterialization.cpp:196-206` (`task_arena` + `parallel_for`, caller blocks),
`SVOBuilder.cpp:244`; the kernel executor builds a **per-run** `tbb::task_arena(workerCount)`
(`libraries/KernelDispatch/src/TaskExecutor.cpp:84`). The only persistent worker pool is
`AsyncShaderBundleBuilder` (`libraries/ShaderManagement/src/AsyncShaderBundleBuilder.cpp:14`), unwired
in the app. `BatchedUploader` is a real async transfer path (timeline semaphores, `maxPendingBytes=64MB`,
`flushDeadline=16ms`, `libraries/ResourceManagement/include/Memory/BatchedUploader.h:118-124`) and
supports offset uploads that no call site uses (lazy-procedural §3, "every call site passes offset 0").

**Budget substrate:** `ResourceBudgetManager` is accounting-only — `SetBudget / TryAllocate /
RecordAllocation / GetAvailableBytes / IsOverBudget / IsNearWarningThreshold /
DetectHostMemoryBytes / DetectDeviceMemoryBytes` (`libraries/ResourceManagement/include/Memory/ResourceBudgetManager.h:110-159`);
**no `Evict`/`Trim`/`MakeRoom` exists in the library**; the lock inventory notes `TryAllocate` does
not reserve before later accounting (RM4, `:263`). `HostBudgetManager` defaults
`heapBudget = 256 MB, strictHeapBudget = false` (`HostBudgetManager.h:89-95`).

### 1.2 Kernel + undertow: manifests without a residency column; save is whole-world and synchronous

- **`RuntimeState` save/load is the only hot↔cold codepath, and it is whole-world, synchronous,
  and unaddressed by space.** `UndertowSim.SaveCampaign()` (`core/src/Undertow.Sim/UndertowSim.cs:4021-4031`)
  gates on `DataPipes.IsSafeCheckpoint` (`PIPE010`), writes one `MemoryStream` through
  `AddressedBinaryNodeCodec.Write` (`core/src/Undertow.Substrate/RuntimeStateNodeCodec.cs:26`):
  `{ id, version, length, payload }` per node, membership/order/version from the generated
  `RuntimeStateNodeManifest`; header mismatch is **reject-not-migrate** (`:57-58`). Payload policies
  `stack-hash | campaign-seed | substrate-snapshot | agent-knowledge | owned-slice`
  (`UndertowSim.cs:4033-4055`); `owned-slice` dispatches to the owning `SystemDescriptor.Save`.
  The address is `(format, node id, owner system id)` — a logical component, never a scope. **This
  IS the seed of the persistence contract (§5): its node framing, its per-owner slice dispatch and
  its seed-regen-t₀-plus-sparse-override shape are exactly a per-scope seal, minus the scope key.**
- **`DataPipe` already has the seal and the delta-only snapshot.** `SealAtBarrier()`
  (`core/src/Undertow.Substrate/DataPipe.cs:324`) merges private per-producer segments into canonical
  `(epoch, publisherOrdinal, stableWorkKey, localSequence)` order; `PipeSavePolicy.OutstandingOnly`
  (`:25`) snapshots only un-drained items (`SnapshotAtOrAfter`, `:354-356`) — a delta save. The
  `DataPipeManifest` `savePolicy` field is the one manifest-level persistence hook (kernel
  `SourceGenerator~/Transpiler/DataPipeManifestEmitter.cs:217-301`).
- **`GaiaFieldAccessManifest`** (`Transpiler/GaiaFieldAccessManifest.cs:10-42`): per body
  `Accesses[] {origin, kind, field, read, write}` + `ReachedResources[]` + fail-closed `Fallback`,
  committed by undertow as JSON and re-parsed by the kernel emitter — the existing subset+defaults
  handshake. **It is the working-set oracle: it already names every `(kind, field)` a row touches.**
- **No manifest carries a scope/level/chunk address.** N-scope's `ScopeAddress` is design-only
  (`undertow/docs/design/TDD.md:1024`), except that the kernel now owns `ScopeLevelAttribute`
  (`Scope, Representation, Clock`) with byte-identical plan emission at S1
  (`Yeroket-Fantasy/.claude-worktrees/nscopelevels/docs/design/2026-09-08-nscopelevels-s1-kernel-proof.md:13-25`,
  `:69-76`).
- **The word "residency" already has two undertow meanings that must not be conflated:** render
  residency (TDD `:667` "targeted residency, not a general world streamer"; `:741` "a general
  chunk/multi-volume lifecycle (stream, evict, recover, budget across a world) does not [exist]")
  and the shard-federation `ProducerWorldReplica` "distinct from its leased execution working set"
  (`:896`) — the second is precisely the hot/cold split this design instantiates. `EvictionKickRow` /
  `core:eviction-requests` is tenant eviction (an economy pipe), not memory eviction.
- **Precedent for budgeted fixed-capacity residency with lazy reclaim:** the photon world-cell cache
  (`undertow/docs/design/2026-09-02-photon-cells.md:124-185`, D3/D4): compile-time capacity pair,
  fail-soft overflow ("drops the deposit — never a stall or a corrupt merge"), age-based lazy
  reclaim at claim time with a config knob (`kPhotonCellMaxAge = 1024` generations), on-demand full
  clear "never per-frame". That is the eviction discipline §6 adopts.

### 1.3 The three designs this composes with (unratified; read from their lane worktrees)

- **Wave assembler** (`Yeroket-Fantasy/.claude-worktrees/waveassembler/docs/design/2026-09-08-wave-assembler-dispatch.md`):
  `AssembleWaves(WaveAssemblyInput{forEpoch, active, seal, hooks}) → WaveSchedule` (§2.3); the frame
  protocol `spawn → run → seal → join → bind → active → flip` over `slots[2]` on a host
  `tbb::task_arena(1)` (§4.2, §5.2); activation reads sealed summaries + demand records only (§4.3,
  D8); per-declaration `BindWaveRow` hooks composed by an emitted switch, no registry (§2.5).
- **N-scope** (`undertow/.claude-worktrees/nscopeapi/docs/design/2026-09-08-n-level-scope-scale-dispatch-api.md`):
  `Level = (ScopeKind, Representation, Clock)`; the per-tick active set `{(level, scope) |
  L.Activate(scope) ∨ demanded} \ budgetDemotions` (§5.3); `OnMissing = Fallback.Downgrade` (§3.1);
  chain-disaggregation antidote — activating one fine scope never forces its siblings fine (§2);
  manifest handshake = subset-with-defaults for data, EXACT for the path (§6.3).
- **Delta-set transpilation** (`undertow/.claude-worktrees/deltatranspile/docs/design/2026-09-07-delta-set-transpilation-audit.md`):
  per-write-field base views, "deliberately no single base-state buffer for the world" (§2.1);
  delta payload = pipe payload with the canonical ordering tail (§2.2); envelope key reserved as
  `(scopeAddress, rowKey)` (§4, Q2); the `OutstandingOnly` snapshot IS the delta log the networking
  specs need (§6); D2 persistence deltas are "a related cousin" (§0) — **this document is where D1
  and D2 meet: the sealed dispatch delta is the persistence delta.**
- **Lock inventory** (this base): CACHE/MEMO = "publish immutable results; invalidation is an epoch
  change"; PARTITION = per-slot ownership; the assembler's join-then-flip is those two classes with
  no mutex. CashSystem C1/C3/C4, SVO3, RM4, RM7–RM12 are the families residency touches (§7.2).
- **Lazy-procedural delta baseline** (`Vixen-Docs/01-Architecture/Lazy-Procedural-Delta-Baseline-Design-2026-07.md`):
  the region ladder Virtual → Materialized → Coarse (§2, `:139-145`); "GPU residency is always a
  cache over persistent artifacts … persistent artifacts are (recipes, deltas, voxel assets)"
  (`:190-195`); paged pool with fixed reserved capacity, evict-first/resize-last (§4.3); keyed
  residency generalizing `RequestBrickResidency(bool)` (§4.3); topology-self-sufficient,
  producer-versioned delta records (§4.4); undertow's delta log authoritative for sim divergence,
  VIXEN's materialized result "evictable cache, never the persisted delta" (`:172-175`, `:484-490`).

---

## 2. The residency model — tiers as a declared dimension, no manager

### 2.1 Four tiers, one ladder

| Tier | Meaning | Re-arm cost | VIXEN instance today | undertow instance |
|---|---|---|---|---|
| **T0 HOT** | Bound into the tick's working set: readable/writable by an active row (CPU) / by a shader with its ready bit set (GPU) | — | `readyMask` bit set; `brickResident=1` | rows of an active `(level, scope)` |
| **T1 WARM** | Bytes retained in RAM/VRAM but **not bound**: no row may read them, no I/O needed to re-arm | a flag flip at a boundary | the demoted-but-retained payload (`reusable_populated_bytes`, wholesale-admission `:126-127`); CPU `ConcatenatedOctrees` slices with no ready bit | rows of a scope demoted this tick, still in Gaia chunks |
| **T2 COLD** | Persisted (disk / network / cache dir); bytes not in memory | I/O + reconstruct (a load task) | `cache/devices/<id>/*.bin`; bake artifacts; a materialized-delta journal (lazy-procedural §4.4, unbuilt) | the per-scope seal (§5): owned-slice payloads + delta tail |
| **T3 VIRTUAL** | Nothing stored: **derivable** from base (seed / recipe / instructions) + the authoritative delta log | compute (a producer task) | the lazy-procedural "Virtual (instructions-only)" state; `PROVIDER_PROCEDURAL` | `campaign-seed` regeneration at t₀ + sparse override slices (the shipped deltas-only save) |

The ladder is ordered by re-arm cost. **A kind declares which tiers it supports** (a stored voxel
asset has no T3; a pure-procedural region has no T2 of its own — its T2 is the recipe's; a node/
material/config payload is pinned at T0 as the "minimum correctness root", wholesale-admission
`:104`) and **which transitions are legal**. Eviction always walks *down* the ladder one step at a
time (§6.2); a load may skip up (COLD→HOT) but publishes only at the boundary (§4).

The persistent root levels of `SVOStreaming.h`'s sketch (`persistentLevels = 3`) and ESVO's
always-resident top levels survive as a declaration, not a manager field: the **coarsest level of
every N-scope scope kind is T0-pinned**. That is what lets a demoted scope keep running — the coarse
aggregate is always hot, and R4's "the mine keeps producing because the coarse economy ran it" is a
statement about tier pinning.

### 2.2 The residency predicate (derived, not hand-managed)

For a kind `k` and a scope `s`:

```
demanded(k, s, N+1)  :=  ∃ row r ∈ active(N+1) ∪ prefetch(N+1) :
                          k ∈ manifest(r).kinds  ∧  s ∈ scopes(r)          -- §3
desiredTier(k, s)    :=  T0  if demanded
                         max(T1, k.floorTier)   otherwise, subject to budget (§6)
```

`manifest(r)` is the row's `GaiaFieldAccessManifest` body (undertow) or its payload manifest
(VIXEN: the `WholesaleAvailability` payload mask per octree — `channelPool|brickLookup`, `mipPool`,
`tierRefTable`, `occupancyGrid`, `bricks` — wholesale-admission `:98-105`). Nothing in the predicate
is authored per scope. There is no "residency system"; there is a column computed at the same
boundary step that computes `activeNext`.

### 2.3 The per-scope state: `ResidencyState` = `WholesaleAvailability` generalized

`WholesaleAvailability` (`libraries/SVO/include/WholesaleAvailability.h:19-24`) is already the right
struct; it is merely keyed by octree index and limited to two regimes. Generalized per `(kind, scope)`:

```cpp
struct ResidencyState {                     // POD; one per (kind, scopeAddress); owned by the boundary step
    Tier      desired, committed;           // the ladder position wanted / published
    uint32_t  generation;                   // bumps on every committed transition (the handle epoch)
    uint32_t  pendingMask, readyMask;       // per-payload in-flight / bound (unchanged from today)
    uint16_t  demandedFrames, idleFrames;   // hysteresis counters (2-up / 4-down today, :72,:85)
    uint32_t  lastDemandedGen;              // age input for eviction (§6.3)
    PageRange range[kPayloads];             // arena ranges (CPU ledger + GPU segment), generation-stamped
};
```

`AdvanceWholesaleAvailability` (`:63-93`) already implements the hysteresis and the
`pending→ready` protocol; `PublishWholesaleReady` (`:95-98`) is the publication; the FNV signature
(`:102-116`) is the parity witness. **This design changes the key and the tier set, not the
protocol.** The struct is arena-owned and touched by exactly one owner (the boundary step) — the
PARTITION lock class; shaders/readers see the immutable `readyMask` mirror per frame slot — the
CACHE/MEMO class.

### 2.4 The declaration — a residency row beside each dispatch candidate (compile-time composed)

The assembler emits `kNativeDispatchCandidates[]` from declarations. Residency adds one constexpr
table keyed by **kind**, emitted from the same run, with two optional per-kind hooks composed by an
emitted switch exactly like `BindWaveRow`:

```cpp
struct ResidencyRow {
    const char*  kind;             // the schema kind / VIXEN payload kind
    TierMask     tiers;            // which of T0..T3 this kind supports
    Tier         floorTier;        // never evicted below this (T0 = pinned; e.g. nodes/config, coarsest level)
    BudgetClass  budget;           // which ResourceBudgetManager row it draws from (host, device, staging)
    EvictPolicy  evict;            // Age | DistanceFromActive | CostBenefit  (§6.3)
    ColdFormat   cold;             // SealedSnapshotPlusDeltaTail | DeltaTailOnly | ExternalArtifact | None
    uint8_t      prefetchHorizon;  // frames of lookahead (0 = boundary only)
};
inline constexpr ResidencyRow kResidencyRows[kResidencyKindCount] = { … };

// Emitted iff the kind has a cold or virtual tier. Pure over immutable input; run on the host arena.
inline void <Kind>Seal(const ScopeView& hot, SealSink& out);           // HOT/WARM → COLD bytes
inline void <Kind>Reconstruct(const ColdSource& in, ScopeSlot& back);   // COLD/VIRTUAL → back slot (HOT at flip)
```

For undertow kinds `Seal` is the existing `owned-slice` `SystemDescriptor.Save` restricted to one
scope; `Reconstruct` is `Load` + replay of the scope's delta tail (§5.2). For VIXEN payload kinds
`Reconstruct` is the lazy-procedural **producer** (`(recipe, region address, LOD, delta chain) →
subtree + bricks`, its §2, `:183-188`) or an artifact read; `Seal` is the materialized-delta
journal write for authored render deltas only (§5.4). Nothing is registered at runtime; adding a
kind adds a row and (optionally) two hooks from the same codegen run. **Spelling of the row is an
owner decision (D1) — `0cz` forbids minting here.**

What this dissolves: `ISVOStreamingManager` (`SVOStreaming.h:139-244`) — a manager with
`requestLoad/evictBrick/flush/saveCacheToDisk` methods and its own config — is replaced by the row +
the boundary step; the header should be deleted, not revived (D6).

---

## 3. The working-set contract — derived from the active set

```
workingSet(N+1) := ⋃_{r ∈ active(N+1)} manifest(r) × scopes(r, N+1)          -- must be T0 at N+1
prefetch(N+1)   := ⋃_{r ∈ candidates} manifest(r) × horizonScopes(r, N+1)    -- may be loading
```

- `active(N+1)` is the assembler's `activeNext` (its §4.2 step 6; N-scope §5.3), evaluated from
  sealed summaries + demand records only (assembler D8) — never from live components and never from
  "is it loaded" (R4).
- `scopes(r, N+1)` is the scope set of the active `(level, scope)` pairs `r` participates in
  (N-scope: the level's scope kind instances; VIXEN: the instances classified by
  `UpdateBodySceneResidency`, moved into this step — §1.1 site 7).
- **`horizonScopes`** is the prefetch set: scopes the declared `prefetchHorizon` predicts will be
  demanded within h frames. Sources are declared per kind and are all sealed-data or demand-record
  reads: (i) N-scope demand records (`SimulationDemand{Scope, Capability, MinimumResolution}`, its
  §5.3 — the presentation raising a level is the only allowed hidden-clock-free source), (ii) the
  scope tree's adjacency (siblings/parent of an active scope at distance ≤ h), (iii) VIXEN's camera
  analytics extrapolated by velocity — the same `ClassifyCellFootprintRegime` with the camera
  advanced h frames (the lazy-procedural §4.3 "v1 stays CPU-side camera analytics"). The GPU
  ray-observed request buffer stays the recorded v2 candidate it already is.
- **The invariant the executor relies on:** `active(N+1) ⊆ ready(N+1)` — established by the boundary
  rule `activeNext = EvaluateActiveSet(seal, demand) ∩ ready`, where `ready` is the set of scopes
  whose `ResidencyState.committed == T0` after this boundary's flips (§4.2 step 6′). A scope that is
  demanded but not ready is in `prefetch` (its load is in flight) and, if its level declares
  `OnMissing = Downgrade`, its coarser level runs for it. Because the coarsest level is T0-pinned
  (§2.1), there is always a level that can run: **the sim never waits for bytes, it runs coarser.**
- **Corollary (writes only through active rows):** a scope not in `active(N)` has no writer at N, so
  its T0/T1 bytes are **immutable during N**. That is what makes the off-tick seal (§4.2 step 1b)
  safe without a lock: it reads data no one is writing — the CACHE/MEMO class by construction.

---

## 4. Off-tick load/unload — the anti-stutter contract (riding the assembler's protocol)

### 4.1 Extension of the assembler's frame protocol

The assembler's `§4.2` protocol gains two spawned task classes at step 1 and one intersection at
step 6. Nothing is added to the executor; nothing is added on the path except the intersection and
the flips.

```
state (per kind, per scope): ResidencyState rs;            // owned by the boundary step
       Arena pages;                                         // CPU ledger (WholesaleCapacityArena shape) + GPU segments
       ResidencyPlan resPlan[2];                            // ping-pong list of transitions, immutable once published

frame N:
  1.  spawn assembly(N+1)                                   (unchanged)
  1a. spawn LOADS for  s ∈ demanded(N+1) ∪ prefetch(N+1) with rs.committed > T0, admitted by budget (§6.1):
        task: acquire back range (pages.Acquire) → <Kind>Reconstruct(coldSource, back)   -- I/O + compute, host arena
              → for GPU kinds: BatchedUploader::Upload(offset) + FlushUploads (never WaitAllUploads)
              → mark rs.pendingMask; completion observed by IsUploadComplete / task join, NOT by waiting
  1b. spawn UNLOADS for s with rs.committed == T1 and evictable (§6.2), one ladder step:
        T1→T2: task: <Kind>Seal(hot view, sink)   -- reads immutable bytes (§3 corollary); writes cold; on completion pages.Retire(range, safeFrame=N)
        T2→T3: drop the cold artifact only if the kind's base+log can regenerate it (declared)
  2.  run  RunAssembledWaves(front)                          (unchanged; reads only T0 ranges of generation ≤ front.forEpoch)
  3.  seal N                                                 (unchanged; the persistence seal is the SAME barrier — §5.1)
  4.  join assembly(N+1)                                     (unchanged: wait; overrun == bug)
  5.  bind counts                                            (unchanged)
  6.  activeNext = EvaluateActiveSet(seal[N], demand)        (unchanged)
  6′. ready     = { s | rs.committed == T0 ∨ (rs.pending complete ∧ upload visible) }   -- O(transitions), a flag scan
      activeNext ∩= ready;  downgrade(activeNext \ ready)    -- Delayed class; never waits
      flip generations for completed loads (rs.committed = T0, ++generation, readyMask = pendingMask)
      flip HOT→WARM for scopes leaving the active/prefetch set (readyMask = 0; bytes retained)
  7.  flip front ^= 1;  pages.Reclaim(completedFrame = last fence/Handle-complete frame)
```

**Latency class.** A demand recorded at boundary N spawns its load during N+1; the earliest the
scope can be active is N+2 (if the load completes within one frame) — the same one-frame `Delayed`
latency the assembler already assigns to activation, plus load time. The prefetch horizon exists so
that for predictable demand the load completes before the demand arrives and the scope is active
the first frame it is wanted. For unpredictable demand the visible behaviour is "coarse for k
frames, then fine" — the lazy-procedural doc's mip-fallback ("render before content exists",
`:221`) and N-scope's downgrade are the same policy on the two sides.

### 4.2 Where the tasks run

The assembler owns a host `tbb::task_arena(1)` for assembly (its §5.2). Loads and seals are I/O-bound
and must not sit behind a single assembly slot, so the host owns a **sibling `tbb::task_arena(k_io)`**
(small `k_io`, e.g. 2–4 — `SVOStreaming.h:113` guessed `ioThreads = 4`) into which 1a/1b are
`enqueue`d. All arenas share one TBB market, so this oversubscribes nothing and adds no worker to
the executor's 1/2/N contract (the assembler's own argument). Blocking file reads inside a TBB task
are acceptable at this concurrency; an async-I/O backend is a later substitution behind the same
`ColdSource` (D8). **No engine change is required**: `BatchedUploader` already accepts uploads from
any thread (`BatchedUploader.h:113-263`), and the ledger is a header-only CPU class.

### 4.3 Double-buffer / handle-swap safety (the ping-pong pattern extended to bytes)

A resident scope is addressed by a **generation-stamped handle** `(scopeAddress, generation)`
resolving to a page range. The rules, each already present somewhere in the tree:

1. **A load writes only a back range** it acquired itself (`pages.Acquire`), never the range a
   reader may hold — the assembler's "no slot is ever shared" and the shell cache's two distinct
   `VkBuffer` slots (`BodyOctreeSceneNode.cpp:541-546`, "disjoint Resource* … no barrier").
2. **Publication is a flip at the boundary**, on the orchestrator thread, after the task's join and
   (for GPU) after `IsUploadComplete` — the `PollBrickUploadCompletion` phase-1/phase-2 discipline
   (`:1522-1567`: bricks visible → then config re-upload → then `PublishWholesaleReady`). The ready
   bit is never set before the bytes are visible.
3. **Old ranges are retired, not freed**: `pages.Retire(range, safeFrame = N)` at the flip;
   `pages.Reclaim(completedFrame)` only when frame N's execution has completed (CPU: the `Handle`
   completion; GPU: the timeline value / frame fence). This is `WholesaleCapacityArena`
   (`WholesaleCapacityArena.h:31-49`) verbatim, and the wholesale-admission "later capacity-reclaim
   slice waits for the last referencing frame fence" (`:82`).
4. **Readers pin a generation, not a pointer**: the executor's stage table binds a scope's range
   for `front.forEpoch` at step 1 of the *previous* frame and never re-resolves mid-wave; a shader
   reads through the per-frame config mirror (`_tailPad`/ready bits re-emitted each Execute,
   `:1543`, `:1570`) — "per-frame values already travel in ring-slot SSBOs" (wholesale-admission `:57-59`).
5. **Fixed reserved capacity**: pool buffers are created once at Compile; a load that cannot be
   admitted is not a resize — it is a demotion (§6.1). Resize survives only as the explicit
   Rematerialize-shaped stall the lazy-procedural doc accepts (§4.3), taken off the steady-state
   path.

Under these rules the four `vkDeviceWaitIdle`/`vkQueueWaitIdle` sites in §1.1 lose their reason to
exist: Rematerialize is an unload(g)+load(g+1) with a flip (rule 1–3); the shell revalidate writes
the back slot and polls (rule 2); the TLAS rebuild is a derived-payload load (rule 1–3); the ring
overflow is an admission failure (rule 5).

### 4.4 Determinism of the pipeline

- **The schedule is unchanged by residency timing except through the active set**, and the active
  set is a pure function of `(seal[N], demand, ready)`. `ready` depends on load completion time —
  wall-clock. To keep D1 (schedule determinism) the boundary records the **residency decision
  sequence** (which scopes were admitted/flipped at each boundary) as part of the fingerprint
  stream; a replay consumes the recorded sequence instead of live completion. This is the same
  move the assembler makes for demand records, and it is the only place wall-clock enters. Owner
  decision D3 states the consequence: a box that loads slower runs coarser for longer, deterministically
  given its recorded sequence; two boxes are not byte-identical to each other, only each to its own
  replay — the scale-to-the-box ruling N-scope already carries.
- **Worker-count invariance**: load/seal tasks produce immutable outputs published by a flip; their
  completion order affects only *when* a scope becomes ready, which the recorded sequence pins.
  The 1/2/N gate holds for the executor exactly as today.

---

## 5. The persistence contract (cold storage)

### 5.1 One seal, two projections

The assembler reads `DeltaSealSummary` from the native seal at step 3. The persistence seal is the
**same barrier**: the canonical merged pipe segments (`SealAtBarrier`) plus the scope's committed row
state. Two projections of one immutable-after-publish object:

- the **summary** (counts, dirty scopes) — feeds `AssembleWaves` and `EvaluateActiveSet`;
- the **payload** (row bytes + outstanding deltas) — feeds `<Kind>Seal` for scopes leaving T1.

No second "compactor" object is introduced. The name `DeterministicCompactor` has no source hits in
either repo (delta archaeology, `undertow/docs/design/2026-09-01-delta-archaeology.md`; the lock
inventory `:50` warns it is unverified); the role it names — "produce the canonical, replayable
record of a scope at a barrier" — is `Seal` above, and is the thing to build, under whatever name
the owner rules.

### 5.2 Cold format — sealed snapshot + delta tail, per scope, in the existing framing

```
ColdRecord(k, s) :=  header{ format, version, kind, scopeAddress, producerVersion, generation }
                   + base      : one of  { SealedSnapshot(rows in stable key order)
                                          | SeedRef(campaign-seed, t₀)            -- T3-capable kinds: regenerate
                                          | ArtifactRef(content hash)             -- stored assets }
                   + deltaTail : the scope's OutstandingOnly-style deltas since `base`, in canonical
                                 (epoch, publisherOrdinal, stableWorkKey, localSequence) order
                   + digest    : FNV/SHA over (base, deltaTail) — the reload-parity witness
```

- **Framing:** `AddressedBinaryNodeCodec` unchanged in shape — `{ id, version, length, payload }`
  per node (`RuntimeStateNodeCodec.cs:28-50`) — with the node id space widened to carry the scope
  address. This is the delta audit's Q2 ("reserve the key field now, populate at slice #3") landing
  in the persistence format; it is ALSO the N-scope F7 rule (scope instance is runtime, never in the
  constexpr plan) — the address lives in the *record*, not the *plan*. Header check stays
  reject-not-migrate (`:57-58`); adding the scope-addressed node kind is a new `format` string, so
  old campaigns are refused loudly, never misread (D4).
- **Whole-world save = fold of scope seals.** `SaveCampaign` becomes: global nodes (`stack-hash`,
  `campaign-seed`, `substrate-snapshot`, `agent-knowledge`) + for every scope, its `ColdRecord` if
  cold, else `Seal` now. Because non-active scopes are immutable during the tick (§3), the per-scope
  seals of cold/warm scopes are **already on disk or already computable off-tick**; only the active
  scopes need sealing at the checkpoint. A save stops being a stop-the-world serialization of
  everything and becomes a stop-the-world of the working set. `PIPE010` (save during dispatch)
  remains the guard.
- **Which base a kind uses is declared (`ColdFormat` in the row):** sim rows with a seed-regenerable
  base declare `DeltaTailOnly` (the shipped deltas-only save generalized per scope); rows without one
  declare `SealedSnapshotPlusDeltaTail`; VIXEN pure-procedural regions declare `None` (their cold
  tier is the recipe program, persisted elsewhere) and materialized authored deltas declare
  `SealedSnapshotPlusDeltaTail` with **topology-self-sufficient, producer-versioned** records
  (lazy-procedural §4.4, `:470-477`) — the base is only per-build deterministic, so the record must
  carry the brick-existence set and the producer version it was cut against.

### 5.3 Reconstruct and the reload-parity gate

```
Reconstruct(ColdRecord) :=  base' = { snapshot | regen(seed, t₀) | loadArtifact(hash) }
                            then fold deltaTail onto base' in canonical order
                            assert digest(base', deltaTail) == record.digest
```

**Gate RP1 (byte parity, Deterministic kinds):** for any scope `s` sealed at boundary N and
reconstructed at boundary M > N with no active row touching `s` in (N, M]:
`bytes(Reconstruct(Seal(s@N))) == bytes(s@N)`, at workers 1/2/N. This is the D2-style row-state
parity the assembler's G2 already asserts, applied across an unload/reload.

**Gate RP2 (coarse continuity):** if `s` was demoted to its coarser level in (N, M] and that level
ran, then `bytes(s@M) == Refine(coarse@M, Reconstruct(Seal(s@N)))` within the transform's declared
tolerance — i.e. reloading a scope and applying the coarse level's committed boundary equals what
N-scope's Refine promises. This is N-scope law 2 (round trip) extended by a persistence hop, and it
is what makes "runs coarser while cold" honest rather than lossy-by-accident.

**Gate RP3 (cross-build for producer-versioned records):** same-build replay cannot expose topology
drift; the lazy-procedural doc requires a cross-build (ideally cross-machine) replay case
(`:476-477`). Adopted as-is for VIXEN materialized-delta kinds.

**Gate RP4 (visible-representation tolerance):** virtual↔materialized transitions are a visible
representation change (lazy-procedural `:147-151`); the gate compares geometry within stated
tolerances, not pixels. Lossy per-row parity (assembler §6.2) is the sim-side twin of this.

### 5.4 Ownership of truth (unchanged rulings, restated so residency cannot erode them)

- undertow's delta log is the authoritative persistent record for sim divergence; VIXEN's
  materialized bake of reified state is evictable cache, never a second source of truth
  (lazy-procedural `:172-175`, `:484-490`). Under this design that means: a VIXEN region whose
  content derives from sim state has `ColdFormat = None` — its reconstruct is "ask the sim's cold
  record + re-produce"; VIXEN persists only *authored* render-side deltas.
- The network cold source is the delta wire: a scope fetched from a peer is `Reconstruct` over a
  `ColdRecord` whose `deltaTail` arrived as the subscription stream (delta audit §6). Same hook,
  different `ColdSource`.

---

## 6. Budget → eviction contract (the knob that makes one architecture fit every box)

### 6.1 Budgets and admission

- **Budgets are per `BudgetClass`** (host heap, device local, staging), one row each in
  `ResourceBudgetManager`, **pre-bound at init** (the inventory's RM4 fix: stable immutable row
  directory, no exclusive lock on the allocation path) and sized from `DetectHostMemoryBytes` /
  `DetectDeviceMemoryBytes` × a declared fraction, overridable by config. The 4 GB box and the 64 GB
  box run the **same tables**; only the numbers differ.
- **Reserve before load.** A load task is spawned only after `TryAllocate(class, bytes)` succeeds
  at the boundary step (strict, reservation semantics — the RM4 admission gap must be closed:
  today the check does not reserve before later accounting, `ResourceBudgetManager.cpp:263`). The
  reservation is released on retire+reclaim, not on demotion.
- **Admission failure is a demotion, never a wait or a resize.** If the working set of
  `activeNext` cannot be admitted, the boundary drops candidates by the declared `OnMissing`
  fallback — the N-scope "budget demotion" (`§4.2`, "if the box cannot afford `(city-plan, C)` this
  tick … drops C to `(city-market, C)`"). On the VIXEN side the equivalent is regime demotion: the
  instance renders from mips (`brickResident==0` → mip fallback) — fail-soft, never a stall, never an
  out-of-range read (wholesale-admission `:93-96`).
- **Hysteresis is declared**, per kind, with today's defaults (promote after 2 consecutive demanded
  frames, demote after 4 — `WholesaleAvailability.h:72,:85`; frustum padding 5°) so the boundary
  step never thrashes at a threshold.

### 6.2 The eviction ladder (one step per boundary, off-tick)

When a class is over budget, or when `demanded ∪ prefetch` needs pages the class cannot admit,
candidates are the resident-but-not-demanded scopes, walked **down one tier per boundary**:

1. **T0 → T1** (free): clear the ready bits at the flip; bytes stay. This alone is what VIXEN's
   demotion does today, and it is always the first step — it makes a later re-promotion a zero-
   transfer flip ("reuse a ready-but-demoted whole payload on re-admission before copying", `:149-150`).
2. **T1 → T2** (a seal task, §4.1 step 1b): only if T1 exceeds *its* budget share. Bytes are retired
   after the seal completes and the last referencing frame has completed.
3. **T2 → T3** (drop the cold artifact): only for kinds whose `ColdFormat` says the base + log can
   regenerate it (`DeltaTailOnly` keeps the tail; `None` drops everything).

Never more than one step per boundary per scope; never a table-wide pass per frame (photon-cells
D4: "on-demand full clear … never in the steady-state frame").

### 6.3 Eviction policy — declared per kind, three shapes

| `EvictPolicy` | Order candidates by | Precedent | Default for |
|---|---|---|---|
| `Age` | generations since `lastDemandedGen` (largest first); a scope re-demanded within the window is a hit regardless | photon-cells D4 lazy age reclaim, `kPhotonCellMaxAge`; `SVOStreaming.h` `maxFramesBeforeEvict` | sim scopes (undertow rows), CashSystem entries |
| `DistanceFromActive` | scope-tree distance to the nearest active scope (N-scope parent/sibling hops) / world-space footprint regime (VIXEN `CellFootprintRegime`) — farthest first | `SVOStreaming.h` `distanceEvictFactor`; `InstanceWantsBrickResidencyByFootprint` | VIXEN payloads |
| `CostBenefit` | `bytesFreed / reArmCost` where re-arm cost is the ladder distance to T0 (a T3-capable region is cheap to evict, a stored asset is not) | lazy-procedural §2 "eviction is safe … regenerate from instructions" | derivable (T3-capable) kinds as tiebreak |

The policy ranks; the ladder decides *how far*. A `floorTier` pin is never a candidate.

### 6.4 Assets (CashSystem) are the same rows with an external cold source

Every `TypedCacher<T>` entry is a `(kind = T, scope = key)` residency row: T0 = live device object,
T1 = CPU bytes retained (e.g. SPIR-V / voxel scene data before `UploadToGPU`), T2 =
`cache/devices/<id>/<Cacher>.bin`, T3 = recreate from create-info/recipe. The one API change this
implies (D7): `GetOrCreate(ci)`'s **synchronous miss** (`TypedCacher.h:108-118`, `Create(ci)` on the
calling thread; `VoxelSceneCacher::Create` ending in `WaitAllUploads`, `VoxelSceneCacher.cpp:936`)
becomes `Demand(ci) → handle` + `Get(handle)` that returns the fallback/absent per the kind's
fail-soft rule until the load task (1a) has flipped it — the `BodyOctreeSceneNode` poll pattern
applied to cachers. Publication becomes one immutable slot per key (the inventory's C1/C3/C4
CACHE/MEMO candidates), and the same-key `future.get()` wait-cycle the inventory documents
(`Findings` item 2) disappears because nobody waits on a key inside a lock any more. `MainCacher::
SaveAllAsync/LoadAllAsync` are already the fan-out shape of steps 1a/1b at init; the design keeps
them and stops `.get()`-ing them on the frame thread where they are not init.

---

## 7. What it composes with / dissolves — one architecture, not four

### 7.1 Composition table

| Design | Residency reuses | Residency adds | Nothing duplicated |
|---|---|---|---|
| **Wave assembler** | `active(N+1)` as demand; `slots[2]` + host arena + join-then-flip; `DeltaSealSummary` as the seal; `BindWaveRow` hook composition | step 1a/1b (tasks), step 6′ (`∩ ready`, flips), a sibling I/O arena, the residency decision sequence in the fingerprint stream | the working set is not a second active set — it is the manifest closure of the one active set |
| **N-scope** | `ScopeAddress` as the residency key; the active set `{(level, scope)}`; `OnMissing = Downgrade`; chain-disaggregation (activating one scope loads one scope); coarsest level always runs | `floorTier = T0` for each kind's coarsest level (the "persistent levels"); gate RP2 (reload + Refine round trip) | no second predicate family: `horizonScopes` reads the same demand records |
| **Delta transpilation** | `(scopeAddress, rowKey)` envelope key; `SealAtBarrier` canonical order; `OutstandingOnly` snapshot; per-write-field base views (no world buffer) | the cold record = seal + delta tail; D1 and D2 senses unified at the seal; GPU resident base buffers + delta upload (its slice #4) are exactly T0 GPU residency with delta-apply | no persistence-only delta format |
| **Lock inventory** | CACHE/MEMO (immutable-after-publish ready mirrors, cold records), PARTITION (per-scope `ResidencyState` with one owner, worker-owned back ranges), ORDER (mutate→publish→read generations for SVO3) | removes the *reason* for C1/C3/C4 (per-key publication), RM4 (prebound rows + reservation), SVO3 (generation publish instead of a rebuild guard), and the two `vkQueueWaitIdle`-under-VK2 sites the inventory flags (`:121`) | no new mutex anywhere |
| **Lazy-procedural baseline** | region ladder (Virtual/Materialized/Coarse = T3/T0-T1/pinned mips), producer as `Reconstruct`, paged pool + keyed residency + retire/reclaim, delta store §4.4 | the ladder gets a declared row, a budget class and an eviction policy; "keyed residency" gets its key from N-scope's address | `SVOStreaming.h` deleted rather than revived |

### 7.2 Lock families touched (from the inventory, by id)

C1 `TypedCacher::m_lock`, C3/C4 `MainCacher` registries → per-key immutable slots published by the
boundary (§6.4). RM4 `ResourceBudgetManager::mutex_` → prebound rows + reservation (§6.1). RM7–RM12
(staging/upload) → unchanged in shape; the design only ever calls the async `Upload/FlushUploads/
IsUploadComplete` path, never `WaitAllUploads`/`vkQueueWaitIdle`. SVO3 `LaineKarrasOctree::m_renderLock`
→ generation publish (rebuild writes a back generation). SVO4 voxel memoizer → a cacher row. APP1
body-bake guard → the bake becomes a `Reconstruct` task, so the startup serialization the inventory
documents (`:74`, `:84`) moves off the graph-setup path.

### 7.3 R4 — the rule residency must not erode

"Voxel residency is never an input to simulation" (`bridge-provisions.md:45-50`, "does this make
loadedness observable to a system?"). Under this design the answer is structurally no on both sides:

- undertow: the residency gate (`∩ ready`) lives in the **runner's boundary step**, not in any
  system signature; a system observes only which level it runs at, and that is a declared,
  deterministic, replay-recorded decision. No `ref`/`in` parameter can name a tier.
- VIXEN: a payload's tier changes what the shader *samples from* (mip vs brick), never what the
  sim *computes*; VIXEN's `ColdFormat = None` for sim-derived regions means VIXEN can never become
  the place a sim fact is persisted.

Owner decision D9 asks for this to be ratified as an R4 corollary so no later lane adds a
"loaded" flag to a manifest.

---

## 8. CPU-first prioritized roadmap (each slice its own parity gate)

| # | Slice | Repo | Delivers | Gate | Depends on |
|---|---|---|---|---|---|
| **R0** | **Residency rows + `ResidencyState`, no behaviour change.** Emit `kResidencyRows[]` beside the dispatch candidates from the declared kinds; generalize `WholesaleAvailability` → `ResidencyState` keyed by `(kind, scope)` with the same protocol; delete `SVOStreaming.h` | kernel (emitter) + VIXEN (header) | the declared dimension | existing wholesale-admission frame hashes byte-identical; `WholesaleResidentSignatureFNV64` unchanged for the per-octree case | D1 spelling |
| **R1** | **VIXEN proof: per-region SVO payload residency, off-tick, no device waits.** `RequestBrickResidency(bool)` → keyed per-`TierAddress` requests; producer/`Reconstruct` on the sibling I/O arena; `WholesaleCapacityArena` backed by segmented buffers; `BatchedUploader` offset uploads; generation flip replaces `Rematerialize`'s `vkDeviceWaitIdle`; shell revalidate → back-slot + poll (kills §1.1 sites 1–2) | VIXEN | the stutter fix and the first true eviction (T0→T1→T2 for materialized regions) | steady-state frames contain **zero** `vkDeviceWaitIdle`/`vkQueueWaitIdle` (asserted by a VK2-acquisition trace); frame hash parity vs today on the all-resident case; `steady_state_bytes_uploaded` bounded over a long boundary-crossing session (the Deep-Field falsification probe, `:284-294`) | R0; no kernel/undertow change |
| **R2** | **undertow per-scope seal/reload (pure CPU).** Scope-addressed node id in `AddressedBinaryNodeCodec`; `<Kind>Seal`/`Reconstruct` from the `owned-slice` codecs; `SaveCampaign` = global nodes + fold of scope records; gate RP1 | undertow (+ kernel codec/format) | the persistence contract; the first cold tier for sim rows | RP1 byte parity at 1/2/N over the 4-tick harness; whole-world save bytes identical to today for a single-scope world (move-only) | delta Q2 (scope address on the envelope) ruled; N-scope S1 landed (it is) |
| **R3** | **Assembler integration.** Steps 1a/1b/6′; `activeNext ∩= ready`; prefetch horizon from demand records; residency decision sequence in the fingerprint stream; gate RP2 | undertow + kernel | the working set derived from the active set; downgrade-while-loading | assembler G4 (double-buffer parity) still holds with residency tasks live; RP2; overrun counter still zero (loads never wait) | assembler W2; N-scope S3 (active-set runner) |
| **R4** | **Budget admission + eviction ladder.** Prebound budget rows + reservation; per-kind `EvictPolicy`; one-step-per-boundary ladder; box-detected budgets | VIXEN (RM4) + kernel tables | the knob: same tables, two boxes | a constrained budget produces a deterministic, replayable demotion sequence; no admission path waits; memory high-water ≤ budget on a long run | R1, R3 |
| **R5** | **CashSystem as residency rows.** `Demand/Get` beside `GetOrCreate`; per-key immutable publication; startup `LoadAllAsync` no longer `.get()`'d on the frame thread where not init | VIXEN | asset fetching as the same model; C1/C3/C4 dissolved | no synchronous `Create` on the frame thread; the inventory's same-key wait-cycle no longer constructible | R0; D7 |
| **R6** | **GPU resident base + delta upload; network cold source** | kernel + VIXEN + undertow | T0 GPU residency with delta-apply (delta slice #4); peer `ColdSource` | delta audit's ≥10× upload reduction gate; RP1 over a peer-fetched record | `Backend::GpuCompute`; delta slice #4 |

R0 and R1 are dispatchable after D1/D6; R1 is the recommended proof because the stutter is
visible there, the queue-then-poll pattern is proven there, and it needs no cross-repo change. R2
is the sim-side proof and is pure CPU with a byte gate; it should run in parallel, not after.

---

## 9. Owner decisions surfaced

| # | Decision | Recommendation |
|---|---|---|
| **D1** | Spelling of the residency row (`tiers, floorTier, budget, evict, cold, prefetchHorizon`) under `0cz`: a facet on the kind/`[GpuStruct]`/`[KernelSystem]` declaration, or a schema-DSL entry (the `KindSchema`/`CodegenConfig` family, ruling `0dn`) | **Schema-DSL entry** (zero mints; the DSL is the approved home for non-derivable codegen options); derive everything derivable (T3 capability from "has a seed/recipe base", floor from "is coarsest level") |
| **D2** | Residency gate on activation: `activeNext ∩= ready` with downgrade (never wait) vs the assembler's overrun-wait | **Never wait for bytes**; wait only for assembly. Ratify as an amendment to assembler D4: "wait" applies to assembly, "downgrade" to residency |
| **D3** | Budget and load timing enter the determinism domain: the residency decision sequence is recorded in the fingerprint stream; two boxes replay to themselves, not to each other | **Yes** — this is scale-to-the-box made explicit; the alternative (identical bytes across boxes) forbids any budget-dependent behaviour and therefore forbids out-of-core |
| **D4** | Cold format: widen `AddressedBinaryNodeCodec`'s node id with the scope address as a new `format` (reject old campaigns loudly) vs a sibling codec | **Widen, new format string**; reject-not-migrate stays |
| **D5** | Per-kind `ColdFormat` defaults: `DeltaTailOnly` for seed-regenerable sim kinds, `SealedSnapshotPlusDeltaTail` otherwise, `None` for sim-derived VIXEN regions | **As stated**; VIXEN persists authored render deltas only (existing ruling) |
| **D6** | `SVOStreaming.h`: delete vs revive | **Delete** — it is a manager; its config fields become row fields |
| **D7** | `TypedCacher::GetOrCreate` contract: keep the synchronous miss as an init-only API and add `Demand/Get` for frame-time use, vs replace | **Add beside, then migrate callers**; the synchronous form stays legal only outside `ExecuteImpl` |
| **D8** | I/O execution: sibling `tbb::task_arena(k_io)` with blocking reads (CPU-first, no engine change) vs an async-I/O backend now | **Sibling arena now**; async I/O behind the same `ColdSource` later |
| **D9** | Ratify the R4 corollary: no manifest, signature or plan row may carry a residency tier; the gate lives in the runner's boundary step only | **Yes** |
| **D10** | Default `EvictPolicy`: `Age` for sim/asset kinds, `DistanceFromActive` for VIXEN payloads, `CostBenefit` as tiebreak for derivable kinds; one ladder step per boundary | **As stated**; thresholds declared per kind and measured, never guessed (the photon-cells knob discipline) |
| **D11** | Naming of the seal role (the brief's `DeterministicCompactor`, which has no source presence): `Seal`/`Reconstruct` hooks vs a named object | **Hooks**; a named compactor object is a manager by another name |

---

## 10. Verification of this deliverable

Design artifact only. No build, generation or test was run; an audit/design deliverable is
intrinsically independent of those gates, and the brief names no build precondition. Every
MEASURED claim carries a file:line in this checkout, the kernel checkout, the undertow checkout, or
the named lane worktrees; the wave-assembler, N-scope and delta-transpilation designs are cited as
unratified inputs. No timings, byte counts or before/after deltas are claimed beyond those quoted
from their source documents with citation. The two claims a reader might dispute — that
`SVOStreaming.h` has no implementation and no caller, and that CashSystem's miss path is synchronous
on the calling thread — are pinned to `FrustumCull.h:6-8` / the absence of any `SVOStreaming` entry
in `libraries/SVO/CMakeLists.txt`, and to `TypedCacher.h:108-118` respectively.

## CONSOLIDATION ISSUES

None. No non-facade delta was required; the design depends on no engine change (the sibling I/O
arena, offset uploads and the CPU ledger all exist), and the one engine API addition it names
(`Demand/Get` beside `GetOrCreate`, D7) is a facade-side option, not a dependency of R0–R4.
