# boundscore run 3 — native capture classification and merged-tip witness

## LANDABLE NOW

At the witness point, VIXEN lane tip was `17fef666cdb9417bd0daf0dd2998af78eafa5b22`, merged with `origin/wave/authoring-convergence` at `bd8e3f2588fc20eeabdbf57b8bc6ae7d6bbd1360`. Kernel lane tip was `372d459b2181be1f071598450c4d3ec8e8d13ef3`, merged with `origin/main` at `f8e8180950cd44a41958ba76be55cba53c51d778`. The run 3 commits add this report and evidence; they do not change product code. Both tested lane tips are therefore the parents named above.

### Native capture classification

The run 2 baseline and final configure logs both select `Release` (`.tmp/boundscore/run2/baseline-configure.log`, `final-configure.log`). Their capture logs report the same DZN ICD `/home/liory/.cache/vixen/wsl-vulkan/mesa/build/src/microsoft/vulkan/libvulkan_dzn.so`, Vulkan SDK 1.4.350.1, and device `Microsoft Direct3D12 (AMD Radeon(TM) 8060S Graphics)` (vendor `0x4098`, device `0x5510`, driver `104865800`). The current ICD SHA256 is `fd0a704995d91e02b338f2d0d161f00ce8c45b5dce8b8299c628f6d587553a00`; the historical run 2 before-capture ICD bytes were not archived, so a same-path/version log is not proof of identical runtime bytes.

Two fresh Release captures of unchanged wave source `bd8e3f2588fc20eeabdbf57b8bc6ae7d6bbd1360` were byte-identical across all seven PNGs (`1791498957-test-run3-wave-capture-a.log`, `1791499069-test-run3-wave-capture-b.log`). Both sets also match the run 2 after hashes and the current merged-tip captures. The saved edited document replay likewise reproduces all seven wave hashes (`1791502641-test-run3-capture-mutated-edited-absolute.log`). The original run 2 before manifest remains unchanged.

The original run 2 comparison was 0/7 byte-identical. The historical before images differ in decoded RGB pixels, not only PNG encoding:

| Captures | Changed pixels | Changed area | Bounding box (inclusive) | First differing 16 px tile | Maximum channel delta |
|---|---:|---:|---|---|---:|
| Editor 105/45 | 81,496 / 250,000 | 32.5984% | `[103,95,397,378]` | `[6,5]` | 178 |
| Editor 5/75 | 74,187 / 250,000 | 29.6748% | `[103,95,397,378]` | `[6,5]` | 178 |
| HUD 45/5/75 | 4,694 / 250,000 | 1.8776% | `[86,187,441,279]` | `[6,11]` | 148 |

The profiles match; the repeated wave route is stable; and the stable unguarded wave images equal the merged guarded images. The screenshots show coherent shading changes on the central editor object and HUD objects, not container-only differences. That localizes the run 2 mismatch away from a boundscore transfer/guard change on this sample. Replaying the saved 916-byte `sample_tri_layer.edited.vxd` also produced the same images, while the editor log says it loaded `sample_tri_layer.vxd` (SHA256 `76f4dd1d9aa522976222d2f5660b6d710a69b99e655c33d8cbbe16fd88ae7d1d`). The tracked `.edited.vxd` side effect is a separate capture-helper issue and did not cause these pixels.

**Classification:** the stable deterministic equality witness passes: wave A = wave B = run 2 after = current merged tip for all seven PNGs. The single historical before set is an unreproducible pixel outlier. Its exact cause remains unclassified because its executable/runtime state was not preserved; a historical DZN/runtime/cache/window-state difference is possible but not proven. No baseline file was replaced. The detailed SHA256 and pixel record is in [the run 3 comparison manifest](boundscore-run3-capture-comparison.json).

### Renderer coverage

The generated coverage has 184 named entries (182 numeric opcodes plus aliases), 15 CPU/strict-SIMD value capabilities, 15 conditional precise value capabilities per admitted Vulkan/Dozen GPU profile, five structural extent/occupancy capabilities, and 169 unknown entries. Unqualified GPU has zero certified entries. Conditional GPU facts still require the device/compiler contract and valid finite operands.

