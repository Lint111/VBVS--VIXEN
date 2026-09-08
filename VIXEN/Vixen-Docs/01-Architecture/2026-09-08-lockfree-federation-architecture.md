---
title: Lock-free federation architecture — extending the kernel's deterministic-plan + delta + ownership discipline into VIXEN's runtime
status: DESIGN (awaiting owner ratification — STOP before implementation)
created: 2026-09-08
lane: locksynthesis (branch lock-synthesis, base d0646590 = inventory commit on 178b838b)
author: senior-architecture design pass (federation lock audit, part 2)
composes-with:
  - 2026-09-08-runtime-lock-inventory-and-classification.md            (INPUT — the 47+1 classified families; not re-inventoried here)
  - kernel docs/design/2026-09-08-wave-assembler-dispatch.md              (lane waveassembler, branch kernel-waveassembler 0ac776d5 — the ORDER/PARTITION elimination mechanism; vocabulary adopted here)
  - undertow docs/design/2026-09-07-delta-set-transpilation-audit.md    (lane-deltatranspile 70633a91c — the DELTA mechanism + envelope order + no-monolith check)
  - undertow docs/design/2026-09-08-n-level-scope-scale-dispatch-api.md (lane-nscopeapi fa2baff74 — active-set runner, manifest handshake §6.3)
  - WorkUnit-Convention-Interface-Design-2026-09-07.md                   (lane-workunitdesign e6070e7b — C-5 parity flag, C-7 two conformance rules)
  - TaskConsumer-Contract-Audit-2026-09-07.md                            (lane-taskconsumeraudit 1be7feda — content boundary, `Backend::GpuCompute` stub)
governing-rulings: CPU-first; per-dispatch-system parity flag (Deterministic | Lossy); capability-based smooth degradation; manifest handshake = subset-compatible-with-defaults, SDI = EXACT; NO central manager (re-monolith guard); 0cz (never mint vocabulary)
---

# Lock-free federation architecture

**One-sentence thesis (owner).** Locks are unnecessary wherever the schedule is known before the data is touched,
the data touched by concurrent workers is disjoint by ownership, cross-owner effects travel as deltas merged in a
canonical order at a commit barrier, and shared reads see only immutable published generations. The kernel already
runs this way (`RunSystemWaves` executes a plan some upstream authority proved disjoint — K1; every item owns a
disjoint `(stage,index)` key — K2). This document extends the same four primitives into VIXEN's runtime and shows,
class by class, which of the inventory's 47 host mutex families + 1 GPU protocol each primitive dissolves. What
remains is hardware-mandated serialization, minimized to a single **owner** — an invariant, not a mutex.

**Legend.** **[MEASURED]** = read from source at the audited base (path:line). **[INVENTORY]** = taken from the
classification doc, not re-verified here. **[PROPOSED]** = this design's choice. **[OD-n]** = an owner decision
collected in §8. ⚠ = a dependency on work not yet landed (only `Backend::GpuCompute` remains one).

Paths below are relative to `VIXEN/` unless prefixed with a repo name.

**Vocabulary (adopted from the wave-assembler doc, so the two designs read as one).** A **candidate row** is a
static, compile-time declared dispatch (the kernel's `kNativeDispatchCandidates`; in VIXEN, the row a subsystem
declares — §4.1). A **`WaveSchedule`** is the per-tick unit `AssembleWaves(WaveAssemblyInput) → WaveSchedule`
emits for frame N+1: a fixed-capacity POD of **waves** (concurrent groups) of **rows** (`WaveRow{candidate,
itemCount, kind}`). "**Plan**" names only the static artifact (`NativeDispatchPlan.g.h`, `WirePlan`), never the
per-tick unit.

---

## 0. Headline

- **Two views of one thesis.** The kernel wave assembler replaces the static `NativeDispatchPlan` as the per-tick
  authority: during frame N it computes N+1's `WaveSchedule` — topology (colouring/order) from the codegen-time
  conflict tables over *declared* write-sets, activation and sizing from the sealed delta summary of the latest
  committed frame — off the critical path, and feeds TBB. That assembler IS the elimination mechanism for the inventory's **12 ORDER-able + 11 PARTITION-able**
  families (23 of 47): an ORDER-able lock exists because two phases are not yet sequenced by a schedule; a
  PARTITION-able lock exists because ownership is not yet assigned by one. Once the assembler owns both, those
  locks have nothing left to protect. This doc does not build a second scheduler; it specifies the **rows VIXEN
  contributes** to the assembler's input and the **owner/delta/generation shapes** VIXEN adopts so the assembler's
  output is safe to run (§4).
- **The 12 DELTA-able** families ride the delta-transpilation pattern the assembler already reads: per-producer
  append segments sealed in canonical `(epoch, producerOrdinal, stableKey, localSequence)` order at the barrier
  (`DataPipe.SealAtBarrier`, undertow), applied by one consumer. Note the delta doc's own caveat, which the inventory
  repeats: **an append log gives ORDER, not commutativity** — callbacks, destruction and overlapping copies are
  sequenced, never folded (§2.2).
- **The 11 CACHE/MEMO** families ride immutable-after-publish: the content boundary's shape-hash already makes a
  generated artifact immutable; a runtime cache entry becomes a **generation** published at a barrier and retired
  after the wave in which its last reader joined (§2.4). The six same-key `future.get()` deadlock paths in
  CashSystem disappear because the pattern has exactly one builder per key and never waits under a shared lock.
- **The 1 GENUINELY-NEEDED** family (VK2, per-physical-`VkQueue` host serialization) becomes a **queue-owner stage**:
  workers record into owned command pools (RM9 partition) and publish `SubmitRecord` deltas; one owner per physical
  queue drains them in canonical order and calls `vkQueueSubmit2` once per batch. Serialization is preserved as an
  invariant of the `WaveSchedule`; the mutex, the device-wide map lock in front of it (VK1), the `vkQueueWaitIdle` calls
  held under it, and the unguarded `SkyProjectionNode.cpp:565` path all go away **by construction**, because no
  node holds a raw queue handle any more (§2.5). Today VIXEN creates exactly **one** queue
  (`VulkanDevice.cpp:32-35`, `queueCount = 1`, graphics family) [MEASURED], so "one owner per physical queue" is one
  owner in the shipping process.
- **Residual set, honestly** (§7): the physical-queue owner (serial, not locked); GPU completion (fences/timelines,
  not mutexes); third-party internals the inventory left unclassified (VMA v3.1.0 internal lock until the
  externally-synchronized flag is safe — [OD-6]; Gaia's process-wide chunk allocator; glslang; GLFW main thread;
  TBB; the driver); the cold exception-collection path (K3 makes it slot-owned, so even that is a fold, not a
  lock); and OS-level multi-process cache-directory ownership, which is outside runtime locks entirely.
- **Constraints honored:** CPU-first (every phase gates on the CpuTbb path; GPU phases wait on `Backend::GpuCompute`,
  `libraries/KernelDispatch/include/KernelDispatch/Abi.h:46`, declared-not-implemented [MEASURED]); the per-system
  parity flag relaxes ORDER for `Lossy` systems, never OWNERSHIP (§3); smooth degradation is **structural** — a
  subsystem without an access-manifest row is scheduled as a whole-slot stage, the conservative barrier
  `RunChainPerItem` already applies to non-opted-in stages (`Dispatcher.h:213-218`) [MEASURED], so a not-yet-migrated
  subsystem is slower, never wrong (§4.3); and there is **no `LockFreeManager`** — every mechanism is a declaration
  on the subsystem plus a candidate row, composed at build time (§4.4).

---

