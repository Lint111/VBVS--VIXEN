# boundscore run 2 — renderer consumer witness

## LANDABLE NOW

Committed consumer implementation with verified kernel/focused/RenderGraph/SVO increments; the complete run-2 landing witness is not green because native PNG byte equality fails. VIXEN final tip is the commit containing this report. Assigned VIXEN base `bd8e3f2588fc20eeabdbf57b8bc6ae7d6bbd1360`; kernel semantic tip `415d752e11f69d925481b411a3e59dc4412fbdc9` (final semantic code), with documentation-only kernel tip `372d459b`. The tracked kernel pin is unchanged; configure uses the dispatched SHA override and worktree-local source/tool/cache roots.

## Coverage

The [generated renderer coverage snapshot](boundscore-run2-coverage.json) has 184 named entries (182 numeric opcodes plus existing aliases), 15 CPU/strict-SIMD and conditional Vulkan-preserve/Dozen precise value capabilities, five structural extent/occupancy capabilities, and 169 unknown entries. Unqualified GPU has zero certified entries. Conditional coverage requires device/compiler admission and valid finite operands; it is not an unconditional certificate for every input.

| Capability | Generated coverage | Conservative behavior |
|---|---|---|
| Value transfer | Sphere, Box, Union, Subtract, Intersect, Round, MathAdd, MathSub, MathMul, MathDiv, MathNegate, PositionChannel, Displacement, Select, PushParam | Other entries return the original tape; invalid domains/operands/budgets remain unpruned |
| Extent and occupancy | Sphere, Box, Union, Subtract, Intersect | Existing legacy extent/occupancy fallbacks remain separate; no additional mathematical/GPU certificate is asserted for them |
| Named noise/sampling | None admitted | NoiseDeform, NoiseSimplex3D, NoisePerlin3D, NoiseVoronoiF1, NoiseFBMPerlin3D, NoiseFBMRidged3D, NoiseFBMTurbulence3D, CurlNoise3D, VoxelLightSample remain unknown |
| Normalized octave composition | Ordered amplitude, numerator and signed normalizer recurrence over certified octave input ranges | Unknown samplers, zero normalizers, overflow and nonfinite inputs remain unknown |

## Replaced handwritten handling

- `RecipeTileSpecialization.h`: removed the double/64-epsilon interval implementation, sphere/box equations, handwritten hard-CSG winner switch and owned expression tree. It now consumes generated coverage, backend-aware facts and dependency/selection edges, then copies original instructions in postfix operand order. Unsupported/effectful programs return verbatim. Bounds-only analysis omits CSE keys and compiled strings.
- `RecipeBounds.h`: five declared geometric operations dispatch through generated structural extent transfers. Other legacy operations keep their previous fallback. Mathematical extents are distinct from GPU rounded value intervals.
- `RecipeOccupancy.h`: the same declared subset uses generated occupancy capabilities. Its generic block reduction can stop after the generated saturated-zero predicate proves the final stored value fixed. Admission checks finite generated facts, pure coverage and exact agreement with the old float-to-cell mapping. The original loop remains the unsupported-domain path and the independent unguarded test oracle.
- `RecipeWholeDomainCompaction.h`: forwards the explicit generated proof backend. GPU duplicate min/max folding now stays conservative without a non-interference certificate; declared lowered-away no-ops remain admitted because they emit no execution. Its pre-existing fixed output-channel declaration model is not extended here; full R447 output-set propagation remains open.

The real-GPU fixture chooses the native Dozen/D3D conditional profile and checks FP32, absence of RelaxedPrecision, and NoContraction in the arithmetic/primitive witness modules. Other devices remain unqualified. This is test-side admission, not a new production device/compiler contract. The new certified transfer path is generic over generated tables; no new per-op or per-output renderer rule was added. Existing uncovered fallbacks and the inherited output declaration model remain.

## Baseline and recovery

All heavy commands use the live global queue, agent `boundscore`. Fresh baseline configure/build and all generation checks passed against kernel `9981f5cf`. Focused CPU bounds/occupancy/interval/compaction: 32 passed. GPU full/pruned/unrolled and whole-domain fixture passed on Microsoft Direct3D12 (AMD Radeon(TM) 8060S Graphics), recording two immutable 65536-byte RGBA32F oracles. Seven independent editor/HUD PNGs were produced before semantic consumer edits.

First baseline red: queued `ctest --test-dir build/wsl -j1 -R '^RecipeSimdParity.AllCorpusProgramsAreBitIdenticalAcrossFourLanes$' --output-on-failure`, exit 8, `M4d_Output_IsPassthrough: recipe gradient capability mismatch: 94`; `.tmp/boundscore/run2/baseline-opcode94.log`. This matches T-1449 on the assigned base and is the sole explicitly permitted SVO red. It is not a baseline-unobtainable STOP.

