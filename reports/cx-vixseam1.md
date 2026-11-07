# cx-vixseam1: slim VIXEN generated interface seam

## STATUS

LANDED NOW. The lane is complete on base `cd22c2b9` (tag `velocity-1` was already up to date). Changes are committed in the lane branch; nothing was pushed.

The generated seam now has a compact declaration header plus a validation-only layout companion for each of the 24 manifest programs. `BuildRenderGraph.cpp` consumes declaration metadata only. Host push-block mirrors are included by parity/render tests that need them. `VixenApp` now uses its own application-domain PCH instead of `Core/Headers.h`.

## BEFORE/AFTER

| witness | before | after |
|---|---:|---:|
| `BuildRenderGraph.cpp` preprocessed output (`-H -E`) | 15,461,541 bytes | 15,140,534 bytes (-321,007, -2.08%) |
| merged generated declaration headers | 491,829 bytes | 185,692 bytes (-62.2%) |
| layout companions | not present | 24 files, 26,832 bytes |
| `BuildRenderGraph.cpp.o` | 4,294,544 bytes | 4,294,544 bytes |
| include edges | 1,471 | 1,471 |

GCC 15 does not support the requested `-ftime-trace`; the queued witness used `-ftime-report` for the preprocessor and Ninja object timings. The clean baseline Ninja entry for `BuildRenderGraph.cpp.o` was 75.956 s; the final after entry was 0.034 s. `VulkanGraphApplication.cpp.o` remained unchanged in the final incremental target build. The final queued VIXEN target build completed in 34.80 s (17 incremental steps; the baseline was a clean 666-step build, so those wall times are not directly comparable).

No shader sources or runtime manifest files changed. `sdi_tool merge-variants ... --check` reports all 24 merged pairs up to date, and the layout static assertions preserve the reflected push offsets/sizes.

## LANDABLE NOW

- `SpirvInterfaceGenerator` emits compact fixed-width binding/push/member metadata and a separate host layout mirror.
- `shader_tool merge-variants` writes and checks both generated files.
- `VixenAppPch.h` owns the application PCH; broad `Core/Headers.h` is no longer the application compilation seam.
- Twelve RenderGraph tests were moved to `.layout.g.h` where they use `PushBlock`.
- The stale, unmanifested `VIXEN/generated/sdi/merged/ExposureTonemap-SDI.g.h` was removed.
- Standalone syntax checks passed for all 24 declaration headers and all 24 layout headers.
- Affected build targets linked, and the targeted CTest slice passed 52/52 in 117.80 s: all SDI merge tests, the merged-SDI drift check, six SDI parity tests, and the affected GPU render cases.

Verification commands were routed through the live queue as `cx-vixseam1`.

## CONSOLIDATION ISSUES

- proposed: Preprocessing witness cannot append output flags to compile_commands entries
- proposed: Merged SDI cleanup leaves unmanifested heavy headers

Both proposals are committed in `.spt-proposals/cx-vixseam1.jsonl`.

## STOPs

- None. Baseline configure and the clean VIXEN target build were green before semantic edits.
- CodeGraph was unavailable because this checkout has no `.codegraph/` index; no local index was created.
- The first preprocessing witness failed because the compile-commands entry already contained `-o`; the command was corrected by removing that existing output token. The first standalone sweep exposed the stale unmanifested `ExposureTonemap` header; it was removed and the manifest-scoped retry passed. These recoveries are recorded as consolidation proposals above.
