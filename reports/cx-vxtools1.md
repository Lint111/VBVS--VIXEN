# cx-vxtools1

## STATUS

COMPLETE WITH FINDINGS. The tooling-only fast-forward is ready for the controller. No product C++ or shader files were imported. The requested native build, affected native/WSL CPU witnesses, no-window checks, and cancellation process-tree witness passed. Windows per-job CPU attribution was not fully observable for the native CTest phase; that gap is recorded as an SPT consolidation issue rather than claimed green.

## SHAs

- Base and merged wave: `c012413c3a9bbfae26d715e79ccda6e410d29487` (`origin/wave/authoring-convergence`), re-checked immediately before final validation.
- Report/proposal checkpoint commit: `5b473568c2c4b42ea2da7fbd67b7ad93b4ac6c09`; the final lane tip is stated in the controller handoff after this metadata correction.
- Imported source commits:
  - `98a384ef3` from `lane-vxwidth1` -> local `cb315bee8`: queue-grant-driven native width and tests.
  - `0e2a5fc90` from `lane-nodialog1` -> local `191dadee9`: preserve CTest arguments through the native batch boundary.
  - `455b681e1` from `lane-nodialog1` -> local `6c5f2648c`: forward the selected native build tree through `WSLENV`.
  - `441f9eea9` from `lane-leakroot1v` -> local `34cdc5928`: reap the heartbeat sleep child on native cancellation.
  - `2a604b13a` from `lane-nodialog1` was mixed: only its tooling hunks were imported in local `7355d9da0` (CPU batch, queue-width GPU batch, and split-runner width). Its `HeadlessRuntimePolicy.cpp` product hunk was excluded.
  - local `445594c97` removes the foreign source-lane proposal inboxes so this lane owns only `.spt-proposals/cx-vxtools1.jsonl`.
- Deliberately excluded: proposal-only `48870ffaf`, `3945b2bbe`, and `e9a4abc71`; product C++ commits `bf3171ba6` and `f100e6c42`; all shader changes.

## ROOT CAUSE / CHANGE

The native launcher had to be made queue-native at both boundaries: build width now rereads the live grant and gives Ninja the queue semaphore, while native CTest batches receive safe argument transport, the selected build tree, and queue-derived parallelism. The native heartbeat now reaps its child sleep process, and the Windows process path remains rooted in `box-winjob.exe`.

Key locations:

- `tools/vixen-native.sh:238-242, 304-317, 348-375` — queue grant/jobserver width, `WSLENV` forwarding, heartbeat cleanup, and cancellation handling.
- `tools/run-native-cpu-suite.bat:7-39` — CPU CTest selection and queue width.
- `tools/run-native-gpu-suite.bat:8-45` and `tools/run-tests-split.sh:323-326` — selected tree, safe arguments, and queue-derived CTest width.
- `VIXEN/scripts/build/run_build_with_summary.ps1` and `tools/run-vixen-windowed-captures.sh` — native progress/child lifecycle fixes.

## WITNESS

All build/test/light commands below used `/home/liory/.local/bin/with-test-lock.sh` with agent `cx-vxtools1`.

- Final tooling checks: `cx-vxtools1:final-tooling-check`, exit 0. Shell syntax, diff check, node-width tests, launcher tests, queue-width tests, manual-grant tests, and winjob tests all passed.
- Native Release build: `cx-vxtools1:native-release-build`, exit 0. `tools/vixen-native.sh` logged queue width `2`, `source=queue grant-cpus=2`, jobserver semaphore `boxq-js-1778585`, and `no -j`; the build completed all 1,395 actions with `All targets built successfully`. Queue log: [native-release-build.log](/home/liory/codeman-cases/undertow/reports/cx-vxtools1-evidence/native-release-build.log).
- Elastic status: live `--status` samples during the native run showed grants moving through `grant-cpus=2, 6, 8, 10` and returning as other jobs consumed/released tokens. A lane-specific sample showed `granted=3`, `tokens=0,1,2`, `grant-cpus=6`, `winnative=1`, and `windows-native: running 2/none`; the exact samples are in [status-samples.txt](/home/liory/codeman-cases/undertow/reports/cx-vxtools1-evidence/status-samples.txt). The grant/jobserver behavior passed; the per-job Windows attribution field was `unknown`/`unregistered` and is not claimed green.
- Native CPU CTest: the first queued invocation failed before CTest because the batch defaulted to the absent `C:\vixen-native\vxtools1\build\ninja`; [first-fail log](/home/liory/codeman-cases/undertow/reports/cx-vxtools1-evidence/native-cpu-ctest-first-fail.log). With the documented selected tree `VIXEN_NATIVE_BUILD_DIR=/mnt/c/vixen-native/vxtools1/build/ninja-release`, `cx-vxtools1:native-cpu-ctest-retry` passed `test_spatial_reuse_history_mirror` 1/1, 100%; [retry log](/home/liory/codeman-cases/undertow/reports/cx-vxtools1-evidence/native-cpu-ctest-retry.log).
- WSL CPU unchanged witness: `cx-vxtools1:wsl-cpu-build` built `test_spatial_reuse_history_mirror`; `cx-vxtools1:wsl-cpu-ctest` ran the CPU label selection with native/nightly excluded and passed 1/1, 100%. Logs: [WSL build](/home/liory/codeman-cases/undertow/reports/cx-vxtools1-evidence/wsl-cpu-build.log), [WSL CTest](/home/liory/codeman-cases/undertow/reports/cx-vxtools1-evidence/wsl-cpu-ctest.log).
- No visible console/orphan path: the static launcher and winjob tests passed; the native build log shows `[winjob] reaped 1 process(es)`. The live queued cancellation witness started the `cl.exe`/`ninja.exe` stand-ins, killed the interop relay, and found `left 0` after one second: [cancellation retry](/home/liory/codeman-cases/undertow/reports/cx-vxtools1-evidence/native-orphan-cancel-retry.log).

## REDS / RECOVERY

- Recovered native CPU red: missing selected build-tree propagation, fixed by the existing launcher contract and re-witnessed green. It is the first consolidation proposal below, not a product regression.
- Recovered cancellation red: the first live harness command expanded `UT_QUEUE_CORE_DIR` before entering the queue, producing an invalid `WINJOB_EXE` and no started tree. The retry used the explicit deployed helper `/home/liory/.local/state/box-queue/payload-core/box-winjob.exe` and passed. First invocation: [log](/home/liory/codeman-cases/undertow/reports/cx-vxtools1-evidence/native-orphan-cancel-first-invocation.log).
- The queue status reported `jobuse-win=unknown` after the Release build and `jobuse-win=unregistered` for the CTest phase despite a queue grant. This is an attribution/reporting gap, not a build/test failure; it is filed below.
- Cold native checkout/provisioning was required because `/mnt/c/vixen-native/vxtools1` was absent and the pinned Ninja tool was missing. It recovered through the documented launcher provisioning path. Existing inbox proposals already cover per-lane checkout provisioning and first-run native tool/cache provisioning; no duplicate was filed.
- CodeGraph-first discovery reported no `.codegraph/index`; this is an existing VIXEN consolidation finding, so source inspection continued as required.
- No STOP. Full unfiltered VIXEN CTest was not required for this tooling-only scope and was not run; no unrelated base red was relabeled as introduced.

## SPT DISPOSITION

No pre-existing SPT task IDs were worked or closed.

## CONSOLIDATION ISSUES (non-facade deltas this lane needed)

- proposed: Native CTest batches should inherit the selected build tree (`f9992815c946`)
- proposed: Native CTest phases should register their Windows process root (`281447ae0c16`)