Recovery A: checked documented global launcher, own CMake entry point, dependency/cache/source paths; configured and built fresh. B: ran documented recipe/kernel generators from unchanged source inputs; the baseline recipe header regeneration is identifiable separately from later generator changes. C: bounds/occupancy/interval/compaction and GPU baseline pass fresh; opcode 94 is an independently reproduced finding under the brief's explicit exception. An optional full baseline RenderGraph run was canceled at 656/1343 completed cases (wrapper exit 76, payload 141); it is inconclusive as a full-suite result. Its earlier test #677 did fail FramePipelineTest.AdjacentFramesOverlapButCommitInSubmissionOrder with maxActive=1. The first complete final run reproduced the same sole red on two affinity cores. Recovery with --credits 2 gives four affinity cores and passes that unchanged test. The final full suite repeats serially with that supported allocation; the required gate is not excluded.

Introduced build red: first final build failed because occupancy's new generated helpers lacked a direct header include. Added the explicit include and reran the fresh build; `.tmp/boundscore/run2/final-build.log` and `final-build2.log` preserve both attempts. No gate was disabled and no generated artifact was hand-edited. Final-tip configure first rejected our abbreviated SHA (exit 1); the full-SHA retry waited about six minutes behind a memory-blocked queue head, then passed with no source/generated changes. Both invocations remain in tip-configure logs.

## Final witness

All commands use `/home/liory/.local/bin/with-test-lock.sh --agent boundscore`, build/test/light resources as appropriate. CTest is serial; full renderer suites use two queue credits. Evidence is worktree-local under `.tmp/boundscore/run2/`.

| Required check | Final result | Evidence |
|---|---|---|
| Fresh current-tree build | Passed, including all 22 `*_check` targets | `fast-configure.log`, `fast-regen.log`, `fast-build.log`, `fast-build3.log` |
| Focused consumers and actual GPU containment | 38 passed, 0 failed | `fast-focused.log`, `fast-focused.xml` |
| GPU extrema/ties/NaN/infinity | 91 certified readbacks contained; 40 unknown retained | `fast-focused.log`, SPIR-V NoContraction/FP32 validator |
| rva1 / rvcompact exactness | Full/pruned/unrolled/whole outputs byte-identical; both immutable 65536-byte pre-change buffers match | `fast-focused.log`, `gpu-oracles/*.rgba32f.bin` |
| Serial full RenderGraph | 1332 passed, 0 failed, 11 skipped; 1343 total, exit 0 (347.15 s) | `fast-rendergraph.log`, `fast-rendergraph.xml` |
| Serial full SVO | 761 passed, 1 known opcode-94 failure, 8 skipped, 1 disabled; 771 total, exit 8 (77 s) | `fast-svo.log`, `fast-svo.xml` |
| Canonical 11 capture checks and native baseline bytes | 11/11 canonical checks passed after focused recovery; native before/after file comparison FAILED: 0/7 PNGs byte-identical, exit 1. Full landing witness remains red. | `fast-rendergraph.xml`, `final-captures.log/xml`, `capture-recovery.log/xml`, `capture-byte-comparison.log/json` |
| Final kernel-tip configure/build and no-op rebuild | Final full-SHA configure/build passed; all 22 contracts rechecked, zero product C++ compile/link actions. Separate no-op rebuild passed: ninja reports no work to do, exit 0. | `tip-configure-full-sha.log`, `tip-build.log`, `noop-build.log` |

Kernel current semantic tree: SourceGenerator 981 passed, CodegenTool 2081 passed/3 skipped; both fresh builds and recipe/core generator `--check` passed. Kernel native cost fixture: 15,509,741→601,099 ns; 512 oracle points and nonfinite/domain escapes bit-identical; costly calls 1024→512 and 1536→72. Kernel `reports/boundscore.md` records that scope separately.

## Exact versus sampled cells

Seven alternating median runs per fixture, dense 64³ points / 16³ cells. “Exact” means the final margined min reduction is proved exactly zero after a finite sample; it does not mean an analytically empty cell. Non-exact cells retain their complete dense sample reduction.

| Fixture | Before exact / sampled | After exact / sampled | Points before → after | Median before → after | Complete grid |
|---|---:|---:|---:|---:|---|
| Sphere | 0 / 4096 | 1160 / 2936 | 262144 → 201584 | 2.06012 → 1.39792 ms | Byte-identical |
| Box | 0 / 4096 | 760 / 3336 | 262144 → 221375 | 3.25903 → 2.29883 ms | Byte-identical |
| Union | 0 / 4096 | 832 / 3264 | 262144 → 220764 | 2.91695 → 2.57775 ms | Byte-identical |
| Subtract | 0 / 4096 | 1452 / 2644 | 262144 → 187872 | 3.83629 → 2.40478 ms | Byte-identical |

GPU tiles in both fixtures retain 64/64 exact branch-selection proofs before and after; residual field/output evaluation is sampled. The generated transfer retains the same tile instruction histogram as A1: dead terms 1:64; visible edit min/median/max 7/42/123, 45 tiles retaining at least 32 instructions. Whole-domain instruction counts remain 325→1 and 433→177, all guarded output bytes matching the unguarded oracle.

## Measured cost and limits

