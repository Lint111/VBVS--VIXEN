---
title: Baked-Content Render-Path Performance — Consolidated State
status: active investigation
created: 2026-07-16
---

# Baked-Content Perf — Consolidated State (2026-07-16)

## 0. End-state target (user, 2026-07-16)

A simple scene like the tested Cornell box must run at **realtime capability**. That is
stricter than closing the virtual↔baked gap: the ~52-FPS exp-A ceiling components
(per-frame debug_capture readback, DDGI probe budget, 8-octree descent, CPU/GPU sync
stalls) must fall too, not just the brick-march. **All capability benchmarking runs
Windows-native** (`cmd.exe` → `binaries\VIXEN.exe`, real driver) to avoid WSL
cross-mount/translation cost; bench rig: `temp_bench/` at repo root
(`VIXEN_PERF_CSV` + tick-150 HUD capture + `[CornellDiag]` map).

Consolidates the multi-session, multi-agent investigation into why the baked stored-SDF
Cornell demo (`VIXEN_DDGI_CORNELL_BAKED_DEMO=1`) is orders of magnitude slower than the
analytic virtual variant (`VIXEN_DDGI_CORNELL_VIRTUAL_DEMO=1`, ~126 FPS). One session
stalled; a background agent continued in a separate worktree. This doc is the single
source of truth for what is proven, what is refuted, and where every piece of work lives.

## 1. The gap (validated numbers)

**Fresh Windows-native baseline on main `1e853511` (2026-07-16, `temp_bench/` rig,
mean over frames 31–160):**

| Variant | wall ms/frame | FPS | GPU esvo pass | Correctness |
|---|---|---|---|---|
| Virtual analytic | 6.49 | **~154** | 2.8 ms | ground truth |
| Baked stored-SDF | 877.5 | **~1.1** | 222.4 ms | **all 8 bodies present** in instIdx map (0–7) — preserve this; OOB far-hit pixels still present (hitT≈77.5) |

Gap: ~135× wall, ~80× GPU. The 877−222=655 ms remainder is NOT mostly sync/readback:
the CSV instruments only the `test_dispatch` ESVO pass; the graph runs 3 more
un-instrumented compute passes (DirectLighting + 2 probe-atlas stages) whose
lighting/GI rays march the SAME stored SDF — most of the remainder is other GPU
passes paying the same brick-march tax (⇒ a march fix multiplies across all passes).
The debug_capture readback (checked 2026-07-16): fires every 10th frame only
(`PARAM_FRAMES_PER_EXPORT=10`, `BuildRenderGraph.cpp:3930`; early-return otherwise at
`DebugBufferReaderNode.cpp:75`), but on those frames does a blocking
`vkWaitForFences(UINT64_MAX)` pipeline drain + 256-trace JSON export
(`DebugBufferReaderNode.cpp:105`) — bounded ≈10% of the run, NOT the root cause
(exp-A hit 52 FPS with it running); must be gated off for capability benches.
Cheap follow-up: add the other 3 passes as PerfCsvWriter columns for full frame
attribution. Realtime target: 60 FPS ⇒ ~55× needed from 1.1 FPS.

Historical session numbers (older mains, WSL-launched):

| Variant | FPS | Source |
|---|---|---|
| Virtual analytic Cornell | ~126 | rootfix doc, uncontested |
| Baked Cornell, main baseline | **~0.9** (Opus validation) / 0.09 (one unreproduced session) | bench is session-noisy — ALWAYS measure your own before/after by toggling the change |
| Baked + `*subdiv` grid-unit fix | ~3.4 | both sessions agree on the absolute number |
| Exp A ceiling (march returns trivial hit at brick entry) | ~52 | rest-of-pipeline ceiling: 8-octree ESVO descent + DDGI probes + per-frame debug_capture readback |
| Red-worktree experiment runs (see §4) | wall 62–170 ms/frame; `esvo_traverse_shade` 25–42 ms | GPU pass ~30 ms but wall ~62+ ms → large sync/stall component |

## 2. What is PROVEN

- **Brick-march dominates.** Exp A (skip sphere-trace+trilinear, keep everything else):
  0.09→52 FPS. The cost is `marchBrickSdf` → `sampleSdfTrilinear` → 8× `_samplePoolVoxel`
  (`StoredSdf.glsl`), not CPU, residency, ESVO descent, or probe count.
