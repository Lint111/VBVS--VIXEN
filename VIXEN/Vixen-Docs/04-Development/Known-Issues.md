---
title: Known Issues / Bugs To Fix
status: living log
created: 2026-07-02
tags: [known-issues, bugs, tech-debt]
---

# Known Issues / Bugs To Fix

Living log of confirmed-but-unfixed issues. Each entry: symptom, root cause, impact, fix options, severity. Add new issues at the top; move fixed ones to a `## Resolved` section with the fixing commit.

---

## KI-025 — Frame-1 accumulation artifact: a small patch renders sky-colored for ~5 frames before self-converging

**Discovered:** 2026-07-11, during Sampled Lighting Inc3 M2 (geometric reprojection reject) gate testing — surfaced incidentally, not caused by M2's change.

**Symptom:** with `VIXEN_ACCUMULATION_ENABLED=1` (independent of reprojection on/off — identical either way), a ~32×32px patch renders sky-colored on frame 1 instead of the correct body color, then self-converges to the correct color over ~5 frames.

**Root cause (not yet bisected):** confirmed PRE-EXISTING, not introduced by M2 — `git diff shaders/DirectLighting.comp` shows the `alpha>=1.0` guard and the plain non-reproject accumulation branch are byte-for-byte untouched by M2's change, and the artifact reproduces identically with reprojection entirely unset. Likely lives in Inc2 M1-M3's original accumulation/history-image initialization path (the persistent `historyImage` starts undefined; something in the frame-1/`alpha>=1.0` handling may still read a stale or uninitialized texel for that specific patch before the guard fully takes effect). Not yet isolated further.

**Impact:** does not affect Inc3 M1's or M2's own gates (M1's byte-identical check uses `enabled=0`; M2's reprojection-quality check samples ticks 2+ frames past any reset, past the self-convergence window). A user watching frame 1 with accumulation enabled would see a brief, self-healing visual glitch — cosmetic, not a correctness/crash issue.

