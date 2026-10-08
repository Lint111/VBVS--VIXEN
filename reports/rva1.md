# Lane rva1 — recipe visibility increment A1

## LANDABLE NOW

- VIXEN base/tip before the lane commit: `92804a8f67c48653514c042a8f7b87b70bbb642b`.
- Kernel tip: `8d68e9839ac3af02b937b2b419c27f829e90cccc` (unchanged; no kernel source edits).
- The only completed VIXEN source-tree change so far is the documented regeneration of five stale merged-SDI headers. The recipe visibility implementation is not started.

## Scope status

The lane guide assigns both the VIXEN and kernel worktrees, but the dispatch fact says all file work must stay in VIXEN. The required interval extensions must come from the kernel CodegenTool declarations that also drive evaluator dispatch; implementing them correctly therefore requires a kernel source change. The cited Undertow design object `01b83e6d2:reports/recipevis.md` is absent from the local Undertow object database, and the documented lookup did not find a copy. I am waiting for the owner to resolve these two inputs before editing feature code.

## Changes made

The documented `sdi_tool merge-variants shaders/sdi-variants.json` regeneration refreshed `LightingConfigSSBO` metadata in:

- `VIXEN/generated/sdi/merged/BodyInstanceRayMarch-SDI.g.h:696-706`
- `VIXEN/generated/sdi/merged/DirectLighting-SDI.g.h:676-686`
- `VIXEN/generated/sdi/merged/HitAccumCellShade-SDI.g.h:696-706`
- `VIXEN/generated/sdi/merged/ShadowVisibilityWave-SDI.g.h:676-686`
- `VIXEN/generated/sdi/merged/SpatialReuseShade-SDI.g.h:36-46`

Each output now reports the canonical size `208` and layout hash `0x21ad6abbc3ea8eae`; each was stale at size `144` with hash `0x6dc24fcf8fba6cee`. Only those five generated files changed in the regeneration.

## Baseline recovery record

All reds below were captured against VIXEN base `92804a8f67c48653514c042a8f7b87b70bbb642b`, before any recipe-visibility source edits.

| First red | Recovery | Result |
|---|---|---|
| `cmake --preset vixen-wsl` exited 1 because kernel pin `8d68e9839ac3af02b937b2b419c27f829e90cccc` was looked up in the auto-discovered `/home/liory/Github/Yeroket-Fantasy`, where that object was absent. Diagnostic: `.tmp/rva1/vixen-configure-baseline.log`. | Re-ran the documented preset with `VIXEN_FETCHCONTENT_CACHE="$PWD/.tmp/fetch-wsl"` and `-DYEROKET_ROOT=/home/liory/projects/Yeroket-Fantasy`. | Configure passed; log `.tmp/rva1/vixen-configure-retry.log`. Existing proposals already cover the kernel-root discovery gap; no duplicate was filed. |
| Initial full CTest exposed stale merged-SDI metadata in `sdi_merged_drift_check` (and the base opcode 94 failure). | Ran the documented `build/wsl/bin/sdi_tool merge-variants shaders/sdi-variants.json`, then its `--check`, then rebuilt. | Regeneration updated only the five headers listed above; drift check and incremental build passed. Logs: `.tmp/rva1/sdi-merged-regen-baseline.log`, `.tmp/rva1/sdi-merged-check-baseline.log`, and `.tmp/rva1/vixen-build-sdi-recovery.log`. |
| Kernel CodegenTool tests without `UNDERTOW_ROOT` exited 1: 9 failures, 2,066 passed, 4 skipped. The assertions report `UNDERTOW_ROOT is required to locate the production migration`. Queue log: `/home/liory/.local/state/undertow/undertow-box-logs/1791413932-test-rva1:kernel-test-baseline.log`. | Re-ran the suite with `UNDERTOW_ROOT=/home/liory/projects/undertow`. | 2,077 passed, 2 skipped; log `.tmp/rva1/codegen-tests-undertow-root.log`. An SPT proposal records this manual environment requirement. |
| Recovered full CTest on unchanged base exits 8 on `RecipeSimdParity.AllCorpusProgramsAreBitIdenticalAcrossFourLanes`: `M4d_Output_IsPassthrough` gradient capability mismatch, opcode 94. | Compared against the known base issue `T-1449`; retained this as the only full-suite red and ran the supported fresh build and generator checks. | Recorded as a pre-existing scoped baseline red, not attributed to this lane. Log `.tmp/rva1/vixen-ctest-recovery.log`. |

## Baseline and witness numbers

| Check | Result |
|---|---:|
| VIXEN fresh configure | Passed with explicit `-DYEROKET_ROOT=/home/liory/projects/Yeroket-Fantasy` |
| VIXEN full fresh build | Passed, 1,141/1,141 steps, queued, `--parallel 3` |
| Incremental build after SDI regeneration | Passed, 12/12 steps |
| SDI merged drift check | Passed after documented regeneration |
| Kernel SourceGenerator suite | 966 passed |
| Kernel CodegenTool suite | 2,077 passed, 2 skipped after setting `UNDERTOW_ROOT=/home/liory/projects/undertow`; first run was 2,066 passed, 4 skipped, 9 failed |
| Direct RecipeSimd CodegenTool `--check` | Passed |
| Full VIXEN CTest | Exit 8; one failure among 2,871 tests, the known `T-1449` opcode 94 gradient-capability mismatch (`M4d_Output_IsPassthrough`) |
| Full-suite headless and CelShading captures | Passed in the full CTest run |
| Captures with caller display/Vulkan variables unset | Still queued at report time; no pixel comparison result yet |
| Existing capture byte comparison | Not run |
| Pruning-off/on identical-pixel gate | Not run; no implementation |
| Recipe clause reduction, proof work, CPU specialization time, upload bytes, GPU time | Not measured; no implementation |

The full-suite CTest output reports `99% tests passed, 1 tests failed out of 2871` and 540.79 seconds total. The separate environment-clean capture run remains queued in the global box queue; its watcher has reported eligibility but no admission for more than ten minutes.

## Shared files touched

No files shared with lanes `editordocument` or `insttransform2` were touched.

## SPT DISPOSITION

- `T-1449`: known opcode 94 recipe parity failure remains present on the recorded base.
- `T-1450`: keep the R328 interval-arithmetic follow-on open; this lane has not implemented or closed it.
- New incidental tooling proposals are listed by title in the final section below.

## STOPs

The A1 feature work is awaiting resolution of the worktree-scope conflict and the missing design reference. This is not a baseline-unobtainable STOP: the fresh VIXEN build and relevant generator checks passed, and the remaining full-suite failure is the known `T-1449` case. Pixel equivalence and clause reduction are unmeasured because no feature implementation exists.

## CONSOLIDATION ISSUES

- proposed: CodegenTool tests should discover the Undertow root
- proposed: Standard VIXEN build should regenerate merged SDI before drift checks
