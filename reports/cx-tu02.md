# cx-tu02 — VIXEN graph-builder split

Base: `44a9cd87` (`origin/lane-vixrebuild1`, merged staging write-if-changed), with
`origin/wave/authoring-convergence` merged before the final witness (already up to date).
Worktree: `lane-tu02`. CodeGraph was attempted first, but this checkout has no `.codegraph`
index; the required source inspection then used `rg`/`sed`.

## LANDABLE NOW

- Split recipe/SVO scene authoring into `BuildRecipeSvoGraph.cpp` while keeping the call at the
  original parameter phase in `BuildRenderGraph.cpp`.
- Split diagnostics, UI/presentation parameter setup, and lighting/shadow probe dispatch setup into
  `BuildRenderGraphDiagnostics.cpp`, `BuildPresentationGraph.cpp`, and
  `BuildLightingShadowGraph.cpp`.
- Added all four graph-family TUs to `VIXEN_APP_SOURCES`; the existing manifest scan therefore
  covers every new source without a second hand-maintained manifest.
- Central `BuildRenderGraph.cpp`: 10,959 lines in the audit baseline, 6,323 lines after the split.
- Committed together with the lane report and `.spt-proposals/cx-tu02.jsonl`.

## BEFORE/AFTER

The audit baseline recorded 175.208 s for the monolithic `BuildRenderGraph.cpp` compile and
14.77 MiB preprocessed. The initial queued baseline configure/build was green on this machine.
The first queued baseline log did not retain a separately timed clean full-build wall value.

After, Ninja object timings from the same Release `-j4` build directory were:

| TU | lines | preprocessed bytes | compile seconds |
| --- | ---: | ---: | ---: |
| `BuildRenderGraph.cpp` | 6,323 | 15,251,352 (14.545 MiB) | 74.104 |
| `BuildRecipeSvoGraph.cpp` | 4,481 | 13,777,025 (13.139 MiB) | 64.386 |
| `BuildRenderGraphDiagnostics.cpp` | 64 | 10,754,540 (10.256 MiB) | 16.977 |
| `BuildPresentationGraph.cpp` | 80 | 10,985,882 (10.477 MiB) | 18.121 |
| `BuildLightingShadowGraph.cpp` | 89 | 10,677,573 (10.183 MiB) | 13.227 |

The measured post-split preprocessing probe totals 61,446,372 bytes across the five graph TUs;
the central TU itself is no longer the 14.77 MiB monolith. The final queued full target build
(`cmake --build build-tu02 --target VixenApp -- -j4`) completed in 50.15 s including cached
codegen checks. GCC in this environment does not emit Clang-style `-ftime-trace` JSON, so Ninja
object timings and explicit preprocessing sizes are the available compiler evidence.
The size probe preceded only a comment relocation in the final cleanup; the final source was
rebuilt and witnessed afterward.

## WITNESS

- Final queued configure/build: `cx-tu02:final-build-after-cleanup`, exit 0.
- Node manifest regenerated during configure: 67 node types; final hash
  `57d50edab31e4f8ce51fa506be224507d155b499d360bcaf9c0ce756dc4559a5`.
- Final witness relink: `cx-tu02:final-witness-build`, exit 0.
- CPU app lifecycle witness: `cx-tu02:final-app-run-tick`, 6/6 `AppRunTick` tests passed,
  0.13 s total test time.
- GPU production RenderGraph startup/readback witness:
  `cx-tu02:final-headless-cornell`, 1/1 `HeadlessCornellGraph` test passed, 86.74 s total
  test time. Its deterministic production-graph and stable-wall-seam assertions passed.
- The requested native witness IDs (`world 10fc17f580f12ea7`, `save d0399b15c0de4b59`) are
  not literal expectations in this VIXEN checkout, so no separate ID-printing comparison was
  available in-lane; the targeted deterministic Cornell witness passed unchanged.
- `git diff --check` and `spt propose --check .spt-proposals/cx-tu02.jsonl` passed.

## STOPs

None. The queue emitted intermittent missing temporary sidecar diagnostics, but all required
source/build/runtime checks recovered and passed.

## CONSOLIDATION ISSUES (non-facade deltas this lane needed)

- proposed: Box queue status sidecar can disappear during a queued build
- proposed: Box queue diagnostic breaker reuses a fixed compile error after the source is repaired
- proposed: Queued light payloads do not reliably inherit the build worktree directory
