# cx-lodperf1

## STATUS

COMPLETE. Row 53 is resolved in this lane. The original assertion was a wall-clock regression threshold inside the normal LOD correctness test. The correctness check is now deterministic and the wall-clock measurement is benchmark-only.

Base SHA: `c27282c48db63e1d8bb71ad5b3445b7423d43d3d` (`wave/authoring-convergence`). The worktree did not have the brief's `lane-lodperf1` branch and no branch switch/create was permitted, so the commit is on the already checked-out branch.

## CAUSE

`LODRayCastingTest.NoPerformanceRegressionWithoutLOD` timed 1,000 regular casts and 1,000 disabled-LOD casts, then asserted a host wall-clock ratio (`<1.2` in Release, `<2.0` in Debug). That is scheduling-sensitive timing in a correctness suite. The disabled-LOD implementation already hoists disabled parameters to the non-LOD path; the base replay showed no semantic regression.

## CHANGE

- Added `RayHit::traversalIterations`, a deterministic ESVO node-visit count, populated by the ESVO traversal for LOD hits, leaf hits, and misses. GPU-mirror hits retain zero because they use a separate traversal.
- Extended `LODRayCastingTest.DisabledLODMatchesRegularCast` to compare traversal work as well as hit identity, scale, bounds, point, and normal.
- Removed the wall-clock `EXPECT_LT` from `NoPerformanceRegressionWithoutLOD`; it now records and prints the explicit measurement.
- Registered `LODRayCastingTest.NoPerformanceRegressionWithoutLOD` with the existing `BENCHMARK_TESTS` convention, so normal correctness CTest runs disable it and label explicit runs `benchmark`.

## WITNESS

All heavy commands used the live `/home/liory/.local/bin/with-test-lock.sh` queue; no synthetic load was started.

Base replay before semantic edits:

- Release configure and `test_lod` build passed through `cx-lodperf1:configure-base` and `cx-lodperf1:build-base-lod`.
- Isolated base command `ctest --test-dir .tmp/lodperf1-base -R '^LODRayCastingTest.NoPerformanceRegressionWithoutLOD$' --output-on-failure --no-tests=error` passed 1/1. Queue node load was `12.64`, `cpus=32`, `concurrent=1`.
- Full required `RenderGraph|SVO` base command passed: `2,270` active tests, `0` failures. The row-53 test passed in `0.07s`; queue node load was `11.12`, `cpus=32`, `granted=7`.

Implementation witness:

- Queued `test_lod` rebuild passed.
- Focused deterministic correctness test passed 1/1.
- The explicit benchmark CTest run passed and reported:

  `regular=0.121ms lod-disabled=0.121ms ratio=1.000x mean-of-1000`

  It ran with no concurrent GPU job; queue node load was `9.07` on `cpus=32`.

- Fresh aggregate `rendergraph_svo_tests` rebuild completed all `231/231` steps. Queue node load was `26.32`; `cgpeak=29130MB`.
- Final fresh-tree correctness witness ran the full `RenderGraph|SVO` scope three times. Each run passed `2,269` active tests with `0` failures; `LODRayCastingTest.DisabledLODMatchesRegularCast` passed each time, and `NoPerformanceRegressionWithoutLOD` was disabled each time. Queue node load was `19.33`, `cpus=32`, `granted=7`.

Evidence logs are under `/home/liory/codeman-cases/undertow/reports/cx-lodperf1-evidence/`, including `replay-base-isolated.log`, `replay-base-full.log`, `lod-measurement-visible.log`, `build-final-rendergraph-svo.log`, and `full-correctness-final-run{1,2,3}.log`.

## REGISTER UPDATE

Update `reports/base-reds.md` row 53 to `RESOLVED`: the timing assertion was split; correctness now checks deterministic hit/work parity, the timing case is benchmark-only, the base isolated/full replay passed, the explicit measurement passed at `1.000x`, and the final correctness scope passed three times.

## REDS

The first queued aggregate baseline build stopped at `[728/1040]` with only `Terminated "$@"` and no `UT_JOB_DONE` or payload diagnostic. Recovery rung A verified the documented global queue and CMake target; rung B reconfigured/generated without source churn; the queued verbose retry completed the same target in `272s` (`load=25.16`, `cgpeak=13463MB`). This was an invocation/supervisor red, not a product failure, and is recorded as a consolidation proposal. Untouched pre-existing compiler deprecation/unused-parameter warnings appeared in build logs; no new warning was introduced by the changed files.

## STOPs

None. No external design decision or required verification blocker remains.

## CONSOLIDATION ISSUES (non-facade deltas this lane needed)

- proposed: Queued aggregate VIXEN build termination has no diagnostic or resumable result
- proposed: Benchmark queue cadence has no bounded path for a short CPU measurement
