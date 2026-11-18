# cx-tilebinorder1

## Status

Implementation complete and committed. The TileBin GPU schedule now declares the real Prepare → Scatter dependency. The generated schedule check is green against the d12row kernel. Persistent byte-for-byte visual/load comparisons remain incomplete because the live GPU queue refused the required capture admissions before payload execution.

## SHAs and scope

- VIXEN base: `4a129b60e995` (`origin/wave/authoring-convergence)); source/proposal tip used by the final native witness: `22338a92456cfde29860863a0d75933e3dfac6c7`.
- Kernel base: `f71d09ef9994b510020164e339cec3b918791e33` (`origin/main`).
- d12row kernel merge: `060e4f855eba6c570fb44a36d1ce9a1abd59108f`, containing `1782c2900).
- Kernel final lane tip: `b7d13898c2c11c572d89fcec6861a3f3c339ff29`.
- Paired Undertow tip used by preflight: `385bc65dd227afc60ef2ac3b2d1e3b00a41f6176`.
- Launcher `origin/main` tip: `bd9f8d9a1cda9d56069f68f1d534a0304d3a4d1b`.
- VIXEN kernel pin: `060e4f855eba6c570fb44a36d1ce9a1abd59108f`.

## Change

In `VIXEN/codegen/gpu-systems/TileBinningSystem.cs:111-177`:

- Added `TileBinningPhases.Prepare` and `TileBinningPhases.Scatter` dispatch scopes.
- Declared `[AfterSystem(typeof(Prepare))]` on Scatter and placed each system in its matching phase.
- Kept access declarations honest: Prepare reads bounds/view/viewDepth and writes InstanceOrder + TileRankWord; Scatter reads bounds/view/viewDepth/order and writes TileRankWord + TileBinStats.
- No suppression was added. The earlier typed resource edge was rejected by SCHED005 because TileRankWord is not a runtime-state owner; the bare order hint did not resolve the write/write conflict.
- Regenerated `VIXEN/shaders/Generated/GpuSystems.manifest.json`. It records planHash `c1a813cfed79c6886e42a68b56ceb428f9507a1ef55c75420379619ba39fbd18` and Prepare-before-Scatter barriers for InstanceOrder.* and TileRankWord.*.

## Required checks

- Baseline reproduction: queued native `gpu_systems_check` reproduced SCHED004 unordered writes for Prepare/Scatter on TileRankWord.* and SCHED001 antichain conflicts on InstanceOrder.* and TileRankWord.*. Evidence: [gpu-systems-baseline-retry.log](/home/liory/codeman-cases/undertow/reports/cx-tilebinorder1-evidence/gpu-systems-baseline-retry.log).
- Recovery: the first fresh native build failed before compilation because `build/ninja` was unconfigured; rerunning the documented `build all` path through the live queue configured the tree and reproduced the semantic red. The stale generated manifest was then regenerated through a queued light invocation.
- Final queued native `gpu_systems_check`: PASS; all targets built successfully and the GPU manifest/shader/schedule golden check passed. Evidence: [gpu-systems-check-green.log](/home/liory/codeman-cases/undertow/reports/cx-tilebinorder1-evidence/gpu-systems-check-green.log).
- Release capture target `vixen_lookdev_capture`: built successfully (708/708). Evidence: [release-capture-target.log](/home/liory/codeman-cases/undertow/reports/cx-tilebinorder1-evidence/release-capture-target.log).
- Look-dev CTest retry: 4/4 state tests passed; each state covers four angles and its internal repeat-pixel witness passed. Evidence: [lookdev-16-retry.log](/home/liory/codeman-cases/undertow/reports/cx-tilebinorder1-evidence/lookdev-16-retry.log). The queue-owned capture root was deleted after that successful run, so no external vx11 byte comparison was possible.
- 1080p load scenario target `vixen_load_scenario`: built successfully. Evidence: [load-scenario-target.log](/home/liory/codeman-cases/undertow/reports/cx-tilebinorder1-evidence/load-scenario-target.log). The scenario execution/hash capture was not admitted.
- Kernel full suite: SG `1510 passed, 0 failed, 0 skipped`; CG `3905 passed, 2 skipped, 1 pre-existing failure`. Logs: [sg.log](/home/liory/projects/Yeroket-Fantasy/.claude-worktrees/tilebinorder1/.tmp/cx-tilebinorder1-kernel-suite/kernel-test-suite/sg.log) and [cg.log](/home/liory/projects/Yeroket-Fantasy/.claude-worktrees/tilebinorder1/.tmp/cx-tilebinorder1-kernel-suite/kernel-test-suite/cg.log).
- Lane preflight: `PREFLIGHT lane-preflight-385bc65dd227 385bc65dd227afc60ef2ac3b2d1e3b00a41f6176: green all-gates`. Report: [/home/liory/codeman-cases/undertow/reports/lane-preflight-cli/20261118T090516Z-3239761/report.md](/home/liory/codeman-cases/undertow/reports/lane-preflight-cli/20261118T090516Z-3239761/report.md).

## Findings and classification

- No other GPU system tripped the new schedule check. The baseline red named only TileBinPrepareSystem and TileBinScatterSystem at `TileBinningSystem.cs:112`; the final check had no SCHED diagnostics.
- The one CG failure is outside this change: `MalformedPipeSchema_IsNotAnItemExclusion` in `SystemRegistrationFlatSignatureTests.cs:253`, with unchanged test/router inputs relative to the kernel base. It expects an `error:`/data-pipe diagnostic while the run returned the diagnostic on stderr and an empty captured error string. It is recorded as a pre-existing, out-of-scope baseline red.
- Required persistent look-dev/GlyphSdf byte comparisons and the 1080p hash comparison could not be completed. Repeated GPU admissions (`lookdev-16-persist*` and `load-1080p-hashes*`) were refused before payload start by the live queue: resident-core identity RPC timed out after 10 seconds while the health endpoint returned HTTP 200. A short queued recovery check later passed, but GPU-resource identity continued to refuse. This is an infrastructure witness gap, not a product failure; the exact successful internal look-dev run and target builds remain recorded above.

## Consolidation issues

See the committed `.spt-proposals/cx-tilebinorder1.jsonl` inbox. The proposals are listed below by title.

## CONSOLIDATION ISSUES (non-facade deltas this lane needed)

- proposed: Native lane build should configure a fresh tree before a build-only action
- proposed: Native CTest batch argument escaping must support alternation regexes
- proposed: Native capture witnesses need an explicit persistent-root workflow
- proposed: Native run-root registration must tolerate an existing PID marker
- proposed: Native lane bootstrap should pre-provision or locate the pinned toolchain before GPU builds
- proposed: Queue admission should retry resident-core identity timeouts as an infrastructure state
- proposed: Native wrapper should fail before syncing when the source worktree is dirty
- proposed: Kernel full-suite runner should deploy analyzer artifacts as part of its keyed witness
- proposed: Kernel pair provisioning should guarantee the suite planner helper

