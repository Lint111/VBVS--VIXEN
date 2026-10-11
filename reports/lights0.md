# Lane lights0: count-agnostic lighting, slice S0 (R502, R502.1, R502.2)

Emitter data only. Nothing renders from it yet: no GPU binding, no shading change.

## LANDABLE NOW

All commits are self-contained and green on `lane-lights0` (base `origin/wave/authoring-convergence`, merge-base `a31fd077`).

| SHA | Commit | Checks |
|---|---|---|
| `14a6be11` | sparse emitter power lane in the mip bake | `test_emitter_power` (9 tests incl. Σ leaf power == root power at n=16/32/64) |
| `06b1dfaf` | `EmitterSample`/`EmitterTotals`/`RecipeEmitterEntry`/`SparseEmitterEntry` `[GpuStruct]` records | 4 new check/regen pairs in `codegen/CMakeLists.txt`; all `_check` targets green |
| `efe4fd1b` | recipe/sparse emitter table, `EmitterSampling.glsl`, CPU mirror, seeded replay | `test_emitter_sampling` (11 tests, replay pinned `0x80e03dc3f6724b73`); GLSL compiles with glslang |
| `d4aecf25` | `BodyOctreeSceneNode` builds the emitter table from instances and recipe shapes | full WSL build; static_assert CPU/generated record parity |
| `032c4580` | lane bake-time / memory cost report test | `test_emitter_power` |

Gate results:

- Mirror and replay tests: pass, WSL (GCC) and native Windows (MSVC, same pinned replay hash).
- Frames byte-identical to the base: 7/7 captures `cmp`-identical between a fresh `a31fd077` build and the lights0 build.
- Generation checks: 26 green (22 pre-existing + 4 new). The build has 27 `_check` targets; the extra is `no_new_mutex_check`.
- `sdi_tool --check`: not applicable. No shader interface changed (`EmitterSampling.glsl` is not yet included by any pipeline) and the tool has no `--check` mode.
- Dozen GPU correctness tests: pass (correctness only).
- Native Windows: configure, `test_emitter_power`, `test_emitter_sampling` and `vixen_editor` build and the two test executables pass in `C:\vixen-native\lights0` (see "Native build caveat").
- `Program.Legacy.cs` delta: 0 lines. New `CodegenFieldConfig(`: 0. Yeroket kernel pin `c3fb3847979e3f9eef04783510c42b70d7058ae7` untouched (no kernel generator changed).
- `UndertowCodegenOutputs` lives in the Undertow repo, not VIXEN. The new `.g.h`/`.glsl` outputs are registered here by CMake check/regen pairs only. The Undertow-side registration and `tools/check-wizard-incremental-contract.py` run still need doing there.

### Native build caveat

The wave base does not build natively with MSVC on its own, independent of this lane. Both failures are already fixed in lane-gpuyield:

1. `RecipeSimd.g.hpp(2831)` C4457 treated as an error under SVO's `/W4 /WX`. gpuyield makes `include/Recipe/generated` a SYSTEM include and sets `_ENABLE_EXTENDED_ALIGNED_STORAGE`.
2. `AppFlowChannel.cpp(77)` `std::max` collides with the `max` macro. gpuyield uses `(std::max)`.

I applied the same two fixes in the native checkout only, as uncommitted edits, to verify my code. They are not in the lane commits, to avoid conflicting with gpuyield. Landing gpuyield first makes the native build of this lane pass with no further change.

## Design as built

Power model: Φ = π·Y(radiance)·A, Y = Rec.709 luminance.

- Recipe instance: radiance = `color × recipeParams[3]`. A_inst = A_recipe·s² with A_recipe = N_surfaceCells·cellSize², or the bound sphere 4πr² when the recipe has no occupancy grid (flag bit 1).
- Stored instance: Φ_inst = π·Φ_root·(10·s)², where s = cbrt(|det3(localToWorld)|).
- Sparse lane (appended to the mip stream, whole `MipSample` units): `[0]` leafCount, `[1]` root power, `[2]` nodeCount, then Φ per node, then ascending emissive-leaf node ordinals, then 8 octant sums per leaf. Non-emissive trees bake an empty lane, so their bytes are unchanged. Far-field value/coverage samples are untouched (tested byte-for-byte). `BakeArtifactCache` format bumped to 3.
- Mip bake is a standard step of every stored-body bake. The streaming re-bake is verified: `DeriveShellPool` passes the lane through unchanged (tested), SDF-only edits do not change emission or occupancy, and `BulkMaterialization` bakes the lane per page and hashes it in `canonicalHash`.
- Sampler (`EmitterSampling.h` / `EmitterSampling.glsl`): fresh RNG draw per descent level; key = `[1:0]` pipeline | `[9:2]` instIdx | `[31:10]` origin cell or leaf-ordinal<<3|octant. `emitterPHat = Y(radiance)·cosR·cosE/dist²`.

