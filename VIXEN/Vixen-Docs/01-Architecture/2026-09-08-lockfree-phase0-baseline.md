---
title: Lock-free federation phase 0 — instrument + declaration gate, and the first baseline numbers
status: MEASURED (phase 0 deliverable; the numbers §9 of the design asked for)
created: 2026-09-08
lane: lockfreep0 (branch lockfreep0, base 8985cf39)
composes-with:
  - 2026-09-08-lockfree-federation-architecture.md   (§5 phase 0 = this; §9 = the five measurements)
  - 2026-09-08-runtime-lock-inventory-and-classification.md  (family IDs; the census reproduces its file list)
---

# Lock-free federation phase 0 — baseline

Phase 0 removes no lock. It makes the current state measurable and gates regressions:

1. **Instrument** — `libraries/Core/include/LockCensus.h`. A build-flagged (`-DVIXEN_LOCK_CENSUS=ON`)
   counting wrapper that replaces the *declaration* of each frame-path mutex
   (`Vixen::LockCensus::Mutex<Family::VK2>` etc.); acquisition sites are untouched except for dropping an
   explicit `<std::mutex>` template argument. Off by default the wrapper is a type alias of the std
   type — no code. On, each acquisition records count, contention (try_lock probe), wait and hold
   time per inventory family; `RenderGraph::RenderFrame` samples the counters once per frame together
   with the executed wave shape and prints a mean-per-frame summary at graph teardown
   (`VIXEN_LOCK_CENSUS_CSV=<path>` for a per-frame CSV). Families instrumented (18 of 47): VK1, VK2,
   EB1-3, RM1, RM7-12, C1, C3, C4, RG2-4 — the inventory's "what actually serializes" set.
2. **Gate** — `scripts/check-no-new-mutex.sh` + `scripts/no-new-mutex.allowlist`, wired into every
   build as the `no_new_mutex_check` target. Engine-side twin of the kernel's R-B gate: FAILs on a
   mutex-bearing file absent from the allowlist, on more owning declarations than pinned, or on a
   stale pin; WARNs (exit 0) on rows classified `UNCLASSIFIED`; `--fix` re-syncs. See `tools/README.md`.
3. **Sweep** — `scripts/lock-census-sweep.sh <census binary> <out> <frames>` runs sequential +
   lowered w=1,2,4 legs and collects summaries/CSVs.

## Baseline (this base, WSL2 / GCC Release / Mesa Dozen over D3D12, default scene, 120 frames)

Environment caveat: Dozen (Vulkan-over-D3D12 in WSL2) is not the shipping driver; absolute ms are
indicative, the per-frame acquisition counts and the *shape* (which family holds the frame) are the
point. Frame 0 (graph compile, ~240 ms) is included in the "mean" rows; steady-state = frames 20-119.

| leg | waves/frame | rows/frame | max wave width | whole-slot rows | steady median CPU ms | VK2 hold median ms | contended (all families, 120 fr) |
|---|---|---|---|---|---|---|---|
| sequential (`VIXEN_GRAPH_LOWERED=0`) | 0 | 126 | — | 126 | 9.38 | 4.02 | 0 |
| lowered w=1 | 115 | 126 | 9 | 126 | 10.74 | 4.39 | 0 |
| lowered w=2 | 115 | 126 | 9 | 126 | 10.25 | 4.15 | 1 |
| lowered w=4 | 115 | 126 | 9 | 126 | 10.15 | 3.98 | 0 |

Per-family, lowered w=4 (mean per frame over 120 frames; `hold` is exclusive hold time):