## 1. The four primitives (nothing minted — each already exists in the federation)

| Primitive | Kernel/undertow origin (verified) | VIXEN adoption | Dissolves class |
|---|---|---|---|
| **P1 Owner** — every mutable cell has exactly one writer per wave, addressed by a stable key that outlives the wave | K2 `(stage,index)` hazard keys, `Dispatcher.h:201-218`; per-item RAW/WAW/WAR edges from `StateAccessKey` overlap [MEASURED] | a **partition key** per subsystem (§2.1 table): frame-slot, resource id, upload lane, device, job slot. Never a pointer, never arrival order. | PARTITION-able (11 + GPU1) |
| **P2 Delta channel** — producers append to private segments; the barrier seals them into canonical order; one consumer applies | undertow `DataPipe` envelope `(epoch, publisherOrdinal, stableWorkKey, localSequence)`, `DataPipe.cs:282-419`; the H10 assignment pipe and the `Compute`/`Apply` split (delta doc §1.2) | per-producer append + seal at the tick's commit barrier; apply stage `itemCount` = records, not dataset | DELTA-able (12) |
| **P3 Wave / commit barrier** — stages proved disjoint run concurrently; waves run in sequence; the barrier is where deltas seal and generations publish | K1 `RunSystemWaves`, `Dispatcher.h:353-411`: "does NOT re-derive or re-verify disjointness — the plan/manifest is the safety proof" [MEASURED]; the wave assembler emits the `WaveSchedule` for N+1 (kernel doc `2026-09-08-wave-assembler-dispatch.md`) | VIXEN's host phases (input fold, window drain, upload submit, present, cache publish, structural apply) become **candidate rows** with declared access sets; the frame is a `WaveSchedule` | ORDER-able (12) |
| **P4 Generation** — build once, publish at a barrier, read without synchronization, retire after the last reader's wave joins | content boundary shape-hash (TaskConsumer audit §3.2): a shape-hashed artifact is immutable by identity; `DeterministicCompactor` input-order publication (`Runtime/DeterministicCompactor.cs`) | every registry/cache/lookup becomes an epoch-numbered immutable table; invalidation = next epoch; no reader ever waits | CACHE/MEMO (11) |

**The parity flag is the fifth input**, not a primitive: `Parity { Deterministic, Lossy }` per dispatch system
(WorkUnit C-5, `enum class Parity : uint8_t { Deterministic, Lossy }` in the acceptor sketch). It selects, per
row, whether P2's seal must sort (Deterministic) or may accept race-winner order (Lossy, the kernel's `AppendRef`
family), and whether P4 must publish bit-identical generations or policy-identical ones (§3).

**The tick model VIXEN adopts (the `WaveSchedule` the assembler emits, VIXEN's view):**

```
tick N:   [assemble N+1  (topology from the constexpr conflict/predecessor tables; activation + itemCount from
                          the sealed delta summary of commit[N-1]; off the critical path; double-buffered slots[2],
                          join-then-index-flip, no mutex — assembler doc)]
          [wave 0 … wave k  (RunSystemWaves; each item owns disjoint keys — P1; producers append deltas — P2)]
          [commit barrier: seal every channel (canonical order) → apply stages → publish generations (P4)
                           → observe GPU timeline values completed since last tick → retire generations/resources
                           whose last reader joined AND whose timeline value is reached]
          [queue-owner stage(s): drain SubmitRecords in canonical order → vkQueueSubmit2 batches → present]
```

Two facts about this loop carry most of the design: (a) **the commit barrier is the only place ordering is
decided**, so a subsystem never needs a lock to decide it locally; (b) **GPU completion is never a CPU wait on the
critical path** — it is a timeline value *observed* at the next barrier, which is what lets retirement (RM1, RM11,
RM13, SVO3 GPU generations) be a scheduled stage instead of a polled critical section.

---

## 2. Per-class mechanisms

### 2.1 PARTITION-able → ownership partitioning (11 host families + GPU1)

**Mechanism.** Choose the partition key; move the mutable state *into* the owner addressed by that key; make every
cross-owner handoff a P2 delta (never a shared write); reduce statistics as a fold at the barrier. This is the
kernel's ECS ownership model read into VIXEN: a kernel row is owned by `(stage, index)`; a VIXEN resource is owned by
`(lane, id)` where `lane` is whichever worker/queue/device the assembler assigned this tick and `id` is a stable
identity (never a pointer — the inventory's `BufferRecord*` outliving its guard at `StagingBufferPool.cpp:351` is
exactly the failure a pointer key produces).

| Family [INVENTORY] | Partition key [PROPOSED] | Owner holds | Cross-owner handoff | Note |
|---|---|---|---|---|
| RM5 `DirectAllocator` records, RM6 `VMAAllocator` records | **backing allocation id** (the `VmaAllocation`/`VkDeviceMemory` + offset) | record + mapped state + budget binding travel *with* the owned allocation handle | free/retire = delta to the retirement channel (RM1 row) | aliases of one backing memory converge on one owner (inventory condition). VMA's *internal* lock is untouched until [OD-6]. |
| RM7 `SizeClassBucket`, RM8 `recordsMutex_` | **upload lane** = `(worker ordinal, size class)`, quota per lane | its own deque + records; capacity is a per-lane quota assigned by the assembler from the pool total | returned handles cross lanes as `ReturnRecord{lane, handle, generation}` deltas; a lane drains its returns at the barrier | the inventory's warning holds: moving only the deque leaves the shared record map — records MUST move with the lane. Pool total capacity is preserved as Σ quotas. |
| RM9 `cmdBufferMutex_` | **recording worker** — one `VkCommandPool` per worker per frame-slot | pool + its FIFO; reset at the frame-slot's retire | none needed: completion recycles by frame-slot, not by returning to a shared FIFO | Vulkan requires external sync per pool anyway; partitioning the pool (not just the FIFO) is what makes concurrent recording legal. |
| RM12 `statusMutex_` | **upload handle** → stable slot index (generation-tagged) | one status word per slot, written by the completion owner only | clients read the slot (published at barrier); pruned slot = generation mismatch → `Failed` (preserves `:180` semantics) | K2 "published result slots". |
| C4 `m_deviceRegistriesMutex` | **device id** | per-device registry, immutable outer directory built at device set-up | device teardown = retirement barrier (P4) | the outer map is a P4 generation; the per-device row is the owner. |
| SH3 per-worker `ThreadLocalQueue` | **job slot** assigned from the row's position in the `WaveSchedule` (no stealing) | its queue | submitters publish `JobRecord` deltas to the slot owner's mailbox | stealing breaks single-owner; the recommendation (§7) is not to keep persistent private workers at all. |
| SH5 `buildsMutex_` | **build handle** → coordinator-owned job slot | status snapshot per slot | cancellation = a delta flag observed at the job's next step boundary | polling `wait` becomes a barrier join. |
| SVO2 `BulkMaterializationQueue::m_mutex` | producers own **request slots**; workers own **result slots** (K4: `BulkMaterialization.cpp:190,216` already has disjoint result slots [INVENTORY]) | coordinator owns capacity + close state | enqueue/pop = delta; close = a generation flip | no production caller located — pattern-conformance on activation (§5, phase 6). |
| EB4 `WorkerThreadBridge` | **producer mailbox** per submitter, one consumer | its mailbox | none | same "persistent private worker" question as SH3/SH4. |
| GPU1 hit-accumulation slot claim | **canonical key** `(recipe, cell, mip)` → one owner invocation per key, chosen by a group/sort pass before accumulation | representative + sums for its key | none — reduction is within the key group | replaces CAS-claim + transient reclaim with the kernel's sort-then-reduce (`DeterministicCompactor` shape; the dispatch-vocabulary doc's "sort stage suffices, atomic-claim not needed"). Gated on `Backend::GpuCompute` (§5 phase 7). |

