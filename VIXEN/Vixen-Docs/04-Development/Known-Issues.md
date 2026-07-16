---
title: Known Issues / Bugs To Fix
status: living log
created: 2026-07-02
tags: [known-issues, bugs, tech-debt]
---

# Known Issues / Bugs To Fix

Living log of confirmed-but-unfixed issues. Each entry: symptom, root cause, impact, fix options, severity. Add new issues at the top; move fixed ones to a `## Resolved` section with the fixing commit.

---

## KI-037 — `RecipeInstanceBucketing.comp`'s `ProjectToPixel` silently drops behind-camera bound-sphere extrema, shrinking (not growing) the coverage rect for camera-straddling instances

**Discovered:** 2026-07-16, during [[Recipe-Bucketed-Dispatch-Overhead-Inc3-Plan-2026-07]] M2's
barrier-coalescing correctness analysis — root-caused while establishing whether M1's per-bucket
screen-space coverage rects are a sound (true-superset) basis for a barrier-skip optimization
between spatially-disjoint bucket pairs.

**Symptom (latent, not yet observed in a failing test):** `RecipeInstanceBucketing.comp`'s
`ProjectToPixel` (lines 134-144) projects 7 world-space points (a recipe's bound-sphere center
plus 6 axis-extremal points) and returns `false` — contributing nothing to the coverage union —
for any point with `clip.w <= 0` (behind the camera). For an instance whose bound sphere straddles
the camera's near/W=0 plane (center behind the camera with some extremal points in front, or the
reverse), this DROPS legitimate in-frustum extrema from the union, producing a coverage rect
SMALLER than the sphere's true on-screen footprint — the opposite of the conservative
("over-cover, not under-cover") direction the file's own header comment (lines 220-225) documents
as the design intent.

**Root cause:** `ProjectToPixel` has no near-plane clipping — it's an all-or-nothing per-point
test (`if (clip.w <= 0.0) return false;`), not a sphere-vs-near-plane clip that would produce a
correctly-expanded partial footprint.

