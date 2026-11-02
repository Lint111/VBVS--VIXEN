# vel-053-vixen

| id | title | verdict | evidence |
|---|---|---|---|
| T-6162 | Resolve Windows PowerShell from WSL | STALE | `tools/vixen-native.sh` `find_cmd` (81efae9b) locates cmd.exe via PATH/System32; the route no longer needs powershell.exe. |
| T-6187 | Launch native build from mapped Windows cwd | STALE | `run_bat` cd's to the .bat directory and passes `.\name.bat` (81efae9b, 48479d9c); covered by test_vixen_native_launcher.sh. |
| T-6164 | Preserve native build status on Ninja stderr | STALE | The PowerShell logging wrapper is replaced by `cmd.exe /c … 2>&1 \| tee` with `PIPESTATUS[0]` (exit-code test passes, 15/15). |
| T-6163 | Refresh native Yeroket snapshot when pin advances | NEEDS-LANE | Brief: in `vixen-native.sh prepare`, if the lane's YEROKET_ROOT clone lacks the tracked pin (`_vixen_tracked_yeroket_kernel_sha`), `git fetch` it from the sibling Yeroket checkout, with a fake-git test. Not run natively here; CMake's own error already names the repository. |
| T-6206 | Hand-built C: clone for native witness | STALE-ON-BRANCH (partial) | Entry point exists (`vixen-native.sh checkout/sync/provision/build/bat`, 48479d9c, 8dc74455); lane-vel038vixen@c08cc391 adds lane-derived roots for capture helpers. Remaining unchecked: gitignored FidelityFX exes, stale subbuild dirs, WSL_EXE: needs a native run. |
| T-6207 | Stall watchdog kills last native ninja step | FIXED (8a3bf028) | `run_bat` prints a heartbeat line every 30 s (`VIXEN_NATIVE_HEARTBEAT_S`); test added, PASS under the queue. Watchdog itself untouched (no gate weakened). |
| T-2714 | Capture loses queue ownership on wrapper kill | FIXED (8a3bf028) | Each capture runs in its own session/process group (`setsid timeout -k`); TERM to the wrapper kills the group. Measured with a stub app: wrapper TERM, no surviving children. Watchdog threshold derivation is queue core, not changed. |
| T-4008 | Progress signalling for quiet captures | FIXED (8a3bf028) | 30 s `[heartbeat]` line with elapsed time and log size from `run_capture`. Stub run passes. |
| T-2901 | Isolate runtime cache for byte witnesses | FIXED (7c0e0fc8) | Opt-in `VIXEN_CAPTURE_ISOLATED_CACHE=1` sets fresh `VIXEN_CACHE_DIR`/`VIXEN_SHARED_CACHE_DIR` under the capture dir and prints them. Syntax-checked only; default stays shared cache. |
| T-5372 | Queued GPU witnesses select the WSL display | FIXED (8a3bf028, partial) | The capture runner and the CTest witness already default `DISPLAY=:0` and unset WAYLAND. A `glfwInit` failure now reports DISPLAY and the WSL fix (R796). WindowNode.cpp not compiled in this lane. |

## LANDABLE NOW
Tip 7c0e0fc8 (lane-vel053vixen). Checked: `bash -n` on both scripts, the stub capture runs above, and `test_vixen_native_launcher.sh` 15/15 through the queue. Not checked: no VIXEN build, no native Windows run.

## CONSOLIDATION ISSUES
- Items anchor on `reports/upscalestage1/reproduce/native-build.bat`, which no longer exists. The SPT links point at deleted files, so triage should re-anchor them on `tools/vixen-native.sh`.
- The capture-progress family (T-4008, T-4021, T-4319, T-4324, T-4329, T-2714) is one root cause, closed by the heartbeat. A queue-side "tail log as progress" option would cover other quiet runners. That is queue core, so it was not touched.