**Where is the partition key?** The rule that produced the table: **the key is the identity that outlives a wave and
is known to the assembler before the wave starts.** Frame-slot for anything recycled by GPU completion; resource id
for anything addressed by a handle; lane (worker ordinal) for anything the assembler hands out as quota; device for
anything device-scoped; canonical content key for anything reduced. A key the assembler cannot know before the wave
(a pointer, an arrival ordinal) is not a partition key — it is a delta payload. [OD-1] ratifies the column.

**Anti-monolith check.** There is no partition table object. Each subsystem declares its key *kind* as part of
its candidate row (the same place it declares its access set). The assembler does **not** assign lanes or owner
ids (it owns wave grouping, order and per-row `itemCount` only — assembler doc §(4)); lane/slot identity is
therefore **derived deterministically by VIXEN from the row itself** — `(candidate index, item index)` for
per-item keys, the frame-slot for GPU-recycled resources, the declared id for handle-addressed resources. The
kernel's `DispatcherProfile` maps owner-id → backend today (`Abi.h`, "the native counterpart of the kernel's
`DispatcherProfile`"); the lane derivation rides the same per-row mapping. Nothing is assigned at runtime by a
central object.

### 2.2 DELTA-able → append-only delta + deterministic merge (12 families)

**Mechanism.** The ratified delta-transpilation shape, applied to VIXEN's read-modify-write locks: the producer's
*write* becomes an *append* to a private segment addressed by the producer's ordinal; the commit barrier seals all
segments into canonical `(epoch, producerOrdinal, stableKey, localSequence)` order; exactly one consumer applies.
Three apply flavours, chosen by the payload's algebra exactly as the delta doc chooses them by the write operator:

1. **Commutative fold** (`+=`, `max=`, histograms): partial per-producer accumulators, folded at seal. Order-free;
   a `Lossy` row may skip the sort entirely (§3).
2. **Ordered apply** (assignment with a last-writer rule, stable-sorted collect-then-apply): the canonical order
   IS the semantics; `Deterministic` rows must sort.
3. **Sequenced execution** (callbacks, destruction, overlapping copies): the log provides the order; the consumer
   executes the records serially on its own thread. **Not** commutative and never claimed to be — the inventory's
   classification caveat is honored verbatim.

| Family [INVENTORY] | Producer ordinal / stable key | Flavour | Consumer (single) | What the canonical order must preserve |
|---|---|---|---|---|
| RG1 `errorMutex_`, KD1 `errMutex` | task id (`TaskId{owner, index}`) → **per-task error slot** (K3: `TaskExecutor.cpp:53-67` is the residual lock itself) | ordered (task order) | the coordinator after the wave join | current vector order is nondeterministic (acquisition order); slot collection fixes that as a side effect. |
| RG2 `samplesMutex_` | `(task id, completion ordinal)` | fold for sum/max; ordered for last-sample/EMA | frame registry processing stage | temporal order for EMA consumers; reset epoch = an epoch flip, not a lock. Candidate `Lossy` [OD-3]. |
| RG7 descriptor-tracking events | `(descriptor tracking id, event ordinal)` | ordered | dump/scan stage | tracking-id order for mismatch analysis. Debug-only; `Lossy` acceptable. |
| RM1 `DeferredDestructionQueue` | `(releasing owner ordinal, local seq)` → `RetireRecord{resource, eligibleAfterTimeline}` | sequenced | the **lifecycle owner** at the barrier, after observing the timeline value | GPU-safe eligibility (timeline observed, never polled); reentrant enqueue from a destructor appends to the *next* epoch's segment. |
| RM2 `BatchedUpdater` | `(producer ordinal, destination image, seq)` | ordered by `(destination, priority, ordinal)` | the record stage | today equal-priority stable-sort preserves *arrival* order — nondeterministic between producers; the ordinal fixes it. Resize at quiescence = a stage in tick 0 / a resize epoch. |
| RM10 `pendingMutex_` | `(producer ordinal, destination buffer, seq)` | ordered; conflicting-destination writes sequenced by key | batch-assembly stage feeding the queue owner | the inventory's "coherent publication contract" for timestamp/byte counters becomes the seal itself. |
| RM13 `BudgetBridge` | `(fence/frame key, ordinal)` | sequenced (callbacks) | completion owner; callbacks run on it, outside any lock | quota return + oldest-first completion; header's no-reentry rule becomes structural. Unwired today — pattern on activation. |
| C5 recipe family membership | `(contentHash, recipeId)` | commutative set-union with canonical representative = **min recipeId** [OD-2b] | registration seal | today "first arrival" is order-dependent; min-id is the deterministic choice. |
| SH6 `ShaderLogger` | `(worker ordinal, seq)` | sequenced | one sink owner; callback configuration is a P4 generation | arbitrary callback side effects stay serial on the sink. |
| EB1 `queueMutex` | **producer ordinal** (assembler-assigned) + local seq | sequenced (payload callbacks) | the frame dispatch stage (`RenderGraph.cpp:684,1580` drains today [INVENTORY]) | producer-internal sequence (trivially) and cross-producer order = ordinal [OD-2]. |
| EB3 `statsMutex` | producer ordinal | fold (counts, histogram, high-water = max) | stats query stage | reset = epoch flip; queue-size snapshot = the sealed count. `Lossy` candidate. |

**EventBus, concretely.** `MessageBus` today (`MessageBus.h:255-275` [MEASURED]) is one `PreAllocatedQueue` under
`queueMutex`, one subscription list under `subscriptionMutex`, one `Stats` under `statsMutex`. Under this design:
`Publish` appends to the caller's producer segment (no lock); the frame dispatch stage seals and walks the records
in canonical order calling handlers from the **published subscriber generation** (EB2 → §2.3); `Subscribe`/
`Unsubscribe` append structural deltas applied at the next commit barrier, which is what makes calling them from
inside a handler safe (the inventory's self-deadlock finding #3 is eliminated structurally, not by a recursive
mutex). Counters are per-producer partials folded at seal. `PublishImmediate` (used by `AppFlowRuntime.cpp:17`
[INVENTORY]) is a synchronous call on the caller's thread against the current generation — the dispatch itself
never needed the queue lock, and with subscription edits deferred it needs no lock at all [OD-7].

**Which VIXEN subsystems become append-only + deterministic merge:** the whole upload/lifetime pipeline
(RM1/RM2/RM10/RM13 with the RM7-9/RM12 owners of §2.1), EventBus (EB1/EB3, with EB2 as a generation), telemetry
(RG2/RG7/SH6), and error collection (RG1/KD1). ResourceManagement is the clearest case: today one upload acquires
bucket → records → pending → (on flush) command → submitted → status locks, five to six critical sections per
item; after, it is one append per item and one owner drain per barrier.

**Anti-monolith check (same as the delta doc §5.2).** Channels are per-declaration (one per `(consumer,
payload)`), segments are per-producer, and the only shared object is a control-plane registry that "never
publishes" — no world delta buffer, no `DeltaManager`. VIXEN's channels are declared by the subsystem that consumes
them, next to its candidate row.

### 2.3 ORDER-able → deterministic dispatch order (12 families)