| Capability | Generated support | Conservative behavior |
|---|---|---|
| Value transfer | Sphere, Box, Union, Subtract, Intersect, Round, MathAdd, MathSub, MathMul, MathDiv, MathNegate, PositionChannel, Displacement, Select, PushParam | Other entries retain the original tape; invalid domains, operands, or proof budgets do not prune |
| Extent and occupancy | Sphere, Box, Union, Subtract, Intersect | Existing legacy fallback remains separate; no extra mathematical or GPU zero-set claim |
| Named noise/sampling | None admitted | Each sampler below remains unknown and unpruned |
| Normalized octave composition | Ordered amplitude, numerator, and signed-normalizer recurrence over certified octave inputs | Does not certify unknown samplers, zero normalizers, overflow, or nonfinite values |

Named sampler limits (per R441.4):

| Sampler | Why it remains unknown |
|---|---|
| `NoiseDeform` | Coordinate-warp/weathering path depends on deny-listed FBM behavior; no admitted portable body or finite spatial bound is tied to the analysis IR. |
| `NoiseSimplex3D` | Skewed lattice and gradient behavior has no admitted body declaration or proved machine-range contract. |
| `NoisePerlin3D` | FastFloor, permutation lookup, signed gradients, quintic fade, and nested lerps are not bound to an admitted analysis declaration; hash/index and float-boundary behavior is unproved. |
| `NoiseVoronoiF1` | Discrete cell-feature hashing and neighborhood minimum distance lack an admitted bounded-lattice declaration. |
| `NoiseFBMPerlin3D` | Each octave consumes the unknown Perlin sampler and accumulates frequency/amplitude terms; the generic recurrence cannot supply missing per-octave bounds. |
| `NoiseFBMRidged3D` | Each octave consumes the unknown base sampler and applies nonlinear ridge shaping plus normalization; zero denominator, overflow, and finite-range obligations are unproved. |
| `NoiseFBMTurbulence3D` | Each octave consumes the unknown base sampler and sums its absolute-value transform; no per-octave input bound is admitted. |
| `CurlNoise3D` | Vector derivative-field behavior needs a spatial derivative/Lipschitz and epsilon contract; scalar sample ranges do not prove it. |
| `VoxelLightSample` | Runtime voxel/light lookup depends on world data and is Yeroket-specific, outside the portable straight-line recipe declaration set. |

### Cost and output identity

Fresh merged-tip GPU fixture run: `.tmp/boundscore/run3` queue log `1791502720-test-run3-rva1-cost-focused.log`. The A1 hand-written proof CPU baselines are from `.tmp/boundscore/run2/baseline-gpu-credit2-wait.log`.

| Tile fixture | Full tape GPU | Generated proof CPU | Pruned GPU | Guarded total | Unrolled GPU oracle | A1 proof CPU |
|---|---:|---:|---:|---:|---:|---:|
| Dead terms | 63.4546 ms | 14.722 ms | 0.27844 ms | 15.0004 ms | 0.5764 ms | 0.537634 ms |
| Visible edit | 85.3322 ms | 8.8812 ms | 9.5298 ms | 18.411 ms | 0.79764 ms | 1.24709 ms |

The generic per-tile proof rebuilds interval and dependency facts for each tile, while A1 used specialized hand-written equations. That repeated analysis is the CPU cost gap; bringing it down needs profiling and reusable/batched proof plans. This run did not add another manual fast path. Generated proof CPU is about 27.4x A1 for dead terms and 7.1x for visible edit. The guarded total therefore does not beat the unrolled oracle on these timings, although clause counts fall 99.6923% and 88.0302% versus the full tape.

| Whole-domain fixture | Instructions before → after | Proof CPU | Full GPU → compacted GPU | Pixel bytes differing from unguarded oracle |
|---|---:|---:|---:|---:|
| Dead terms | 325 → 1 | 0.263197 ms | 0.55924 → 0.02832 ms | 0 |
| Visible edit | 433 → 177 | 0.405546 ms | 0.78832 → 0.34068 ms | 0 |

