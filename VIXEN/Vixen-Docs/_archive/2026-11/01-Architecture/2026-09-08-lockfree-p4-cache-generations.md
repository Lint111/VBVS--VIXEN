---
title: Lock-free federation P4 — cache publication as immutable generations (C1, the 15 TypedCachers)
status: LANDED for C1 (MEASURED); handoff for the rest of the CACHE/MEMO class
created: 2026-09-08
lane: lockfreep4 (branch lockfreep4, base b6c99922 = phase 0)
composes-with:
  - 2026-09-08-lockfree-federation-architecture.md   (§2.4 mechanism; §5 phase 4 row)
  - 2026-09-08-lockfree-phase0-baseline.md           (census instrument + the before numbers)
  - 2026-09-08-runtime-lock-inventory-and-classification.md  (C1 row; finding #2 = the six same-key paths)
---

# P4 — cache publication as immutable generations

Phase 4 of the lock-free federation, CACHE/MEMO class, family **C1** (`TypedCacher::m_lock`, inherited by the
15 cachers). The design's §2.4 mechanism is now the base class: **one builder per key, waiters hold nothing,
generations are immutable, invalidation is an epoch flip, retirement is deferred to the last reader.** The
shared lock is deleted; nothing derived can spell the bug class any more because the members it needed
(`m_lock`, `m_entries`, `m_pending`) do not exist.

## 1. The bug this closes (MEASURED on the clean base)

The inventory's finding #2: six derived `GetOrCreate` overrides copied the base's pre-check and kept the
pre-AR#51 shape — the same-key wait `pit->second.get()` **inside** the `shared_lock` scope
(`pipeline_cacher.cpp:108`, `shader_module_cacher.cpp:54`, `pipeline_layout_cacher.cpp:23`,
`SamplerCacher.cpp:63`, `MeshCacher.cpp:69`, `RenderPassCacher.cpp:56`). The base creator had to take the
lock exclusively to publish (`TypedCacher.h:118` on the base), so builder and waiter deadlocked permanently.

Witness (clean base b6c99922, `test_typed_cacher_concurrency` + a subclass reproducing the override
verbatim, two threads, same key, builder held in `Create()` until the waiter entered its wait):

```
[ RUN      ] TypedCacherConcurrency.WITNESS_DerivedOverrideSameKeyWaitUnderSharedLock
  WITNESS: derived-override pattern deadlocked (waiter holds shared m_lock across future.get()).
[  FAILED  ] ... (5000 ms)      <- watchdog; the two threads never return
```

The witness cannot be compiled against the P4 tree — there is no `m_lock` to hold — which is the closure
argument, not a test that happens to pass.

## 2. Mechanism (what `TypedCacher.h` now is)

| piece | shape | why |
|---|---|---|
| **Slot** | `CacheEntry{key, ci, resource}` + `atomic<uint32> state` (`Pending / Ready / Failed / Retracted / Erased`) + `exception_ptr` | key/ci written before the slot is published; `resource` written once before the release-store to `Ready`; readers acquire-load `state` first. |
| **Generation table** | open-addressed array of `atomic<Slot*>` cells (initial 64, power of two), Fibonacci-mixed probe from the key; cells go **null → slot, never back**; a `claims` counter admits at most `capacity/2` claims | a probe always terminates; a published cell is never moved, copied or unlinked, so a reader mid-probe is never invalidated. |
| **Lookup (hit)** | load head, probe newest → oldest table, return `resource` if `Ready` | zero synchronization: one seq_cst pointer load + cell loads. |
| **Claim (miss)** | lookup-miss → `claims.fetch_add` (≥ threshold ⇒ grow, retry) → CAS a null cell to my slot → **re-check `m_head` is still my table** (else retract, retry) | the single atomic the design allows: "a CAS on the pending slot". The head re-check is a Dekker pair with the grower's post-swap lookup, so a claim that lands in a just-superseded table can never become a second builder for the key. |
| **Wait** | `state.wait(Pending)` on the builder's slot — a futex on the slot word, **holding nothing** | the design's "waiters wait on the key's future holding nothing", without a `promise`/`future` pair or any map to look it up in. Retracted/Erased wakes retry the lookup; `Failed` rethrows the builder's exception (the old code left the promise unfulfilled: waiters hung forever and the key was never creatable again). |
| **Grow** | new table 2×, `prev` = old, CAS-published; old tables stay in the chain and are probed after the new one | no migration, no copy; chain depth is log₂(n/64). |
| **Erase / failed build** | state flip to a tombstone; the next requester claims a fresh cell (probing skips tombstones) | invalidation never erases in place. |
| **Clear** | `exchange` an empty generation in; the old chain is pushed on a retire stack freed with the cacher | "retirement waits for the last reader" — today's quiescence point for these caches is teardown (`Cleanup()` → `Clear()`); the assembler's wave boundary can replace the retire stack later without touching readers. |
| **Derived-class surface** | `Find(key)`, `ForEachEntry(fn)`, `Snapshot()`, `Publish(key, ci, resource)`, `EntryCount()` | replaces the 30 direct `m_entries`/`m_lock` uses (Cleanup walks, Serialize/Deserialize, `QueueTLASUpdate`, the two `m_globalCache` reads). Serializers take a `Snapshot()` so the count they write equals the rows they write. |

Same-key rule (design §2.4): equal key ⇒ interchangeable value, so no arbitration beyond the claim CAS is
needed; `Publish()` (deserialize / `Insert`) also defers to an already-live slot for the key, and
`GetOrCreateFromSpirv` destroys its own `VkShaderModule` if it lost the publish.

**Not done here (by design):** publication is per-miss, not "at the barrier" — there is no assembler barrier
to publish at yet, and retirement is at teardown, not "the wave after the last reader". Both are the
`Clear()` seam above; neither changes a reader.

## 3. Gate (all MEASURED, WSL2 / GCC 13 Release / Mesa Dozen, `-DVIXEN_LOCK_CENSUS=ON`, 120 frames)

**Same-key stress** — `libraries/CashSystem/tests/test_typed_cacher_concurrency.cpp`, 9 tests, all pass;
30 repetitions × 9 = 270/270; the whole CashSystem ctest set 114/114:

| test | proves |
|---|---|
| `SameKeyStress_OneBuilderPerKey_NoDeadlock` | 200 rounds × 16 threads on the same fresh key, slow builder: 200 builds exactly, every waiter gets the builder's instance, no watchdog trip |
| `WaitersHoldNothing_UnrelatedKeysProgress` | with a waiter parked on key 1, key 2 builds and publishes and `Has()` answers — a held read-lock would have blocked the publish |
| `FailedBuildReleasesWaitersAndRebuilds` | builder throws → waiter gets the exception → next request rebuilds (2 builds) |
| `ManyKeysManyThreads_ExactlyOncePerKey_AcrossGrowth` | 8 threads × 4096 keys in shuffled orders (six table growths): 4096 builds, every key reachable, hits never rebuild |
| `EraseTombstonesAndRebuilds`, `ClearDuringPendingBuildIsSafe`, `CleanupDuringConcurrentInsertsIsSafe` | tombstone semantics; epoch flip under a pending build delivers to its waiter and does not free the chain under readers; unlocked teardown walk racing publications |

**Census** (`scripts/lock-census-sweep.sh` shape, per-leg capture dir; `p4-sweep.sh` in the lane scratchpad):

| family | leg | before (b6c99922) acq / shared / peak | after (P4) acq / shared / peak |
|---|---|---|---|
| **C1** | sequential, w1, w2, w4 | 0.342 / 0.342 / 82 (seq 0.350 / 0.342 / 83) | **0.000 / 0.000 / 0** |
| C3 | all | 0.150 / 0.983 / 136 | 0.150 / 0.983 / 136 (untouched — handoff) |
| C4 | all | 0.392 / 0 / 47 | 0.392 / 0 / 47 (untouched — handoff) |
| total instrumented | w4 | 68.35 / frame | 67.67 / frame |

The declaration gate (`scripts/check-no-new-mutex.sh`) PASSes with the allowlist re-synced: 93 → 80
mutex-syntax files, 48 → 47 owning declarations; `TypedCacher.h` row is now `1 C2` (the `_DEBUG`-only
collision-check mutex), the 14 `C1` `.cpp` rows are gone, `AccelerationStructureCacher.cpp` is `VK2-borrowed`
only.

**1/2/N byte-identity** — `VIXEN_HUD_CAPTURE_FRAMES=100` per leg: all eight captures (sequential, w1, w2, w4 ×
before/after) are `md5 8163ab76e46cb7eebc44a24740fc970d`. **VL** — 0 validation hits in every leg's log.

**Frame time** — not a claim of this phase: C1's steady-state hold was ≈0 before (the 100-130 µs/frame hold is
frame-0 creation amortised), and the sweeps ran on a box shared with other lanes' builds (mean frame CPU
10.1-12.0 ms both before and after, noise-dominated). The serial floor is still VK2 (~5.5 ms hold/frame).

