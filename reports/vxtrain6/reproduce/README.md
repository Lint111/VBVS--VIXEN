# Native VIXEN witnesses for train 3

The native checkout is `C:\vixen-native\vxtrain6` (`/mnt/c/vixen-native/vxtrain6`).
Native MSVC builds are run directly, with `VIXEN_MAX_BUILD_JOBS=4`; GPU capture and
benchmark runs use the shared GPU queue.

## Current-tip MSVC Release build and R6

From the lane root:

```bash
bash reports/vxtrain6/reproduce/native-build.sh
bash /home/liory/.local/bin/with-test-lock.sh --agent vxtrain7 --resource gpu --gpu-benchmark --label vxtrain7:native-r6 -- bash reports/vxtrain6/reproduce/native-r6.sh
```

The native build compiles `vixen_editor`, the windowed capture target, and
`vixen_lookdev_capture`. The R6 batch runs the editor capture script and then
`EditorToggleUndoCapture.GpuSubmissionsRespectDeclaredBudget` against the 2.0 ms
budget.

## Look-dev visual review

After the Linux look-dev capture test completes, compose each four-angle scene
beside C16 and its matching Track D reference, and write the per-angle visual
measurements:

```bash
python3 reports/vxtrain6/reproduce/make-lookdev-contact-sheets.py \
  build/wsl/runtime-captures/lookdev reports/vxtrain6-visual
```

The output contains one contact sheet per state and `lookdev-metrics.json` with
capture dimensions, display-referred luma p5/p50/p95, and emissive share for
each angle.

## 1080p revision matrix

For each source revision, build the capture target directly, then queue its GPU
matrix as a native benchmark. The build helper preserves its CMake preset and matrix files
under the native checkout's ignored `.tmp/vxtrain6-harness` directory so they
remain available when the source tree is detached at an older revision.

```bash
bash reports/vxtrain6/reproduce/build-matrix-revision.sh a48a42df68332e9e4fa9f58ab01ad19449b3a96f
bash /home/liory/.local/bin/with-test-lock.sh --agent vxtrain7 --resource gpu --gpu-benchmark --label vxtrain7:matrix-wave-base -- bash reports/vxtrain6/reproduce/run-native-matrix.sh a48a42df68332e9e4fa9f58ab01ad19449b3a96f wave-base
bash reports/vxtrain6/reproduce/build-matrix-revision.sh d15a7141e983abc379b3cab1b16508c15ed20b7d
bash /home/liory/.local/bin/with-test-lock.sh --agent vxtrain7 --resource gpu --gpu-benchmark --label vxtrain7:matrix-train2 -- bash reports/vxtrain6/reproduce/run-native-matrix.sh d15a7141e983abc379b3cab1b16508c15ed20b7d train2
tip_sha=$(git rev-parse HEAD)
bash reports/vxtrain6/reproduce/build-matrix-revision.sh "$tip_sha"
bash /home/liory/.local/bin/with-test-lock.sh --agent vxtrain7 --resource gpu --gpu-benchmark --label vxtrain7:matrix-tip -- bash reports/vxtrain6/reproduce/run-native-matrix.sh "$tip_sha" tip
```

Each matrix captures midday, late-afternoon, overcast, and night-service with
`default` and forced-ESVO traversal at 1920×1080. CSV, captures, and status files
are stored under `.tmp/native-matrix/<tag>/1920x1080/` in the native checkout.