| Fixture | Unguarded tape GPU | Generated proof CPU | Pruned GPU | Total guarded | Unrolled GPU oracle |
|---|---:|---:|---:|---:|---:|
| Dead terms | 63.5428 ms | 14.3982 ms | 0.27708 ms | 14.6753 ms | 0.56836 ms |
| Visible edit | 84.5514 ms | 20.5693 ms | 9.459 ms | 30.0283 ms | 0.77392 ms |

The guarded totals include proof and reduce cost 76.9% and 64.5% relative to the unpruned tape. Clause reductions are 99.6923% and 88.0302%; edited pixels change visibly while guarded and unguarded outputs remain identical. This does **not** establish a win over the unrolled shader. Fresh A1 tile-proof CPU baselines were 0.537634 and 1.24709 ms: generated CPU proof cost regresses materially, and should be optimized before claiming an A1 performance improvement. Native consumers compile with `-O3 -fno-fast-math -ffp-contract=off`; the regression is not attributed to a debug build.

Whole-domain dead-terms proof costs 0.210314 ms; compilation 13.252→7.83528 ms, uploads 74700→4400 bytes, GPU 0.55768→0.02824 ms. Visible-edit proof costs 0.426237 ms; compilation 19.4992→10.0927 ms, uploads 99696→44048 bytes, GPU 0.76744→0.33076 ms. Unsupported backends/ops return the conservative source tape. GPU duplicate-identity non-interference is unproved and therefore that fold stays off; it has no claimed GPU cost reduction.

## STOPs / remaining work

STOP — required before/after capture byte identity fails: all seven matching native PNG files differ from the immutable pre-change baseline (0/7 byte-identical). The queued comparison exited 1. Exact SHA256 values and sizes are preserved in [the committed comparison manifest](boundscore-capture-byte-comparison.json); the complete diagnostic is `capture-byte-comparison.log`. Same native script/arguments and one-credit allocation were used. No baseline image was recaptured. The delta remains unclassified; file-byte differences alone do not identify the decoded pixel region or assign causality between lane changes, merged upstream changes, or runtime state. This is an unresolved required witness, not an excluded pre-existing red.

The preceding producer failure recovered: two-credit CaptureSecond failed at `vkMapMemory`, its dependent comparison lacked repeat-on bytes, and native HUD aborted during device creation with `D3D12: Removing Device; Pure virtual function called`. Fresh-process recovery at the documented native baseline one-credit allocation passed CaptureSecond, comparison and native producer (3/3, exit 0, 55.06 s). Thus all canonical 11 capture checks pass across the final RenderGraph/first capture/recovery runs, but that does not clear the stricter before/after file equality check.

Recovery A verified executable/data/cache/ICD/X11 routes and recovered producer completion using the baseline allocation. B: generators and fresh builds are green; the remaining seven unequal files are newly produced output, not missing schema/pin/generated assets. No generated artifact, cache, pin or golden expectation was hand-edited to clear red. C: focused raw GPU oracles, four dense grids, complete serial RenderGraph and canonical capture checks pass; the explicitly required native baseline equality cannot be excluded. No matching baseline evidence proves this delta pre-existing. Fresh baseline was established, so this is not a baseline-unobtainable declaration.

Checkpoint: stopped taking on work at approximately three hours, `2026-10-08 21:48:33 UTC`, immediately after the queued byte comparison failed. Remaining: decode/localize the seven PNG deltas; reproduce against the recorded baseline and the merged upstream base using fresh supported builds; repair any lane-introduced output change; rerun all required capture equality checks without replacing the baseline. Existing R434 CPU cost limitations remain as recorded above. VIXEN feature commit `b78ab519` and the paired kernel commits are reviewable candidates, not a claim that the full run is landing-ready.

REFER-TO-ORCHESTRATOR: `git fetch origin main-wave` failed exit 128: `couldn't find remote ref main-wave`; no such remote ref exists. The assigned wave base is present. Owner must publish/identify the intended wave ref, then merge it and rerun the final witness. Do not substitute origin/main.

Named sampler admission and production GPU contract admission remain open. R434 is not fully discharged as an A1 cost claim: generated tile-proof CPU time regresses, and guarded tape execution does not beat the unrolled shader oracle. The within-run unpruned-tape and occupancy cost reductions above are the narrower measured results. The prior-art report warns against guessed noise ranges; this lane installs none. Existing R447 fixed output-set handling remains open. T-1450 is not closed.

## CONSOLIDATION ISSUES (non-facade deltas this lane needed)

- proposed: VIXEN configure provisioning needs an accurate queue memory profile and isolated cache setup
- proposed: GPU recipe fixture needs worktree-local artifacts and raw baseline oracle export
- proposed: Occupancy consumer manually schedules certified coarse-cell reductions
- proposed: GPU proof-profile admission needs a shared device and shader contract
- proposed: Windowed capture producer overwrites a tracked edited sample document
- proposed: RenderGraph concurrency witness needs more than the queue's two-core allocation
- proposed: Generated duplicate identities need GPU non-interference certificates
- proposed: Kernel snapshot override requires manual full-SHA expansion
- proposed: Memory-blocked queue head can stall a fitting light witness after bypass exhaustion
- proposed: Capture witnesses need a diagnosed device-loss recovery contract
