# vel-409-vixen: FSR stage-cost mode in the native capture harness

| id | title | verdict | evidence |
|---|---|---|---|
| T-6182 | Focused FSR stage-cost mode in native capture harness | FIXED (see tip SHA below) | `run-paired-native.ps1 -StageCost` added; `run-fsr-stage-cost.ps1` removed. Queued `gpu-native --gpu-benchmark` job `vel-409-vixen:native-stagecost-bench` ended `UT_JOB_DONE exit=0 state=OK`: 5 FSR1 profiles (`-Extent 500`), each 256 perf rows (4 yaws x 64 frames) and 4 stills. |

## What changed
`reports/upscalestage1/reproduce/run-paired-native.ps1`
- `-StageCost`: one FSR 1 capture per profile (the same 8 profiles the old script ran: fixed 500 at 1, 2/3, 1/2; coupled 500 at 2/3, 1/2; fixed 1080 at 1, 2/3, 1/2). No A/B/A controls and no 3-round minimum. Output layout `<OutputRoot>/<profile>/{perf.csv,run-timing.txt,captures}` is unchanged from the old script. The manifest records `steady_cost_samples=frames 33-64 per yaw (128 samples)`. `-Extent 500|1080` filters the profiles.
- `-BuildDir` (default `build\stage1-merged-native`, the old hard-coded value): lets the harness run against any native build tree.
- `Invoke-Capture` takes an optional per-run LOD mode and run directory; the A/B/A path is unchanged.
- `run-fsr-stage-cost.ps1` is deleted (it was a duplicate of the capture loop).

## Witness
- Parses with 0 errors (PowerShell parser).
- Run against the existing `uc-recipes` release build (`build\ninja-release`, supports `--render-scale`/`--upscaler=fsr1`); the lane has no native build of its own. Only the 500 extent (5 profiles) was run; the 1080 profiles use the same code path and were not run.
- Raw outputs (stills dropped): `/home/liory/codeman-cases/undertow/reports/vel-409-vixen-evidence/stagecost-500/`, queue log `queue-job.log` beside it.
- Not run: kernel, undertow, launcher suites. Nothing outside this PowerShell script changed, and none of them read it.

## LANDABLE NOW
VIXEN `lane-vel409vixen`: tip SHA given in the final message (the commit holding the script change; this report is committed with it).

## OWNER QUESTIONS
None.

## CONSOLIDATION ISSUES
- `with-test-lock.sh --resource gpu-native` refuses a `bash -lc` wrapper payload (payload must start `powershell.exe`/`cmd.exe`/`*.exe`); a `gpu` job with a bash wrapper sat in the queue ~50 min under CPU pressure and was cut by my timeout. Worth documenting in `_multirepo.md` (the "drive a .bat inside the queued payload" line implies the wrapper form).
- Sparse lane worktrees omit `reports/`, so a task whose files live under `reports/` needs `git sparse-checkout add` first.