The focused GPU test passed full/pruned/unrolled pixel identity assertions; whole-domain compaction reports byte-identical depth and fixture RGB. The 544 visible pixels in the visible-edit tile fixture are the intentional recipe edit versus its base image; they are not a guard/oracle mismatch.

### Run 2 witness list on merged tips

All heavy build/test commands used the live global queue at `/home/liory/.local/bin/with-test-lock.sh`, agent `boundscore`; CTest runs used `--no-build -nodeReuse:false`.

| Check | Result | Evidence |
|---|---|---|
| Generator/check targets | All 23 VIXEN `*_check` targets passed. | `.tmp/boundscore/run3/check-targets.txt`, `1791499524-build-run3-vixen-check-targets.log` |
| Focused consumer list | 38/38 passed before the dependency rebuild; post-rebuild extrema/tie/unknown and full/pruned/unrolled pixel tests passed 2/2. | `1791499917-test-run3-focused-consumers-abs.log`, `1791502720-test-run3-rva1-cost-focused.log` |
| Full serial RenderGraph | 0 failures across 1,343 tests (11 skipped), after one timer assertion failed once and then passed on exact retry and full rerun. | `1791500415-test-run3-rendergraph-final-serial.log` |
| Full SVO | 761 passed, 1 known opcode-94 failure, 8 skipped, 1 disabled. Sole failure: `RecipeSimdParity.AllCorpusProgramsAreBitIdenticalAcrossFourLanes`, `M4d_Output_IsPassthrough: recipe gradient capability mismatch: 94` (T-1449). | `1791500329-test-run3-svo-fresh-serial.log` |
| Canonical capture checks | 11/11 passed; native captures match the stable wave A/B route described above. | `1791500415-test-run3-rendergraph-final-serial.log`; [comparison manifest](boundscore-run3-capture-comparison.json) |
| Rebuild/no-op | Shared FetchContent build roots triggered 507 dependency/test object rebuilds once; the follow-up queued build reported `ninja: no work to do`, exit 0. | `1791499938-build-run3-vixen-noop-build.log`, `1791499996-build-run3-vixen-noop-build2.log`; shared-cache issue was already proposed in run 2 |

The timer failure was `TimerTest.ConsecutiveDeltaTimesAreIndependent`: expected about 30 ms, observed 52.828167 ms (`1791500032-test-run3-rendergraph-fresh-serial.log`). The isolated retry (`1791500299-test-run3-timer-current-retry.log`) and full unchanged-binary RenderGraph run passed. The archived wave build has no registered tests (`1791500304-test-run3-timer-wave-baseline.log`), so no matching-base timer result exists; it is recorded as a scheduling-sensitive witness, not a proven pre-existing red. The full SVO opcode-94 result is the one documented/authorized exception; no unrelated SVO failure is excluded.

## STOPs and remaining gaps

- **STOP — historical capture provenance:** all seven run 2 before/after PNG pairs differed, and pixel diffs show real shading changes. The current Release wave A/B and merged tip are byte-identical, and the saved edited-document replay reproduces wave output. The run 2 before executable/runtime identity (including the DZN binary hash) was not preserved, so its exact cause cannot be tied to a transfer, guard, or environmental input. Keep the original baseline artifacts; do not replace them. The deterministic equality route passes, but accepting it as closure for the historical one-off requires accepting that provenance limit.
- **Known SVO red:** the single opcode-94 test failure is T-1449 and remains the documented exception; the other full SVO tests passed.
- **Cost gap:** generated proof CPU time remains above A1; per-tile fact rebuilding is the identified cost center, and reusable/batched proof plans remain future work.
- **Coverage gap:** all nine named samplers remain unknown for the reasons listed above.

No baseline-unobtainable STOP applies. The timer assertion red recovered on unchanged binaries and the full RenderGraph suite passed.

## CONSOLIDATION ISSUES (non-facade deltas this lane needed)

- proposed: Native PNG captures lack a pixel-diff tool in the lane workflow
- proposed: Focused CTest manifest paths must be resolved before test-dir changes cwd
- proposed: Windowed capture helper must resolve capture_root before changing cwd
- proposed: TimerTest consecutive delta assertion is sensitive to host scheduling