| family | acq/frame | shared/frame | peak/frame | hold us/frame | reading |
|---|---|---|---|---|---|
| VK1 `submitMutexMapLock_` | 11.09 | — | 22 | 6.2 | one directory lock per queue lookup, every submit |
| **VK2 per-queue submit mutex** | **12.14** | — | 29 | **5211** | **~5.2 ms of a ~10 ms frame is spent holding the queue mutex** — the `vkQueueWaitIdle`/present-under-guard paths the inventory named |
| EB1 queue | 5.04 | — | 10 | 1.2 | |
| EB2 subscriptions (handlers under lock) | 2.28 | — | 36 | 6.7 | |
| EB3 stats | 7.07 | — | 15 | 2.1 | |
| RM1 deferred destruction | 1.01 | — | 2 | 0.2 | |
| RM7 / RM8 staging bucket / records | 0.25 / 0.43 | — | 30 / 52 | ~0.2 | upload bursts, not steady |
| RM9-RM12 uploader | 0.02 / 0.13 / 0.05 / 0.23 | — | ≤28 | ≤0.2 | |
| C1 cacher (shared) | 0.34 | 0.34 | 82 | 150.6 | hold is frame-0 creation; steady ≈ 0 |
| C3 / C4 MainCacher (shared) | 0.15 / 0.39 | 0.98 / — | 136 / 47 | ~153 | same — startup |
| RG2 profile samples | 23.38 | — | 77 | 2.4 | highest acquisition count on the frame path |
| RG3 / RG4 input / window | 1.0 / 2.0 | — | 1 / 3 | <0.3 | |
| **total (instrumented)** | **68.3** | | | | |

## The five §9 measurements — status

| # | measurement | phase 0 status |
|---|---|---|
| 1 | mutex acquisitions per frame per family | **taken** — 68.3/frame across the 18 instrumented families; per-family above |
| 2 | wait and hold time per family | **taken** — hold per family; wait/contention ≈ 0 at w≤4 on this driver (the width is bounded by hold, not by contention — as the design predicted) |
| 3 | wave width per tick + whole-slot fraction | **taken** — 115 waves for 126 rows (max width 9, mean 1.1); whole-slot fraction = 100 % by construction (graph node tasks carry no per-item access set) |
| 4 | `vkQueueSubmit` calls per tick, time-to-present | **proxy only** — VK2 acquisitions (12.1/frame) bound the submit+present count; an exact count needs the phase-1 queue-owner funnel (today 25 call sites + the unguarded `SkyProjectionNode.cpp:565`). Time-to-present is in the existing `VIXEN_PERF_CSV` writer, not duplicated here |
| 5 | 1/2/4/N scaling with byte-identical output | **timing taken** (table above: w1→w4 = 10.74→10.15 ms, i.e. the frame does not scale — its serial floor is VK2's hold); **byte-identity not asserted** — pair the sweep legs with the capture/golden tools when a determinism claim is made |

## Drift since the inventory base (178b838b → 8985cf39) the census surfaced

- `RenderGraph/…/TBBVirtualTaskExecutor.{h,cpp}` are gone (RG1 with them); the lowered frame runs on
  `KernelDispatch::TaskExecutor`, which now carries **three mutex families the inventory does not
  have** (`BatchState::mutex`, `blockingMutex_`, `asyncMutex_` — CV-paired, so not wrappable) →
  allowlist rows `UNCLASSIFIED`, gate WARNs until the inventory gains rows for them.
- `KD1 errMutex` no longer exists in `TaskExecutor.cpp`.
- `SVO/BulkMaterialization.{h,cpp}` no longer carry mutex syntax (SVO1/SVO2 gone).
- `ProxyRasterStageNode.cpp` is a new VK2 borrower (20 node files now, not 19).
- Census: 813 files scanned, 93 mutex-syntax files (inventory: 785 / 98), 48 owning declarations.

## What phase 1 should read from this

VK2's hold time is the frame's serial floor on this base; nothing else on the instrumented path holds
for more than ~7 us/frame in steady state. The queue-owner row (design §2.5) is therefore not only
the first correctness win (unguarded submit path) but the first measurable one; the census columns
`VK2_hold_us` and `frame_cpu_ms` are the before/after pair.
