# vel-372-vixen

| id | title | verdict | evidence |
|---|---|---|---|
| T-4853 | Derive executable status manifests from complete CTest results | FIXED (ac34d8a9) | `reports/capturespeed/reproduce/ctest-manifest.ps1` derives per-executable passed/failed/not_run from ctest `--output-junit` files, merged across resumed slices (newest real result wins; a later notrun never overwrites). `native-capture.ps1` writes one junit per run, accepts `-CtestIndex` (`VIXEN_NATIVE_CTEST_INDEX` in the .sh) and calls the helper. Not on origin/wave/authoring-convergence or the landable list beforehand. |

Witness: `test-ctest-manifest.ps1` run under Windows PowerShell 5 (synthetic inventory, two slices: resumed merge gives a.exe 3/3 PASS, b.exe 1 pass + 1 fail = FAIL, c.exe NO_REGISTERED_CTEST_SMOKE; first slice alone gives INCOMPLETE): PASS. JUnit format verified against real ctest 3.31.6 (status=run/fail/notrun, failure/skipped children). native-capture.ps1 parses; native-capture.sh `bash -n` ok. NOT run: a full native Windows all-tests run (not needed for a script-only change; the real run is the first use of -CtestIndex). No C++ touched, so no VIXEN configure/build; kernel/undertow witnesses not applicable (no kernel or undertow change).

## LANDABLE NOW
ac34d8a9c16181f9900b9dedf96e943fec0bfd8e (after report commit: see git log)

## OWNER QUESTIONS
none

## CONSOLIDATION ISSUES
- Worktree is sparse: reports/ is excluded except `reports/vel-372-vixen*`; I added `reports/capturespeed` to this worktree's sparse set.
- SPT link L-11155 uses unknown repo alias 'VIXEN' (should be 'vixen'); the earlier events.hash mismatch warning was not reproduced.