- **World-vs-grid unit mismatch is real.** Cornell's `makeWorldSpaceEval`
  (`BuildRenderGraph.cpp`) bakes WORLD-space distance; the shared march steps in
  GRID-voxel units → understep of `subdiv`× (4× for walls) + occupancy band `subdiv`× too
  thick. Root-cause reasoning independently verified in code.
- **Fix magnitude is ~3.8×, not 38×.** The 38× claim rested on a 0.09 baseline the
  validator could not reproduce (their baseline: 0.9 FPS).
- **The `*subdiv` fix makes bodies VANISH.** `[CornellDiag]` instIdx map: baseline has
  light(5)/sphere(6)/box(7); with the fix all three ABSENT. Their octree data is
  byte-identical → a cross-body interaction in the concatenated-octree instanced march
  (walls' 4×-larger grid-unit Density shadows subdiv=1 bodies). OOB counter rose
  40081→62500. **Verdict: do NOT merge `fix/baked-sdf-perf-rootfix` as-is.**
- **Correctness ground truth** = virtual-variant render (gray sphere + gray box + ceiling
  light visible) + `[CornellDiag]` 25×25 instIdx map at tick 150. A "clean" baked
  screenshot can be clean because objects dropped out.

## 3. Where every piece of work lives

| Location | Branch / state | Contents |
|---|---|---|
| main `cd9e1362` | merged | box-tight SDF bake regions — real but partial (~1.15–1.2×) |
| `.claude/worktrees/baked-sdf-perf-rootfix` | `fix/baked-sdf-perf-rootfix` @ `a421c880`, 1 commit off `bf8dfbf5` | the `*subdiv` one-multiply fix + `Baked-SDF-Perf-Rootfix-2026-07.md` (full experiment table A–F). Blocked by §2 vanishing-bodies; also needs rebase (base predates box-tight merge) |
| `.claude/worktrees/cornell-baked-color-perf` | `fix/cornell-baked-color-perf`, no commits | `temp/` capture harness: `run_baked.bat` / `run_virtual.bat` / `run_baked_hops8.bat` + tick-150 captures — the correctness cross-check rig |
| `/mnt/c/tmp/vixen-baked-perf-red` | **salvaged 2026-07-16** → `fix/baked-perf-red-experiments` @ `6568c3ee` (39 files incl. CSVs) | background agent's work — see §4. Unvalidated snapshot, not merge-ready |
| `fix/baked-sdf-perf-codex` | worktree pruned (`/tmp` wiped), no commits | dead stream; branch can be deleted |
| main `VIXEN/temp_virtual/hud_capture_150.png` | untracked | virtual-variant ground-truth screenshot |

## 4. The background agent's work (red worktree) — UNCOMMITTED, UNDOCUMENTED

`/mnt/c/tmp/vixen-baked-perf-red`: 873 insertions / 222 deletions across 24 files, plus 14
per-experiment 60-frame CSVs. No write-up was left; this section is the reconstruction.

**Code changes (from the diff):**
- `StoredSdf.glsl` (+213/−52): single-brick fast-path trilinear cell load
  (`_loadSdfTrilinearCell` — direct offset fetches when the 2×2×2 cell is inside one
  brick, slow per-corner path only on brick boundaries), corner reuse for exact
  derivative at hit, bounds guards in `_samplePoolVoxel`, `brickLookupBase` exact-prefix
  concatenation (replaces the uniform `octreeIdx*bpa³` assumption — variable bpa support).
- `TraceWorld.glsl` (+51): per-octree conservative allocated-brick trace bounds
  (`getOctreeTraceBounds` from new `traceBoundsMin/Max` config fields, backward-safe
  zero-default) — culls the empty in-cube span grazing rays used to march.
- RenderGraph sync (`SwapchainBarriers.h`, `BlitNode.*`, `ComputeDispatchNode.*`):
  blit-source returned to GENERAL as a stable cross-node boundary layout; barrier
  srcStage properly chained off the acquire-semaphore wait stage (old TOP_OF_PIPE was a
  no-op source); swapchain layout tracking.
- `OctreeConfig.cs` schema + regenerated `.g.h`/`.glsl`; `MipBake.h`, `ShellDerive.h`,
  `ShellOctreeGpu.h` (+58); extended `test_stored_sdf_march_mirror` (+222) and
  `test_soa_sdf_serialize` (+111); new `test_blit_node.cpp`.

**Experiment results** (mean CPU frame ms over frames 31–60; CSV gotcha: the
`steady_state_fps` column is CUMULATIVE and dominated by the ~2-min boot frame — never
read it raw):

| Experiment | mean wall ms | mean esvo ms |
|---|---|---|
| layout_sync | **62.5** | 31.2 |
| trace_bounds | 65.2 | 30.9 |
| clean_sync | 65.8 | 31.0 |
| acquire_sync | 69.2 | 25.2 |
| shadow_trace_bounds | 69.2 | 25.2 |
| specialized_marchers | 72.9 | 26.2 |
| blit_sync | 76.2 | 26.4 |
| anyhit_parameter | 79.9 | 38.4 |
| shadow_anyhit | 79.9 | 38.5 |
| return_hit_cell | 85.9 | 32.5 |
| split_hit_shading | 87.1 | 36.5 |
| analytic_gradient | 104.1 | 27.0 |
| debughooks_off | 115.6 | 24.5 |
| fastpath_windows | 168.3 | 42.4 |

**Two headline signals:**
1. Frame times ALTERNATE ~2 ms / ~125 ms with GPU `esvo_traverse_shade` ≈ 30 ms — the
   wall clock is ~2–5× the GPU pass. A large fraction of steady-state frame time is
   CPU/GPU sync stall, not shader work. Consistent with sync experiments ranking best
   and with the engine's history here ([[Render-Graph sync reuse-while-pending fix]]).
2. At ~62 ms best-case these runs are ~16 FPS — far above the 0.9–3.4 FPS session
   baselines, i.e. the red worktree's combined changes (fast-path trilinear + trace
   bounds + sync fixes) plausibly already deliver a large multiple. UNVERIFIED for
   correctness (no instIdx-map / virtual-variant cross-check recorded).