**Build** — 0 compile errors, 2 warnings on the rebuilt set, both pre-existing `SetBudgetManager` deprecation
warnings (`RenderGraph.cpp:516`, `DeviceNode.cpp:602`). Three `codegen/*_check` golden targets fail on the
**clean base** identically (`recipe_simd_check` STALE vs kernel main, `appflow_check` and
`view_noun_enum_check` abort on `--view-executor-consumer` argument skew): kernel-main drift against the
engine's pinned kernel SHA, outside this lane.

## 4. Handoff — the rest of the CACHE/MEMO class

| family | shape (design §2.4) | what it needs |
|---|---|---|
| C3 `MainCacher::m_globalRegistryMutex` | frozen factory table after an init row; global cachers eagerly published | an explicit `cache.init` → `cache.persist` → `cache.teardown` row order; `RegisterCacher` becomes an init-phase batch; `GetDeviceIndependentCacher` a lookup in a frozen table. The persistence paths already hold nothing across I/O except this lock. |
| C4 `MainCacher::m_deviceRegistriesMutex` | immutable outer directory built at device set-up; per-device row is the owner | device set-up publishes the directory; `ClearDeviceCaches` becomes the device's retirement barrier. |
| C2 `m_debugMutex` (`_DEBUG`) | per-builder diagnostics folded at seal | fold `CheckCollision` into the builder slot (the slot already carries `ci`; the content hash can live on it). |
| SH1, RM4, C6, RG6, SH7, SH8, SVO4 | as tabled in §2.4 | each is a separate owner; none blocks on C1. |
| **[OD-5] precompute** | declaration-derivable keys (pipelines, layouts, samplers, render passes) built once at startup | the generation table accepts `Publish()` from a precompute row today; the missing piece is the key list on the content boundary. |
| **barrier publication / wave retirement** | `Clear()` seam | when the assembler lands: publish pending slots at the commit barrier (today per-miss), free retired chains at the wave after the last reader (today at destruction). |
