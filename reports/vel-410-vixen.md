# vel-410-vixen

| id | title | verdict | evidence |
|---|---|---|---|
| T-6372 | GPU queue must serialize long multi-frame Dozen lookdev captures per device | FIXED (box-queue lane-vel410vixen cbe59a7f) | `queue/with-test-lock.sh` `_gpu_command_is_lookdev_capture` + `_gpu_mode_decide`: a gpu job whose command names a lookdev capture (`vixen_lookdev_capture`, ctest `Application.LookdevCapture`, `VIXEN_LOOKDEV_CAPTURE_*`) gets gpu_mode=exclusive (reason "multi-frame lookdev capture"). It is not a timing run, so it can still replay. Help text updated. `tests/test-with-test-lock-gpu-mode.sh`: 6 witnesses PASS (3 new: lookdev ctest, lookdev binary, non-lookdev stays shared). `bash -n` OK. |

The admission core is in box-queue, not VIXEN; no VIXEN source changed (this commit only adds this report). Per the brief's scope, the kernel/undertow/launcher witnesses do not apply: nothing in those repos or in generated output changed.

## LANDABLE NOW
- box-queue: branch `lane-vel410vixen` @ `cbe59a7f26c3eb379579ec398b925bf49a259a4e` (base origin/master 915210a)
- VIXEN: branch `lane-vel410vixen` (report only; tip is this commit)
- Witness: gpu-mode test 6/6 PASS in an isolated extract of the functions; the live queue was not touched.
- Controller must: deploy the queue (`install.sh --install-with-test-lock`) and refresh `queue/with-test-lock.manifest` (not refreshed here, deploy step).
- Not run: an end-to-end four-capture run through a deployed queue (needs the deploy, and Dozen capture runs are GPU-heavy).

## OWNER QUESTIONS
None. Choice made: detection is by command text, so `--resource gpu` captures serialize automatically. Single-frame lookdev runs matching the name are serialized too, which is a small cost.

## CONSOLIDATION ISSUES
- The VIXEN SPT link on T-6372 uses the unknown repo alias `VIXEN` (should be `vixen`).
