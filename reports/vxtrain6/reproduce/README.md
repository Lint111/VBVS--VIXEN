# vxtrain6 native witnesses

The native checkout is `C:\vixen-native\vxtrain6` (`/mnt/c/vixen-native/vxtrain6`).
Native MSVC builds are run directly, with `VIXEN_MAX_BUILD_JOBS=4`; GPU capture and
benchmark runs use the shared GPU queue.

## Current-tip MSVC Release build and R6

From the lane root:

```bash
bash reports/vxtrain6/reproduce/native-build.sh
bash /home/liory/.local/bin/with-test-lock.sh --agent vxtrain6 --resource gpu --exclusive --label vxtrain6:native-r6 -- bash reports/vxtrain6/reproduce/native-r6.sh
```

The native build compiles `vixen_editor`, the windowed capture target, and
`vixen_lookdev_capture`. The R6 batch runs the editor capture script and then
`EditorToggleUndoCapture.GpuSubmissionsRespectDeclaredBudget` against the 2.0 ms
budget.

## 1080p revision matrix

For each source revision, build the capture target directly, then queue its GPU
matrix exclusively. The build helper preserves its CMake preset and matrix files
under the native checkout's ignored `.tmp/vxtrain6-harness` directory so they
remain available when the source tree is detached at an older revision.

```bash
bash reports/vxtrain6/reproduce/build-matrix-revision.sh a48a42df68332e9e4fa9f58ab01ad19449b3a96f
bash /home/liory/.local/bin/with-test-lock.sh --agent vxtrain6 --resource gpu --exclusive --label vxtrain6:matrix-wave-base -- bash reports/vxtrain6/reproduce/run-native-matrix.sh a48a42df68332e9e4fa9f58ab01ad19449b3a96f wave-base
bash reports/vxtrain6/reproduce/build-matrix-revision.sh d15a7141e983abc379b3cab1b16508c15ed20b7d
bash /home/liory/.local/bin/with-test-lock.sh --agent vxtrain6 --resource gpu --exclusive --label vxtrain6:matrix-train2 -- bash reports/vxtrain6/reproduce/run-native-matrix.sh d15a7141e983abc379b3cab1b16508c15ed20b7d train2
bash reports/vxtrain6/reproduce/build-matrix-revision.sh fd566d91
bash /home/liory/.local/bin/with-test-lock.sh --agent vxtrain6 --resource gpu --exclusive --label vxtrain6:matrix-tip -- bash reports/vxtrain6/reproduce/run-native-matrix.sh fd566d91 tip
```

Each matrix captures midday, late-afternoon, overcast, and night-service with
`default` and forced-ESVO traversal at 1920×1080. CSV, captures, and status files
are stored under `.tmp/native-matrix/<tag>/1920x1080/` in the native checkout.