**Mechanism.** Each ORDER-able lock separates two phases that are not yet sequenced by a schedule. The fix is to
name the phases as candidate rows with declared access sets so the assembler orders them (RAW/WAW/WAR over
*declared* write-sets, computed at codegen into `kNativeDispatchConflicts`; a predecessor edge is a hard wave
boundary — assembler rule R2), and to make the mutation phase publish a P4 generation at the barrier so the read
phase never sees a half-built state. Where eligibility depends on the GPU, the row carries a timeline value and the
assembler places it only after that value has been observed.

| Family [INVENTORY] | Phases today | Candidate rows [PROPOSED] | Generation published | GPU dependency |
|---|---|---|---|---|
| RG3 `InputNode::eventMutex_` | GLFW callback push (pump, in Render) vs drain (Update) | `input.pump` (main-thread-affine stage, writes `input.pending`) → `input.fold` (reads `input.pending`, writes `input.frame`) | `input.frame` snapshot | none. Pump→next-tick-fold latency is preserved unless [OD-8] moves the pump row earlier. |
| RG4 `WindowNode::eventMutex` | GLFW callbacks vs host drain vs node drain | `window.pump` (main-thread) → `window.publish` | immutable per-frame window state (size, focus, events) | none. "Drain even during pause" = the row is unconditional. |
| RG5 `LightTreeBufferNode::cutMutex_` | CPU cut replacement vs GPU-struct copy | `lightcut.replace` (writes cut) → `lightcut.upload` (reads cut, writes flight-indexed buffer) | cut generation; the uploader holds the generation it read | flight-indexed buffer reuse gated by the frame-slot's timeline value. |
| RM3 `HostBudgetManager::arenaMutex_` | arena reset/resize vs allocation | `arena.reset` runs in a **quiescent epoch row** (no allocation row in the same wave) | new arena generation | none. Today's lock does not cover allocation anyway [INVENTORY]; the plan does. |
| RM11 `BatchedUploader::submittedMutex_` | submit vs completion poll | `upload.submit` (queue-owner stage) → next tick's `upload.retire` (after timeline observed) | submitted FIFO = the sealed order | the whole point: completion is observed, not polled under a lock. |
| SVO1 `m_processMutex` | competing batch coordinators | one `svo.materialize` coordinator row per octree/region | — | none. |
| SVO3 `LaineKarrasOctree::m_renderLock` | full rebuild (exclusive) vs render lease | `svo.rebuild` (writes `octree.next`) → `svo.publish` at the barrier → readers hold `octree.current` | octree generation; per-region rows if the rebuild is partitioned (optional, inventory alternative) | old generation retired after last reader's wave AND the GPU timeline value for buffers that referenced it. Fixes the exclusive `lockForRendering()` gap [INVENTORY]. |
| SH2 `ShaderLibrary::libraryMutex` | compile/reflect vs get/swap/watch | `shader.compile[i]` (owned outputs) → `shader.swap` at barrier | program-table generation | none. The nested nonrecursive acquisitions (finding #4) vanish with the lock. |
| SH4 `cvMutex_` (CV sleep/wake) | idle workers vs dispatch | **no CV**: builds are schedule-dispatched rows, workers are the TBB arena | — | none. The inventory flagged that a barrier cannot replace an async wakeup; the resolution is to remove the persistent private worker (§7, [OD-9]). |
| EB2 `subscriptionMutex` | registry edit vs dispatch (handler under lock) | `bus.applySubscriptions` at barrier → `bus.dispatch` reads the generation | subscriber table generation | none. Handlers run on the dispatch owner, unlocked; edits from handlers land in the next epoch. |
| GA1 `RelationshipObserver::m_mutex` | callback registry + deferred ops vs Gaia structural writes | `gaia.structuralCommit` (single owner; the actual writes at `RelationshipObserver.cpp:122,155,211` were never covered by this lock [INVENTORY]) → `gaia.notify` reads the callback generation | callback registry generation; deferred ops = a P2 channel | none. |
| APP1 `g_gaiaChunkAllocatorMutex` | 8 `std::async` bodies serialized around bake+build | `body.evaluate[i]` (pure, owned plain outputs, parallel) → `body.applyStructure` (one owner touching Gaia's process-wide allocator, sequential) | baked-scene artifact (already cached; the cache hit bypasses both rows) | none. Real allocator partitioning stays out of scope unless Gaia's allocator ownership changes (inventory). |

**RenderGraph construction and the SDI funnel — serializable by build order, yes.** The inventory established there
is no graph-construction mutex: `AddNodeImpl`/`ConnectNodes`/`Compile`/`GeneratePipelines` are single-owner
sequential mutation (`RenderGraph.cpp:137,203,502,1256`), and the SDI path is `SynthesizeComputeStage` →
`WireStageFromSdi` → `BuildSdiWirePlan` → registry Apply, over a copyable registry whose maps iterate
deterministically (`SdiStageWiring.h:185`; K4). That is already the right shape for a two-row plan:

- **`graph.plan[stage]`** — one row per SDI stage, parallel, pure: consumes the immutable provider registry
  generation, produces a `WirePlan` (nodes to add, connections, handle *requests*). Deterministic because the
  traversal is (K4).
- **`graph.apply`** — one owner row, sequential in **declaration order**, applies every plan to the shared graph
  containers. Handle allocation is made deterministic by **pre-assigning handle ranges per stage in declaration
  order** before the parallel row runs (the plan requests slot k of its range; the apply row honors it), so
  `nameToHandle`/`instances` are identical regardless of which plan finished first.

Construction is the **tick-0 `WaveSchedule`**: the same assembler, the same `RunSystemWaves`, the same commit barrier. Whether
the funnel dominates startup is unmeasured (inventory); the design makes it parallel-capable without changing the
single-writer invariant on the graph containers. The optional SDI *header generation* registry (SH8) is a separate
cache family (§2.4).

**Two traps the schedule must not paper over** (both from the inventory): (1) queue access "is not fully
represented by data-flow dependencies" (`VulkanDevice.h:100`) — a deterministic graph schedule does *not* serialize
physical-queue access by itself; that is why VK2 is its own owner stage (§2.5) and every submitting row depends on
it explicitly. (2) A CPU barrier never proves GPU completion — every retire/reuse row carries a timeline value.

### 2.4 CACHE/MEMO → immutable-after-publish / precompute (11 families)

**Mechanism.** A cache is a map from a key to a built artifact. Lock-free by construction means: (a) if the key set
is known before the runtime needs it, **precompute** the whole table at build/startup and publish it once (the
content-boundary pattern: shape-hashed, dual-output, immutable by identity); (b) if keys arrive at runtime, assign
**exactly one builder per key** (the first requester in canonical order, or the assembler's pre-assignment when the
requests are candidate rows), publish the entry as a **generation** at the barrier, and let every reader hold a
generation handle; (c) invalidation is an epoch flip, never an in-place erase; (d) retirement waits for the wave in
which the last reader of the old generation joined (and, for GPU-referenced artifacts, the timeline value).

| Family [INVENTORY] | Key known ahead? | Shape [PROPOSED] | Notes |
|---|---|---|---|
| VK1 `submitMutexMapLock_` | yes (queues are created at device set-up; entries never erased) | **queue-owner bindings** built at device set-up, immutable; consumers hold the binding | the map disappears with VK2's mutex (§2.5); the binding is the owner's handle. |
| RM4 `ResourceBudgetManager::mutex_` (`shared_mutex`) | yes (budget categories are declared) | precreated immutable **row directory**; per-row atomic usage counters (already atomic); strict admission = a **reserve-CAS on the row** before the allocation, released on failure | the inventory's caveat is honored: delayed totals cannot replace admission; reservation is the admission contract, and it is per-row, lock-free. Configure/reset = epoch flips at a quiescent row. |
| C1 `TypedCacher::m_lock` (`shared_mutex`, inherited by 15 cachers) | partly (pipelines/layouts/samplers/render passes are derivable from declarations; textures/meshes stream) | **per-key builder + published entry**: `GetOrCreate(key)` = lookup in the current generation (no lock) → miss → claim the key's builder slot (CAS on a per-key slot in the pending table, the only atomic) → build outside any shared state → publish at barrier. Waiters wait on the key's future **holding nothing**. | Eliminates the six same-key deadlock paths (finding #2) because no override can wait while holding the shared lock — the shared lock does not exist. Derived cachers lose their own guard spellings; the inventory's "consolidate publication before widening concurrency" is exactly this. [OD-5] decides how much of the key set becomes build-time precompute via the content boundary. **LANDED (P4, C1 only): `TypedCacher` is the generation table — `2026-09-08-lockfree-p4-cache-generations.md`; the shared lock and the six override paths are gone; C3/C4 remain.** |
| C2 `m_debugMutex` (`_DEBUG`) | — | per-builder diagnostics folded at seal | debug-only. |
| C3 `MainCacher::m_globalRegistryMutex` | yes (factories, names, dependency metadata are registered at init) | **frozen factory table** after an init phase row; global cacher instances eagerly published | lifecycle order (global→device register, device→global ClearAll, persistence holding locks across waits — finding #5) becomes explicit rows: `cache.init` → … → `cache.persist` (snapshot of a generation, no lock across I/O) → `cache.teardown` (retirement barrier). |
| C6 `TypeRegistry::m_mutex` | yes | frozen after registration | no runtime instantiation located — conformance on activation. |
| RG6 UI hit-mask cache | mostly (UI assets are declared) | pre-resolved mask table at UI load; runtime miss = per-key builder | entries never erased today; matches the generation model directly. |
| SH1 `ShaderCacheManager::cacheMutex` | content-addressed | **immutable content-addressed artifacts** (SPIR-V by content hash) + per-key publisher; stats = fold; maintenance/eviction = a staged epoch | filesystem access is not global exclusion; unrelated keys never serialize. |
| SH7 `s_initMutex` (glslang) | — | `shader.initProcess` row in tick 0, before any compiler row | the one residual "once" — replaced by schedule order (a predecessor edge), not `call_once`. |
| SH8 `SdiRegistryManager::mutex_` (`recursive_mutex`) | yes (registrations are a batch) | batch registration row → deterministic alias resolution → one immutable snapshot | optional builder-callback path; recursion vanishes with the lock. |
| SVO4 `VoxelDataCache` | partly | key partitions + per-key builder + generation; returned pointers become generation handles (fixes lifetime-vs-`Clear`) | no external non-test caller — conformance on activation. |

**The content-boundary extension.** The ratified boundary already makes *generated* artifacts immutable (shape-hash
→ identity). This design extends the same identity rule to *runtime-built* artifacts: a cache entry's key is a
shape-hash of its declared inputs; equal key ⇒ interchangeable value ⇒ the entry is immutable and any single
builder's result is canonical. That is why "one builder per key" needs no arbitration beyond a CAS on the pending
slot — two builders would produce the same bytes, and the inventory's RG6 "concurrent misses can duplicate decode
then `emplace` selects one" is already the degenerate form of this rule.

### 2.5 GENUINELY-NEEDED → single-owner minimization (VK2, and the GPU path)

**What hardware mandates.** Host access to a `VkQueue` must be externally synchronized (`vkQueueSubmit2`,
`vkQueuePresentKHR`, `vkQueueWaitIdle`); VIXEN creates its queue with zero flags (`VulkanDevice.cpp:32`,
[MEASURED]) so the driver does not synchronize it. Command pools must be externally synchronized per pool. GPU
completion must be observed through fences/semaphores. None of these mandate a *mutex*; they mandate that at most
one host thread touches the object at a time.

**The queue-owner stage [PROPOSED].**

- **Recording is parallel and owned.** Every node/uploader/UI/cache path that submits today records into a command
  buffer from **its own worker's pool for this frame-slot** (RM9 partition, §2.1). No node receives a `VkQueue`.
- **Submission is a delta.** The recorder publishes `SubmitRecord{producerOrdinal, seq, cmdBuffers[], waits[],
  signals[], timelineValue, fence?}` to the queue's channel (P2). Producer ordinal + seq give the canonical
  submission order; the assembler assigns ordinals so the order is deterministic per tick.
- **One owner per physical queue drains.** The `queue.submit[q]` row runs on exactly one worker, after every
  recording row it depends on (an explicit **predecessor edge** — a hard wave boundary under assembler rule R2 —
  since data flow does not express queue access: the inventory's `VulkanDevice.h:100` note). It seals the channel, coalesces consecutive records into as few
  `vkQueueSubmit2` calls as the semaphore graph allows (batching is a *win* of the owner, not a cost), and issues
  `vkQueuePresentKHR` last. Serialization is an invariant of "one row, one worker"; there is no mutex.
- **Waits leave the frame path.** `vkQueueWaitIdle` under the queue guard (`CommandBufferUtility.cpp:106`,
  `VixenRmlRenderInterface.cpp:264` [INVENTORY]) is replaced by a timeline value the record signals and the next
  barrier observes; full-idle waits are permitted only in tick-0/teardown rows. `PresentNode.cpp:144`'s device-idle
  handling becomes a quiescent row, engine-wide (finding #1's second half).
- **The unguarded path is impossible, not fixed.** `SkyProjectionNode.cpp:565` calls `fpQueueSubmit2_` directly
  today; under the owner design a node has no queue handle to call it on. The bug class is closed by the API shape.
- **Multiple queues (future).** One owner row per physical queue; async compute and transfer get their own owners
  and timelines; cross-queue ordering is semaphores in the records, sequenced by the schedule.

**Tie to `KernelDispatch::Backend::GpuCompute`.** A stage whose backend resolves to `GpuCompute` lowers to exactly
this shape: *record on the worker → publish a `SubmitRecord` → the queue owner submits → the next barrier observes
the timeline value*. The queue-owner stage is therefore **the first concrete piece of the GPU backend**, and
building it CPU-side (with today's CpuTbb stages producing the records) is on the critical path to
`Backend::GpuCompute` rather than downstream of it. The TaskConsumer audit's slice #3 ("implement
`Backend::GpuCompute` so the GLSL ray-march lowers onto a stage") and this design's phase 1 (§5) are the same
work seen from the boundary and from the queue.

**GPU allocator.** VMA's internal lock is outside the 47 (inventory). Once every allocation/free is a row on a
single allocation owner per device (RM5/RM6 owners), `VMA_ALLOCATOR_CREATE_EXTERNALLY_SYNCHRONIZED_BIT` becomes
correct to set and VMA's internal mutex disappears too — but only then, and only after a validation-layer-clean
1/2/N run; [OD-6]. Gaia's process-wide chunk allocator is the analogous case for APP1 and stays serialized by its
single apply row unless Gaia's ownership changes.

---

## 3. Using the parity flag: what `Lossy` may relax

The per-dispatch-system flag (WorkUnit C-5) is read by the assembler per row. The rule:

> **`Lossy` relaxes ORDER, never OWNERSHIP.** A `Lossy` row may seal its channels in race-winner order (no sort),
> fold counters without a canonical ordinal, and publish policy-identical rather than bit-identical generations. It
> may never share a mutable cell with another row in the same wave — the K2 disjointness proof is unconditional,
> because a data race is undefined behavior, not imprecision.

| Family | Deterministic default | May be declared `Lossy` [OD-3] | What is lost if so |
|---|---|---|---|
| RG2 task profile samples | sort by `(task, completion)` | yes — telemetry | EMA/last-sample depends on arrival; predictions become run-dependent (profiles already are) |
| RG7 descriptor tracking | ordered | yes — diagnostic | mismatch analysis needs tracking-id order → keep Deterministic when the tracker is on |
| EB3 bus statistics | fold with ordinal | yes | none observable beyond high-water timing |
| SH6 shader log | sequenced | partially — order across workers only | interleaved log lines |
| C2 debug hash collisions | ordered | yes | none |
| GPU1 hit accumulation | sorted key groups | **no** for the representative choice (must be deterministic); sums are fixed-point and commutative already | — |
| Everything that touches resources, events, caches, submission | Deterministic | **no** | — |

The kernel offers the two append vocabularies this maps onto: `AppendRef<T>` (race-winner counter) for `Lossy`,
`DeterministicAppendRef<T>` + `DeterministicCompactor` (input-order compaction) for `Deterministic` (dispatch-
vocabulary doc §0(b), `Runtime/DeterministicCompactor.cs` [MEASURED]). VIXEN's seal step selects between them by
the row's flag; nothing else in the subsystem changes.

---

## 4. Composition with the wave assembler (one thesis, two views)

The assembler design is committed on kernel `kernel-waveassembler` @ `0ac776d5`
(`docs/design/2026-09-08-wave-assembler-dispatch.md`). This section states the contract this design *gives it*
and *takes from it*, in that doc's vocabulary, so the two compose as one mechanism.

### 4.1 What VIXEN gives the assembler: candidate rows

Every subsystem in §2 contributes **static candidate rows** — the same shape the kernel's
`kNativeDispatchCandidates` + `GaiaFieldAccessManifest` already give `RunSystemWaves` (K1: "the caller is
responsible for grouping stages into waves using that proof"). The row is constexpr; nothing about it is decided
at runtime:

```
candidate { owner id; DECLARED access set {(slot, index-range | whole), Read|Write};   // -> kNativeDispatchConflicts
            declared predecessors;                                                       // -> kNativeDispatchPredecessors (R2: hard wave boundary)
            partition key kind (§2.1); channels produced / consumed (§2.2);
            generations published / read (§2.4); timeline dependencies (§2.3);
            backend set (CpuInline | CpuTbb | … | GpuCompute); parity (Deterministic | Lossy);
            affinity (main-thread for GLFW rows; queue-owner rows are single-worker) }
```

Two rules the assembler doc fixes and this design mirrors: **the conflict relation is computed at codegen over
DECLARED write-sets and is never refined by a frame's actual deltas** (refining from a stale frame's write-set is a
race); and **a declared predecessor edge is a hard wave boundary** (rule R2 — which also closes the measured
disagreement between the `--gaia-app` `std::async` spine and the `RunSystemWaves` driver on same-colour
predecessors, assembler doc §1.3; VIXEN inherits the R2 answer rather than a third driver).

Rows are **declared by the subsystem, composed at build time** — this is the whole anti-monolith argument: the
candidate table is the *sum* of declarations, the assembler is a *function* of them, and nothing owns "the lock
strategy".

### 4.2 What the assembler gives VIXEN — and what it deliberately does not

The assembler consumes `WaveAssemblyInput = { active-set bitmask, pointer to the SEALED DELTA SUMMARY of the
latest committed frame (per-channel counts + dirty-scope bits; `nullptr` ⇒ unknown ⇒ full-dataset),
per-declaration materialized-input hooks }` and emits a `WaveSchedule` for N+1 (`forEpoch = N+1`), double-buffered
`slots[2]`, join-then-index-flip, no mutex. Two-phase pipelining: topology during frame N from commit[N-1]; only
O(channels) count-binding at the N→N+1 boundary.

| The assembler owns | The assembler does **not** own — VIXEN derives it deterministically |
|---|---|
| **Wave grouping + dispatch order** (the ORDER-able elimination) | — |
| **Per-row `itemCount`**: whole-system rows stay `itemCount = 1` and self-partition (option B, unchanged); Apply rows get `itemCount = sealed touched-row count`; safe-skip when the summary says a channel is empty | **per-item hazard layering inside a wave** — left to `TaskDependencyGraph`/`RunChainPerItem` (opt-in), which is exactly the K2 mechanism §2.1 relies on |
| **Activation** (which candidates run this tick) from the active set + delta summary | **Lane / slot / owner identity** — derived from `(candidate index, item index)`, the frame-slot, or the declared resource id (§2.1). The assembler assigns no owner ids; a scope-level single-writer partition, if declared, becomes `(system, scope-class)` *candidate* rows — still constexpr, still not runtime-assigned. [OD-1] |
| — | **Producer ordinals** for delta channels = the producer's **candidate index** (static, canonical) with per-item index as the tiebreak — the DELTA-able canonical order needs no runtime assignment. [OD-2] |
| **Placement of retire rows**: a retire candidate's predecessors include every reader of generation g, so the wave boundary after them IS the retire point; the timeline dependency is a materialized-input hook the assembler reads at tick start (a completed value, never a wait) | **The generation and timeline values themselves** — published by the owner rows at the commit barrier (§2.3, §2.4) |
| **Parity per row**: `NativeDispatchCandidate.parity` (0 = Deterministic default, 1 = Lossy) with the meaning fixed in assembler doc §6.2 — Lossy relaxes order (tick placement / budget demotion, soft precedence-policy edges, delta merge order) and never the conflict relation | **The declared carrier** for the flag does not exist yet (`SystemDecl` has no field; assembler owner decision D3). §3 of this doc uses the §6.2 meaning verbatim and adds nothing to it. [OD-3] |

Net: the assembler dissolves the ORDER-able class outright and dissolves the PARTITION-able class *together with*
§2.1's declared keys — it decides *when* and *how many*; the declaration decides *who owns what*. That split is
what keeps both designs free of a runtime owner-assignment object.

### 4.3 Smooth degradation is structural

A subsystem that has not migrated contributes a row with a **whole-slot access set and no partition key**. The
scheduler already treats such a stage as touching every index and applies the conservative whole-stage barrier
(`Dispatcher.h:213-218`: "fall back to stage-granular StagesConflict … the conservative-correct whole-stage
barrier"). The unmigrated subsystem therefore runs *serialized against anything that overlaps it* and *parallel with
everything that does not* — correct on day one, faster as it declares finer access. This is the capability-based
pattern VIXEN already uses for device features (`CapabilityGraph.h:177` [MEASURED]) applied to scheduling: a
missing capability degrades the schedule, it never hard-fails it. During migration a subsystem may keep its mutex
inside a whole-slot row; the mutex is then provably uncontended (the row is a barrier) and is deleted once the row
gains an access set — the deletion is a *declaration change*, verified by the codegen conflict table, not a leap
of faith.

### 4.4 The re-monolith guard, applied

| Temptation | Why the design refuses it |
|---|---|
| a `LockFreeManager` / `OwnershipRegistry` | ownership is a column on each row; the assembler reads rows; no object aggregates them at runtime |
| a global delta buffer | per-declaration channels with per-producer segments (delta doc §5.2 verbatim) |
| a central cache manager | each cacher publishes its own generation; `MainCacher` shrinks to a frozen factory table + lifecycle rows |
| a "GPU submit manager" | one owner **row** per physical queue; it owns nothing but the channel it drains |
| a scheduler inside VIXEN | none: VIXEN's `TBBVirtualTaskExecutor` and the kernel's `TaskExecutor` already share the Tier-A executor shape; the `WaveSchedule` comes from the assembler, VIXEN only executes waves |

---

## 5. Prioritized elimination roadmap (CPU-first, gated)

Priority follows the inventory's fan-in ranking, re-ordered so each phase is a coordinated ownership change with
its own gate, and so nothing GPU-gated blocks a CPU win. **Gate vocabulary:** *1/2/N* = byte-identical frame/state at
worker counts 1, 2, N (the Multicore determinism gate); *VL* = Vulkan validation layers clean; *census* = the
inventory's reproduction script reports the phase's families absent from `libraries/`. No phase starts before the
previous phase's gate holds; no phase changes authored surfaces.

| Phase | Families | Mechanism | Gate | Why here |
|---|---|---|---|---|
| **0 — instrument + frame-as-`WaveSchedule` skeleton** | none removed | host phases (input/window/upload/submit/present) declared as whole-slot rows and run through `RunSystemWaves` (the opt-in TBB path, `RenderGraph.h:1016`); the inventory's measurement prescription (owner, acquisition count, wait/hold time, worker, queue, phase) added as a build-flagged census | 1/2/N with the sequential executor and the wave executor producing identical frames; acquisition counts recorded per family | establishes the plan shape and the baseline numbers §9 needs; nothing is lock-free yet, nothing can regress |
| **1 — queue owner + recording pools** | VK1, VK2, RM9 (+ SkyProjection unguarded path, WaitIdle under guard) | §2.5 owner row; RM9 per-worker per-frame-slot pools; `SubmitRecord` channel; timeline values replace `WaitIdle` in frame paths | 1/2/N, VL, census(VK1/VK2/RM9); `SubmitMutex` deleted from `VulkanDevice` | inventory priority 1: reaches 19 node files + upload/UI/cache helpers; it is also the first piece of `Backend::GpuCompute` |
| **2 — upload/staging/lifetime lanes** | RM7, RM8, RM10, RM11, RM12, RM1, RM13, RM2 | §2.1 lanes + §2.2 channels + §2.3 retire row after timeline | 1/2/N, VL, census; upload throughput not below baseline at N=1 | inventory priority 2 + 9; depends on phase 1's timeline discipline |
| **3 — EventBus** | EB1, EB2, EB3 | per-producer segments; subscriber generation; folded stats; `PublishImmediate` on generation | 1/2/N; a handler that subscribes/unsubscribes/publishes-immediate no longer deadlocks (test from finding #3); census | inventory priority 3 + 4; hot per-publication; independent of GPU |
| **4 — cache publication** | C1 (15 cachers), C3, C4, SH1, RM4, C2, C6, RG6, SH7, SH8, SVO4 | §2.4 per-key builder + generations; frozen factory tables; RM4 reserve-CAS admission; content-boundary precompute for declaration-derivable keys [OD-5] | 1/2/N; the six same-key wait paths pass a concurrent same-key stress test; census | inventory priority 5 + 6; unlocks parallel resource creation safely. **Status: C1 (15 cachers) landed — `2026-09-08-lockfree-p4-cache-generations.md`; C3/C4/SH1/RM4/C2/C6/RG6/SH7/SH8/SVO4 open (handoff in that doc)** |
| **5 — host phases + world** | RG3, RG4, RG5, APP1, SVO3, RM3 | §2.3 rows; APP1 evaluate/apply split; SVO3 generations | 1/2/N; input semantics test (press/release/cursor-at-click, pause accumulation) unchanged unless [OD-8]; cold-start body bake wall time not above baseline | inventory priority 7 + 8 + the "small cleanups"; needs phase 1 for GPU-referenced retirement |
| **6 — dormant families, on activation only** | SH2, SH3, SH4, SH5, SVO1, SVO2, EB4, GA1, C6, RM13 | conformance to §2 on the day they gain a caller; or retirement [OD-9] | a **declaration gate**: no `std::mutex` may be added or re-activated under `libraries/` without a candidate row (the census script as a CI check) | inventory: activation evidence first; do not displace measured work |
| **7 — GPU-gated** | GPU1; GPU delta-apply for RM channels | key-owned aggregation (sort/group then reduce); resident buffers + delta upload (delta doc slice #4) | `Backend::GpuCompute` landed; GPU parity per WorkUnit C-5 | CPU-first ruling |
| **demonstrations, any time** | RG1, KD1, RG7 | per-task error slots (K3) — small, cold, and the cleanest illustration of the pattern | 1/2/N error order deterministic | pattern proof, not throughput |

**Cross-repo sequencing.** Phase 1-3 need nothing from the kernel beyond the executor VIXEN already has; they can
run with a *static* wave table while the assembler lands (the static plan is the degenerate assembler — "active set
= one level, all scopes", N-scope doc §0). Phases 4-5 benefit from assembler-assigned lanes/ordinals but can use the
deterministic fallback of §4.2. Phase 7 waits on `Backend::GpuCompute`, whose first slice is phase 1.

---

## 6. Correctness constraints carried forward (the bugs this design must not reintroduce)

| Inventory finding | How the design closes it | What would reintroduce it |
|---|---|---|
| #1 unguarded `fpQueueSubmit2_` (`SkyProjectionNode.cpp:565`); idle handling outside guard (`PresentNode.cpp:144`) | nodes have no queue handle; submission is a record; idle = quiescent row | handing any row a `VkQueue` "for convenience" |
| #2 six same-key `future.get()` under shared lock (CashSystem) | no shared lock; one builder per key; waiters hold nothing | an override that "caches locally under its own mutex" |
| #3 callback under nonrecursive mutex (EB2, GA1, SH6) | handlers run on the owner, unlocked; registry edits are next-epoch deltas | applying a subscription edit synchronously inside dispatch |
| #4 SH2 nested nonrecursive acquisitions, pointers outliving scope | no lock; program table is a generation handle | returning raw pointers into a generation that can retire |
| #5 lifecycle order held across waits (MainCacher persist/clear) | persist = snapshot of a generation; clear/teardown = retirement barrier rows | I/O inside a row that other rows depend on in the same wave |
| #6 inconsistently covered state (bus filter counters, memoizer flags, arena bypass) | every counter is a per-producer partial folded at seal; every flag is a generation field | "just an atomic" on a field that also lives in a generation |
| #7 `.session.lock` is a marker, not exclusion | out of scope for runtime locks; multi-process cache-directory ownership is an OS-level file lock or a per-process namespace [OD-10] | assuming the generation model protects the *directory* |

---

## 7. The residual genuinely-needed set (honest, minimized)

| Residual | Form after this design | Is it a scaling wall? |
|---|---|---|
| Host access to each physical `VkQueue` | one owner row per queue; serial by schedule, no mutex; submissions batched | no — recording is parallel; the owner does O(records) bookkeeping + a handful of `vkQueueSubmit2` per tick. It becomes a wall only if a tick needs more submits than one core can issue, which is a queue-count question (more owners), not a lock question |
| GPU completion | fences/timeline semaphores observed at the barrier | no — never waited on in the frame path after phase 1 |
| VMA internal synchronization | present until [OD-6] flips the externally-synchronized flag behind the per-device allocation owner | bounded to allocation rows; not on the per-frame path once staging is lane-owned |
| Gaia process-wide chunk allocator (APP1 apply row) | one structural-apply row | cold-start only; cache hit bypasses it |
| glslang process init (SH7) | a tick-0 row | never on the frame path |
| GLFW main-thread affinity (RG3/RG4 pump rows) | main-thread-affine rows | inherent to the windowing API; two rows per tick |
| TBB / standard library / driver internals | uncounted (inventory scope) | not addressable from first-party source; measure, do not assume |
| Exception-path error collection (RG1/KD1) | per-task slots (K3) — a fold, not a lock | cold |
| Asynchronous wakeup for persistent private workers (SH4, EB4) | **eliminated by not keeping persistent private workers** — builds become schedule-dispatched rows in the TBB arena. If the owner keeps them [OD-9], a CV protocol remains and is the one honest "sleep lock" in the process | isolated to shader build tooling |
| Multi-process cache-directory ownership | OS file lock or per-process namespace [OD-10] | not a runtime lock |

"Lock-free by construction" here means what the inventory says it means: correctly proved task ownership and
scheduling — not a formal wait-free progress guarantee for every library the process links.

---

## 8. Owner decisions surfaced (need ratification; not mechanically decidable)

| # | Decision | Recommendation |
|---|---|---|
| **OD-1** | **Partition-key column of §2.1** — in particular whether the upload lane key is `(worker ordinal, size class)` or `(frame-slot, size class)`. The assembler has ruled it assigns no owner ids, so the key is derived by VIXEN from the candidate row; ratify the derivation rule `(candidate index, item index)` / frame-slot / declared id | worker-ordinal lanes (matches recording-pool partitioning); derivation from the candidate row, no runtime assignment anywhere |
| **OD-2** | **Cross-producer ordering contract** for every delta channel = the producer's static candidate index (+ item index), never arrival; **OD-2b** recipe family representative = `min(recipeId)` | ratify both; they are the same rule the delta doc's envelope already encodes, with the ordinal now static |
| **OD-3** | **Which telemetry families are `Lossy`** (RG2, RG7, EB3, SH6, C2 candidates in §3) — and the flag's **carrier**, which does not exist yet (assembler decision D3: `SystemDecl` has no field; VIXEN's acceptor sketch has `Parity kParity`) | RG2/EB3/SH6/C2 Lossy; RG7 Deterministic while tracking is enabled; one carrier for both repos, decided with D3 |
| **OD-4** | **Queue-owner placement**: one submit row per wave that produced records (lower latency, more submits) vs one per tick (max batching); present as part of the last submit row or its own row | per-wave when any record signals a value another wave in the same tick waits on, else per-tick; present in its own final row |
| **OD-5** | **How much of CashSystem becomes build-time precompute through the content boundary** (pipelines/layouts/samplers/render passes are derivable from declared contracts + SDI; textures/meshes/AS are not) | precompute the declaration-derivable four; runtime per-key builders for the rest |
| **OD-6** | **Set `VMA_ALLOCATOR_CREATE_EXTERNALLY_SYNCHRONIZED_BIT`** once allocation is routed through the per-device owner | yes, after phase 2's VL-clean 1/2/N gate; never before |
| **OD-7** | **`PublishImmediate` semantics**: synchronous on the caller against the current subscriber generation (recursion allowed, edits deferred), or converted to a queued delta | keep synchronous on the generation; `AppFlowRuntime` depends on immediacy |
| **OD-8** | **Input latency**: keep pump-in-Render → fold-next-Update (today's semantics) or move the pump row before the fold | keep; a latency change is a separate, explicitly approved change (inventory) |
| **OD-9** | **Dormant families** (SH2-SH5, SH8, SVO1/2/4, EB4, GA1, C6, RM13 — no located shipping caller): migrate-on-activation under the declaration gate, or delete now | gate now; delete `ShaderLibrary`'s persistent worker model (SH3/SH4) in favor of plan-dispatched jobs when shader hot-reload is next touched |
| **OD-10** | **Multi-process cache directory** ownership | per-process namespace by default; OS file lock only if sharing is a requirement |
| **OD-11** | **The declaration gate**: no new `std::mutex`/`shared_mutex`/`recursive_mutex` under `libraries/` without a candidate row + classification (CI reuses the inventory's census script) | adopt at phase 0 so the count can only go down |

---

## 9. The measurable target (qualitative — numbers come from phase 0, not from this document)

**What the ceiling is today [INVENTORY, MEASURED where cited].** The per-frame parallel capacity of VIXEN's runtime
is bounded by: a device-wide map lock taken on *every* queue lookup (`VulkanDevice.cpp:389`), then one queue mutex
shared by nineteen node files plus upload/UI/cache helpers, held in two places across `vkQueueWaitIdle`; five to
six critical sections per upload item; two to three per published message; a shared lock per cache hit across
fifteen resource types; and a sequential-by-default graph executor. Lock *hold time*, not data dependency, sets the
width.

**What this design changes the ceiling to.** After phases 1-4, the steady-state frame path in `libraries/` performs
**zero mutex acquisitions**; its parallel width is the **wave width** the access manifests admit — the same bound the
kernel's native dispatch already has — and its serial floor is the sum of the owner rows (queue submit, input/window
pump, structural apply), each O(records) per tick. Recording, uploads, cache hits, event publication and telemetry
scale with worker count up to the manifest's width; worker count 1/2/N produces identical bytes.

**What to measure (phase 0 instruments; each later phase reports the delta):**
1. mutex acquisitions per frame per family (target: the phase's families → 0; total → residual set only);
2. wait and hold time per family, per phase (the inventory's prescription);
3. wave width per tick (rows per wave, items per wave) and the fraction of rows still whole-slot (the degradation
   residue — it should fall phase by phase);
4. `vkQueueSubmit2` calls per tick (batching effect of the owner) and time-to-present at N=1 vs N=hardware;
5. worker-count scaling of frame time at 1/2/4/N with byte-identical output (the only speedup claim this design
   will make is one the 1/2/N gate has witnessed).

No speedup number is asserted here: the inventory measured no wait time, and this design invents none.

---

## 10. Summary

- **Four primitives, all pre-existing** — owner (K2), delta channel (undertow `DataPipe` envelope), wave/commit
  barrier (K1 + the in-flight assembler), generation (content-boundary immutability) — plus the C-5 parity flag as
  the per-row relaxation knob, dissolve **46 of 47** host mutex families and the GPU claim protocol, class by class,
  with a concrete mechanism per family (§2).
- **The one hardware-mandated serialization** (physical-queue host access) becomes a **queue-owner row** — serial by
  schedule, lock-free by construction, batching as a side effect, and the first concrete piece of
  `Backend::GpuCompute` (§2.5).
- **The design composes with the wave assembler** as two views of one deterministic-dispatch thesis: VIXEN
  contributes declared rows; the assembler emits waves, lanes, ordinals and retire points; unmigrated subsystems
  degrade to whole-slot rows, which the executor already handles conservatively (§4).
- **The roadmap is CPU-first and gated** (§5): instrument → queue owner + pools → upload lanes → EventBus →
  cache publication → host phases/world → dormant-on-activation → GPU-gated. Every phase gates on 1/2/N
  byte-identity, validation layers, and the census.
- **The residual set is named and minimized** (§7); **eleven owner decisions** are listed with recommendations
  (§8); the **target is stated as what to measure**, not as a number (§9).

**STOP — awaiting owner ratification of the per-class mechanisms (§2), the parity-relaxation rule (§3), the
assembler composition contract (§4), the phase order and gates (§5), and OD-1…OD-11 (§8) before any engine code
moves. Phase 0 (instrumentation + whole-slot rows) is the only work that can start on ratification of §5 alone.**

## CONSOLIDATION ISSUES (non-facade deltas this lane needed)

- **None introduced.** Design-only; no engine, tooling, generator or setup change was made. §4 was aligned to the
  committed wave-assembler doc's vocabulary (`WaveSchedule` / wave / row / candidate; "plan" = static artifact only)
  before this commit; no term is used here that the assembler doc does not define.
