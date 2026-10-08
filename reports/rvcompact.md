# R438 — whole-domain recipe compaction

## LANDABLE NOW

- VIXEN lane: `lane-rvcompact`, based on A1 tip `e1e6f94342e55b83f4a370b80afff77646c3c25c`; implementation commit `3fdb5c56f1b1f0a898eef6f24cc9a28cfc58b149`.
- Kernel lane: unchanged at `f1ac3fb0f8aa71f98075fb33e35d035999c4f334`; no kernel source or generated schema changed.
- VIXEN is not yet merged with `origin/main-wave`: this checkout's `origin` advertises only `main`, and fetching `refs/heads/main-wave` returned “couldn't find remote ref”. The requested merge target is pending owner direction.

## Implementation

`VIXEN/libraries/SVO/include/Recipe/RecipeWholeDomainCompaction.h:39` adds a CPU rewrite that returns a separate instruction stream and carries the caller's source-recipe revision. It leaves source history untouched. Its interval pruning requires an enforced certified domain and declared dependencies for every required channel. Before duplicate simplification it runs A1's interval evaluator as a safety preflight; unsupported or invalid input keeps the conservative form. The existing unroll, field emitter, shader compiler, and upload path consume the compacted recipe in `test_recipe_declared_position_render.cpp:1160`.

`VIXEN/libraries/SVO/tests/test_recipe_interval_pruning.cpp:187` covers enforced-domain dead branches, undeclared emission, unenforced bounds, generated exact no-ops, metadata-guarded duplicate unions, invalid-primitive fallback, cut/refill retention, and a visible-corner rejection. Exact generated no-ops are identified from generated opcode metadata; no opcode-number table was added. Duplicate subtree removal is limited to identical operands and compatible winner rules. Hard subtraction is never treated as an idempotent duplicate.

## Measurements

Both A1 fixtures used the enforced domain `[-6, 6]^3`. Each source and compacted recipe went through the existing unrolled compile/upload path. Upload bytes include SPIR-V plus the shared 24-byte parameter block. GPU time is the median of three dispatches after warm-up.

| Fixture | Instructions before → after | CPU proof (ms) | Compile before → after (ms) | Upload before → after (bytes) | GPU median before → after (ms) | Differing pixel bytes |
|---|---:|---:|---:|---:|---:|---:|
| `run2-dead-terms` | 325 → 1 | 0.022392 | 12.6742 → 5.79237 | 74,436 → 4,300 | 1.26976 → 0.058368 | 0 |
| `visible-edit-heavy` | 433 → 177 | 0.091101 | 16.8571 → 10.7268 | 99,332 → 43,684 | 2.18522 → 0.969728 | 0 |

The dead-terms fixture removed 324 instructions across its far branches. The edit-heavy fixture removed its 128 explicitly offscreen tail terms (256 instructions), while retaining the 177-instruction visible construction and cut recipe. It did not simplify the earlier construction/cut history into a refill: cut plus refill is not assumed to cancel. Exact no-op and duplicate cases are covered by CPU tests and are not included in these fixture counts.

The focused GPU gate compared the full RGBA32F output, including hit depth and the fixture RGB channels, byte-for-byte for both fixtures. The fixture's other output dependencies are constant; CPU tests cover channel dependency gating. A removal of a term visible at `(1.3, 0, 0)` was rejected: source distance `-0.3`, proposed distance `0.3`, `intervalRemoved=0`.

The mixed virtual/voxel-ordered fixture is not supported by A1's recipe-only tree. A1's report records the missing ordered materialized-brick references and per-brick bounds needed to certify that stack; this lane did not fabricate a mixed-domain proof.

## Channels and revision key

The fixture explicitly declares geometry-winner and independent dependencies for the required geometry, material, provenance selection, smoothness, and emission outputs. Missing declarations are conservative blockers: in particular, emission is a separate callback in the production contract, so undeclared emission blocks global interval removal. `UndeclaredEmissionBlocksGlobalBranchRemoval` verifies that case.

The result carries the caller-supplied source revision. The fixtures use revisions 1 and 2. The current `RecipeRegistry` exposes a global generation rather than a per-recipe revision, so a production cache caller still needs a stable per-recipe revision source before wiring this derived form into registry-driven reuse.

## Witness record

- Focused compaction CPU tests: 8/8 passed after the invalid-input preflight change.
- Focused GPU fixture: passed; both source-versus-compacted comparisons had zero differing output bytes.
- Fresh target rebuild and full VIXEN build passed through the global queue before the attempted merge. The full build had no compile work and the existing no-new-mutex gate passed with its two pre-existing UNCLASSIFIED warnings in `KernelDispatch/TaskExecutor`.
- Final-tree full RenderGraph CTest passed 1,337 tests with zero failures and five skips (633.09 sec total).
- Untouched-base SVO CTest had one failure, `RecipeSimdParity.AllCorpusProgramsAreBitIdenticalAcrossFourLanes`, with `M4d_Output_IsPassthrough: recipe gradient capability mismatch: 94`. A targeted rerun on the unchanged base reproduced the same diagnostic. This is outside the compaction target; the full final-tree SVO result remains to be recorded.
- Native windowed capture runner produced seven PNGs. All seven 500×500 editor/HUD PNG pairs were byte-identical to the untouched parent, with zero differing pixels and zero channel delta.
- The 11 standard capture checks passed as part of a 13/13 CTest selection: seven editor/HUD assertions, three `HeadlessStarfieldGraph` checks, `vixen_wsl_capture_witness`, and the two fixture producer setup tests.

## Checkpoint

At the lane checkpoint (2026-10-08 12:08 UTC), the implementation, focused tests, full RenderGraph suite, and capture gates were complete and committed. Remaining: run the full final-tree SVO suite (the unchanged-base opcode-94 failure is already reproduced and the focused compaction/SVO tests passed), the no-op rebuild, and all 22 `*_check` targets; obtain the intended `main-wave` ref, merge it, and rerun the merged-tree witness. No further work was started after this checkpoint.

## Integration follow-up

The compaction API is implemented and exercised in the SVO GPU fixture; there is no runtime recipe-registry caller in this step. For the production renderer, the follow-up integration point is `KernelFederationRenderer/app/src/session_renderer.cpp`, in `SessionRenderer::BuildRenderGraph()`. This lane did not edit KFR.

## SPT DISPOSITION

- `T-1449` remains open for the reproduced opcode-94 SIMD parity failure.
- No existing task was closed. The per-recipe revision integration gap and editor-script log path mismatch were proposed for follow-up.

## CONSOLIDATION ISSUES

- proposed: RecipeRegistry should expose per-recipe revisions for derived evaluation forms
- proposed: Capture runner should place editor script logs where CTest reads them
