# cx-leafinstr3 — leaf instrumentation continuation

## STATUS

PARTIAL / STOPPED by the required default-capture pixel gate. The leaf instrumentation code is present and committed, and the merged native build, counter capture, default angle-0 capture, and direct SDI drift gate passed. The canonical four-angle capture changed three required hashes, so I did not continue with the 720p/1080p matrix, shader-clock, brickmap A/B, or BLAS measurements.

Tip: `127311ba` (`fix(shader): pass recipe bound through shadow trace`).

The required P845 merge was performed once: `99aefdbf` merged `3974cb94`. Its sole conflict was `VIXEN/shaders/TraceWorld.glsl`; I retained the leaf hooks and added P845's `bestT / worldUnitsPerInstUnit` argument at the primary call, then fixed the shadow call in `127311ba` after the first merged runtime compile exposed the stale signature.

The first CodeGraph query was attempted before source search; this checkout has no `.codegraph` index, so it returned no graph result.

## MEASURED

| Check | Result | Evidence |
|---|---|---|
| Native `vixen_lookdev_capture` build at `C:\vixen-native\leafinstr2` after the shader fix | PASS, queued label `cx-leafinstr3:native-build-shader-fix` | `.tmp/native-build-shader-fix.log`; queue log `/home/liory/.local/state/undertow/undertow-box-logs/1794218094-build-cx-leafinstr3:native-build-shader-fix.log` |
| Direct native `sdi_merged_drift_gate` | PASS, queued label `cx-leafinstr3:sdi-merged-drift-native-retry` | `.tmp/sdi-merged-drift-native-retry.log`; queue log `/home/liory/.local/state/undertow/undertow-box-logs/1794221106-build-cx-leafinstr3:sdi-merged-drift-native-retry.log` |
| 500×500 leaf counter capture | PASS; shader compile and debug drain completed | `/home/liory/codeman-cases/undertow/reports/cx-leafinstr2/counters-500-final/run.log` |
| 500×500 default capture, angle 0 | PASS: `76ce784672154c37` | `/home/liory/codeman-cases/undertow/reports/cx-leafinstr2/default-500-final/hash.txt` |
| 500×500 canonical four-angle default capture | STOP: only angle 0 matches | `/home/liory/codeman-cases/undertow/reports/cx-leafinstr2/default-500-final-all/run.log.xz` |
| 500×500 search/march/full variant smoke captures | PASS as processes; timing CSV was not emitted by this temporary runner | `/home/liory/codeman-cases/undertow/reports/cx-leafinstr2/{search-500-final,march-500-final,full-500-final}/run.log` |

The final counter drain reported 8,500,000 pixels:

```
steps       b5=429216  b6=6157434  b7=1876426  b8=34884  b9=1768  b10=272
bodies      b4=8500000
candidates  b0=8500000
path_class  b0=4963762  b1=3536238
brick_steps all zero in this non-brickmap scene
```

The required default hashes were compared as the first 16 hexadecimal characters of SHA-256 over the PNGs:

| Image | Required | Final merged tree |
|---|---:|---:|
| `midday-angle-0.png` | `76ce784672154c37` | `76ce784672154c37` |
| `midday-angle-1.png` | `f2cc7c15c693667c` | `5bf44a336dcc3e58` |
| `midday-angle-2.png` | `068b2ee553fb47ec` | `d290b51816e83ebf` |
| `midday-angle-3.png` | `0bf0f7444c1132db` | `a9e357859030fda5` |

## LANDABLE NOW

- `09f777bd`, `56a0b6f5`, `a4513455`: the gated leaf timing, histogram, shader-clock, ABI/readback instrumentation slice.
- `99aefdbf`: the required P845 merge, with the conflict resolution recorded above.
- `127311ba`: the merged-tree shader signature fix; native target build and direct SDI drift gate pass.

The complete measurement lane is not landable as an acceptance-clean change until the three changed default hashes are explained and restored or explicitly ruled on by the owner.

## REDS

- Required default-capture red: the final merged-tree four-angle run changed angles 1–3. This is an acceptance red, not classified as pre-existing because a matching final-base witness was not available in this lane. Do not attribute it to the instrumentation without a base comparison.
- The first post-P845 native build hit the 1800-second machine-wide build-lock timeout with no compile diagnostic. Retrying with `VIXEN_BUILD_LOCK_TIMEOUT=7200` succeeded; this is filed as a consolidation issue.
- The first queued CTest probe failed before selecting the target because the scoped build tree referenced missing generated CTest include/stub fragments. The direct native `sdi_merged_drift_gate` target then passed; this is filed as a consolidation issue.
- A direct WSL `cmake --build /mnt/c/vixen-native/leafinstr2/build/ninja-release --target sdi_merged_drift_gate` invocation failed because the cache was created under `c:/...` and CMake rejected the `/mnt/c/...` namespace. The supported native wrapper/path route passed; this is filed as a consolidation issue.

## STOPs

STOP: required default-capture hash gate failed for angles 1–3 on the merged final tree. Per the brief, no tuning or further measurement was performed after the canonical four-angle witness. Owner action: compare `99aefdbf`/`127311ba` against the published golden-base capture using the same four-angle fresh-process setup, determine whether the red is from P845 convergence or this lane, and restore the required hashes before continuing the matrix.

## SPT DISPOSITION

- `cx-leafinstr2` measurement brief: PARTIAL — instrumentation, native build, 500×500 counter/default/variant witnesses completed; the default pixel gate and the 720p/1080p, clock, brickmap, and BLAS measurements remain open.
- `1c62cf98655c0`, `73dfcffc8850`, `7f04e28ce0f1`: OPEN — lane-local consolidation proposals, awaiting triage.

## BENCH REQUEST

No additional quiet-window request was started after the mandatory pixel STOP. The queued 500×500 timing jobs were admitted after a pinned native drain; the remaining benchmark command, if the owner clears the pixel red, is the existing native runner through the live queue, one job per resolution/variant:

```
bash /home/liory/.local/bin/with-test-lock.sh --agent cx-leafinstr3 --resource gpu --gpu-benchmark --label 'cx-leafinstr3:<purpose>' -- bash -lc 'cd /mnt/c && /mnt/c/Windows/System32/cmd.exe /c C:\\vixen-native\\leafinstr2-run\\run-leafinstr2.bat <name> midday - - VIXEN_WINDOW_WIDTH:<w> VIXEN_WINDOW_HEIGHT:<h> VIXEN_LOOKDEV_CAPTURE_FRAMES:32'
```

## CONSOLIDATION ISSUES

- proposed: Global queue identity joined a VIXEN job to an unrelated worktree command
- proposed: Native pinned-tool provisioning cannot recover a transient GitHub Ninja download timeout
- proposed: Native configure cannot use the existing ccache when GitHub ccache download fails
- proposed: Native VIXEN build lock timeout needs a queue-aware wait
- proposed: Native CMake gate must run in the Windows path namespace
- proposed: Scoped VIXEN build must emit complete CTest include fragments