**Impact today:** none observed — no test in the current suite constructs a camera-straddling hot
-recipe instance, so `RecipeInstanceBucketing.comp`'s coverage rects have always been a true
superset in every scene exercised so far (confirmed: `test_recipe_multi_bucket_compositing`'s
camera at `eye=(0,0,20)` looking at the origin, all instances well in front). **This is a latent
soundness gap, not an active bug** — but it is exactly the kind of gap that matters for correctness
-critical work built ON TOP of these coverage rects: Inc3 M2 needed the rects to be a sound
conservative superset to justify skipping `MultiDispatchNode`'s inter-bucket barrier between
provably-disjoint buckets, and this gap is precisely why that reduction was NOT implemented (see
[[Recipe-Bucketed-Dispatch-Overhead-Inc3-Plan-2026-07]] M2's Progress Log for the full reasoning).
Any FUTURE consumer of these coverage rects for a correctness-relevant decision (not just this
barrier-skip idea) should check this entry first.

**Fix options:** (a) clip the bound sphere against the camera's near plane before projecting
(replace the 6-axis-extrema approximation with a near-plane-aware version, or fall back to
full-screen coverage for any instance whose extrema have mixed-sign `clip.w`); (b) simpler/more
conservative: detect the mixed-sign-w case explicitly and emit the full-screen rect as a safe
fallback for that bucket only, rather than trying to compute a tight partial rect.

**Severity:** Low today (no observed failure, no current consumer relies on the soundness
property) but **blocking** for any future barrier-coalescing/spatial-culling work that wants to
trust these rects as a hard superset guarantee. · **Status:** OPEN, out of scope for
[[Recipe-Bucketed-Dispatch-Overhead-Inc3-Plan-2026-07]] M2 (an analysis milestone, not a fix
milestone for M1's shipped code) — the natural owner is whichever future increment revisits
barrier-coalescing or otherwise builds a correctness-relevant decision on these coverage rects.

---

## KI-034 — 8 test files hand-mirror `BodyInstanceRayMarch.comp`'s push-constant struct at a stale 76 bytes, now 92; blocks ~30+ render tests

**Discovered:** 2026-07-15, during [[Recipe-GPU-Instance-Bucketing-Inc2-Plan-2026-07]] M1's Opus
validator's regression sweep — the implementer reported "4 pre-existing failures," but the
validator's own full-suite run found the real pre-existing baseline is materially larger
(~34 RenderGraph render-cluster failures sharing this one root cause, plus 4 unrelated benchmark
timeouts under concurrent machine load).

**Symptom:** every test that hand-declares a local `PushConstants` mirror struct and
`static_assert(sizeof(PushConstants) == 76, ...)` fails at `vkCreateComputePipelines` with
`VUID-VkComputePipelineCreateInfo-layout-10069` (`[0,92] outside VkPushConstantRange [0,76]`) —
pipeline creation fails, the shader never runs, and every downstream assertion (hit count,
silhouette, IoU, etc.) fails on zero rendered pixels, masking the tests' actual intended coverage.

**Root cause:** `shaders/SceneBindings.glsl`'s real `PushConstants` block grew from 76 to 92 bytes
when Sampled Lighting Inc2 M2 added `uint accumFrameCount` (`SceneBindings.glsl:228`, "consecutive
STATIC-camera frame count... drives the accumulate seam's converging-1/N alpha"). The production
shader/pipeline-layout path picked this up correctly; 8 test files' hand-copied C++ mirror structs
were never updated to match:
- `libraries/RenderGraph/tests/Nodes/test_hitrecord_readback.cpp:54-63` (the structural template
  every other file copied — `static_assert(sizeof(PushConstants) == 76, ...)` at :63)
- `test_appflow_editor_toggle_render.cpp`, `test_body_instance_occlusion_reject.cpp`,
  `test_editor_document_render.cpp`, `test_recipe_authoring_gate.cpp`, `test_recipe_pool_render.cpp`,
  `test_shadow_correctness.cpp`, `test_tier_crossing_lod_residency.cpp` — same stale 76-byte mirror.

**Confirmed pre-existing and independent of any single recent branch**: commit `bad30727` (the
`accumFrameCount` addition) is an ancestor of `main` well before this discovery; none of the 8
files above were touched by the increment that surfaced this.

**Impact:** ~30+ render-cluster tests across 8 files currently fail at pipeline-creation time, not
at their actual intended assertion — meaning this class of test has provided ZERO real geometry/
render coverage since `bad30727` landed. This is a significant, silent loss of test signal, not
just cosmetic failures. Also a trap for future validators: this class of failure shares symptoms
with several ALREADY-CATALOGUED unrelated pre-existing issues (KI-032's VUID-layout-07988 class),
so a future session should diff a new failure's specific VUID/message against this entry before
assuming "pre-existing, same as always."

**Fix options:** (a) the honest fix — update all 8 hand-mirrored structs to 92 bytes (add
`accumFrameCount`) and their `static_assert`s, one mechanical edit per file, following
`test_hitrecord_readback.cpp`'s struct as the canonical shape to copy from `SceneBindings.glsl`
correctly this time; (b) the durable fix — these hand-mirrored structs are exactly the class of
drift the project's `[GpuStruct]`/generated-struct codegen convention exists to prevent (see
[[kernel-codegen-framework-direction]], [[runtime-kernel-pipeline-direction]]'s config-struct
codegen program) — promoting this push-constant block to a generated/shared struct (like
`OctreeConfig`'s precedent) would make this whole class of bug structurally impossible instead of
mechanically fixed 8 times over. (a) is the fast unblock; (b) is the real fix if this drift
recurs again.

**Severity:** High (silent, wide loss of test coverage — not just noisy failures) · **Status:**
OPEN, pre-existing, out of scope for [[Recipe-GPU-Instance-Bucketing-Inc2-Plan-2026-07]] (a
render-pipeline-architecture increment, not a test-maintenance fix) — needs its own dedicated
session, ideally reached before it grows to a 9th/10th copy-pasted stale mirror.

---

## KI-032 — `test_baked_vs_virtual_parity`/`test_mip_fallback_render`/`test_recipe_pool_render` read back a color image no shader in their single-pass dispatch ever writes (Sampled-Lighting-Inc3 M5 pass-split fallout)

**Discovered:** 2026-07-15, during [[Recipe-Parameterization-Plan-2026-07]] M4's Task 11 (baked-vs-virtual
parity gate for a `ReadParam` recipe) — while root-causing why `test_baked_vs_virtual_parity`'s
`VirtualRendersGeometricallyEquivalentToBaked` gate had been failing (`bakedHits=0 virtualHits=0`) across
two independent prior validator passes (M2, M3), both of which correctly established the failure
pre-dates recipe-parameterization but had not root-caused it further.

**Two separate bugs were found and disentangled here — do not conflate them:**

1. **FIXED this milestone (M4):** all three test files hand-build a local `VkDescriptorSetLayout`
   including a stale `binding = 8` entry (a debug `ShaderCountersBuffer`). That binding was compiled
   out of `BodyInstanceRayMarch.comp`'s reflected SPIR-V interface unconditionally by
   `8509f58b` ("perf(widescreen): M2.1 compile shader debug counters out of the live path") on
   2026-07-03 — `SceneBindings.glsl`'s own binding-8 comment states this explicitly, and the real
   shader's binding set is `{0,1,2,3,4,5,9,10,...,22}` (verified by grepping every file
   `BodyInstanceRayMarch.comp`/`SceneBindings.glsl` transitively `#include`s — zero `binding = 8`
   declarations anywhere in the live path). Production's own descriptor-gatherer wiring
   (`BuildRenderGraph.cpp:795-801`) agrees — no binding 8. A local test layout that includes a
   binding the SPIR-V module doesn't declare is a `VUID-VkComputePipelineCreateInfo-layout-07988`-
   class validation error: the pipeline layout is incompatible with the shader module's actual
   resource interface. This is a genuinely SEPARATE bug from the `body_octree_scene` boot-recompile
   descriptor-staleness bug M3 filed (see KI-033 below) — that bug is real and still open, but is
   NOT what was causing these three specific tests' VUID cascade (different code path: a live
   render-graph recompile vs. these tests' own hand-built descriptor layouts); the
   stale binding-8 entry is a distinct, simpler, now-fixed root cause. **Fix:** removed the
   `bindL(8, ...)` layout entry, its `VkWriteDescriptorSet`, the pool-size count, and the
   now-unused `ctrBuf`/`ctrMem` plumbing from all three files. Confirmed live (Windows-native, real
   AMD Radeon GPU): `vkCreateComputePipelines` now succeeds with **zero** VUID/validation-layer
   output across all three files, where every prior run aborted or failed at pipeline-creation time.

2. **STILL OPEN, NOT fixed this milestone, filed here as the carried blocker for Task 11:** with
   the VUID gone, all three tests now run their full dispatch+readback cleanly but produce a
   totally empty (`bakedHits=0 virtualHits=0`) color-buffer readback — not a validation error, not
   a crash, just genuinely zero content in the image these tests read back. Root cause: **the
   `outputImage`/`colorImg` these tests dispatch-then-readback is no longer written by the single
   compute pass they invoke.** `BodyInstanceRayMarch.comp` stopped writing `outputImage` in
   `784adff7` ("wip(sampled-lighting-inc3): M1 shader-side pass split (KI-018) — DirectLighting.comp
   extracted, march traversal-only", 2026-07-11) — binding 0 is kept `writeonly`-declared only for
   its `imageSize()` call (see the shader's own header comment, ~lines 35-40/191-206). The pass that
   inherited the write, `DirectLighting.comp`, ALSO doesn't write it (bound `readonly` there,
   "EXCLUSIVELY for imageSize()" per its own header) — `747e156c` ("feat(sampled-lighting-inc3-m5):
   2-dispatch split -- RIS+temporal producer / spatial-reuse consumer", 2026-07-12) moved the real
   `imageStore(outputImage, ...)` a second time, into a THIRD shader, `SpatialReuseShade.comp`
   (`:33,591,594`). The production `RenderGraph` correctly chains all three passes with the right
   barriers; these three standalone test harnesses (authored/last-touched between 2026-06-29 and
   2026-07-11, i.e. straddling the M1/M5 pass-split) dispatch ONLY the first pass
   (`BodyInstanceRayMarch.comp` alone) and then copy back an image that pass never touches — reading
   back genuine, correctly-zeroed, untouched memory. `HitRecord` (binding 18) and `idOutputImage`
   (binding 9) ARE written by the single dispatched pass and would show real hit data if read back;
   the harnesses just don't read those.

**Confirmed pre-existing, not introduced by recipe-parameterization work:** both root causes
(`8509f58b`, `784adff7`/`747e156c`) predate `feat/recipe-parameterization-inc1`'s fork point by 8-16
days and touch zero recipe/param files; `git diff` of this milestone's actual corpus/bake/render-call
changes against `SceneBindings.glsl`/`BodyInstanceRayMarch.comp`/`DirectLighting.comp`/
`SpatialReuseShade.comp` is empty.

**Impact:** `test_baked_vs_virtual_parity`, `test_mip_fallback_render`, and `test_recipe_pool_render`
cannot produce a real pass/fail geometry signal via their current single-pass dispatch+colorImg-readback
design — ANY corpus entry (pre-existing or the new Task 11 `readparam_sphere` one) fails identically
with `bakedHits=0 virtualHits=0`, independent of recipe correctness. This is why Task 11's `ReadParam`
parity gate is CODE DONE / LIVE GATE PENDING for this milestone (same carried-obligation pattern as
Lazy-Procedural M2's boot-lazy live gate) rather than a real green pass — the corpus entry, bake-time
snapshot wiring, and virtual `recipeParams[]` wiring are all confirmed correct (register/bake/splice/
dispatch/readback all run cleanly, zero VUIDs, zero crashes) but the harness itself cannot currently
produce a nonzero IoU for ANY recipe, parameterized or not.

**Fix options:** (a) chain all three passes (`BodyInstanceRayMarch` → `DirectLighting` →
`SpatialReuseShade`) with correct barriers/bindings in each of the three test harnesses — the
production-correct fix, but nontrivial: a 3-stage pipeline with cross-dispatch buffer hazards per
`DirectLighting.comp`'s own M5 header comment, effectively porting a slice of `RenderGraph`'s real
pass-chaining logic into 3 already-large standalone harnesses; (b) switch each harness's pass/fail
signal from `colorImg` readback to `HitRecord`/`idOutputImage` readback (both ARE written by the single
dispatched pass) — far less surface to maintain, though it changes what "parity" measures (hit/id
buffer content vs. final shaded color) and would need re-deriving each harness's threshold/silhouette
logic against the new buffer's format. Not investigated further — a dedicated fix session should choose
between (a)/(b) deliberately, not as a drive-by inside a future unrelated milestone.

**Severity:** Medium (three real geometry-correctness gates are currently unable to assert anything —
not a production runtime bug, but a meaningful loss of test coverage that predates and is unrelated to
this branch) · **Status:** OPEN (item 2 only — item 1, the binding-8 VUID, is RESOLVED this milestone,
commit range TBD at merge) · out of scope for [[Recipe-Parameterization-Plan-2026-07]] to fully fix (a
RenderGraph/shader-pass-chaining problem, not a recipe/param-VM one) — Task 11's corpus entry and
wiring are provably correct and ready to pass once either fix option above lands.

---

## KI-033 — `VIXEN_PROCEDURAL_UBER_DEMO` boot recompile leaves shared descriptor set stale, producing a persistent VUID cascade

**Discovered:** 2026-07-15, during [[Recipe-Parameterization-Plan-2026-07]] M3's live validation-layer
render gate — the first time this exact gate (`VIXEN_PROCEDURAL_UBER_DEMO` + real Windows-native GPU +
`VK_LAYER_KHRONOS_validation`) was ever run to completion (it had been flagged "STILL CARRIED
(windowed only)" and never executed in
[[Lazy-Procedural-Delta-Baseline-Inc0-Inc1-Plan-2026-07]] M5's own Progress Log).

**Symptom:** running the demo with validation layers on produces ~80-160 VUID lines (run-to-run
variance), all on `voxelGridNode`-sourced bindings (`InstanceIterDebugBuffer` binding 14,
`RayTraceBuffer` binding 4) — never on `recipeParams`/binding-10/`bodyInstances`. The count and
bindings are identical whether or not any `ReadParam`-using body is present.

**Root cause (confirmed, not yet fixed):** a ONE-TIME boot-time recompile of `body_octree_scene`
fires right after the Render-pause/resume (swapchain-settling) sequence — NOT
`BodyOctreeSceneNode::SetInstances`'s `MarkNeedsRecompile` (confirmed zero occurrences of that log
line across full runs; "exceeds ring capacity" count is 0 every time). That recompile leaves the
shared descriptor set stale for `voxelGridNode`-sourced bindings, and the staleness persists/
recurs through most of the run rather than resolving after one frame.

**Confirmed pre-existing, not introduced by recipe-parameterization work:** independently isolation-
tested twice — once by the M3 implementer (env-gating out the `ReadParam` body, rebuilding, re-
running the original unmodified 3-body demo: byte-identical VUID/recompile counts) and once
independently by the M3 Opus validator (same experiment, same result: 1 boot recompile of
`body_octree_scene`, same VUID cascade on the same bindings, present or absent the `ReadParam`
body). Zero VUIDs reference any recipe-parameterization-touched binding.

**Impact:** any future live validation-layer gate through this demo path will see this cascade
unless/until fixed — a source of noise that could mask a REAL new VUID regression in a later
session unless carefully diffed against this known baseline signature (bindings 4/14, not the
bindings a given change actually touches).

**Fix options:** not investigated beyond root-causing the trigger (boot-time recompile timing vs.
swapchain settle) — likely a descriptor-set refresh/rebind gap in whatever handles
`voxelGridNode`'s bindings across a recompile, scoped to `RenderGraph`/`DescriptorSetNode`
territory, not the recipe/procedural system. Needs its own investigation session.

**Severity:** Medium (noisy but not a correctness bug in anything downstream of this cascade — no
crash, no wrong render output observed; the concern is masking future real VUIDs, not this one
itself) · **Status:** OPEN, pre-existing, out of scope for
[[Recipe-Parameterization-Plan-2026-07]] (a recipe/param-VM change, not a descriptor-refresh fix)

---

## KI-027 — `VoxelInjectionQueue`/`VoxelInjector` concurrent voxel creation is unsound (heap corruption, ECS assertion mismatch)

**Discovered:** 2026-07-12, in a broad ctest sweep audit — 7 tests fail: `VoxelInjectionQueueTest.ProcessMultipleVoxels` (SEGFAULT: `_CrtIsValidHeapPointer`/`is_block_type_valid` debug-heap assertions — real heap corruption, not a benign crash), `.ProcessBatchCreation` (Gaia ECS internal assertion `entityExpected == entityPresent` at `gaia.h:30565`), `.ConcurrentEnqueue`, `.StopDuringProcessing`, `VoxelInjectorTest.InsertEntities_SingleEntity` (`Exit code 0xc0000409`, a Windows stack-buffer-overrun/security-cookie trap), `.InsertEntities_MultipleEntities`, `.InsertEntitiesBatched_SingleBrick`, `.InsertEntitiesBatched_MultipleBricks`, `.CompactOctree`, `.InsertEntities_MixValidAndInvalid`, `.LargeBatchInsertion`.

**Confirmed pre-existing, not introduced by any 2026-07-12 session work:** the failing machinery (`VoxelInjectionQueue`'s parallel voxel creation, `createVoxelsBatch`'s "optimize for speed" path) was added by commit `25eb5989` ("feat: Implement parallel voxel creation using VoxelInjectionQueue and optimize createVoxelsBatch"), which predates this session's changes and is an ancestor of the branch tip before today's work started. An independent stale sweep log (`/tmp/ctest_final.log`, from a prior, unrelated session) already shows `VoxelInjectionQueueTest.ProcessMultipleVoxels` failing with the same interleaved/racing `"Batch progress:"` log output pattern — direct evidence of two threads concurrently and unsafely writing through the same non-thread-safe path, confirming this is a genuine, long-standing data race, not a new regression from today's `SdfBake.h`/`createVoxelsBatch` caller change (that change only added a new *single-threaded* call site; `VoxelInjectionQueue`'s existing *concurrent* call sites are untouched by it).

**Symptom shape:** interleaved log output from concurrent threads (`"[GaiaVoxelWorld] Batch progress: [GaiaVoxelWorld] Batch progress: 0%0%"` — two threads' `std::cout` calls torn mid-line), heap corruption, and Gaia ECS-internal invariant violations — consistent with `GaiaVoxelWorld`/`createVoxelsBatch`/the Gaia ECS `world` object being mutated from multiple threads without adequate synchronization somewhere in `VoxelInjectionQueue`'s worker/enqueue path.

**Root cause:** not yet investigated in depth — this entry documents the discovery and evidence for pre-existing status, not a diagnosis. Likely candidates for a future investigation: `GaiaVoxelWorld`'s underlying `gaia::ecs::World` may not be safe for concurrent `add()`/entity-creation calls at all (ECS libraries frequently assume single-threaded mutation with external synchronization left to the caller), and `VoxelInjectionQueue`'s worker-thread dispatch may be missing a mutex/lock around its calls into the shared `GaiaVoxelWorld` instance.

**Impact:** `VoxelInjectionQueue`/`VoxelInjector`'s parallel voxel-creation path is currently unsound — heap corruption and ECS state corruption under concurrent use. Any production caller of this path (if one exists — not yet audited) is at risk; any future caller should not be added until this is fixed.

**Fix options:** (a) add proper synchronization (a mutex around the shared `GaiaVoxelWorld` mutation, or per-worker private ECS staging + a single-threaded merge step) in `VoxelInjectionQueue`'s worker path; (b) confirm/document whether Gaia ECS itself is fundamentally single-threaded and restructure `VoxelInjectionQueue` to serialize all `GaiaVoxelWorld` mutation onto one thread regardless of how work is parallelized elsewhere (e.g. parallel voxel *computation*, serial *insertion*).

**Severity:** High (heap corruption is a real memory-safety bug, not just a failing assertion) · **Status:** OPEN, pre-existing, out of scope for the 2026-07-12 sweep audit that discovered it (that audit's scope was fixing/triaging failures surfaced by its own session's changes, not auditing unrelated pre-existing concurrency bugs)

---

## KI-026 — SDI regeneration silently no-ops on a reused UUID with changed shader source

**Discovered:** 2026-07-12, via `SdiLifecycleTest.UpdateShaderAndRegenerateSdi` failing deterministically (100%, 5/5 reproductions) in a broad ctest sweep audit.

**Symptom:** `SpirvInterfaceGenerator::Generate()` (`libraries/ShaderManagement/src/SpirvInterfaceGenerator.cpp:157`) skipped regeneration whenever the target SDI file already existed on disk, keyed only on UUID (`GetSdiPath`, `:243`) — the skip guard's own comment claimed "same descriptor hash" but no hash was ever computed or compared. Rebuilding a shader under the same UUID with genuinely different reflection data (e.g. a new binding) silently returned the stale file: `last_write_time` never changed and the new binding never appeared in the generated header. This breaks shader hot-reload / SDI-update whenever a UUID is reused across a shader edit.

**Root cause:** existence-only skip guard (`:164-168`, pre-fix) instead of a content/hash comparison, most likely a build-time perf shortcut whose promised hash check was never implemented.

**Fix (commit this session, 2026-07-12):** `Generate()` now always computes the code string first, then skips only the **write** if on-disk content is byte-identical to the newly generated code — preserving the original perf intent (no spurious rewrite/mtime bump when nothing changed) while fixing the correctness bug (a changed shader always produces different code, so it's always rewritten).

**Severity:** Medium (silent data-staleness — no error, no log distinguishing "reused, unchanged" from "reused, would-have-changed") · **Status:** RESOLVED

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

**Severity:** low today (Inc2 scope never triggers it), high at Inc3 (a silent, hard-to-diagnose accumulation-defeat bug on exactly the layer Inc3 is built to speed up) · **Status:** ✅ RESOLVED 2026-07-11, Sampled Lighting Inc3 M2 (commit `102c316e`, re-gated `f4a47ac7`). Applied fix option (a): a new `WorldPosHistoryNode` (persistent rgba32f@binding-22, mirrors `AccumulationHistoryNode`'s single-persistent-image pattern) plus a geometric reproject reject (`distance(histWorldPos,bestWorldPos)≤0.1` world units) replacing the color-consistency check (c); bounds (a) + motion-magnitude (b) untouched. Re-gated on a genuinely-relinked binary (the original gate was invalid — caught by build-graph forensics, see the plan doc's M2 Progress Log for the full false-pass story): same-tick per-pixel diff vs ground truth shows 0.0016–0.0044% of pixels differing (max diff 1–2/255), STRICTER than the old color-reject baseline (Inc2 M4's own 0.002–0.076%) at every comparable tick. This companion buffer also serves ReSTIR's own reservoir-reprojection validity (Inc3 M4/M5). Not an Inc2 defect — was always a validator-confirmed, explicitly-scoped-out prerequisite for Inc3, tracked here so it wasn't silently inherited; now closed by the increment it prerequisited.

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

### KI-020 — Two pre-existing MSVC-portability compile failures break the all-targets Windows build (`build.bat build`)

**Discovered:** 2026-07-10, by the `validate-gaia-sync` Opus validator during the Gaia v0.9.2 sync (both files are byte-identical to base `ab40cb97`; unrelated to that sync — pre-existing app-rot). Surfaced because the validator ran a full `build.bat build`, which halts with `ninja: build stopped` on these two.

**Symptom:** a full Windows all-targets build (`build.bat build`) does NOT go fully green — `ninja: build stopped` on two independent compile errors in non-Gaia test code. The Gaia libraries + all three Gaia test exes, and the individual targets people usually build, compile fine; only the aggregate all-targets Windows build is affected.

**The two failures:**
1. `libraries/RenderGraph/tests/.../test_body_instance_raymarch_render.cpp` — uses POSIX `setenv`/`unsetenv`, which do not exist on MSVC → `C3861: 'setenv': identifier not found` (and `unsetenv`). MSVC provides `_putenv_s` (and `_putenv("VAR=")` to clear) instead.
2. `libraries/RenderGraph/tests/.../test_octree_config_sdi_parity.cpp` — includes SVO's `SdfRecipes.h` → generated `SdfCoreKernels.g.hpp`, which uses bare `min`/`max` that collide with the Windows `<windows.h>` `min`/`max` macros → `C2589: '(' : illegal token on right side of '::'` (the classic macro-expansion collision). Same root cause as KI-017 below.

**Root cause:** both are Windows/MSVC-portability gaps in test/generated code that presumably compiled or were only exercised under WSL/GCC. Neither is a logic bug; both are include/identifier portability.

**FIXED 2026-07-12** (commit `f3f25e35`, branch `fix/ki-020-ki-017-windows-portability`):
1. Added a portable `SetTestEnv`/`UnsetTestEnv` shim to `test_body_instance_raymarch_render.cpp` (`_putenv_s`/`_putenv("NAME=")` on `_WIN32`, `setenv`/`unsetenv` elsewhere) — matches the existing convention already used in `ScenarioHarness.cpp`'s `AppHarness` ctor. Grepped for other `setenv`/`unsetenv` call sites first: `ScenarioHarness.cpp` was already guarded, `VulkanGlobalNames.h`'s calls are inside `#if defined(__linux__)` blocks (never compiled on Windows) — this file was the only genuine gap, so a local shim (not a shared cross-file helper) was the right scope.
2. Fixed via KI-017's root-cause fix below (the `#undef min`/`max`/`abs` guard) — confirmed by rebuild that `test_octree_config_sdi_parity` now compiles and passes (1/1).

**Verification:** full Windows/MSVC `build.bat all` — both named targets now compile+link. `test_body_instance_raymarch_render` builds and runs (3/6 subtests pass; the other 3 fail at runtime on an unrelated pre-existing device-verification gate — this machine's real AMD GPU isn't in the test's verified-device allowlist (lavapipe/Dozen only), so it refuses to submit rather than a portability defect). `test_octree_config_sdi_parity` 1/1 pass.

**Severity:** Medium (blocked the aggregate Windows build; per-target builds unaffected) · **Status:** RESOLVED

---

### KI-017 — `SdfRecipes.h`/`SdfBake.h`'s transitive include chain fails to compile on Windows/MSVC (Windows-macro `min`/`max` pollution, no `#undef` guard)

**Discovered:** 2026-07-08, during Tiered-ESVO Inc2 M3 (GPU traversal-restart), when building `test_gpu_parity`/`test_tier_crossing_construction`/related SVO test targets via the `vixen-ninja` (Windows/MSVC) preset for the first time in the `tiered-esvo-inc2` worktree.

**Symptom:** any test TU that includes `SdfRecipes.h` or `SdfBake.h` (directly or transitively, e.g. via `ShellOctreeGpu.h` → `SdfBake.h` → `SdfRecipes.h`) fails to compile with a cascade of `error C2589: '(': illegal token on right side of '::'` / `error C2059: syntax error` / `error C2672: 'glm::length': no matching overloaded function found` starting in `SdfRecipes.h:85` (`std::max(-b - sq, 0.0f)`) and continuing into `Recipe/generated/SdfCoreKernels.g.hpp` (`glm::min`/`glm::max`/`glm::abs` calls) — the classic signature of `<windows.h>`'s `min`/`max` (and here, apparently `abs`) function-like macros clobbering `std::max(`/`glm::min(` call syntax.

**Root cause:** `SdfRecipes.h` and `SdfBake.h` have NO `#undef min`/`#undef max`/`#undef abs` guard at all (unlike `GpuTraversalMirror.h`, `test_tier_crossing_construction.cpp`, and several other files in this codebase, which DO carry this guard specifically because `<windows.h>` gets pulled in transitively on the Windows build via Vulkan/GTest). Some other header included earlier in a given TU's include order drags in `<windows.h>` before `SdfRecipes.h`/the generated kernel file are parsed, and nothing undoes the macros in between.

**Impact:** `VIXEN.exe` itself builds fine on Windows/MSVC (confirmed clean, `vixen-ninja` preset) — the failure is isolated to specific SVO test translation units (`test_gpu_parity.cpp`, `test_tier_crossing_construction.cpp`, and likely others that pull in `ShellOctreeGpu.h`/`SdfBake.h`/`SdfRecipes.h` without their own `#undef` guard already in scope before those headers). Reproduced independently on a clean, unmodified `4db93715` (pre-Tiered-ESVO-Inc2-M3) checkout via `git stash` — confirmed pre-existing and unrelated to any single increment's own changes; likely never previously exercised on this worktree's Windows/MSVC toolchain until M3 needed the WSL-vs-Windows comparison this session.

**Re-confirmed 2026-07-11 (Lazy-Procedural-Delta-Baseline Inc0 M6 Task 15 full sweep, before the fix below landed):** independently re-discovered the identical failure signature doing a from-scratch full-solution `vixen-ninja` build, expanding the known-affected-target list to 20 SVO test targets (`test_octree_config_sdi_parity`, `test_soa_sdf_serialize`, `test_soa_mip_serialize`, `test_tier_ref_table`, `test_tier_crossing_construction`, `test_tier_crossing_mirror_parity`, `test_channel_format`, `test_mip_sample_bake`, `test_stored_sdf_march_mirror`, `test_shell_derive`, `test_sdf_bake`, `test_recipe_bake`, `test_recipe_bake_center`, `test_octree_pool`, `test_generation_cost_benchmark`, `test_recipe_boot_ingest`, `test_recipe_baker`, `test_residency_default`, `test_gpu_parity`, `test_shell_octree_gpu`). That session tried fix option (a) scoped to `SdfRecipes.h` alone and found it insufficient in several TUs where `<windows.h>` was already poisoned by an earlier header before `SdfRecipes.h` was even reached — concluding global `NOMINMAX` (option (c)) was the only fix that could work for every affected TU. The fix actually applied below (2026-07-12) resolves this differently: rather than a single global `NOMINMAX`, it places the `#undef` guard at the SPECIFIC includer of the generated header (`Recipe/SdfRecipeEval.h`, not just `SdfBake.h`) — closing the exact gap the 2026-07-11 session's option-(a) attempt hit, without the wider blast radius of a global `NOMINMAX`. Also from that session: a separate, unrelated disk-capacity incident (see the standalone note further down this doc) meant "`VIXEN.exe` itself builds fine" could not be re-confirmed at the time — re-confirmed clean by the fix below.

**FIXED 2026-07-12** (commit `f3f25e35`, branch `fix/ki-020-ki-017-windows-portability`) — applied fix option (a): added `#undef min`/`#undef max`/`#undef abs` to `SdfRecipes.h` and `SdfBake.h`, matching the exact convention already used by `GpuTraversalMirror.h` (root-cause fix, not the `NOMINMAX`-global option (c), which stays out of scope per the wider-blast-radius caveat already on this entry). One additional wrinkle beyond the originally diagnosed shape: the actual includer of the generated kernel header is `Recipe/SdfRecipeEval.h` (reached via `SdfBake.h → Recipe/SdfRecipeEval.h → Recipe/generated/SdfCoreKernels.g.hpp`), and it pulled in the generated header **before** `SdfBake.h`'s own `#undef` block ran (that block sits after `SdfBake.h`'s `#include` list, per the existing convention's placement) — so the first rebuild attempt still failed with the identical error. Fixed by moving the guard into `Recipe/SdfRecipeEval.h` itself, immediately before its `#include "Recipe/generated/SdfCoreKernels.g.hpp"` line — the guard must precede the specific include it protects, not just live "after this header's own includes" when the header in question re-exports a further include at its very top. The generated file itself (`SdfCoreKernels.g.hpp`, confirmed genuinely generated via its own `// GENERATED from SdfCoreKernels.cs ... Do not edit` banner) was never touched, per the "fix at the includer, not the generated output" guidance already on this entry.

As due diligence per fix option (b), swept `libraries/SVO/include/` for other headers with the same missing-guard shape (bare `std::min`/`std::max`/`std::abs`/`glm::min`/`glm::max`/`glm::abs`, no `#undef` guard, included by a GTest/Vulkan-adjacent TU) and found + fixed four more: `TierAddress.h`, `TierDirection.h`, `TierMagnitude.h` (all included by `SkyProjectionNode.cpp` — a live RenderGraph node — plus their own dedicated test files), and `Recipe/SdfRecipeCodegen.h` (included by `test_procedural_recipe_render.cpp` and `test_recipe_codegen.cpp`).

**Verification:** full Windows/MSVC `build.bat all` rebuild — all 17 SVO test targets that share the `SdfRecipes.h`/`SdfBake.h` transitive include (the bulk of the original 19-target failure baseline) now compile and link. Spot-ran a representative sample: `test_soa_sdf_serialize` 11/11, `test_soa_mip_serialize` 6/6, `test_tier_ref_table` 5/5, `test_tier_crossing_construction` 5/5, `test_tier_crossing_mirror_parity` 6/6, `test_channel_format` 2/2, `test_mip_sample_bake` 5/5, `test_recipe_bake_center` 1/1, `test_recipe_boot_ingest` 5/5, `test_octree_pool` 5/5, `test_shell_octree_gpu` 9/9, `test_gpu_parity` 6/6 — all PASS. `test_shell_derive`/`test_recipe_bake`/`test_recipe_baker` compile+link and start running cleanly but were not run to completion in this session (compute-heavy SDF bake loops taking multiple minutes each); no assertion failures observed before the verification's time budget was spent elsewhere. `VIXEN.exe` and `vixen_editor.exe` both build clean with zero regressions.

**Severity:** low-medium (did not block the live app or any Windows-side production build; blocked a subset of SVO test targets from being buildable/runnable on Windows/MSVC specifically) · **Status:** RESOLVED (root-cause `#undef` guard applied at the correct include site + due-diligence sweep of the rest of `libraries/SVO/include/`)

---

### KI-026 — `test_recipe_boot_ingest.cpp` missing `<algorithm>` include for `std::is_sorted` (MSVC)

**Discovered:** 2026-07-12, while re-verifying the aggregate Windows build after the KI-020/KI-017 fix (commit `f3f25e35`) — this was the last of the original 19-target failure baseline still failing, and turned out to be an unrelated, separate MSVC-portability gap (not a `min`/`max`/`setenv` issue).

**Symptom:** `error C2039: 'is_sorted': is not a member of 'std'` / `error C3861: 'is_sorted': identifier not found` at `test_recipe_boot_ingest.cpp:69`. Compiled fine on GCC/WSL (`<algorithm>` transitively pulled in via another standard header there) but not on MSVC, which does not guarantee that transitive availability.

**Root cause:** the file used `std::is_sorted` without including `<algorithm>` directly.

**FIXED 2026-07-12** (commit `a82b8210`, branch `fix/ki-020-ki-017-windows-portability`) — added `#include <algorithm>`.

**Verification:** `test_recipe_boot_ingest` now compiles, links, and passes 5/5. Full aggregate Windows `build.bat all` rebuild: 0 failed targets (down from the 19-target pre-existing baseline, all cleared across this KI + KI-020 + KI-017).

**Severity:** low (single test target, single missing include) · **Status:** RESOLVED

---

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

---

### KI-028 — `IRenderTarget*` silently fails to populate a descriptor when routed into a descriptor-binding slot (only valid for hazard/sync slots)

**Discovered:** 2026-07-13, during Sampled Lighting Inc4 M3 (DDGI probe-update pass) — a live-syncval-only bug, invisible to compile-time checks and to an aggregate VUID-type census.

**Symptom:** a new node's storage-image `imageStore` writes silently target an unpopulated descriptor. Live validation reports `VUID-vkCmdDispatch-None-08114` ("the descriptor is being used in dispatch but has never been updated via vkUpdateDescriptorSets()") for the affected binding(s) — but ONLY if inspected at the instance/binding level; the AGGREGATE VUID-type count can look completely unchanged (same type, same overall count) since `08114` is already a pre-existing VUID type in this codebase's baseline noise (from the unrelated `test_dispatch` demo pipeline gap, KI-024) — a new instance on a NEW binding hides inside an old type's existing count unless you actually read the per-line binding/resource-name detail.

**Root cause:** `Resource::SetHandle<T>()`'s `descriptorExtractor_` capture (`CompileTimeResourceSystem.h`) only fires for types declaring a `conversion_type` typedef. `IRenderTarget` has no such typedef — only an `operator VkImageView()` conversion operator, which the descriptor-extraction machinery does not use. A `Resource` holding a raw `IRenderTarget*` gets typed `ResourceType::PassThroughStorage`; `GetDescriptorHandle()`'s type-dispatch switch has no case that lets a `PassThroughStorage` resource yield a `VkImageView`, so `GetHandle<VkImageView>()` on it always returns `VK_NULL_HANDLE`. **`IRenderTarget*` is correct and sufficient for HAZARD/SYNC-TRACKING slots** (e.g. `ImageSyncGathererNode::PreRegisterImageSlots`, `IMAGE_WRITE`/`IMAGE_WRITE_ARRAY` — these want the pointer identity for the auto-sync scheduler, not a Vulkan handle) — **but it can NEVER populate a descriptor-binding slot** (a `DescriptorResourceGathererNode` connection). The type system accepts the wiring silently; only a live GPU run with validation layers surfaces the failure.

**Fix pattern (the established, working precedent — `RenderTargetNode` already does this correctly)**: any node that owns a storage image and needs BOTH (a) hazard/sync tracking AND (b) a real descriptor binding for shader access must publish TWO separate output slots — the existing `IRenderTarget*`-typed slot for sync/hazard consumers, PLUS a dedicated `CURRENT_VIEW` (`VkImageView`) output slot (mirroring `RenderTargetNodeConfig::CURRENT_VIEW`) for descriptor consumers. Connect the `IRenderTarget*` slot into sync-hazard-tracking nodes (`ImageSyncGathererNode`, etc.) and the `CURRENT_VIEW` slot into `DescriptorResourceGathererNode` connections — never route the same `IRenderTarget*` output into both. `ProbeAtlasNode` (Inc4 M3, commit `084fe603`) applies this fix by adding its own `CURRENT_VIEW` slot alongside its existing `PROBE_ATLAS` (`IRenderTarget*`) slot.

**How to catch this class of bug going forward:** when validating ANY milestone that adds a new descriptor-bound storage-image producer, inspect live syncval output at the INSTANCE/binding level, not just the aggregate VUID-type census — a new `08114` (or similar) instance on a new binding can hide inside an unchanged overall count if that VUID type already exists in the baseline for an unrelated reason (as `08114` does here, via KI-024).

**Severity:** Medium (silent at compile time and at the aggregate-gate level; only caught by instance-level live syncval inspection — but easy to fix once identified, and the fix pattern is now established) · **Status:** RESOLVED (Inc4 M3, commit `084fe603`) — recorded here as a durable pattern for any FUTURE node needing the same shape, not because the specific M3 instance is still open.

---

### KI-029 — `probeUpdatePushConstantGatherer` never wired `instanceCount`: every probe ray was a guaranteed miss since M3 shipped

**Discovered:** 2026-07-13, during Sampled Lighting Inc4 M4's leak-test gate (`VIXEN_DDGI_LEAK_GATE_DEMO`) — the gate's own `diagNearProbeHitCount` debug readback initially read 0 for a scene the march visibly renders, isolating the gap. Retroactive to M3 (commit `f9453b76`), NOT an M4 defect.

**Symptom:** `TraceWorld`/`TraceWorldShadow` (`TraceWorld.glsl`) both bound their scene-instance iteration loop by `pc.instanceCount` (`numInstances = clamp(pc.instanceCount, 0, 3*64)`). `ProbeUpdate.comp`'s push-constant gatherer (`probeUpdatePushConstantGatherer`, `BuildRenderGraph.cpp`) never connected `BodyOctreeSceneNode::INSTANCE_COUNT` to binding 10 — so every dispatch read `instanceCount=0`, making `numInstances` clamp to 0 and every probe ray a guaranteed miss regardless of scene content, for every scene, since M3 shipped.

**Root cause:** M3's own file-header comment claimed ProbeUpdate.comp "reads NONE of" the PushConstants block's fields (true for camera ray / per-pixel debug target, since the pass is probe-indexed not screen-indexed) — but this claim missed that `TraceWorld`'s OWN internal instance-loop bound also lives in that same push-constant block, and IS read regardless of the pass's own screen-vs-probe indexing. Not caught by M3's own gate, which only checked "probes visibly light a scene" qualitatively (a render happened, with zero hits contributing zero radiance — visually indistinguishable from "dim but working" without a numeric hit-count check).

**Fix:** wire `BodyOctreeSceneNode::INSTANCE_COUNT` into `probeUpdatePushConstantGatherer` binding 10, mirroring `DirectLightingNode`'s own identical wiring (`BuildRenderGraph.cpp:3756-3757`) exactly.

**How to catch this class of bug going forward:** any NEW standalone (non-march) compute shader pulling in `SceneBindings.glsl`/`TraceWorld.glsl` needs `instanceCount` wired even if the pass's own screen/probe-indexed logic reads none of the OTHER push-constant fields — the scene-traversal helpers themselves have their own field dependencies, separate from the calling shader's own field usage. A milestone gate that only checks "did it render something" cannot distinguish "0 rays hit, 0 contribution, still renders (dimly)" from "working correctly" — a numeric hit-count/ray-cast diagnostic (as this milestone's own leak-test debug SSBO added) is needed to actually verify ray-casting correctness, not just qualitative visual presence.

**Severity:** High while undiscovered (every M3 probe-update dispatch was a silent no-op on the ray-casting side — irradiance/visibility atlases were never meaningfully populated) but trivial to fix once identified · **Status:** RESOLVED (Inc4 M4, commit `f4ce2e4e`, retroactive M3 fix).

---

### KI-030 — DDGI visibility moment poisoned by miss-sentinel depth, making the Chebyshev visibility test structurally unable to reject occlusion

**Discovered:** 2026-07-13, during Sampled Lighting Inc4 M4's leak-test gate, immediately after KI-029's fix restored real ray hits — the Chebyshev-enabled and ablation-disabled gather readbacks were nearly identical (0.1249 vs 0.1351), meaning the leak-test scene wasn't discriminating despite hits now being real. Retroactive to M3 (commit `f9453b76`), NOT an M4 defect.

**Symptom:** `chebyshevVisibility(mean, mean2, d)`'s `d <= mean` early-out (return fully-visible) was true for essentially any realistic test distance, regardless of actual nearby occluder geometry — the Chebyshev test could never reject a shading point as occluded.

**Root cause:** M3's ray loop wrote a miss-sentinel depth (`1e4`) and depth² (`1e8`) for rays that hit nothing, and mixed this sentinel into the SAME scalar depth/depth² moment average used for real hit distances (weighted by `sampleWeight`, which was 1.0 for both hits and misses). An omnidirectional probe normally has a non-trivial miss fraction (open-sky directions) — even a modest miss count (e.g. 23/64 observed) drags the mean/variance up by orders of magnitude (observed `avgDepth≈3594.6` against real nearby-occluder distances of a few units), making `d <= mean` trivially true for any test point actually worth checking.

**Fix (option (a), the one the controller selected over alternatives):** exclude ray misses entirely from the depth/depth² moment accumulation — new `sampleHit`/`sharedHitCount` tracking, hit-count-weighted averaging (`avgDepth = sharedDepth[0] / hitCountThisTick`, falling back to a (0,0) moment — "no occlusion data, don't spuriously reject" — when a probe has zero hits this tick) instead of the old `sampleWeight`-weighted (always-1.0) averaging. The IRRADIANCE accumulation's own miss handling (divide by the full `raysPerProbe`, contributing zero radiance per miss) is UNCHANGED and was already correct — this is a VISIBILITY-moment-only fix. Rationale: a ray miss means "no occluder found in that direction," a different fact from "occluder found very far away" — RTXGI's own reference treatment does not conflate the two via an arbitrary large sentinel in an omnidirectional scalar moment.

**Live-gate-verified divergence (Inc4 M4's own leak-test gate, post-fix):** Chebyshev-enabled `gatheredLuma=0.015836` (correctly low/rejecting) vs. ablation-negative-control (Chebyshev test bypassed) `gatheredLuma=0.135071` (~8.5x higher, matching the unweighted `diagNearProbeAvgRadianceLuma=0.139617` — i.e. leaking through with the mechanism disabled) — a genuine, correctly-directioned ablation result, not just "both readings differ."

**How to catch this class of bug going forward:** any moment/statistic meant to bound or reject based on distance (Chebyshev, variance shadow maps, etc.) must be checked for what a "no data" sample (a miss, an out-of-range read, etc.) contributes to the SAME accumulator used for real samples — mixing an arbitrary large/small sentinel into a shared scalar statistic can silently make the statistic's own decision boundary vacuous for realistic inputs. An ablation gate (per the recipe-epic's "vary exactly one factor" discipline, reused here) is what actually surfaces this: a milestone gate that only checks "the value changed" without checking DIRECTION and MAGNITUDE against a known-should-differ negative control would have missed this exact bug (the pre-fix Chebyshev-vs-ablation readings DID differ, just not by nearly enough, and not obviously in the wrong direction without doing the ablation comparison at all).

**Severity:** High while undiscovered (the DDGI leak-mitigation mechanism was the entire point of the milestone, and was silently non-functional) but the fix itself is small/contained · **Status:** RESOLVED (Inc4 M4, commit `54d59dc8`, retroactive M3 fix bundled with the M4 Chebyshev feature commit — see that commit's message for why it wasn't split further).

---

### KI-031 — `probe_update_push_constant_gatherer` logs a constant "Type mismatch" internal-validation line at every graph-validate, regardless of `probeGridEnabled`

**Discovered:** 2026-07-13, during Sampled Lighting Inc4 M6's live gates — a "Type mismatch" internal validation log line fires 11× at graph-validate time on every run, independent of `VIXEN_PROBE_GRID_CONFIG_ENABLED`. Present since M3 (`f9453b76`) shipped the `probeUpdatePushConstantGatherer`, unrelated to M6's own work.

**Symptom:** the log line is emitted every run (including the default no-DDGI boot path) but is NOT a Vulkan VUID, does not affect rendering, and is not a regression — it has been present, unchanged in kind, across every Inc4 milestone's own gate runs (M3 through M7).

**Root cause:** not yet investigated — flagged for whoever next touches `probeUpdatePushConstantGatherer` or the push-constant gatherer's internal type-checking path to root-cause and either fix or explain why it is expected. Deliberately NOT investigated as part of M7's docs-only close-out scope (would require RenderGraph library source changes, out of scope for a measurement+docs milestone).

**Severity:** Low (cosmetic log noise, no functional/behavioral impact observed across 4 milestones of live gating) · **Status:** OPEN, tracked, not blocking.