**Fix options:** bisect the frame-1 accumulation/history-image initialization path (Inc2 M1's `AccumulationHistoryNode` + the `alpha>=1.0` shader guard) to find why THIS specific patch reads as sky-colored before converging; likely an uninitialized-read edge case narrower than the guard currently covers.

**Severity:** low (cosmetic, self-converging, doesn't affect M1/M2 gates) · **Status:** OPEN · not a Sampled Lighting Inc3 M1/M2 defect — pre-existing Inc2 accumulation-path behavior, surfaced by M2's gate testing.

---

## KI-024 — `compute_desc_gatherer`'s resource array never grew to cover bindings 18-21, breaking the `test_dispatch` demo pipeline

**Discovered:** 2026-07-11, during Sampled Lighting Inc3 M1's live gate (byte-identical + syncval capture) — surfaced as a side effect, not something M1's own code touches.

**Symptom:** running the default capture with validation live produces repeated `[compute_desc_gatherer] ERROR: Binding 21 out of range (resourceArray_.size()=18)`, correlated with `VUID-vkCmdDraw-None-09600` (image layout UNDEFINED/TRANSFER_SRC vs GENERAL) and `VUID-vkUpdateDescriptorSets-None-03047`/`-03868` — all on the `test_dispatch` generic demo `ComputeDispatchNode` (`BuildRenderGraph.cpp` ~:168-172), a pipeline SEPARATE from the march/DirectLighting/BlitNode chain. Confirmed live-reproduced by an independent Opus validator (20 occurrences at default scale).

**Root cause (not yet fully bisected):** `compute_desc_gatherer`'s `resourceArray_` is sized 18, but Inc2 (AccumulationConfig@19, historyImage@20, PrevCameraConfig@21) and Inc1 (HitRecord@17, ShadowConfig@18) added bindings that pushed the live scene past that size — this specific demo gatherer's array was never grown to track those additions, unlike the march's own gatherer which was updated each increment.

**Impact:** ZERO pixel impact on the real render path — Inc3 M1's byte-identical gate passed exactly (`fde9c268…`) despite these errors being present, confirming the demo pipeline's failure is fully isolated from the march/DirectLighting/Blit chain. Only affects whoever exercises `test_dispatch`'s demo path directly.

**Fix options:** grow `compute_desc_gatherer`'s `resourceArray_` to track the current binding count (mirror whatever mechanism keeps the march's own gatherer in sync as bindings are added), or make the demo pipeline binding-count-agnostic if it's meant to be a generic smoke-test rather than track the live scene's binding layout.

**Severity:** low (isolated demo/test pipeline, zero effect on the shipped render path) · **Status:** OPEN · not a Sampled Lighting defect — surfaced by Inc3 M1's live gate exercising validation more thoroughly than prior milestones, not caused by it.

---

## KI-020 — Two pre-existing MSVC-portability compile failures break the all-targets Windows build (`build.bat build`)

**Discovered:** 2026-07-10, by the `validate-gaia-sync` Opus validator during the Gaia v0.9.2 sync (both files are byte-identical to base `ab40cb97`; unrelated to that sync — pre-existing app-rot). Surfaced because the validator ran a full `build.bat build`, which halts with `ninja: build stopped` on these two.

**Symptom:** a full Windows all-targets build (`build.bat build`) does NOT go fully green — `ninja: build stopped` on two independent compile errors in non-Gaia test code. The Gaia libraries + all three Gaia test exes, and the individual targets people usually build, compile fine; only the aggregate all-targets Windows build is affected.

**The two failures:**
1. `libraries/RenderGraph/tests/.../test_body_instance_raymarch_render.cpp` — uses POSIX `setenv`/`unsetenv`, which do not exist on MSVC → `C3861: 'setenv': identifier not found` (and `unsetenv`). MSVC provides `_putenv_s` (and `_putenv("VAR=")` to clear) instead.
2. `libraries/RenderGraph/tests/.../test_octree_config_sdi_parity.cpp` — includes SVO's `SdfRecipes.h` → generated `SdfCoreKernels.g.hpp`, which uses bare `min`/`max` that collide with the Windows `<windows.h>` `min`/`max` macros → `C2589: '(' : illegal token on right side of '::'` (the classic macro-expansion collision).

**Root cause:** both are Windows/MSVC-portability gaps in test/generated code that presumably compiled or were only exercised under WSL/GCC. Neither is a logic bug; both are include/identifier portability.

**Fix direction (NOT applied — logged per user decision 2026-07-10 to stay focused on the view-contract track):**
1. Replace `setenv(k,v,1)`/`unsetenv(k)` with a portable helper: `#ifdef _WIN32 _putenv_s(k,v)` / `_putenv((std::string(k)+"=").c_str())` `#else setenv/unsetenv #endif` — or a small `SetEnv`/`UnsetEnv` shim in test utilities.
2. For the min/max collision: `#define NOMINMAX` before `<windows.h>` reaches that TU, or wrap the generated-header call sites as `(std::min)(...)`/`(std::max)(...)`, or have the generator emit `(min)`/`(max)` guarded. Because the offending symbols are in a **generated** header (`SdfCoreKernels.g.hpp`), the durable fix is at the generator (emit MSVC-safe min/max) rather than editing the `.g.` output by hand.

**Impact:** the individual Gaia/editor/most targets build and test fine; CI or anyone relying on a single fully-green `build.bat build` on Windows hits these two. Does not affect the Gaia sync, the editor residency fix, or the view-contract work.

**Severity:** Medium (blocks the aggregate Windows build; per-target builds unaffected) · **Status:** OPEN

---

## KI-021 — Existing build dirs keep the STALE pre-v0.9.2 Gaia after the pin bump (FetchContent does not re-fetch on reconfigure)

**Discovered:** 2026-07-10, immediately after the Gaia v0.9.2 pin bump (merge `7dde7ee7`).

**Symptom:** `VIXEN/dependencies/CMakeLists.txt` now pins Gaia to the v0.9.2 SHA (`2293594`→`f2ea77a`), but every EXISTING main-checkout build dir (`build/wsl`, `build/ninja`, `build/ninja-release`, `build/wsl-debug`) still has `_deps/gaia-src` checked out at the OLD `6f0a947`. FetchContent caches `_deps` and does NOT re-fetch when only `GIT_TAG` changes — a plain reconfigure keeps building against the stale Gaia. (This is the very caching behavior that caused the original ~18-commit drift.)

**Why it bites:** the wrapper adaptations in the same merge (`VoxelVolumeArchetype.cpp` `auto&` write-fix + `.all<T&>()` query-constness fix) assume v0.9.2 semantics. Built against stale `6f0a947` Gaia they are at best a loud compile error (binding `auto&` to the old `set<T>` proxy) and at worst semantic mismatch — either way NOT the validated green state. Any downstream Gaia consumer (`CashSystem`, `SVO` tests) in a stale build dir is likewise on old Gaia.

**Fix (per build dir, mechanical):** remove the cached Gaia deps so the next configure re-fetches at the new pin — `rm -rf <builddir>/_deps/gaia-*` (gaia-src, gaia-build, gaia-subbuild), then reconfigure. Verify with `git -C <builddir>/_deps/gaia-src rev-parse HEAD` == `f2ea77a…`. (A full fresh build dir also picks up v0.9.2 directly.) The gaia-sync validation confirmed a cleared/fresh dir fetches v0.9.2 correctly.

**Impact:** anyone reusing an existing build dir builds the wrong Gaia until they clear `_deps/gaia-*`. Fresh build dirs are fine. Not a code bug — a build-cache-hygiene footgun inherent to FetchContent pin bumps.

**Planned fix:** `Dep-Cache-AutoHeal-Design-2026-07.md` — a CMake reconcile-against-pin step that auto-clears a stale `_deps` cache at configure (+ an opt-in adopt-newer-local path + a `-DVIXEN_CLEAR_DEP_CACHE` knob). Will close this KI when implemented.

**Severity:** Low (one-time per-build-dir clear; fresh dirs unaffected) · **Status:** OPEN (self-clears as build dirs are recreated; auto-heal fix designed)
## KI-022 — `VIXEN_RESIZE_AT_FRAME` mid-run window resize crashes with an access violation (pre-existing, unrelated to Sampled Lighting)

**Discovered:** 2026-07-10, during Sampled Lighting Inc2 M4 (camera-motion reprojection + per-pixel history validation), as a side effect of live-gating the accumulation work — no prior Inc0-2 gate in this program had exercised a mid-run resize before.

**Symptom:** triggering a live window resize mid-run via `VIXEN_RESIZE_AT_FRAME` (the existing programmatic-resize test lever in `VulkanGraphApplication.cpp` — `glfwSetWindowSize` → `WindowResizedMessage` → swapchain/imageview recreation, entirely within `VulkanGraphApplication.cpp`/the swapchain-recompile path) crashes with an access violation. The validation-layer signature points at command-buffer/imageview lifetime VUIDs (`VUID-vkFreeCommandBuffers` / `VUID-vkDestroyImageView` "in use") — a resource still referenced by an in-flight command buffer is freed/destroyed during the resize-triggered recompile.

**Root cause:** not yet isolated to a specific node's `CleanupImpl`/`CompileImpl` ordering — the symptom shape (destroy-while-in-use during a recompile) is consistent with the same general class of bug KI-004/KI-006/KI-007/KI-009 already found and partly fixed in swapchain/render-target/pipeline nodes, but this specific crash was not one of those; not yet bisected to a specific node.

**Impact:** PROVEN pre-existing and unrelated to Sampled Lighting — reproduces identically with `VIXEN_ACCUMULATION_ENABLED` (and every other Sampled Lighting accumulation env var) entirely unset, and also reproduces on the pre-existing orbit demo (`VIXEN_RESIDENCY_GATE_DEMO`-shaped scripted camera motion, no accumulation involved). Newly SURFACED by this program only because Inc2 M4 was the first milestone in this program to actually exercise a live resize during a gate run; the `AccumulationConfig`/`AccumulationHistoryNode`/reprojection work itself is unaffected — this is a swapchain/imageview lifetime bug independent of the lighting program.

**Fix options:** (a) bisect which specific node's `CleanupImpl`/`CompileImpl` pair is destroying a still-in-flight resource during a `VIXEN_RESIZE_AT_FRAME`-triggered recompile (the same investigative approach that resolved KI-004/KI-007/KI-009); (b) audit every render-target/command-buffer-adjacent node's recompile-guard for the same "destroy before the GPU is done reading it" shape those fixes addressed, since a resize-triggered `Recompile` and those prior fixes' `CleanupReason` handling are directly relevant; (c) add a `vkDeviceWaitIdle` (or a more targeted fence wait) immediately before the resize-triggered teardown begins, if profiling shows the recompile path doesn't already wait for in-flight frames to drain before destroying resize-invalidated resources.

**Severity:** medium-high (a live-resize access violation is a real crash a user could hit via ordinary window-maximize/resize interaction, not just a synthetic fault) · **Status:** OPEN · not a Sampled Lighting Inc2 defect (pre-existing, newly surfaced by this program's first resize-exercising gate).

---

## KI-023 — Inc2 M4's color-consistency reprojection reject will fight Inc3 ReSTIR's stochastic sampling

**Discovered:** 2026-07-10, during Sampled Lighting Inc2 M4 (camera-motion reprojection + per-pixel history validation); confirmed as a real forward-looking defect by the M4 Opus validator, filed here as the tracked Known Issue + Inc3 prerequisite the plan's "TWO FLAGS" section called for.

**Symptom (projected, not yet reproduced — Inc3 doesn't exist yet):** M4's reprojection validation check (c) rejects a reprojected history sample when `|history.rgb - outColor| > 0.15` (tonemapped color space) — see `BodyInstanceRayMarch.comp`'s reprojection branch. This check is sound for M4's own noise-free, deterministic march: legitimate per-frame deltas from camera motion alone are 0.01-0.05, well under the 0.15 threshold, so it only fires on genuine disocclusion/edge smear. But once Inc3 (ReSTIR DI) makes the CURRENT frame's shading a single NOISY stochastic sample (the entire point of temporal accumulation is to average many noisy samples into a converged image), the converged HISTORY will legitimately differ from any one noisy current sample — that is accumulation working as intended, not a disocclusion. A fixed 0.15 color-reject cannot distinguish "history is stale because the surface changed" from "history is correct and the current sample is just noisy" — it will fire ON the noise it exists to average, at exactly the highest-variance pixels (specular / indirect lighting, ReSTIR's whole target), silently defeating accumulation precisely where it matters most.

**Root cause:** the plan's ORIGINALLY-INTENDED validation was a worldPos/depth GEOMETRIC reject (noise-invariant — rejects on true disocclusion/surface-change, tolerant of arbitrary per-pixel color noise) — see `Sampled-Lighting-Inc2-Plan-2026-07.md` Task 4. M4 shipped the color-consistency check instead because the geometric reject needs a companion worldPos/depth HISTORY buffer, and `historyImage` (M1) stores color only (rgba8) — building that companion buffer was out of M4's scope, deferred rather than silently dropped.

**Impact:** none yet — Inc2's own scope (deterministic march, no stochastic sampling) never exercises the failure mode; all of M4's own gates pass cleanly (color deltas 0.01-0.05 vs the 0.15 threshold, confirmed by the M4 validator's own numpy diff re-run). This is a REQUIRED Inc3 prerequisite, not an Inc2 defect: Inc3 ReSTIR MUST add the geometric (worldPos/depth) reject plus its companion history buffer BEFORE enabling stochastic sampling, or accumulation will be silently defeated at exactly the pixels ReSTIR is meant to help.

**Fix options:** (a) add a worldPos/depth companion history buffer (parallel to `historyImage`, same persistent-image pattern `AccumulationHistoryNode` already established in M1) and switch check (c) from a color-consistency test to a worldPos/depth-consistency test against it — the plan's original design, now unblocked by having a real second consumer to justify the extra image; (b) make the color-reject noise-aware (e.g. widen or adapt the threshold based on a variance estimate) — a smaller change but heuristic and harder to reason about correctness for, not preferred; (c) keep both checks (geometric primary, color as a secondary sanity check with a much wider or adaptive threshold) if Inc3 planning finds a reason color still adds value once geometric rejection is the primary gate.

**Severity:** low today (Inc2 scope never triggers it), high at Inc3 (a silent, hard-to-diagnose accumulation-defeat bug on exactly the layer Inc3 is built to speed up) · **Status:** OPEN · not an Inc2 defect — a validator-confirmed, explicitly-scoped-out prerequisite for Inc3, tracked here so it isn't silently inherited.

---

## KI-019 — `GPUQueryManager::ReadAllResults` never unblocks in some graph configurations (all GPU dispatch timing silently no-ops)

**Discovered:** 2026-07-10, during Sampled Lighting Inc1 M5 (shadow-ray cost measurement), while trying to use the existing `GPUPerformanceLogger`/`GPUQueryManager` timestamp machinery to time the `BodyInstanceRayMarch` compute dispatch in isolation.

**Symptom:** zero `"Dispatch: ... ms avg"` summary lines are ever logged by ANY `GPUPerformanceLogger` instance in the graph (not just the march node — `test_dispatch`, `ui_composite_render`, `VoxelGrid_Memory` all affected), across 1500-frame runs at multiple resolutions and `ShadowConfig` states. GPU timestamp queries ARE reported as supported at startup ("GPU timestamp queries enabled (period: 10.000000 ns/tick, ...)"), so the machinery isn't simply disabled — it silently never produces a reading.

**Root cause:** `GPUQueryManager::ReadAllResults()` gates every read behind `AllAllocatedSlotsReset(frameIndex)` — true only once EVERY allocated consumer query slot across the WHOLE app has had its per-frame-in-flight queries reset in a submitted command buffer at least once (comment at `GPUQueryManager.cpp:240`: avoids `VUID-vkGetQueryPoolResults-None-09401` by never reading before every slot's first reset). In the default editor/main-app graph configuration, at least one allocated slot apparently never completes that first reset→submit cycle for a given frame-in-flight index, so `AllAllocatedSlotsReset` never returns true for that index and `ReadAllResults` — and therefore every consumer's `CollectResults`/`TryReadTimestamps` — returns false forever. Not yet localized to which specific slot/node.

**Impact:** the isolated-GPU-dispatch-ms measurement path (the intended tool for any future per-pass perf budget, e.g. Inc3 ReSTIR / Inc4 DDGI probe-ray costing) is currently non-functional end-to-end, silently — no error or warning is logged when this happens, it just never produces output. M5 substituted `VulkanApplicationBase`'s CPU-side `FrameTimer` (full-frame wall-clock, coarser: includes CPU submit + present) to get the Inc1 shadow-ray cost number; see `gate-artifacts/inc1-m5-shadowray-cost.txt` for the substitute method and its caveats.

**Fix options:** (a) instrument `AllAllocatedSlotsReset` (or add a one-shot warning) to name which allocated slot(s) are stuck un-reset, so the actual dormant node/slot can be identified; (b) audit every `AllocateQuerySlot` call site for a node whose `Execute`/`BeginFrame` might not run every frame-in-flight index (conditional/gated dispatch, or a node compiled but not wired into the active frame path); (c) consider relaxing the whole-pool gate to a per-slot reset-tracking scheme so one dormant consumer doesn't block every other consumer's readings (larger change, touches the VUID-avoidance invariant directly).

**Severity:** medium (doesn't crash or corrupt anything — it's a silent measurement-tooling gap, not a render bug — but it blocks the intended precise-timing tool for every future perf-budget gate) · **Status:** OPEN · not a Sampled Lighting Inc1 defect (pre-existing infrastructure gap, surfaced by M5 being the first milestone to actually need per-dispatch GPU timing numbers).

---

## KI-018 — Sampled Lighting direct-lighting pass runs INLINE, not as a separate `DirectLighting.comp` pass (RenderGraph `ComputeStageNode` 3-slot cap)

**Discovered:** 2026-07-10, during Sampled Lighting Inc1 M4 (`ShadowConfig` + direct-lighting pass with shadow rays).

**Symptom:** the design (`Sampled-Lighting-Design-2026-07.md` §3, §5) and Inc1 plan (Task 4) call for shading to move OUT of `BodyInstanceRayMarch.comp` into a separate `DirectLighting.comp` pass/`DirectLightingNode`, consuming the `HitRecord` buffer (M3) + `LightingConfig` + `ShadowConfig`. M4 shipped shadow rays INLINE instead — `computeLightingWithShadows()` still lives in `BodyInstanceRayMarch.comp`, called from `main()` right after the `HitRecord` round-trip, rather than in a separate dispatch.

**Root cause:** `ComputeStageNode` caps at 3 hazard-tracked buffer slots, but a separate shadow/direct-lighting pass would need to share roughly 9 scene SSBOs with the march pass (octree/brick buffers, `HitRecord`, `LightingConfig`, `ShadowConfig`, instance buffers, ...) to run `TraceWorldShadow` against the same scene data. The wired `ComputeDispatchNode` (the node type actually used for the march) additionally has no producer/consumer chaining mechanism to hand a buffer from one dispatch node to the next the way the PassGroupNode auto-sync machinery (Auto-Sync FrameGraph epic, P4/P5) expects. Diagnosed by code-read before attempting the split (not a debugged runtime failure).

**Impact:** the `TraceWorld`/`HitRecord`/`TraceWorldShadow`/`ShadowConfig` foundation itself is unaffected and fully functional — only the pass SPLIT is deferred. This blocks Inc3 (ReSTIR DI), which structurally REQUIRES the separate pass (reservoir/reuse machinery doesn't fit inline the way a single shadow-ray term does) — tracked in the design doc §4 Inc3 entry as a prerequisite.

**Fix options:** (a) extend `ComputeStageNode`'s hazard-slot capacity beyond 3 to cover the ~9 scene SSBOs a shared-scene lighting pass needs; (b) migrate the march pass (and its future siblings) from `ComputeDispatchNode` onto `ComputeStageNode`/`PassGroupNode`'s producer/consumer wiring so passes can be chained with auto-baked barriers instead of hand-run in one dispatch. Either is a RenderGraph library change, not a Sampled Lighting shader/node change — scoped to Inc3 planning.

**Severity:** low for Inc1/Inc2 (no functional loss — shadows work correctly inline); becomes a hard blocker at Inc3 · **Status:** ✅ RESOLVED 2026-07-11, Sampled Lighting Inc3 M1. The reframing that unblocked it: the "3-slot cap" was misdiagnosed above — those are AUTO-SYNC hazard-tracking slots, not descriptor bindings (the march already binds ~21 buffers via a separate `DescriptorResourceGathererNode`, decoupled from sync slots entirely); scene SSBOs are read-only in both passes so need NO hazard slot at all. The real fix (option b, migrate off `ComputeDispatchNode`'s non-chaining model) exploded into 4 RenderGraph changes once actually wired end-to-end: a standalone `BlitNode` (presentation split out of `ComputeDispatchNode`), a generic `IMAGE_WRITE` sync slot on `ComputeStageNode` (WSI-free image-write hazard tracking — it could already chain buffers but not images), a `BUFFER_WRITE` slot on `ComputeDispatchNode` (closing a genuine silent HitRecord read-before-write race across the new cross-submit boundary), and a `PARAM_WRITES_NO_IMAGE` flag (for a dispatch that manages no presentable image at all). All 4 landed byte-identical + live-syncval-clean (zero hazards on both the HitRecord and swapchain-layout checks), independently re-derived by an Opus validator from a fresh build. `DirectLighting.comp` now runs as a genuinely separate `ComputeStageNode` pass consuming `HitRecord`. Full detail: `Sampled-Lighting-Inc3-Plan-2026-07.md` M1 decision blocks + `gate-artifacts/inc3-m1-hashes.txt`.

---

## KI-017 — `SdfRecipes.h`/`SdfBake.h`'s transitive include chain fails to compile on Windows/MSVC (Windows-macro `min`/`max` pollution, no `#undef` guard)

**Discovered:** 2026-07-08, during Tiered-ESVO Inc2 M3 (GPU traversal-restart), when building `test_gpu_parity`/`test_tier_crossing_construction`/related SVO test targets via the `vixen-ninja` (Windows/MSVC) preset for the first time in the `tiered-esvo-inc2` worktree.

**Symptom:** any test TU that includes `SdfRecipes.h` or `SdfBake.h` (directly or transitively, e.g. via `ShellOctreeGpu.h` → `SdfBake.h` → `SdfRecipes.h`) fails to compile with a cascade of `error C2589: '(': illegal token on right side of '::'` / `error C2059: syntax error` / `error C2672: 'glm::length': no matching overloaded function found` starting in `SdfRecipes.h:85` (`std::max(-b - sq, 0.0f)`) and continuing into `Recipe/generated/SdfCoreKernels.g.hpp` (`glm::min`/`glm::max`/`glm::abs` calls) — the classic signature of `<windows.h>`'s `min`/`max` (and here, apparently `abs`) function-like macros clobbering `std::max(`/`glm::min(` call syntax.

**Root cause:** `SdfRecipes.h` and `SdfBake.h` have NO `#undef min`/`#undef max`/`#undef abs` guard at all (unlike `GpuTraversalMirror.h`, `test_tier_crossing_construction.cpp`, and several other files in this codebase, which DO carry this guard specifically because `<windows.h>` gets pulled in transitively on the Windows build via Vulkan/GTest). Some other header included earlier in a given TU's include order drags in `<windows.h>` before `SdfRecipes.h`/the generated kernel file are parsed, and nothing undoes the macros in between.

**Impact:** `VIXEN.exe` itself builds fine on Windows/MSVC (confirmed clean, `vixen-ninja` preset) — the failure is isolated to specific SVO test translation units (`test_gpu_parity.cpp`, `test_tier_crossing_construction.cpp`, and likely others that pull in `ShellOctreeGpu.h`/`SdfBake.h`/`SdfRecipes.h` without their own `#undef` guard already in scope before those headers). Reproduced independently on a clean, unmodified `4db93715` (pre-Tiered-ESVO-Inc2-M3) checkout via `git stash` — confirmed pre-existing and unrelated to any single increment's own changes; likely never previously exercised on this worktree's Windows/MSVC toolchain until M3 needed the WSL-vs-Windows comparison this session.

**Fix options:** (a) add `#undef min` / `#undef max` / `#undef abs` to `SdfRecipes.h` and `SdfBake.h` (the minimal, surgical fix, matching the pattern several other headers in this codebase already use); (b) audit every header under `libraries/SVO/include/` that calls `std::min`/`std::max`/`glm::min`/`glm::max`/`glm::abs` for the same missing guard, since this is likely not the only affected file; (c) define `NOMINMAX` globally in the Windows build's CMake config so `<windows.h>` never defines these macros in the first place (the most robust fix, but a wider-blast-radius change to verify).

**Workaround used:** build/run the affected SVO test targets via the WSL/GCC path (`build/wsl` preset) instead, where GCC has no such macro-pollution issue — confirmed this compiles and passes cleanly (`test_gpu_parity` 4/4, `test_tier_crossing_construction` 5/5, etc.) on the same source.

**Severity:** low-medium (does not block the live app or any Windows-side production build; blocks a subset of SVO test targets from being buildable/runnable on Windows/MSVC specifically, forcing a WSL fallback for those tests) · **Status:** OPEN · not a Tiered-ESVO Inc2 defect (pre-existing, surfaced by this milestone's Windows-build attempt).

**Re-confirmed 2026-07-11 (Lazy-Procedural-Delta-Baseline Inc0 M6 Task 15 full sweep):** independently re-discovered the identical failure signature (`SdfRecipes.h:100` `std::max`/`glm::max`, cascading into `SdfCoreKernels.g.hpp`) doing a from-scratch full-solution `vixen-ninja` build, expanding the known-affected-target list — `test_octree_config_sdi_parity`, `test_soa_sdf_serialize`, `test_soa_mip_serialize`, `test_tier_ref_table`, `test_tier_crossing_construction`, `test_tier_crossing_mirror_parity`, `test_channel_format`, `test_mip_sample_bake`, `test_stored_sdf_march_mirror`, `test_shell_derive`, `test_sdf_bake`, `test_recipe_bake`, `test_recipe_bake_center`, `test_octree_pool`, `test_generation_cost_benchmark`, `test_recipe_boot_ingest`, `test_recipe_baker`, `test_residency_default`, `test_gpu_parity`, `test_shell_octree_gpu` — 20 SVO test targets total, all via the same transitive `SdfRecipes.h`/`SdfBake.h` chain. Tried fix option (a) scoped to `SdfRecipes.h` alone (a `glm::max` swap + a local `NOMINMAX` guard) — insufficient, because in several of these TUs `<windows.h>` is already poisoned by an EARLIER header (often via `gtest.h`'s own transitive includes) before `SdfRecipes.h` is even reached, so a guard local to that one file can't help; **fix option (c) (global `NOMINMAX`) is the only fix that can work for every affected TU**, confirming the original note's assessment. Left unfixed this session (out of M6's scope; a build-system-wide change deserves its own verification pass, not a drive-by inside an unrelated milestone). **Also this session: could NOT re-confirm "`VIXEN.exe` itself builds fine"** — the attempted full-solution build ran the local disk (`C:`, 931GB) to 0 bytes free partway through (a SEPARATE, unrelated capacity issue — see the disk-note added to this doc's own section below) before reaching `VIXEN.exe`'s own compile step, so that specific claim is UNVERIFIED as of this note, not falsified.

---

## Disk capacity note (2026-07-11, observed during Inc0 M6 Task 15's full sweep)

Not a code defect — recording because it silently corrupted 21 test binaries (0-byte `.exe` files from linker writes that ran out of disk mid-write) and could mislead a future sweep into reporting false compile/link failures. The `lazy-baseline-inc0` worktree's OWN `build/ninja` directory alone is ~56GB; the shared `C:` drive (931GB total) was at 930GB used / <1GB free when a from-scratch `cmake --build --preset vixen-ninja` (no target filter — every target across ~15 sibling worktrees' worth of accumulated build output sharing the same physical drive) was attempted. Symptoms if this recurs: `LINK : fatal error LNK1116: cannot grow ilk file` (mid-link disk-full) and `LINK : fatal error LNK1140: limit exceeded for program database` (a 4GB PDB size cap, hit by `VIXEN.exe`/`vixen_editor.exe`'s large debug PDBs specifically, independent of free space). **Recovery:** `find <build-dir> -iname "*.exe" -type f -printf "%s %p\n" | awk '$1==0{print $2}'` finds the 0-byte casualties; delete them so `ctest -N`'s `gtest_discover_tests` probe (which otherwise hard-errors on the FIRST corrupt binary it tries to list, blocking test discovery for the ENTIRE suite) can proceed — the removed binaries correctly show as `<name>_NOT_BUILT` placeholders in the resulting test list, an honest reflection of "never successfully linked this run," not a new failure category. No fix suggested here (freeing disk across sibling worktrees is a cross-agent/user decision, not a single milestone's call) — just the recovery recipe and the failure signature, so the next person who hits `LNK1116`/`LNK1140` mid-sweep checks `df -h` before assuming a code regression.

---

## KI-016 — editor undo (`rt_.Undo()`) has no visible render effect: post-toggle state persists

**Discovered:** 2026-07-06, during View Contract Inc-2 M3 close-out (the first fresh re-run of the editor windowed gate since AppFlow Inc-2b shipped it).

**Symptom:** `test_editor_toggle_undo_capture` FAILS on a fresh unattended `vixen_editor` run (`VIXEN/temp/run_editor_script.bat`, script `toggle:2@30,undo@60,redo@90`, captures @5/45/75/105). The toggle half works — `boreDiffPixels(png5,png45)=1024` (the cut layer visibly toggles off). But **`png75 != png5`**: undo@60 does NOT restore the baseline. md5 shows `editor_capture_45`/`_75`/`_105` are byte-IDENTICAL to each other and differ from `_5` — i.e. `rt_.Undo()` had no visible effect at all; the render stays in the post-toggle state for the rest of the run (which also makes the redo@90 assertion pass for the wrong reason).

**Root cause:** unknown / not yet investigated. Confirmed it is NOT introduced by View Contract Inc-2: `git status`/`git diff` scoped to `application/editor/`, the node sources, and the undo/`ActionStack` path show ZERO changes across M1–M3 of Inc-2 (the increment deliberately walled off the editor/ActionStack surface — Global Constraint). This is a genuine, previously-LATENT regression: AppFlow Inc-2b's own gate (`test_editor_toggle_undo_capture`, added `79786a66`) passed when it shipped, and the M2 validator of this increment explicitly did NOT re-run it fresh (assumed-safe because M2's `RenderTargetReadback.h` change was purely additive). So the regression landed somewhere between Inc-2b's ship and now, from some other change to main — the View Contract increment merely SURFACED it by being the first to re-run the gate fresh. (This is exactly the "live-run gate is authoritative for GPU work" lesson: assuming a GPU gate safe without re-running it hid a real regression for multiple increments.)

**Impact:** editor layer-toggle **undo** is broken in the live windowed editor (redo likely too — untested independently since it trivially "passes" against the un-undone state). Toggle itself works. The headless AppFlow undo logic (`test_appflow_editor_toggle_render`, Inc-2's byte-exact headless gate) should be re-run to localize whether the break is in the undo LOGIC (ActionStack/AppFlowRuntime) or in the windowed re-flatten→render path specifically — that bisects it.

**Fix options:** (a) re-run `test_appflow_editor_toggle_render` (headless) — if it PASSES, the break is in the windowed EditorApplication re-flatten/render path (input→ActionStack→`rt_.Undo()`→onChanged→re-flatten→capture), not the undo logic; if it FAILS, the undo LOGIC regressed. (b) `git bisect` the editor windowed gate between the Inc-2b merge (`79786a66`) and current main to find the introducing commit. (c) inspect whether `rt_.Undo()`'s `onChanged` callback still fires the re-flatten (`dirty_=true` → `enabledMask` re-applied) — a likely suspect given the toggle works but the undo doesn't.

**Severity:** medium (a shipped editor feature — undo — is silently broken in the live path; headless logic may be fine) · **Status:** OPEN · not a View Contract Inc-2 defect (surfaced by, not caused by, that work).

---

## KI-015 — codegen `--check` gates (`octreeconfig_check`, `view_editorhud_check`) silently no-op on a Windows-side CMake configure

**Discovered:** 2026-07-06, during View Contract Codegen Inc-1 (M3 gate wiring).

**Symptom:** `codegen/CMakeLists.txt` sets `YEROKET_ROOT` to `$ENV{HOME}/Github/Yeroket-Fantasy` and guards the codegen targets on `if(EXISTS "${_yk_tool}/CodegenTool.csproj")`. Under a Windows-side configure (`cmake.exe` inside `vcvars64`), `$ENV{HOME}` resolves against the *Windows* `HOME`, not WSL's — yielding a path like `/Github/Yeroket-Fantasy` that doesn't exist — so BOTH `octreeconfig_check` and the new `view_editorhud_check` are silently skipped ("Yeroket tool not found"). The Yeroket kernel-framework repo is a WSL-only clone here (no `\\wsl$` mount used), so no Windows path reaches it.

**Root cause:** the schema→header drift guards depend on the Yeroket tool being reachable, but the `YEROKET_ROOT` default assumes a WSL `$ENV{HOME}`. This predates the view work and affects `octreeconfig_check` identically.

**Impact:** on a Windows-side build the drift guards do not run — a hand-edit or stale generated header (`OctreeConfig` GLSL/C++, `EditorHud.g.h`) would NOT be caught at configure/build time. The gate logic itself is correct: verified via direct WSL-side `dotnet run … --check` (exit 0) for both `octreeconfig` and `view_editorhud`.

**Fix options:** (a) resolve `YEROKET_ROOT` robustly across Windows/WSL configures (e.g. accept a `-DYEROKET_ROOT=` override + probe both a WSL `$ENV{HOME}` and a Windows-visible path); (b) run the codegen `--check` gates in CI on the WSL side explicitly; (c) emit a loud `message(WARNING …)` when the tool is not found instead of a silent skip, so a Windows configure surfaces "drift guard disabled" rather than passing quietly.

**FIXED 2026-07-06** (commit `63d74075`, `codegen/CMakeLists.txt`) — implemented (a) + (c): `_home_candidates` now probes `$ENV{HOME}` → `$ENV{USERPROFILE}` → mounted-WSL-home (`//wsl$/Ubuntu/home/$USERNAME`), the dotnet `find_program` gains system-path fallbacks (`/usr/bin`, `/usr/local/bin`, `/usr/share/dotnet`, `C:/Program Files/dotnet`), and `YEROKET_ROOT` is resolved by probing those candidates for an actual `CodegenTool.csproj` (with the `-DYEROKET_ROOT=` cache override still winning). Critically, BOTH not-found paths (no dotnet; no Yeroket tool) now emit a loud `message(WARNING … DRIFT GUARD DISABLED …)` instead of a silent `STATUS`, so a Windows-side configure surfaces that the generated headers are un-checked rather than passing quietly. Resolution logic validated in isolation: WSL-side → tool found, no warning; simulated Windows (empty `HOME`) → loud WARNING + guard disabled, as intended. The residual reality is unchanged and now *documented + surfaced*: the Yeroket repo is a WSL-only clone here, so a Windows-side configure legitimately cannot reach it — run the WSL-side configure (or mount `\\wsl$`, or pass `-DYEROKET_ROOT=`) to actually run the guard. The *silent-no-op bug* is fixed.

**Severity:** low (guard-coverage gap on one configure path; the generators + `--check` are proven correct WSL-side) · **Status:** RESOLVED (silent skip → loud warn + robust probe; WSL-only reachability now a documented limitation, not a hidden trap) — **superseded by the 2026-07-07 fix below, which closes the reachability gap itself.**

**FIXED (execution) 2026-07-07** (`codegen/CMakeLists.txt`) — the 2026-07-06 fix above only made the unreachable case *loud*; the Yeroket tool + dotnet were still resolved via a `\\wsl$` UNC mount on a Windows configure, and even when `find_program`/`EXISTS` found them there, ninja's `cmd.exe` cannot **execute** a Linux ELF at a UNC path — so `octreeconfig_check`/`view_hud_check`/`view_hud_markup_check`/`view_hud_blob_check` (and their `_regen` siblings) built but failed at execution time on Windows. Fixed by bridging through `wsl.exe`, which Windows processes can invoke a WSL-side binary through: when `WIN32` and the resolved `VIXEN_DOTNET`/`YEROKET_ROOT` matched a `wsl` hint, all five target pairs now run `wsl.exe -e <wsl-dotnet> run --project <wsl-tool> ...` instead of invoking the UNC path directly, with every `${CMAKE_SOURCE_DIR}/...` argument translated from its Windows form to `/mnt/c/...` via `wsl.exe -e wslpath -u` at configure time. One shared `_CODEGEN_RUNNER` variable (native `${VIXEN_DOTNET}` or the `wsl.exe` bridge) and one `_codegen_to_wsl_path()` helper are reused by all five targets — not five hand-diverged blocks. `wsl.exe`-absent or dotnet-unresolvable-inside-WSL both fall back to the existing loud `message(WARNING ... DRIFT GUARD DISABLED ...)`. Native configures (WSL-side, or a future native-Windows tool) are unaffected — the `if(WIN32 AND ... MATCHES "wsl")` gate leaves `_CODEGEN_RUNNER` as plain `${VIXEN_DOTNET}` there. Verified live: Windows-side reconfigure (`cmake --preset vixen-ninja`) then `cmake --build build/ninja --target view_hud_blob_check` and `--target view_hud_check` both now **exit 0** (previously failed) — output shows the bridge invoking the WSL dotnet build of the Yeroket tool and the golden `--check` passing.

**Status:** RESOLVED (both the silent-skip bug and the UNC-execution bug are fixed; a Windows-side configure now actually runs the drift guards against the WSL-only Yeroket tool).

*(Related scope note, not a KI: the View Contract emitter's non-array nested-struct field path (`ViewFieldKind.Struct` → `Name*` bind pointer) is implemented but untested — every Inc-1 schema uses only scalars + `StructArray`. Add coverage when a single-struct view field is first used; tracked for a future View Contract increment, not a bug.)*

---

## Test-suite note (not a KI): `test_fail_scenario_sweep` is flaky under the Vulkan validation layer

Running any SINGLE `FailScenarioSweep*` test that does a live resize+recompile (e.g. `LiveResizeRecompilesPickIdRing`) under `VK_LAYER_KHRONOS_validation` alone can segfault (`vkCmdBindPipeline` referencing an already-deleted `VkDescriptorSetLayout`, stale command-buffer-in-use errors, then SIGSEGV) — but the SAME test passes cleanly with `[ PASSED ]` when run without the validation layer. This reproduces identically both before and after this session's changes, so it's pre-existing validation-layer/test-timing interaction, not a functional regression. Use the validation layer for spot-checking specific VUIDs on `vixen_editor` directly (as this session did for KI-009/KI-012); trust the plain (no-validation-layer) test run for pass/fail signal on `test_fail_scenario_sweep`.

---

## KI-008 — lavapipe is no longer usable for this project

**Discovered:** 2026-07-04, standing rule for the widescreen-perf-fix program's worktrees.

**Symptom/rule:** lavapipe (Mesa's `lvp_icd.json` software rasterizer) must not be used as a dev-loop ICD in this project going forward — a separate cleanup effort is removing it from the codebase entirely. Any doc, script, or `VK_ICD_FILENAMES` reference that still points at `lvp_icd.json` as a live option is stale guidance, not history.

**Impact:** affects any contributor or agent reaching for lavapipe as a quick headless-GPU stand-in for local iteration; WSL sessions without a provisioned real-GPU path (e.g. Mesa Dozen/Vulkan-over-D3D12) lose that fallback and must rely on CPU-only build+test gates, deferring live-render verification to a session where a real GPU is available.

**Fix:** none needed — this is a policy/environment note, not a bug. Swept the widescreen-perf-fix plan and findings docs (2026-07-04) for any forward-looking instruction still citing lavapipe/`VK_ICD_FILENAMES`/`lvp_icd`; all remaining occurrences were historical gate-result records (describing runs that already happened) and were left as-is per the sweep's own rule.

**Severity:** N/A (policy) · **Status:** OPEN (standing rule, not something to "resolve")

---

## Resolved (see below)

### KI-013 — `FailScenarioSweep_FrameSync.DeviceLostRecovery` segfaults inside Dozen's swapchain-image destroy path (regression against KI-004's documented-fixed state)

**Discovered:** 2026-07-04, while verifying the KI-012 pick-ID fix didn't regress `test_fail_scenario_sweep` — running the FULL suite in one process segfaulted right after `ResizeBurstDoesNotRecompileOncePerEvent`, before `FailScenarioSweep_FrameSync.DeviceLostRecovery` completed. Confirmed via `git stash` that this reproduced byte-identically at the pre-KI-012/pre-flicker-fix baseline (`origin/main` `9ddbb854`) — not caused by that session's other changes.

**File/line:** `libraries/RenderGraph/src/Nodes/SwapChainNode.cpp` (`CleanupImpl`).

**Symptom:** `DeviceLostRecovery` (run alone, isolated — same crash) segfaulted during `RenderGraph::RecoverFromDeviceLoss()`'s rebuild phase, specifically while rebuilding `main_swapchain` (`SwapChainNode::CompileImpl` → `CreateSwapchainAndViews` → `VulkanSwapChain::CreateSwapChainColorImages`). GDB backtrace:
```
Thread 1 received signal SIGSEGV
#0  0x... in ?? ()
#1  wsi_destroy_image () from .../libvulkan_dzn.so
#2  x11_swapchain_destroy () from .../libvulkan_dzn.so
#3  VulkanSwapChain::CreateSwapChainColorImages(VkDevice_T*, VkSwapchainKHR_T*)
#4  SwapChainNode::CreateSwapchainAndViews()
#5  SwapChainNode::CompileImpl(...)
#6  NodeInstance::Compile()
#7  RenderGraph::RecoverFromDeviceLoss()
#8  VulkanGraphApplication::Render()
```

**Root cause:** `SwapChainNode::CleanupImpl` treated `CleanupReason::Recompile` and `CleanupReason::DeviceLost` identically (`if (ctx.reason != CleanupReason::FinalTeardown)`) — both took the "keep the swapchain HANDLE alive across the boundary, destroy only per-image views" branch, so that `CreateSwapchainAndViews()` could pass the still-live handle as `oldSwapchain` for the driver to recycle/hand over presentation state. That's correct for `Recompile` (the SAME `VkDevice` recreates it), but wrong for `DeviceLost`: `RenderGraph::RecoverFromDeviceLoss()` has `DeviceNode::CompileImpl` create an entirely NEW `VulkanDevice` (`RenderGraph.cpp`) before `SwapChainNode` rebuilds — a `VkSwapchainKHR` is device-scoped, so the old handle belongs to the OLD, about-to-be-destroyed device. Passing it as `oldSwapchain` into the NEW device's `fpCreateSwapchainKHR`/`fpDestroySwapchainKHR` (resolved via the new device's dispatch table) is exactly the KI-004 bug class (a resource carrying stale device state across recovery) and segfaults deep in the driver's swapchain-destroy internals. The `VkSurfaceKHR`, by contrast, is instance-scoped and correctly survives a device recreation untouched.

**Fix (2026-07-04):** split the `Recompile`/`DeviceLost` branches. `Recompile` keeps the existing behavior (`DestroyImageViewsOnly`, swapchain handle survives for reuse). `DeviceLost` now calls `swapChainWrapper->DestroySwapChain(device)` — destroys the image views AND the swapchain handle (against the OLD, still-valid-but-lost device, which is safe per the `CleanupReason::DeviceLost` doc comment: calls against a lost device are expected to be harmless/no-ops), leaving `scPublicVars.swapChain = VK_NULL_HANDLE` so the later rebuild's `CreateSwapchainAndViews()` correctly does a cold creation (`oldSwapchain = VK_NULL_HANDLE`) against the new device instead of handing it a foreign-device handle. The surface is untouched in both branches (survives, as before).

**Verification:** `FailScenarioSweep_FrameSync.DeviceLostRecovery` passes in isolation (previously segfaulted) — log shows "RECOVERY COMPLETE: rendering resumes on the new device". Full `test_fail_scenario_sweep` suite: 10/10 tests run to completion (previously crashed after test 3/10) — 8 passed, 2 skipped by their own logic (pre-existing, unrelated). Full project rebuild + all 7 render-gate test suites (28 tests) re-verified passing with zero regressions.

**Severity:** High (crash in a documented-fixed regression gate for a real reliability feature) · **Status:** RESOLVED

### KI-012 — `VoxelSelectionProviderNode`'s pick-ID readback violates queue transfer-granularity on Dozen

**Discovered:** 2026-07-04, live-gate run of `vixen_editor` under `VK_LAYER_KHRONOS_validation` while chasing KI-009/render flicker (unrelated — surfaced only on a mouse click, not idle rendering).

**File/line:** `libraries/RenderGraph/src/Nodes/VoxelSelectionProviderNode.cpp` (`ReadCenterPixel`), `libraries/VulkanResources/{include,src}/VulkanDevice.cpp` (`RequiresFullImageTransfers`).

**Symptom:** on every click, two validation errors:
```
VUID-vkCmdCopyImageToBuffer-imageOffset-07747
pRegions[0].imageOffset (x = 250, y = 250, z = 0) must be (0, 0, 0) when the command buffer's
queue family minImageTransferGranularity is (0, 0, 0) as this queue doesn't allow for any offset.
pRegions[0].imageExtent (width = 1, height = 1, depth = 1) must match the image subresource
extent (width = 500, height = 500, depth = 1) when ... this queue only allows full image copies.
```

**Root cause:** the code copied a single 1×1 texel at an arbitrary offset (the cursor's pick position) out of the full-size ID image — a partial-image-region copy. Dozen's (Mesa Vulkan-over-D3D12) transfer-capable queue family reports `minImageTransferGranularity = (0,0,0)`, which per spec means that queue **only accepts whole-image copies at offset (0,0,0)** — no sub-region copies at all. lavapipe apparently tolerated this (hence it went unnoticed until the lavapipe-removal work this session put Dozen in the default path).

**Fix (2026-07-04):** checked once at startup, not re-queried per click, following the existing "ask `VulkanDevice` about queue capabilities" convention (alongside `HasPresentSupport()`): added `VulkanDevice::RequiresFullImageTransfers()`, computed from the already-queried `queueFamilyProperties[graphicsQueueIndex].minImageTransferGranularity == (0,0,0)`. `VoxelSelectionProviderNode::CompileImpl` caches this once per Compile (`requiresFullImageTransfers_`); `ReadCenterPixel` branches on it — the common per-click path (single-texel sub-region copy) is unchanged for devices with real transfer granularity, while devices that need whole-image transfers copy the ENTIRE id image into a (grow-only, reused-across-clicks) full-size staging buffer and index the center texel on the CPU side instead.

**Verification:** full build + `test_fail_scenario_sweep` (excluding the pre-existing `DeviceLostRecovery` crash, see KI-013) — 7/7 pass, 2 skipped by the tests' own logic, 0 regressions. `LiveResizeRecompilesPickIdRing` (which injects a real click and exercises the readback) passes cleanly without the validation layer; the same test is separately flaky under the validation layer alone (see the test-suite note above), unrelated to this fix.

**Severity:** Low (worked today even before the fix, spec-invalid, not on the hot/idle render path) · **Status:** RESOLVED

### KI-009 — `vixen_editor` render view flickers black/content on real GPU; VUID-vkCmdDraw-None-09600 layout mismatch

**Discovered:** 2026-07-04, investigating a user report that vixen_editor's render viewport alternates between showing the loaded geometry and solid black/dark-blue, at idle (no interaction needed to reproduce; camera framing was a separate, already-fixed bug that didn't affect this).

**File/line:** `libraries/RenderGraph/src/Nodes/RenderTargetNode.cpp` (`ExecuteImpl`).

**Symptom:** every few frames, `vkQueueSubmit2KHR` reported `VUID-vkCmdDraw-None-09600` (the descriptor-layout-mismatch VUID, applied here to the compute dispatch that binds the render target as a `STORAGE_IMAGE` — validation's message text says "draw" but the same rule governs a bound descriptor read by any command, dispatch included): `VkImage` (the offscreen render-target ring) expected in `VK_IMAGE_LAYOUT_GENERAL`, actually `UNDEFINED` or `TRANSFER_SRC_OPTIMAL`. Visually: UI panel stable, only the 3D render area flickered.

**Root cause:** `RenderTargetNode` maintains a ring of `imageCount_` offscreen images and rotates `currentIndex` every frame in `ExecuteImpl` (`currentIndex = (currentIndex + 1) % imageCount`) — but published its `CURRENT_VIEW` output **only once, in `CompileImpl`**, frozen at whatever ring slot `currentIndex` happened to be at compile time (slot 0). `DescriptorSetNode` binds the compute shader's binding-0 `STORAGE_IMAGE` descriptor from that frozen `CURRENT_VIEW` every frame (correctly re-writing the descriptor set each Execute, but always with the SAME stale image view) — while `ComputeDispatchNode` resolves the image it actually barriers-and-dispatches against via the LIVE `IRenderTarget::GetCurrentImage()` (`RENDER_TARGET_INFO`, following the rotating `currentIndex`). The descriptor's bound image and the barrier/dispatch's actual image are the same physical ring slot on only one phase out of every `imageCount` frames — every other frame they're two different images, so the barrier's careful `GENERAL` transition (already correct per KI-007's fix) applies to the WRONG slot from the descriptor's point of view.

**Fix (2026-07-04):** `RenderTargetNode::ExecuteImpl` now re-publishes `CURRENT_VIEW` (`ctx.Out(RenderTargetNodeConfig::CURRENT_VIEW, target_.GetCurrentView())`) immediately after advancing `currentIndex`, so the descriptor set tracks the live ring slot every frame instead of a compile-time snapshot.

**Two adjacent, independently-real synchronization bugs found and fixed en route** (neither was the flicker's actual cause, but both were genuine spec violations caught by `VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT`):
- `ComputeDispatchNode::BlitRenderTargetToSwapchain`'s swapchain-image entry barrier used `srcStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT` — a no-op source that doesn't chain an execution dependency with the WSI acquire semaphore's wait (declared at `VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT` on this command buffer's submit). Harmless only while `oldLayout` was always `UNDEFINED` (nothing to wait for); once real prior-layout tracking was added (see below) this produced `SYNC-HAZARD-WRITE-AFTER-READ` against `vkAcquireNextImageKHR`. Fixed by changing `srcStageMask` to `VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT`.
- The same function's swapchain entry barrier also hardcoded `oldLayout = VK_IMAGE_LAYOUT_UNDEFINED` unconditionally, when the swapchain image's real layout after the first frame is `VK_IMAGE_LAYOUT_PRESENT_SRC_KHR` (left there by the UI render pass's `finalLayout` + present). Fixed the same way as KI-007 — tracked via the same `renderTargetImageLayouts_` map, keyed by the swapchain image handle too.
- `RenderPassNode.cpp`'s UI composite render pass subpass-external dependency (built via `libraries/CashSystem/src/RenderPassCacher.cpp`) hardcoded `dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT` only. Since this render pass uses `LOAD_OP_LOAD`, the implicit initial-layout transition also needs `COLOR_ATTACHMENT_READ_BIT` to synchronize against the LOAD read — its absence produced `SYNC-HAZARD-READ-AFTER-WRITE` at `vkCmdBeginRenderPass`. Fixed by adding `VK_ACCESS_COLOR_ATTACHMENT_READ_BIT` to `dstAccessMask` whenever `colorLoadOp == Load`.

**Verification:** live-gate run of `vixen_editor` under `VK_LAYER_KHRONOS_validation` + synchronization validation: `VUID-vkCmdDraw-None-09600` and both `SYNC-HAZARD-*` messages are gone after all three fixes (confirmed zero occurrences across a multi-second run cycling all 4 ring slots repeatedly). Two unrelated, pre-existing validation messages remain (`VUID-vkCmdCopyImageToBuffer-imageOffset-07747` — see KI-012; `VUID-vkGetQueryPoolResults-None-09401` — GPU perf-logger query pool not reset before first read, not yet triaged).

**Severity:** Medium (visual only, no crash, no data loss) · **Status:** RESOLVED

### KI-007 — `ComputeDispatchNode::seenRenderTargetImages_` never prunes stale `VkImage` handles across resizes

**File/line:** `libraries/RenderGraph/include/Nodes/ComputeDispatchNode.h` (was `seenRenderTargetImages_`, now `renderTargetImageLayouts_`), `libraries/RenderGraph/src/Nodes/ComputeDispatchNode.cpp` (`RecordComputeCommands`/`BlitRenderTargetToSwapchain`).

**Symptom (as originally filed):** `seenRenderTargetImages_` was a `std::set<VkImage>` used to pick the correct `oldLayout` (`UNDEFINED` vs `TRANSFER_SRC_OPTIMAL`) for the render-target image's WSI-acquire barrier, keyed on whether a given `VkImage` handle had been seen before. Entries were only ever inserted, never erased, and — worse than originally filed — the seen/not-seen scheme was also simply WRONG once multiple frames are in flight: it assumed every handle strictly alternates GENERAL<->TRANSFER_SRC_OPTIMAL in lockstep, which doesn't hold when a command buffer is re-recorded against a ring slot whose actual last transition doesn't match that two-state guess.

**Fix (2026-07-04):** replaced the set with `std::unordered_map<VkImage, VkImageLayout> renderTargetImageLayouts_`, tracking the ACTUAL last-recorded layout per handle (updated at both the compute-write entry barrier and the post-blit exit barrier), via a small pure/testable free function `DecideRenderTargetPriorLayoutAndUpdate` (`ComputeDispatchNode.h`). Exact instead of guessed; also incidentally fixes the original unbounded-growth complaint (the map is keyed the same way but now semantically correct, and could be pruned the same way if that's ever a real concern).

**Verification:** 4 new unit tests in `test_compute_dispatch_node.cpp` (first-use-is-undefined, second-use-reports-real-tracked-layout, distinct-ring-slots-tracked-independently, map-updates-to-new-layout) — all pass. Does NOT fix the visible flicker/VUID-vkCmdDraw-None-09600 symptom that prompted this investigation — see KI-009 above; this was a real bug found along the way, not the one being chased.

**Severity:** Low (as filed) · **Status:** RESOLVED

---

## KI-006 — `CleanupImpl`-no-Recompile-guard class in `DescriptorSetNode`/`ComputePipelineNode`

**Files/lines:** `libraries/RenderGraph/src/Nodes/DescriptorSetNode.cpp:976-1001` (`DescriptorSetNode::CleanupImpl` — destroys descriptor pool + descriptor set layout unconditionally); `libraries/RenderGraph/src/Nodes/ComputePipelineNode.cpp:124-141` (`ComputePipelineNode::CleanupImpl` — destroys shader module + resets pipeline/layout/cache handles unconditionally).

**Symptom:** neither `CleanupImpl` checks the `CleanupReason` (`Recompile` vs `FinalTeardown`/`DeviceLost`) before tearing down its Vulkan objects — both destroy pool/layout/pipeline/shader-module on every cleanup call, including ordinary resize-triggered recompiles.

**Root cause:** same bug CLASS as the already-fixed KI-004 (device-scoped state torn down/rebuilt without regard to *why* cleanup is happening) — except here the objects are recreated every recompile regardless (no create-once guard reusing a stale handle), so this manifests as extra destroy/recreate churn on every resize rather than a crash. It is the same missing-`reason`-check shape, just without KI-004's crash-causing persistent-handle-reuse half.

**Impact:** wasted Vulkan object churn (descriptor pool/layout, shader module, pipeline) on every resize-driven recompile, and — per KI-005 below — the resulting layout-handle recreation is what feeds the L2 cache-key mismatch's stale-pipeline-bind VUID burst. Not a crash on its own.

**Fix options:** add the same `if (reason == Recompile) return;`-style (or equivalent explicit branch) guard pattern used to fix KI-004's affected nodes, once it's decided which of these objects legitimately need to survive a recompile (likely: none here, since shader/layout content can change across a recompile — needs a design decision, not a blind copy of the KI-004 fix).

**Severity:** Medium (perf/churn + contributing cause of KI-005, not a crash) · **Status:** OPEN (filed, not fixed — out of the widescreen-perf-fix program's bounded scope)

---

## KI-005 — L2 cache-key mismatch: `ComputePipelineCacher` hashes a resize-invariant string while `PipelineLayoutCacher` hands out a live handle

**Files/lines:** `libraries/CashSystem/src/ComputePipelineCacher.cpp:47-56` (`ComputeKey` hashes `ci.layoutKey`, a `std::string`); `libraries/RenderGraph/src/Nodes/ComputePipelineNode.cpp:~201` (`layoutParams.layoutKey = shaderBundle->uuid + "_pipeline_layout"` — constant across resizes, since the shader UUID doesn't change).

**Symptom:** after a resize-triggered recompile, one frame's compute dispatch binds a pipeline object that references the OLD (destroyed) `VkPipelineLayout` handle, producing a burst of stale-pipeline-bind validation errors (VUID) for that single frame before self-correcting.

**Root cause:** `ComputePipelineCacher`'s cache key is computed from `ComputePipelineCreateParams::layoutKey`, a string identifier (`shaderBundle->uuid + "_pipeline_layout"`) that is identical before and after a resize — so the compute-pipeline cache reports a hit and returns the previously-cached `ComputePipelineWrapper` (built against the OLD layout handle) even though `PipelineLayoutCacher` has since recreated the actual `VkPipelineLayout` for the new swapchain extent. The two cachers disagree on identity: one keys by content-string, the other hands out a live, resize-mutable handle.

**Impact:** one frame of VUID validation-layer noise per resize; self-heals on the next recompile pass since the cache eventually converges. Not observed to cause a crash or visible artifact, but is exactly the kind of one-frame hazard window the widescreen-perf-fix program was hunting — filed here because it's shared `CashSystem` infra (used by other cachers too), making a fix out of this program's bounded per-node scope.

**Fix options:** (1) include the live layout handle (not just its string key) in `ComputePipelineCacher::ComputeKey`'s hash, invalidating the cache entry whenever the underlying layout handle changes; (2) have `ComputePipelineNode` explicitly invalidate/evict its cached pipeline entry when it detects `PipelineLayoutCacher` returned a new handle for the same `layoutKey`, rather than relying on the cache's own key comparison.

**Severity:** Low-Medium (validation noise only, self-correcting, one frame) · **Status:** OPEN (filed, not fixed — shared CashSystem infra, out of this program's bounded scope)

---

*(No further open issues at present beyond KI-004 below — see Resolved for everything else.)*

---

## Resolved` section with the fixing commit.

---

## KI-004 — Nodes downstream of `FrameSyncNode` keep executing on a condemned frame after device loss, racing recovery teardown

**Update 2026-07-03 (partial fix landed, bug NOT fully resolved):** `RenderGraph::NotifyDeviceLost()` now calls `AbortCurrentFrame()` before latching (mirroring `9d95bd75`'s central abort for the out-of-date acquire path). **Verified this closes the originally-diagnosed race** — the log now shows "Frame aborted before node '...' — skipping the rest of this frame" fire the instant device loss is detected, and no downstream node executes on the condemned frame anymore.

**However, the scenario still crashes** — one step later, with the same symptom, for a **different, not-yet-isolated reason**: the FIRST frame after `RecoverFromDeviceLoss()` completes ("RECOVERY COMPLETE" logs, rebuild reports success) still crashes in `ComputeDispatchNode` with `vkBeginCommandBuffer: Invalid commandBuffer [VUID-vkBeginCommandBuffer-commandBuffer-parameter]`. Diagnostic logging (added and removed this session) proved this is NOT the original race:
- `imageIndex=0` at the crash — legitimately fresh (not a stale value from before recovery)
- `frameAborted=0` — correctly not aborted; this is a genuinely new, un-condemned frame
- The command-buffer handle is a **new address**, freshly allocated milliseconds earlier via `vkAllocateCommandBuffers` returning `VK_SUCCESS` from a freshly-recreated `VkCommandPool`

So a handle the driver just reported as successfully allocated is rejected as structurally invalid by the loader's own parameter check almost immediately after. Ruled out this session: double-compile of `ComputeDispatchNode` during rebuild (compiles exactly once), a stale `imageIndex` flowing from before recovery (value is fresh), a stale cached device pointer on the node (it has none — uses base `NodeInstance::device` via `SetDevice`/`GetDevice()` per convention). Not yet checked: whether `CommandPoolNode`'s newly-created pool and `ComputeDispatchNode`'s allocation from it are genuinely against the SAME new `VkDevice`, or whether some node in between the pool's rebuild and the dispatch node's rebuild reintroduces a stale device/pool reference; whether `vkResetCommandPool` or an implicit pool-level reset runs between allocation and use.

**Fix (landed, real but partial):** `RenderGraph::NotifyDeviceLost()` — `AbortCurrentFrame()` added before the idempotent latch check (`RenderGraph.cpp`). Confirmed via source diff and live log this fires correctly and stops the ORIGINAL race. `FailScenarioSweep_FrameSync.DeviceLostRecovery` remains `knownIssueId`-gated (report-not-block) — un-gate it only once the post-recovery command-buffer bug above is independently fixed and verified. Reproduction unchanged: `cmake --build build-wsl --target test_fail_scenario_sweep -- -k 0` (or `build/wsl` on the main checkout) then `./test_fail_scenario_sweep --gtest_filter='FailScenarioSweep_FrameSync.DeviceLostRecovery'`.

**Severity:** High (crash, defeats the device-loss recovery feature under precise timing) / only reachable via synthetic fault injection · **Status:** OPEN (partially fixed — see update above).

---

## Resolved

### KI-004 — Nodes/resources surviving device-loss recovery with stale device-scoped handles (crash class)

**Discovered:** 2026-07-02 by `FailScenarioSweep_FrameSync.DeviceLostRecovery` (Fail-Scenario-Simulation Inc 1 Task 7). **Resolved:** 2026-07-03, in three layers — the "one bug" was a CLASS.

**Symptom (evolution across the fix layers):** forcing a one-shot `VK_ERROR_DEVICE_LOST` out of `FrameSyncNode`'s fence wait → recovery completes ("RECOVERY COMPLETE") → SIGABRT. Layer by layer: (1) originally, a node downstream of the detection site executed on the condemned frame; (2) after fixing that, the FIRST post-recovery frame crashed at `vkBeginCommandBuffer: Invalid commandBuffer` (gdb: `UIRenderNode` beginning a command buffer allocated from the destroyed device's pool); (3) after fixing that, 30 post-recovery frames ran clean but FINAL teardown crashed at `vkUnmapMemory: Invalid device` / SIGSEGV in `PickIdTargetNode::DestroyImages` (destroying old-device images with the new device handle).

**Root cause (the class):** components holding device-scoped state across `CleanupReason::DeviceLost`:
1. `RenderGraph::RenderFrame`'s Execute loop checked `frameAborted_` between nodes but `deviceLost_` only at frame END — a mid-frame loss let downstream nodes execute/submit on the condemned frame. **Fix:** `NotifyDeviceLost()` calls `AbortCurrentFrame()` before latching (every current and future detection site aborts the frame for free). Commit `51a8dbd7`.
2. **Persistent-resource guards `if (reason != FinalTeardown) return;` in node CleanupImpls** — written for the Recompile case (device survives), but they ALSO kept device-scoped resources across `DeviceLost`, and the create-once guards in CompileImpl (image-count / null-handle checks) then reused the stale handles post-recovery. Affected and fixed (guard flipped to `if (reason == Recompile)` so DeviceLost tears down like FinalTeardown): `UIRenderNode` (per-image command buffers + RmlUi GPU objects; the post-recovery `vkBeginCommandBuffer` crash), `PickIdTargetNode` (the final-teardown crash), `BodyOctreeSceneNode`, `DynamicInstanceBufferNode`, `InstanceBufferNode`, `MvpUniformNode`, `StorageBufferNode`, `RenderTargetNode`, and `FrameSyncNode`'s persistent timeline semaphore (latent — timeline edges dormant in the default graph). Correct keeps left untouched: `WindowNode` (window+surface), `InstanceNode` (VkInstance), `InputNode` (GLFW hooks) — instance/OS-scoped, they legitimately survive a device loss.
3. **(Related hygiene, same session):** the synchronization2 entry points (`vkCmdPipelineBarrier2KHR`/`vkQueueSubmit2KHR`) were process-global function pointers (`VulkanGlobalNames.h`) despite being DEVICE-LEVEL dispatch — wrong for multi-device and a stale-dispatch window during recovery. Moved to per-instance `VulkanDevice::fpCmdPipelineBarrier2`/`fpQueueSubmit2` (resolved in `CreateDevice`); nodes reach them via `GetDevice()`, device-less recorders (`PassRecorder`, `BatchedUpdater::RecordAll`) receive the PFN as an explicit caller-injected parameter.

**Verified (2026-07-03, WSLg + Dozen):** `DeviceLostRecovery` passes as a HARD gate (no `knownIssueId`): detection → frame abort → teardown-reverse/rebuild-forward → 30 continuous post-recovery frames → clean final teardown, `[ PASSED ]` exit 0. `VIXEN_SIMULATE_DEVICE_LOSS=10` bridge on `*BootWarmupTeardown*` also passes. No-regression: registry 5/5, BootWarmupTeardown, AcquireOutOfDate/Suboptimal (KI-003 hard gates), PresentOutOfDate, MaximizeLikeFullscreenButton all PASS; the two WM-refusal skips unchanged. This completes Device-Loss-Recovery-2026-06.md Inc 3's automated-test item for real.

**Durable rule:** a `CleanupImpl` persistence guard must distinguish WHY it persists — device-scoped resources may only survive `Recompile`; only instance/OS-scoped state (window, surface, VkInstance) may survive `DeviceLost`. And device-level function pointers are per-`VulkanDevice` members, never globals.

**Severity:** was High (crash, defeated device-loss recovery) · **Status:** RESOLVED.


**Discovered:** 2026-07-02, by the `FailScenarioSweep_FrameSync.DeviceLostRecovery` fail-scenario (Fail-Scenario-Simulation-Design-2026-07.md, Inc 1 Task 7) — forcing a one-shot `VK_ERROR_DEVICE_LOST` out of `FrameSyncNode`'s `vkWaitForFences` on an otherwise-healthy frame (after 30 clean warmup frames).

**Symptom:** live-gated on WSLg + WSL2 Dozen ICD, reproduced 3 times across 2 different fault-arming paths:
- Twice via the scenario's own `ArmFault` (`timeout 90 ./build-wsl/application/main/test_fail_scenario_sweep --gtest_filter='FailScenarioSweep_FrameSync.DeviceLostRecovery'`) — both SIGABRT, exit 134, "Aborted".
- Once via the pre-existing `VIXEN_SIMULATE_DEVICE_LOSS` env-var bridge on an unrelated test case (`VIXEN_SIMULATE_DEVICE_LOSS=10 ./build-wsl/application/main/test_fail_scenario_sweep --gtest_filter='*BootWarmupTeardown*'`) — SIGSEGV, exit 139 (same underlying use-after-free-class race, different manifestation signal depending on exact memory/timing state, which is normal for this bug class). This confirms the race is in the recovery orchestration itself, not specific to the new `ArmFault`/`ScenarioContext` plumbing — the pre-existing env hook hits the identical bug once it actually lands the fault frame-precisely, something a human timing a real TDR by hand essentially never did.

Log sequence (ArmFault runs): `RenderGraph::RecoverFromDeviceLoss` logs "===== RECOVERY COMPLETE: rendering resumes on the new device =====", `VulkanGraphApplication::Render` logs "Device recovery succeeded — resuming rendering", then a `ComputeDispatchNode::RecordComputeCommands` call for a swapchain image (index varied 0→1 between runs — see root cause), then `[Vulkan Loader] ERROR: vkBeginCommandBuffer: Invalid commandBuffer [VUID-vkBeginCommandBuffer-commandBuffer-parameter]`, then abort. This is the Vulkan **loader's** own always-on parameter check (VK_LAYER_KHRONOS_validation is confirmed NOT installed in this environment — see M3 handoff notes — so this fires even without the validation layer), meaning the command-buffer handle in play is not just semantically stale but structurally invalid (freed/reallocated).

**Root cause (confirmed by source read across `RenderGraph.cpp`, `FrameSyncNode.cpp`, `ComputeDispatchNode.cpp`):** `RenderGraph::RenderFrame`'s sequential per-frame Execute loop (`RenderGraph.cpp:855-895`, mirrored in the parallel path `:791-849`) calls every node's `Execute()` unconditionally with no check of `deviceLost_` between iterations. When the injected fault fires, `FrameSyncNode::ExecuteImpl` (`FrameSyncNode.cpp:156-161`) calls `GetOwningGraph()->NotifyDeviceLost(...)` and `return`s early — without throwing, and before its own `ctx.Out()` calls (lines 166-174) republish this frame's fence/semaphore outputs. Because it returns normally (no exception), the loop treats it as ordinary completion and proceeds to the next node in `executionOrder`. `ComputeDispatchNode` sits downstream of `FrameSyncNode` in that order (consumes its `IN_FLIGHT_FENCE`/semaphore outputs, `ComputeDispatchNode.cpp:164`), so it still runs `ExecuteImpl`/`RecordComputeCommands` in the SAME condemned frame, reading stale (previous-frame) sync values and recording/submitting against a command buffer that is about to be invalidated. `RenderGraph::RenderFrame` only checks `deviceLost_` at the very end of the frame (`RenderGraph.cpp:938-940`) — after the whole node loop, including this stray submit, has already run — so `RecoverFromDeviceLoss`'s teardown (which does call `WaitForGraphDevicesIdle()` before tearing down pools/buffers, `RenderGraph.cpp:652`) starts strictly *after* the out-of-band submit is already in flight, racing it. The image-index variance between runs (0 vs 1) is consistent with this being a genuine race keyed on swapchain-acquisition timing, not a deterministic off-by-one.

Ruled out during investigation (confirmed clean, no need to re-check): `RecoverFromDeviceLoss` does cover every node via topologically-sorted reverse-teardown/forward-rebuild (`RenderGraph.cpp:654-678`); `ComputeDispatchNode::CompileImpl` unconditionally reallocates command buffers fresh every compile (no cross-compile reuse bug); `CommandPoolNode`/`DeviceNode` correctly destroy-and-recreate their Vulkan objects each recovery cycle with no stale-pointer caching.

**Impact:** High in principle — any device-loss event on a device fast enough to still be mid-frame-loop when the fault lands can hit this race, defeating the very recovery path Device-Loss-Recovery Inc 1-3 built. Low observed impact historically because this is the first deterministic, frame-precise way to trigger `VK_ERROR_DEVICE_LOST` in this environment (a real TDR/driver-reset is not something a human reliably times to a specific frame).

**Fix options:**
1. Add a `deviceLost_` check immediately after each `node->Execute()` call in `RenderGraph::RenderFrame`'s sequential loop (`RenderGraph.cpp:855-895`) and the parallel path (`:791-849`); `break` out of the frame the instant `NotifyDeviceLost` latches, so no node downstream of the fault site executes on the condemned frame. Smallest, most direct fix — addresses the actual race.
2. Have `FrameSyncNode::ExecuteImpl` throw instead of early-`return`ing on `VK_ERROR_DEVICE_LOST`, if the node-execution wrapper's exception handling already stops the frame cleanly (needs verification — mirroring option 1's effect via a different mechanism).
3. Both: option 1 as the primary defense (works regardless of individual node behavior), option 2 as defense-in-depth for the specific FrameSyncNode call site.

**Recommended:** option 1 — it is the single choke point that guarantees no downstream node observes a mid-recovery frame, regardless of which node happens to sit after `FrameSyncNode` in future graph topologies.

**Reproduction:** `cmake --build build-wsl --target test_fail_scenario_sweep -- -k 0` (VIXEN_FAIL_SCENARIOS=ON) then EITHER `timeout 90 ./build-wsl/application/main/test_fail_scenario_sweep --gtest_filter='FailScenarioSweep_FrameSync.DeviceLostRecovery'` OR `VIXEN_SIMULATE_DEVICE_LOSS=10 timeout 60 ./build-wsl/application/main/test_fail_scenario_sweep --gtest_filter='*BootWarmupTeardown*'`.

**Severity:** High (crash, defeats the device-loss recovery feature under precise timing) / only reachable via synthetic fault injection.

**Resolved:** 2026-07-03. **Fix applied (variant of option 1, one choke point):** `RenderGraph::NotifyDeviceLost()` now calls `AbortCurrentFrame()` before latching (`RenderGraph.cpp`) — the same central frame-abort `9d95bd75` introduced for the out-of-date acquire path. Any device-loss detection site (current: FrameSyncNode's fence wait; future: acquire/present/submit backstops) therefore aborts the frame the instant it latches, and the sequential Execute loop's existing `frameAborted_` check skips every downstream node on the condemned frame. Chosen over wiring the call inside `FrameSyncNode::ExecuteImpl` because the invariant belongs to the latch, not to one detection site.

**Verified:** `FailScenarioSweep_FrameSync.DeviceLostRecovery` un-gated (`knownIssueId` removed) and passing as a hard gate — full recovery (teardown-reverse/rebuild-forward, "RECOVERY COMPLETE") + 30 continuous post-recovery frames, no crash; the `VIXEN_SIMULATE_DEVICE_LOSS=10` env bridge on `*BootWarmupTeardown*` likewise recovers and passes. The scenario is now the permanent regression gate for this bug, completing Device-Loss-Recovery-2026-06.md Inc 3's automated-test item.

**Residual (follow-up, not this bug):** the PARALLEL execution path (`RenderGraph::RenderFrame`'s `ExecutePhase` branch) contains no `frameAborted_` checks at all — the central abort (KI-003's fix AND this one) only takes effect in sequential mode. The live app and all gates run sequential today; wire abort-awareness into the parallel executor before enabling parallel execution in production.

---

### KI-001 — 3 RenderGraph tests fail to build: missing `xcb/xcb.h` (WSL env)

**Discovered:** 2026-07-02 (during the config-struct codegen epic full-build gate; pre-existing, not caused by that work).
**Resolved:** 2026-07-02.

**Symptom:** a full `cmake --build build-wsl -- -k 0` failed to compile 3 test TUs:
- `libraries/RenderGraph/tests/test_array_type_validation.cpp`
- `libraries/RenderGraph/tests/test_field_extraction.cpp`
- `libraries/RenderGraph/tests/test_resource_gatherer.cpp`

Error (all three, identical): `.vulkan-sdk/1.4.350.1/x86_64/Include/vulkan/vulkan.h:52:10: fatal error: xcb/xcb.h: No such file or directory`.

**Root cause:** `vulkan.h` includes `<xcb/xcb.h>` when `VK_USE_PLATFORM_XCB_KHR` is defined; the WSL build environment has no XCB development headers installed (`libxcb1-dev` / `libxcb-*-dev`). These three tests pulled the full Vulkan platform header (transitively) rather than a headless subset — despite `test_type_system.cmake`'s own header stating "Compatible with VULKAN_TRIMMED_BUILD (headers only, no Vulkan runtime needed)".

**Fix applied (option 2 — root-cause):** removed the `VK_USE_PLATFORM_{XCB,WIN32,MACOS}_KHR` `target_compile_definitions` blocks from all 3 targets in `libraries/RenderGraph/tests/test_type_system.cmake`. These are header-only compile-time/type-trait tests that never link a real Vulkan surface; no sibling headless `.cmake` in the same directory (`test_core_systems.cmake`, `test_critical_nodes.cmake`, `test_graph_systems.cmake`, `test_voxel_systems.cmake`) defines a platform macro at all — this file was the outlier.

**Verified:** all 3 targets build clean and pass at runtime on WSL (no XCB headers installed) — `test_array_type_validation`, `test_field_extraction`, `test_resource_gatherer` all print their `✅ ALL TESTS PASSED` banners, exit 0.

**Severity:** Medium · **Status:** RESOLVED

---

### KI-002 — `test_shell_octree_gpu.ConcatRejectsMoreThanThree` fails (stale test vs removed cap)

**Discovered:** 2026-07-02 (config-struct codegen C1 gate; pre-existing, unrelated to that byte-identical struct alias).
**Resolved:** 2026-07-02.

**Symptom:** `test_shell_octree_gpu` was 8/9 — `ShellOctreeGpu.ConcatRejectsMoreThanThree` (`libraries/SVO/tests/test_shell_octree_gpu.cpp:179`) failed. The test built 4 shell octrees and asserted `EXPECT_THROW(Concatenate(four), std::length_error)`.

**Root cause:** the `kMaxOctrees = 3` cap was intentionally removed in the earlier recipe-authoring epic (the octree pool became memory-budgeted / count-unbounded — see the `recipe-authoring-pipeline-shipped` work; `ShellOctreeGpu.h`'s own `Concatenate()` docstring already read "Count is unbounded", and the sibling `ConcatenateSdf()` had a matching `OctreePool.ConcatenatesMoreThanThreeSdfOctrees` accept-test). `Concatenate` only throws `std::invalid_argument` on a null pointer, never on count, so the stale `EXPECT_THROW` failed. The test was never updated when the cap was removed.

**Fix applied (option 1 — genuinely unbounded):** renamed the test to `ConcatAcceptsMoreThanThree` and rewrote it to assert `Concatenate(four)` succeeds and produces a valid combined pool — `count == 4`, per-octree `nodeArrayBase`/`brickArrayBase` non-decreasing, and the concatenated `nodes`/`bricks` byte buffers equal to the sum of per-octree element counts times their stride (`sizeof(ChildDescriptor)` / `SerializedOctree::kBrickStrideBytes`), mirroring the existing `ConcatRecordsPerOctreeBaseOffsets` test's assertion style. Also corrected 3 stale "<=3 octrees" comment headers in `ShellOctreeGpu.h` (lines 55/57/663) that contradicted the function's own "Count is unbounded" docstring.

**Verified:** `test_shell_octree_gpu` is 9/9 passing at runtime (lavapipe-free, headless gtest).

**Severity:** Low · **Status:** RESOLVED

---

### KI-003 — `GeometryRenderNode` crashes (SIGSEGV) when `SwapChainNode` reports `IMAGE_INDEX = UINT32_MAX`

**Discovered:** 2026-07-02, by the `FailScenarioSweep_SwapChain.AcquireOutOfDate` fail-scenario (Fail-Scenario-Simulation-Design-2026-07.md, Inc 1 Task 6) — the first thing to deterministically force `VK_ERROR_OUT_OF_DATE_KHR` out of `vkAcquireNextImageKHR` on this dev box; no real GPU/driver here had apparently ever returned it outside a human-timed window resize.
**Resolved:** 2026-07-03, by the user's own parallel live-debugging session — commit `9d95bd75` "fix(render): fullscreen/maximize segfault — central frame abort on out-of-date acquire" (main), merged into this branch via `1df4ac4d`.

**Symptom:** live-gated on WSLg + WSL2 Dozen ICD. `./build-wsl/application/main/test_fail_scenario_sweep --gtest_filter='FailScenarioSweep_SwapChain.AcquireOutOfDate'` dumped core (confirmed via `timeout ... ; echo EXIT_CODE=$?` → `timeout: the monitored command dumped core`). No gdb/lldb was available in this environment initially to pull a symbolized backtrace; the crash was isolated from log analysis instead — exactly 31 `VoxelGridNode::ExecuteImpl ENTERED` cycles ran (30-frame warmup + the 1 frame where the fault fires), zero `SwapChainNode::Compile`/`RECOMPILATION TRIGGERED` lines ever appeared, and the process died silently mid-frame with no gtest `[  FAILED  ]`/assertion/signal text — consistent with a hard SIGSEGV inside frame N+1's execution, not a hang.

**Root cause (as originally diagnosed, confirmed correct by the independent fix):** `SwapChainNode::AcquireNextImage` correctly detected `VK_ERROR_OUT_OF_DATE_KHR`/`VK_SUBOPTIMAL_KHR`, called `MarkNeedsRecompile()`, and returned `UINT32_MAX`. `MarkNeedsRecompile()` only set a **deferred** flag while the node was `Executing` — it did not recompile synchronously. `SwapChainNode::ExecuteImpl` was correctly guarded and published `ctx.Out(IMAGE_INDEX, UINT32_MAX)` before returning early. But `RenderGraph::RenderFrame`'s per-frame node loop did not stop or skip remaining nodes this frame after the sentinel was set — it continued executing the rest of `executionOrder` in the same frame. `GeometryRenderNode::ExecuteImpl` read that propagated `IMAGE_INDEX = UINT32_MAX` and indexed `renderCompleteSemaphores[imageIndex]` — an **unchecked `operator[]`** — before its own `imageIndex == UINT32_MAX` guard, which existed but sat ~29 lines later, gating only the command-buffer/submit logic. `UINT32_MAX * sizeof(VkSemaphore)` (~34GB) past the vector's data pointer landed on an unmapped page → SIGSEGV. The user's independent debugging (via the live fullscreen/maximize crash, not this scenario) found the SAME root cause plus 5 additional consumers with the identical guard-placement mistake (DescriptorSetNode + AccelerationStructureNode had no guard at all — the actual user-reported maximize SIGSEGV; ComputeDispatch/GeometryRender/PassGroup indexed before their guard).

**Fix applied (commit `9d95bd75`, matches this KI's recommended "option 2" — graph-level hardening, generalized beyond a single guard fix):** `RenderGraph` gained a generic `AbortCurrentFrame()`/`IsFrameAborted()` mechanism (`RenderGraph.h`/`.cpp`) — any node can call it mid-frame to signal the frame cannot proceed; the sequential (and parallel) Execute loop checks it after every node's `Execute()` and `break`s before the next node. `SwapChainNode`'s OUT_OF_DATE/SUBOPTIMAL sentinel branch now calls it, so every downstream per-image consumer is skipped wholesale for that frame instead of needing an individually-correct guard. Per-node guards remain as second-layer defense (six were fixed to be correctly-placed/present as part of the same commit, including `GeometryRenderNode`'s).

**Verified (post-merge, this branch):** `FailScenarioSweep_SwapChain.AcquireOutOfDate`/`AcquireSuboptimal` no longer crash — 30 full post-injection frames complete, 0 validation errors, no core dump (initially observed via the scenarios' own known-issue-mode report: "progressed=true, validationErrors=0"; the `knownIssueId` gate has since been removed from both scenario declarations in `SwapChainNode.cpp` so they now hard-gate this fix as permanent regression tests).

**Impact was:** High in principle (any real OUT_OF_DATE/SUBOPTIMAL swapchain result crashed the app instead of skipping the frame); this KI's crash was ONE INSTANCE of the user's separately-tracked live fullscreen-button crash class (same "swapchain reports an extent/index change, a downstream node doesn't defend against the transient invalid state" shape) — confirmed identical by the shared fixing commit, not merely plausibly related as originally noted.

**Severity:** High (crash) · **Status:** RESOLVED

---

### KI-014 — Library tests are not `ctest`-discoverable project-wide (`enable_testing()` ordered after `add_subdirectory(libraries)`)

**Discovered:** 2026-07-05, during AppFlow Inc-1 Milestone 3 (the first library added since; the gap is pre-existing and affects every VIXEN library, not AppFlow specifically).

**Symptom:** `ctest --test-dir <build> -N` (or `-R <anything>`) reports `Total Tests: 0` for the ENTIRE project — no library's gtest targets are discovered, despite every library's `tests/CMakeLists.txt` calling `gtest_discover_tests`. No `CTestTestfile.cmake` is generated under `build/libraries/<lib>/tests/`.

**Root cause:** `VIXEN/CMakeLists.txt` calls `add_subdirectory(libraries)` (line ~393) BEFORE `enable_testing()` (line ~445, inside the `if(BUILD_TESTS)` "TESTING INFRASTRUCTURE" block near the bottom). `gtest_discover_tests` only registers CTest entries when testing was enabled at the point the subdirectory was processed; because `enable_testing()` runs afterward, no library subdirectory ever sees an enabled test harness, so nothing is registered with CTest. Verified project-wide: SVO, RenderGraph, CashSystem, EventBus, AppFlow, etc. all lack a generated `CTestTestfile.cmake`.

**Workaround (current, documented):** run the gtest binaries directly — `./build/libraries/<lib>/tests/[Debug/]test_*[.exe] --gtest_brief=1` — which is already `VIXEN/CLAUDE.md`'s documented test command. AppFlow Inc-1's suite (17 tests) was gated this way (build the 5 test targets, run each binary; all exit 0).

**Fix direction (not applied — out of scope for the AppFlow work that found it):** move `enable_testing()` (and the `include(GoogleTest)`/`FetchContent` gtest setup it depends on) ABOVE `add_subdirectory(libraries)` in `VIXEN/CMakeLists.txt`, gated by `if(BUILD_TESTS)`. Then `ctest --test-dir <build>` would discover all library tests. Low-risk, mechanical, but touches the top-level build ordering — worth its own small verified change so the full suite is CI-runnable via one `ctest` invocation.

**Severity:** Low (tests run fine directly; only the aggregate `ctest` runner is affected) · **Status:** OPEN
