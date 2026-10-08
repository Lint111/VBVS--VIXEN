# rvwave — b18 evaluator and R438 compaction on the VIXEN wave

## LANDABLE NOW

- Branch: `lane-rvwave`. Feature tip before this report commit: `6f864cf3bc4a90f0bbdd50043e9d4ce9f1897769`.
- Wave base: `75a207883ea61132a8f3b9c63485900b80befd2a` (`origin/wave/authoring-convergence`). This VIXEN clone has no `origin/main-wave` ref; the advertised wave ref was used.
- Kernel pin: `77574f29728c40da71365d9e3c8125acf6a506ed`.
- Task commits:
  - `0f9a4848` — `build(vixen): track kernel b18 evaluator pin`
  - `fd1a8792` — `Merge lane-rva1: interval-aware SDF recipe tape evaluator`
  - `6f864cf3` — `Merge lane-rvcompact: conservative whole-domain recipe compaction`
- The kernel checkout remained read-only. The merged VIXEN tree preserves the wave's look-dev, framesync, and editor-document work, plus both SDF recipe lanes.

## Scope and baseline

The required verification scope is the pinned kernel evaluator API, VIXEN's recipe code generation and generated evaluator, the rva1 interval-pruning GPU consumer, rvcompact's CPU/GPU compaction paths, RenderGraph CTest and capture fixtures, SVO CTest, and a no-op rebuild.

Before semantic edits, the unchanged wave at `75a207883ea61132a8f3b9c63485900b80befd2a` had a fresh WSL build pass (1,145 Ninja steps) and the 15-entry capture CTest selection pass. Its SVO suite had one failure: `RecipeSimdParity.AllCorpusProgramsAreBitIdenticalAcrossFourLanes`, with `M4d_Output_IsPassthrough: recipe gradient capability mismatch: 94`. This is the existing T-1449 issue and is outside the evaluator and compaction changes.

CodeGraph and the VIXEN helper reported no available index, so navigation used `rg`. Generation was run after each merge; `recipe_simd_regen` reported no generated-file changes.

## Merged-tree witness

- Fresh full build: `nice -n 10 cmake --build build/wsl --parallel 4` passed. The final build log contains 22 `[codegen] golden check` entries; all 22 `*_check` targets passed. The generated recipe SIMD, CPU/GLSL dispatch, and GPU tape evaluator match the merged declarations. See `build/final-build.log`.
- Focused recipe tests passed 13/13 in `build/final-recipe-focus.log`. This includes rva1's consumer `DeclaredPositionRenderTest.IntervalPrunedTileTapesMatchFullAndUnrolledGpuPixels` and the interval-pruning and whole-domain-compaction CPU tests.
- rvcompact's merged-tree GPU check reported zero differing output bytes in both fixtures:

  | Fixture | Instructions before → after | Source GPU median | Compacted GPU median | Differing pixel bytes |
  |---|---:|---:|---:|---:|
  | Dead terms | 325 → 1 | 0.55380 ms | 0.02804 ms | 0 |
  | Visible edit-heavy | 433 → 177 | 0.15868 ms | 0.06860 ms | 0 |

- Full RenderGraph CTest passed serially: 1,332 passed, zero failures, 11 skipped (1,343 listed); total time 274.07 seconds. Log: `build/final-rendergraph-serial.log`.
- The initial `--parallel 4` RenderGraph run had 13 failures: two body raymarch tests lost the DZN Vulkan device, both capture producers segfaulted, and nine dependent capture assertions did not run. All four underlying failures passed in isolation (`build/rendergraph-isolation.log`); the full serial suite then passed. The two device-loss diagnostics included SPIR-V 1.6 rejected for the Vulkan 1.2 target. This exposes a shared-DZN concurrency constraint and is filed as a consolidation proposal.
- Capture fixtures produced 20 PNGs. Comparing the merged-tree files against the unchanged-wave snapshot with `tools/compare-capture-pixels.py --require-byte-identical` found all 20 byte-identical, with zero pixel or channel differences. The 11 standard capture assertions passed as part of RenderGraph CTest. Snapshots: `build/wave-captures-before/` and `build/wave-captures-after/`.
- Full SVO CTest: 766 listed, 756 passed, 1 failed, 8 skipped, and 1 disabled. The only failure is the baseline T-1449 case above, with the same opcode-94 diagnostic; log: `build/final-svo-ctest.log`.
- Final no-op rebuild passed: `nice -n 10 cmake --build build/wsl --parallel 4`; Ninja reported `no work to do`. Log: `build/final-noop-build.log`.

## Recovery record

- First required red was captured before semantic edits at base `75a207883ea61132a8f3b9c63485900b80befd2a`: `ctest --test-dir build/wsl --output-on-failure --parallel 4 -L SVO` exited 8 (`build/wave-svo-ctest.log`). Its only failure was T-1449 with the opcode-94 diagnostic above. The configured build and queued test invocation were valid; invocation/provisioning recovery was not applicable.
- Regeneration recovery ran after both merges: `cmake --build build/wsl --parallel 4 --target recipe_simd_regen` completed without generated changes. The merged full build then passed all 22 codegen checks. Regeneration was fresh and did not change the pre-existing T-1449 contract.
- The unchanged-base full SVO result establishes that T-1449 predates this lane. The task explicitly permits this one failure; all focused recipe checks passed, and no other SVO case failed.
- A separate merged-tree RenderGraph recovery was required. `ctest --test-dir build/wsl --output-on-failure --parallel 4 -L RenderGraph` exited 8 (`build/final-rendergraph-ctest.log`): two raymarch GPU tests reported DZN device loss and invalid SPIR-V 1.6 for the Vulkan 1.2 target, both capture producers segfaulted, and nine dependent capture assertions did not run. The exact four failing producers/raymarch tests passed in isolation with `ctest --verbose --output-on-failure --parallel 1 -R '^(vixen_editor_capture_producer|vixen_hud_capture_producer|BodyInstanceRayMarchRenderTest.SkipMaskExcludesOnlyTargetedInstance|BodyInstanceRayMarchRenderTest.HitRecordCompositingRealShaderBothOrderingsMatchOracle)$'` (4/4, `build/rendergraph-isolation.log`). The complete serial CTest then passed 1,343/1,343.

## STOPs

- Allowed known red only: T-1449 (`RecipeSimdParity.AllCorpusProgramsAreBitIdenticalAcrossFourLanes`, opcode 94), reproduced on the unchanged wave before implementation. No other required check remains red.

## SPT disposition

- `T-1449`: remains open for the opcode-94 recipe gradient capability mismatch.
- `T-1450`: remains open for the R328 interval-arithmetic follow-on.
- No existing SPT task was closed.

## CONSOLIDATION ISSUES (non-facade deltas this lane needed)

- proposed: Editor capture saves must stay in the build tree
- proposed: Serialize DZN GPU capture and raymarch CTest fixtures
