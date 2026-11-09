# Lane gaiatu1: split GaiaVoxelWorld template-heavy translation unit

## STATUS

Implementation is complete and committed as `9af0a71d`. The focused GaiaVoxelWorld
CPU witness is green, the RenderGraph CPU witness is green, and all six dzn GPU shards
completed. The full GPU witness has five unrelated editor-capture fixture failures;
the Gaia change is otherwise clean. Base
`be4dcfaad21bb41960f4b899a37c5d497c34fdb8` was merged with
`origin/wave/authoring-convergence` before the final witness (already up to date).

## CAUSE

The KFR evidence identifies `GaiaVoxelWorld.cpp` as the slowest VIXEN unit at 371 s
(rank 1/639), with 8.5 MiB preprocessed input and 35 headers. The assigned KFR
`kfrtu1` worktree is absent and no `tools/TuReportTool` is available in the KFR tree,
so the after top-N could not be produced with that tool.

The reproducible local GCC witness used `-ftime-report` in an uncached Release build.
The original `GaiaVoxelWorld.cpp` took 184.76 s of compiler time; optimization and
code generation took 175.11 s (95%), including 144.64 s in callgraph function
expansion and 28.07 s in callgraph IPA passes. Template instantiation itself was only
6.37 s (3%). This supports splitting independent instantiation groups to shorten the
parallel critical path.

`nm -S --size-sort` on the post-split entity object corroborates the source of the
cost: the largest Gaia symbols include `World::move_to_archetype` (21.4 KiB), Gaia
query execution/fetch and group-registration routines (roughly 11.6--13.6 KiB),
`CommandBuffer<ST/MT>::commit` (13.2/16.1 KiB), and the batch
`World::copy_n_inter` specialization (13.2 KiB). The object also carries repeated
per-component sparse-store helpers for `Color`, `Normal`, `Emission`,
`ChunkOrigin`, and `ChunkMetadata`.

Raw evidence:

- `/home/liory/codeman-cases/undertow/reports/gaiatu1-evidence/ftime-baseline.log`
- `/home/liory/codeman-cases/undertow/reports/gaiatu1-evidence/ftime-after-split.log`

## CHANGE

- Added private `GaiaVoxelWorldInternal.h`, keeping the complete `Impl` definition
  private and avoiding a new public Gaia header.
- Kept world initialization, position access, component checks, and existence checks
  in `GaiaVoxelWorld.cpp`.
- Moved entity creation/deletion, batch/deferred operations, and chunk parenting to
  `GaiaVoxelWorldEntities.cpp`.
- Moved spatial queries, block-cache operations, Morton lookups, and statistics to
  `GaiaVoxelWorldQueries.cpp`.
- Moved brick/chunk accessors and lookup helpers to `GaiaVoxelWorldBricks.cpp`.
- Registered the three new implementation units in the GaiaVoxelWorld CMake target.

The moved bodies preserve the existing behavior; no public header or API changed.

## MEASUREMENTS

| Witness | Before | After | Result |
|---|---:|---:|---|
| Gaia critical TU, GCC `-ftime-report` compiler time | 184.76 s | 133.50 s (`GaiaVoxelWorld.cpp`) | 27.7% lower |
| Uncached instrumented Gaia target wall time | 215.49 s | 164.15 s | 23.8% lower |
| Normal Release cold split target build | — | 144.03 s | passed |
| Incremental touch of `GaiaVoxelWorldQueries.cpp` (instrumented tree) | — | 87.53 s | passed |

After-split per-unit compiler totals were: core 133.50 s / 3336 MiB, entities
125.04 s / 3311 MiB, queries 67.39 s / 1893 MiB, and bricks 67.61 s / 1880 MiB.
The sum of compiler CPU time increases because template code is independently
optimized in several units, but the maximum parallel unit and target wall time fall.

The normal Release artifact build of `rendergraph_svo_tests` passed through the build
queue. The focused GaiaVoxelWorld ctest selection passed 98 enabled tests, with 2
intentional disabled tests and 0 failures out of 100 selected.

The queued RenderGraph witness passed all 480 CPU tests. Across the six dzn GPU XML
results, 990 tests passed, 5 failed, and 12 were intentionally skipped. The five
failures are `EditorToggleUndoCapture.ToggleUndoRedoStateTrailThroughWindowedRun`,
`ParameterEditReachesRenderPipeline`, `LayerEditReachesRenderPipeline`,
`ParameterSnapshotAndSaveReopenStayCanonical`, and
`BackButtonReachesReturnInRunningEditor`. Their diagnostics are missing or stale
`run_editor_script.log` state lines and zero-pixel capture diffs; the test source
documents that this suite is pure capture-file I/O, and none of the failures reaches
GaiaVoxelWorld behavior. The `UndoRedoRestoresRenderByteExact`, budget, all Hud, and
all other GPU cases passed. The shard XML is at
`.tmp/build-gaiatu1-uncached/split-run/gpu-shards/shard-0.xml`; the full log is at
`.tmp/build-gaiatu1-uncached/split-run/gpu-shards/shard-0.log`.

## LANDABLE NOW

The source change is landable now with the unrelated editor-capture red recorded as a
finding for the existing fixture infrastructure. The required queued run was:

`tools/run-tests-split.sh --build-dir .tmp/build-gaiatu1-uncached --label RenderGraph --agent cx-gaiatu1 --witness`

It selected 480 CPU tests and 1007 dzn GPU tests across six GPU shards. Results and
logs are under `.tmp/build-gaiatu1-uncached/split-run/`. Existing wave evidence also
records the same editor-capture family as a baseline red in
`reports/gpuyield-run3/verification-initial-red/post-wave-suite-status.json`.

## CONSOLIDATION ISSUES

- None new. Queue admission/status, redirected long-witness progress, and compiler
  cache provisioning friction encountered here are already represented by existing
  lane proposals.

## STOPs

- The KFR `TuReportTool` after top-N is unavailable because the assigned
  `/home/liory/projects/KernelFederationRenderer/.claude-worktrees/kfrtu1` worktree
  does not exist and no matching tool is present in the KFR root. GCC `-ftime-report`
  measurements are supplied instead.
- No Gaia-specific STOP. The five RenderGraph editor-capture failures are retained as
  a non-Gaia finding; their evidence is the missing scripted state lines and zero-pixel
  diffs in `shard-0.log`, not a Gaia compile or runtime diagnostic.