### Cost of the power lane (native Windows, MSVC Release, min of 15 runs, test `LaneBakeTimeAndMemoryReport`)

| fixture | nodes | far-field mip | lane bytes | lane / mip | lane bake | full mip bake (incl. lane) | share |
|---|---:|---:|---:|---:|---:|---:|---:|
| emissive 64³ | 577 | 18,464 B | 12,472 B | 67.6 % | 0.39 ms | 4.25 ms | 9.2 % |
| emissive 128³ | 2,927 | 93,664 B | 58,664 B | 62.6 % | 2.01 ms | 22.4 ms | 8.9 % |
| dark 64³ | 577 | 18,464 B | 0 | 0 | 0.34 ms | 4.22 ms | 8.0 % |
| dark 128³ | 2,927 | 93,664 B | 0 | 0 | 1.76 ms | 22.2 ms | 8.0 % |

About 20 to 22 B per node (4 B Φ + a few B per leaf amortised), against 5 MB of brick data at 128³ (about 1.2 % of the brick bytes). A dark tree that still has a `SEM_EMISSION` channel pays the ~8 % scan to discover it is empty; an early-out on a per-tree "any emission" flag would remove that if it ever matters. WSL (GCC Release) numbers are within about 25 % (mip bake slightly faster there). These are not Dozen numbers.

## STOPs

Design choices the rulings do not settle. Each has a default that I built, so nothing is blocked.

1. **Recipe surface area over-counts a sphere by 2.85×.** The surface-cell count × cellSize² gives k_shape ≈ 0.35 against the design's ≈ 1, so the recipe pdf is about 2.85× too small and f/pdf is biased. Options: (a) coarea-formula area estimate from jittered recipe evaluations at registration, (b) calibrate k_shape per recipe, (c) accept the relaxation as the design states. Recommend (a). Built: the plain cell count.
2. **Scalar power is Rec.709 luminance, not |radiance|.** One-line switch (`EmitterLuminance` / `emitterLuminance`). Recommend keeping luminance, since it matches what the eye and the exposure meter weigh.
3. **Sparse emitter colour tint is deferred** (grey radiance Φ_oct/(3·side²), Q3a). S1 needs a colour lane or a per-leaf average if tinted emitters matter.
4. **Sparse cluster area assumes coverage 1** (sphere of area 3·side²).
5. **Fresh RNG draw per descent level** rather than the design's single rescaled draw. Same distribution, simpler replay; costs a few extra hashes.
6. **Recipe-local space = instance-local space** is assumed for the recipe grid and normal evaluation.
7. **No GPU binding in S0.** S1 must add the SSBOs and define the ten consumer hooks.
8. **GLSL is verified by a glslang compile check plus the CPU mirror tests only**, not a GPU parity run. The stub used for the check is not committed.
9. **Pre-existing bug, probable:** `readMipSample` in `MipFallback.glsl` indexes `mipPoolBase` in floats (`*2u`), while the CPU `mipPoolBase` counts `MipSample` (8 B) units. Likely a multi-octree far-field read error. Not touched here.
10. **Manual concat sites** in `BuildRenderGraph.cpp` (~3565, 4433, 4691, 5131, 5548) do not carry the lane, so `emitterPowerPoolBaked` stays 0 there. Harmless in S0.

## Verification notes

- The replay test pins keys and pdf only; positions are excluded for libm portability.
- Leaf-slot descriptors of brick-backed leaves are not parents. `BakeEmitterPowerLane` marks them so interior sums are not inflated. This was an early bug, now covered by Σ leaf == root and brute-force tests.
- `rngNextFloat` can return exactly 1.0; `emitterNext` caps it at 0.99999994.
- `docs/in-progress.md` untouched.

## SPT DISPOSITION

Three proposals filed in `.spt-proposals/lane-lights0.jsonl`:

- design-question: sphere recipe surface area over-count (STOP 1).
- audit: `readMipSample` base-unit mismatch (STOP 9).
- conversion: one shared concat helper that carries the emitter lane (STOP 10).

## CONSOLIDATION ISSUES

- proposed: Sphere recipe surface area over-counts 2.85x from surface-cell count; emitter pdf biased
- proposed: MipFallback.glsl readMipSample indexes mipPoolBase in floats but CPU mipPoolBase is in MipSample (8 B) units
- proposed: Manual BodyInstance concat sites in BuildRenderGraph do not carry the emitter power lane
- not proposed (gpuyield already fixes it): the wave base fails the native MSVC build on `RecipeSimd.g.hpp` C4457 and `AppFlowChannel.cpp` `std::max`.
