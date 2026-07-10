# Sampled Lighting — Increment 0 Implementation Plan

**Spec:** `Sampled-Lighting-Design-2026-07.md` §4 Inc0 — Material/BRDF + light-data foundation.
**Status:** DONE — merged to main `19db1e84` (merge of `feat/sampled-lighting-inc0`, base `57c0d8f4`) 2026-07-10. All 5 milestones Opus-validated + final whole-diff review; 5 test targets + drift-guard green from fresh rebuild; both apps boot lit + exit clean. Not pushed (user's call).
**Scope:** (a) GGX+Lambert BRDF replaces Blinn-Phong in the shared lighting include; (b) lights become data (`LightingConfig` + light array via config-codegen, seeded with today's directional light); (c) VNDF sampler utility landed with CPU-mirror numeric verification (not yet consumed by shading — it is Inc3/Inc5's sampler). No shadow rays, no HitRecord split, no new passes — that is Inc1.

---

## Global Constraints

- **Build/test:** per repo build-command rules (Windows-native `.bat` via `cmd.exe /c` preferred; WSL fallback). One build of a target at a time — `cmake --build` auto-backgrounds and overlapping builds of one target race and truncate the binary (sync-build-loop gotcha). Watch long builds with a foreground poll loop, never a blind wait.
- **Live-run gate is authoritative** for anything visual (memory rule). lavapipe proves structure; the real GPU (Windows-native, D3D12/dzn is the WSL fallback) proves look/perf. Use the offscreen capture → PNG helper (landed in AppFlow Inc2b Task 3) for scripted visual gates.
- **Kernel-framework skill required** for M1 (config-codegen work): the worker must load the global `kernel-framework` skill before touching `codegen/config-schemas/` or CodegenTool — do not reverse-engineer the codegen repo.
- **Do not touch** `VixenBenchmark/shaders/*` (standalone copies, separate baseline), the `*_backup.comp` shaders, or the legacy `Materials.glsl` ID table. The live path is `shaders/BodyInstanceRayMarch.comp`; `VoxelRayMarch*.comp/.frag` and `VoxelRT*.rchit` pick up the `Lighting.glsl` upgrade for free via the shared include and must merely still compile.
- **Analyzer DLL gotcha:** `SDFNodeGenerator.dll` (and codegen DLLs generally) rebuild non-deterministically — commit only on source change, else `git checkout --` them.
- Workers do not commit/push unless their milestone instructions say so; controller handles integration per multi-worktree-sync.
- Numeric references for BRDF/sampler tests are written **from the papers' equations as scalar CPU code, independent of the GLSL** — never circular GLSL-vs-mirror-of-GLSL parity (kernel-codegen lesson).

## Milestone Map (context-manager pipeline)

| M | Task(s) | Tier | Isolation |
|---|---|---|---|
| M1 ✅ | Task 1 — `LightingConfig`/`Light` [GpuStruct] schema → generated C++/GLSL + drift-guard | Sonnet | worktree |
| M2 ✅ | Task 2 — BRDF swap in `Lighting.glsl` (+ CPU mirror & tests) | Sonnet | worktree |
| M3 ✅ | Task 3 — light data wired: UBO binding, graph wiring, hardcoded light retired | Sonnet | worktree |
| M4 ✅ | Task 4 — VNDF sampler include + CPU mirror + numeric gates | Sonnet | worktree |
| M5 | Task 5 — close-out: whole-diff review, fresh-rebuild gate re-run, docs | Opus validator | — |

M1 ∥ M2 are independent (M1 generates types; M2 changes shading math with the light still hardcoded). M3 depends on M1+M2. M4 is independent of all (lands a tested utility). M5 last.

## Progress Log

- Milestone 1 (Task 1): DONE · commits ae68d6c9..0711e991 · Opus validator APPROVED (drift-guard `lightingconfig_check` green via wsl-bridge, parity gtest 4/4, emitter re-run byte-identical, tree integrity clean) · 2026-07-10. Facts for M3: generated header at `libraries/RenderGraph/include/Generated/LightingConfig.g.h` (RenderGraph placement, validator-endorsed), GLSL at `shaders/Generated/LightingConfig.glsl`; Light=32B std430 (dir@0,kind@12,rad@16,range@28), LightingConfig=144B, lights[]@16; kernel framework emits std430 ONLY (no std140 path) — Light is 16-aligned so std140 stride coincides (hand-analysis; M3 must verify via SPIR-V reflection when the shader declares the block).
- Milestone 2 (Task 2): DONE · commits 0c66d315..e1d4cba9 · Opus validator APPROVED (mirror 9/9 fresh build; golden samples re-derived in Python to 1e-6; reference non-circular = separable-Smith vs height-correlated mirror; `VoxelRT*.rchit` confirmed NOT including Lighting.glsl so unaffected; 2 build failures = setenv/unsetenv + SdfCoreKernels.g.hpp are PRE-EXISTING at M1 tip, not M2) · 2026-07-10. **M3 byte-identity baseline = AFTER hash `fde9c268cf5f07f68588b563b908ec84bb9bd134e3bcbc913280152cac6ed8c1`** (gate-artifacts/inc0-m2-hashes.txt). Gate helper `VIXEN/temp/run_m2_capture.bat` committed (reusable, validator-endorsed). GGX delta = 61 px in specular highlight only (peak lum 139→87), rest identical. NOTE for M3/M4 live gates: two pre-existing build failures exist on this branch's baseline — build past with `ninja -k 0`; the touched test target must still compile/link/pass.
- Milestone 3 (Task 3): DONE · commits e1d4cba9..e811f69e (3 commits) · Opus validator APPROVED — **byte-identity gate PASSED, independently recomputed** by validator (rebuilt spv+editor, fresh capture, SHA256 == M2 AFTER `fde9c268…`; double-normalize CPU-prenorm-vs-GPU-renorm risk resolved empirically bit-exact). SSBO/std430 at binding 16 (free; bindings 0-5,9-15 used); `LightingConfigNode` mirrors PerFrameResources ring; new `test_lightingconfig_sdi_parity` reflects real SPIR-V (144B, lightCount@0/ambient@4/lights@16, arrayStride=32 — impl caught own arrayStride-vs-sizeInBytes bug); legacy VoxelRayMarch*.comp still use preserved 3-arg overload; EditorApplication shares the builder (not unlit); no NEW VUIDs. · 2026-07-10. **MINOR carried to M4:** `.gitignore` PNG rule is root-anchored `/gate-artifacts/*.png` but artifacts live at `VIXEN/gate-artifacts/` → PNG untracked-but-committable; M4 worker to fix rule to `VIXEN/gate-artifacts/*.png`.
- Milestone 4 (Task 4): DONE · commits e811f69e..41aa0776 (2 commits; `d9795c72` gitignore fix + `41aa0776` VNDF impl) · Opus validator APPROVED — `Sampling.glsl` spherical-cap VNDF (`sampleGGXVNDF`/`vndfPdf`/tangent helpers), standalone (no live consumer, confirmed by grep); mirror 6/6 + compile-gate 1/1 built+run fresh; reference density independent (not read from shader's vndfPdf). **Two plan deviations RULED LEGITIMATE:** (1) test-3 changed from bit-for-bit spherical-cap≡Heitz to density-equivalence — the two constructions parameterize (u1,u2) differently even at normal incidence so sample-equality is mathematically false; density-equivalence is the correct VNDF criterion. (2) weight-bound G1/G2 fix was a TEST-harness bug (unmatched fused-smithG2 vs bare-G1 gave 23× overage), sampler never implicated. gitignore fix confirmed (`git check-ignore VIXEN/gate-artifacts/inc0-m3-after.png` prints path). · 2026-07-10. ALL IMPLEMENTATION MILESTONES DONE — M5 close-out next.
- 2026-07-10 M2 first attempt (`impl-m2-brdf`) interrupted mid-milestone by an account spend-limit error (not a code failure); uncommitted partial (Brdf.glsl/test/Lighting.glsl edit) discarded, worktree reset clean to M1 tip `0711e991`. Re-dispatched fresh as `impl-m2-brdf-r2` (Sonnet).
- 2026-07-10 EXECUTION STARTED (post-brainstorm-context-manager). Worktree `/mnt/c/cpp/VBVS--VIXEN/.claude/worktrees/sampled-lighting-inc0` on branch `feat/sampled-lighting-inc0` from main `12145d60`; worktree-local `.codegraph/` initialized; destructive tier auto-passes (worktree-native trust, nothing to bless). Milestones run SEQUENTIALLY M1→M2→M3→M4→M5 (controller never parallelizes implementers). M1 implementer (Sonnet) dispatched.

## File Structure (Inc0)

```
codegen/config-schemas/LightingConfig.cs                      # NEW: [GpuStruct] Light + LightingConfig
libraries/SVO/include/Generated/LightingConfig.g.h            # GENERATED (path per OctreeConfig precedent)
shaders/Generated/LightingConfig.g.glsl                       # GENERATED (follow OctreeConfig's GLSL emit location)
shaders/Brdf.glsl                                             # NEW: GGX+Smith+Lambert eval (evalBRDF, fresnelSchlick, ggxD, smithG2)
shaders/Sampling.glsl                                         # NEW: sampleGGXVNDF (spherical-cap), vndfPdf
shaders/Lighting.glsl                                         # MODIFIED: computeLighting → Brdf.glsl-based; signature preserved
shaders/BodyInstanceRayMarch.comp                             # MODIFIED: +LightingConfig UBO binding; include order
application/main/source/graph/BuildRenderGraph.cpp            # MODIFIED: lighting UBO node wiring (M3)
libraries/RenderGraph/tests/…/test_brdf_mirror.cpp            # NEW: CPU mirror tests (Brdf)
libraries/RenderGraph/tests/…/test_vndf_mirror.cpp            # NEW: CPU mirror tests (Sampling)
libraries/SVO/tests/test_lightingconfig_parity.cpp            # NEW: drift-guard (byte-offsets, per-field — OctreeConfig pattern)
```

(Exact test-target homes: follow where `test_octree_config_sdi_parity` and existing mirror tests live; workers confirm before creating.)

---

### Task 1: `LightingConfig` / `Light` canonical schema (config-codegen)

**Goal:** lights become generated, drift-guarded data types — C++ and GLSL from one C# source.

- `codegen/config-schemas/LightingConfig.cs`: `[GpuStruct] Light { vec3 direction_or_position; uint kind; vec3 radiance; float range; }` (kind: 0=directional, 1=point — point unused until Inc3 but shaped now so the buffer layout survives) and `[GpuStruct] LightingConfig { uint lightCount; float ambientIntensity; … ; Light lights[kMaxLightsInc0]; }` with `kMaxLightsInc0 = 4` (UBO-friendly fixed array; unbounded SSBO comes with ReSTIR in Inc3).
- Follow `OctreeConfig.cs` end-to-end: CodegenTool emit → `LightingConfig.g.h` (namespace per `[GpuStruct].CppNamespace` convention) → generated GLSL include → CMake drift-guard target (runs cross-OS via the `wsl.exe` bridge, KI-015 pattern).
- Drift-guard gtest `test_lightingconfig_parity`: per-field offset assertions C++ vs generated layout (the KI-era lesson: per-field offsets, not just sizeof).
- std140/UBO caveat from the recipe epic: arrays restride under std140 — if the generated GLSL is consumed as a UBO, verify the generated layout against reflection offsets (`SpirvReflector` handles nested structs); if striding bites, bind as SSBO instead (sparse-enum/pad lessons documented in the recipe topic).

**Gate:** drift-guard target green (both OSes), parity gtest green. No consumer changes in this task.

### Task 2: BRDF swap — `Brdf.glsl` + rewritten `computeLighting`

**Goal:** one physically-based BRDF everywhere the include is used; the *only* visual delta of the whole increment.

- `shaders/Brdf.glsl`: `ggxD(NdotH, alpha)`, `smithG2(NdotV, NdotL, alpha)` (height-correlated), `fresnelSchlick(VdotH, F0)`, `evalBRDF(albedo, roughness, N, V, L) → vec3` = Lambert `albedo/π` + GGX specular with `alpha = roughness²`, dielectric `F0 = 0.04` (no metalness channel yet — noted for later).
- `Lighting.glsl`: `computeLighting(color, normal, rayDir, roughness)` keeps its exact signature (all four consumer shaders keep compiling untouched) but internally: `Lo = ambient·albedo + evalBRDF(...)·NdotL·lightRadiance` with the light still the hardcoded directional (retired in Task 3). Keep the no-roughness overload delegating with 0.5. Delete the Blinn-Phong math.
- CPU mirror `test_brdf_mirror.cpp` (gpu-shader-debug pattern): mirror `evalBRDF` 1:1 in C++; verify against an independent scalar reference implementation from the equations: (1) energy sanity — white-furnace-style directional-albedo ≤ 1 across roughness ∈ {0.05,0.3,0.5,0.8,1.0} via hemisphere quadrature; (2) reciprocity swap L↔V; (3) limits — roughness→1 approaches Lambert-dominated, roughness→0 spikes along mirror direction; (4) golden numeric samples (fixed N/V/L tuples) to 1e-5.
- Live gate: standalone `VIXEN.exe` scripted offscreen capture before/after — image *is expected to change* (spec highlights tighten/soften per roughness); gate asserts non-black, sane luminance histogram, and stores both PNGs as the visual-approval artifact. lavapipe + real GPU. All four including shaders compile (ShaderManagement compile check).

**Gate:** mirror tests green; live captures rendered and archived; no VUIDs.

### Task 3: light data wired — UBO binding + graph wiring, hardcoded light retired

**Goal:** `Lighting.glsl` reads the light from `LightingConfig` data; default content reproduces Task 2's image **byte-identically**.

- `BodyInstanceRayMarch.comp`: `#include "Generated/LightingConfig.g.glsl"`, new UBO/SSBO binding (next free binding; remember binding-9 style VUID pitfalls — let SDI reflection confirm), pass light params into `computeLighting` (signature may gain a `Light` argument here since Task 2 already landed; the legacy shaders keep the hardcoded-light overload so they still compile — the overload constructs the same default directional).
- App side (`BuildRenderGraph.cpp` + SDI `BodyInstanceRayMarchNames.h` regeneration): create/upload the `LightingConfig` buffer (host-visible UBO, updated per frame from a `Vixen::Gpu::LightingConfig` CPU struct), wire through `DescriptorResourceGathererNode` like existing bindings. Default value: `lightCount=1`, direction `normalize(1,1,-1)`, radiance matching Task 2's constants, ambient 0.3.
- Editor app path (`EditorApplication.cpp`) gets the same wiring if it assembles its own graph — verify both apps boot.
- No UI/authoring for lights in this increment (AppFlow/View-Contract hook noted for later).

**Gate:** offscreen capture **byte-identical** to Task 2's post-swap capture (the data path introduces zero visual delta — this is the correctness proof for the whole plumbing chain); both apps boot + close clean; syncval clean; drift-guard still green.

### Task 4: VNDF sampler utility (`Sampling.glsl`) + numeric verification

**Goal:** land the program's importance sampler, proven correct now so Inc3/Inc5 consume a verified primitive.

- `shaders/Sampling.glsl`: `sampleGGXVNDF(Ve, alpha, u1, u2)` — Dupuy & Benyoub 2023 **spherical-cap** formulation; `vndfPdf(Ve, Ne, alpha)`; helpers to/from tangent frame. Doc-comment the [0,1] weight bound (F·G2/G1) and its matched-Smith-G assumption.
- SPIR-V compile gate: compile a minimal test kernel including `Sampling.glsl` through `ShaderManagement::ShaderCompiler` in a gtest (runtime compile exists — no live app needed) → valid SPIR-V.
- CPU mirror `test_vndf_mirror.cpp` against an independent scalar reference: (1) weight-bound property — for 10⁴ random (view, roughness, u) tuples, `F·G2/G1 ∈ [0,1]`; (2) distribution check — histogram of sampled half-vectors vs analytic VNDF density (coarse χ² bins, not exact); (3) spherical-cap ≡ Heitz-2018 on golden samples (same distribution by construction — sample-for-sample equality given identical inputs per the papers' bijection, tolerance 1e-5); (4) visible-normal validity — sampled Ne always front-facing to Ve.
- Not consumed by any live shader yet; no visual gate. Fixed seeds only (no `rand()` in tests).

**Gate:** compile gate + all numeric gates green on both build OSes.

### Task 5: Inc0 close-out

- Whole-diff review from a fresh rebuild (reviewer re-runs: drift-guards, all four new/changed test targets, scripted captures on lavapipe, boot-run both apps).
- Byte-identity re-verified for Task 3 vs Task 2 captures from the fresh build.
- Docs: changelog entry; `Sampled-Lighting-Design-2026-07.md` §4 Inc0 marked DONE with date + gates; Known-Issues entry only if something ships imperfect (none expected).
- Real-GPU (Windows-native) capture run archived — the increment's authoritative visual before/after.

## Self-Review

- **Why keep `computeLighting`'s signature in Task 2 and change it only in Task 3?** So the visual change (BRDF) and the plumbing change (light data) land in different milestones with different gate types — look-change gate vs byte-identical gate. Conflating them makes regressions unattributable.
- **Why fixed-size UBO array, not SSBO?** Inc0 needs exactly one light; UBO is the smallest correct step. The std140 restride caveat is called out; if reflection shows striding pain, Task 1 flips to SSBO — both are one-line policy in the schema consumer, and Inc3 moves to SSBO regardless.
- **Why land the VNDF sampler with no consumer?** It is the highest-risk *math* in the program and the cheapest thing to verify in isolation (pure function, CPU mirror). Landing it verified now means Inc3/Inc5 integrate a known-good primitive instead of debugging sampling and reservoirs simultaneously.
- **Known deliberate gaps:** no metalness (F0=0.04 fixed), no shadow rays (world still doesn't occlude its light — Inc1's headline), point lights shaped but unused, no light-authoring UI.

## Execution Handoff

Ready for `post-brainstorm-context-manager`: milestones map 1:1 to the table above (M1∥M2 → M3 → M4 → M5), Sonnet workers in isolated worktrees with the in-tree destructive tier pre-blessed at setup (per the permission-blessing memory rule), Opus validation at M5 plus per-milestone gate checks. Workers touching codegen load the `kernel-framework` skill; workers running builds follow the poll-loop watching rule.