## 5. Consolidated cost model (hypothesis stack, ranked)

1. **March step-scale unit mismatch** — proven, ~3.8×, but the landing fix must not
   shadow subdiv=1 bodies (open root-cause: cross-body interaction in the concatenated
   march).
2. **Trilinear fetch volume** (rays × steps × 8 SSBO fetches + redundant
   `channelBaseFloats` scan) — proven residual; red worktree's single-brick fast path +
   trace bounds attack this. Candidate deeper fix: brick pool as 3D texture w/ hardware
   trilinear (1 sample vs 8 fetches).
3. **CPU/GPU sync stalls** — new evidence (§4): wall 62 ms vs GPU 30 ms, alternating
   frame pattern; sync experiments best-ranked.
4. **Per-frame debug_capture GPU→CPU readback + full instance-SSBO re-upload** — part of
   the 52-FPS ceiling; see [[Instance-SSBO-Dirty-Upload-Direction-2026-07]].
5. **8-octree instanced ESVO descent** — remaining ceiling component.

## 6. Immediate next actions

1. ~~Salvage the red worktree~~ **DONE 2026-07-16**: `fix/baked-perf-red-experiments`
   @ `6568c3ee`. Still to do: validate its correctness with the §2 ground-truth rig
   before believing its numbers.
2. Root-cause the vanishing-bodies interaction (blocks the `*subdiv` fix).
3. ~~Full render-path audit~~ **DONE 2026-07-16**: 74 confirmed findings + 35 research
   patterns + ranked Top-10 action list in [[Baked-Content-Perf-Audit-2026-07]].
   Headline: `brickLookup` base mis-addressing for mixed `bricksPerAxis` (audit B1,
   `StoredSdf.glsl:78` vs `ShellOctreeGpu.h:993`) is the prime suspect for the
   vanishing-bodies blocker in §2 — small fix, unblocks the proven ~3.8×.
4. Prune `fix/baked-sdf-perf-codex` branch + stale worktree entry.

## Related docs

[[Baked-Content-Perf-Audit-2026-07]] (the verified findings + action list) ·
[[Baked-SDF-Perf-Rootfix-2026-07]] (in the rootfix worktree, not on main) ·
[[SDF-Bake-Box-Tight-Region-Plan-2026-07]] ·
[[Instance-SSBO-Dirty-Upload-Direction-2026-07]] ·
[[Sampled-Lighting-Cornell-Box-Demo-Plan-2026-07]] ·
[[BodyOctreeSceneNode-Intermittent-Baked-Hit-Corruption-Bug-2026-07]]
