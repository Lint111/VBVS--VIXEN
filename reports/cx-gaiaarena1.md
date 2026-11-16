# cx-gaiaarena1

## STATUS

REFER-TO-ORCHESTRATOR. R760 rejects the process-wide Gaia lock. The no-lock failure is understood and reproduced on WSL and native Windows, but the requested lock-free repair requires an assigned Lint111 Gaia fork worktree/branch. This lane owns only the VIXEN worktree, so it reverted the rejected lock and did not invent a VIXEN serializer or hand-edited Gaia workaround.

## SHAs

- Base before implementation: `c012413c3a9bbfae26d715e79ccda6e410d29487`.
- `origin/wave/authoring-convergence` was fetched and merged; it was already at the base SHA.
- Round 1 reference commit: `7bff05f9b881` (`fix: enable Gaia arena locking for parallel materialization`), rejected by R760.
- Round 2 revert: `6901112639b0` (`revert: remove rejected process-wide Gaia lock`). The current VIXEN tree does not define `GAIA_ALLOC_ARENA_LOCK=1`.
- Pinned Gaia revision: `84ba7f10dfabd376a862c6fc54d3d43ed365ae54` from `Lint111/gaia-ecs`.
- Latest upstream checked: `richardbiely/gaia-ecs` `5fca8a09692bc6b536568d8d45e16dac13948af2`.

## ROOT CAUSE

`VIXEN/libraries/SVO/src/BulkMaterialization.cpp:120` calls `BakeSdfWorld`; `VIXEN/libraries/SVO/include/SdfBake.h:303-315` creates a fresh `GaiaVoxelWorld` for each materialization. `BulkMaterialization.cpp:181-213` runs those materializations through `KernelDispatch::Backend::CpuTbb`, including the two-worker path exercised by `VIXEN/libraries/SVO/tests/test_voxel_injection.cpp:214-249`.

The pinned Gaia allocator documents process-wide `SmallBlockAllocator`, `ChunkAllocator`, and `PagedAllocator` singletons at `include/gaia/mem/mem_alloc.h:41-52`. With the default `GAIA_ALLOC_ARENA_LOCK=0`, assertion-enabled builds reject concurrent mutation at `mem_alloc.h:75-88`; the same default is declared in `include/gaia/config/config.h:47-51`. The assertion is an allocator ownership check, not a TLS or alignment failure. Its process-wide static lock/owner state makes the required contract explicit.

This is not actually Windows-only. The registered native Windows run exposed the overlap first because MSVC Debug scheduling made the two independent World mutations overlap, while the unchanged WSL Debug baseline also reproduced the same assertion (iteration 3/20). There is no evidence of a Windows allocator, TLS, or alignment defect.

Latest upstream was checked again for Round 2: `richardbiely/gaia-ecs` remains at `5fca8a09692bc6b536568d8d45e16dac13948af2`; the pinned Lint111 branch remains `84ba…`. Upstream still has process-wide `dyn_singleton` allocator state and only the serializing `GAIA_ALLOC_ARENA_LOCK` switch. Therefore the R760 fix is a Gaia allocator/API change, not a VIXEN call-site toggle.

## CHANGE

Round 1 temporarily added `target_compile_definitions(gaia INTERFACE GAIA_ALLOC_ARENA_LOCK=1)` at `VIXEN/dependencies/native/CMakeLists.txt:78-81`; that commit is retained only as historical evidence and was reverted by `6901112639b0`. The current tree has no process-wide lock enabled.

## WITNESS (ROUND 1 GLOBAL-LOCK REFERENCE)

All build/test commands used the live `/home/liory/.local/bin/with-test-lock.sh` queue and `--no-replay` for repeat witnesses.

| Scope | Result | Evidence |
|---|---|---|
| Unchanged WSL baseline, exact test, 20 repeats | FAIL, exit 134 at iteration 3; Gaia arena assertion | `/home/liory/.local/state/undertow/undertow-box-logs/1794805259-test-cx-gaiaarena1:baseline-wsl-20x.log` |
| Post-fix WSL `test_voxel_injection` build | PASS, exit 0; codegen outputs unchanged | queue log `1794806421-build-cx-gaiaarena1:after-wsl-build-svo-retry.log` |
| Post-fix WSL exact test, 20 repeats | PASS, 20/20 | queue log `1794806581-test-cx-gaiaarena1:after-wsl-20x-retry.log` |
| Post-fix WSL CTest exact case | PASS, 1/1, CTest #143 | queue log `1794806606-test-cx-gaiaarena1:wsl-ctest.log` |
| Native Windows Debug target build | PASS, 102/102; Gaia verified at `84ba…` | queue log `1794806778-build-cx-gaiaarena1:native-build-recovery2.log` |
| Native Windows CTest exact case | PASS, 1/1, CTest #143 | queue log `1794809049-test-cx-gaiaarena1:native-ctest.log` |
| Native Windows exact test, 20 repeats | PASS, 20/20 | queue log `1794809061-test-cx-gaiaarena1:native-20x.log` |

The baseline failure is the registered row-110 target red. Native setup failures were recovered and are not product regressions: the first scoped native build lacked a configured tree, the first recovery hit `VIXEN/codegen/CMakeLists.txt:728` while probing the WSL SDK, and the live queue briefly refused ordinary work while its core stamp was stale. The successful native recovery used the already provisioned pinned native kernel snapshot, Windows `dotnet.exe`, and a fresh native binary directory through `vixen-native.sh`; no source workaround was added.

## REDS

- Round 1 historical result: row 110 was reproduced on the unchanged WSL baseline and made green by enabling Gaia's arena lock; native Windows and WSL post-fix witnesses were green.
- Round 2: row 110 remains red with the required no-lock configuration on both platforms. The global-lock green result is not an acceptable R760 implementation.
- The full VIXEN suite was not required for this isolated CPU case; Round 1's platform-specific CTest and 20x repeat witnesses passed, while Round 2's no-lock witness results are recorded below.

## SPT DISPOSITION

Round 1 searches found existing coverage for the incidental setup issues: T-7559 (queue-core drift repair), T-6206 (native C: clone/custom setup), T-6644 (native Ninja/toolchain provisioning), and closed T-4495 (root-relative VIXEN preset configure/build). Round 2 added proposals for the distinct queue transport abort and native direct-test relay cleanup observed during measurement. No SPT task IDs were supplied by the brief.

## STOPS

Round 1 provisioning/invocation failures were recoverable. Round 2 has a design/authority stop: assign a Gaia Lint111 fork worktree/branch, or explicitly authorize a VIXEN fallback that changes the scheduling/ownership architecture. The rejected process-wide lock was reverted and is not reported as a completed fix.

## ROUND 2 — R760: NO PROCESS-WIDE LOCK

### Root cause and scope

The allocator is still process-global in the pinned Gaia source. `include/gaia/mem/mem_alloc.h:41-52` defines the `SmallBlockAllocator`, `ChunkAllocator`, and `PagedAllocator` instances through `dyn_singleton`; `include/gaia/core/dyn_singleton.h` provides a process-wide static singleton, not a World- or thread-owned arena. `include/gaia/config/config.h:47-51` defaults `GAIA_ALLOC_ARENA_LOCK` to zero, and the assertion in the pinned `single_include/gaia.h:6396` (WSL) / `:6399` (native) detects concurrent arena ownership.

VIXEN does create independent Worlds (`VIXEN/libraries/GaiaVoxelWorld/src/GaiaVoxelWorldInternal.h:11-19`), and `VIXEN/libraries/SVO/include/SdfBake.h:303-315` creates one for each bake, but that does not isolate Gaia's allocator singleton. `BulkMaterialization.cpp:181-213` can overlap those bakes on the CPU two-worker path. A VIXEN-only World split therefore cannot satisfy R760; it needs a Gaia allocator context/ownership change or a different scheduler architecture.

### Round 2 change

The rejected process-wide definition was removed and committed as `6901112639b0`. No Gaia source was edited because no Gaia worktree was assigned to this lane. No generated output or pin was changed.

### Measurements

The target is `BulkMaterializationIntegrationTest.CpuRecipeOneVsNWorkerCanonicalHashParity`; the larger bake is `GenerationCostBenchmark.StandardBakeStageBreakdown` (n=64, depth=3, 508 bricks, 7,305,928 pool bytes). The no-lock large bake is a serial performance reference only; it does not establish concurrent safety.

| Configuration | Target / benchmark result | Timing evidence |
|---|---|---|
| No lock, WSL target | FAIL, exit 134 on iteration 2 of 20; Gaia assertion | `1794810162-test-cx-gaiaarena1:round2-no-lock-test-timing.log`, wall 0.56 s |
| Global lock, WSL target (Round 1 reference) | PASS, 20/20 | `1794809812-test-cx-gaiaarena1:round2-global-lock-test-timing.log`, wall 6.31 s |
| No lock, WSL n=64 bake | PASS as a single-worker bake; median total 13,600.1609 ms, 537.2 bytes/ms | `1794810173-test-cx-gaiaarena1:round2-no-lock-bake64.log`, wall 41.18 s |
| Global lock, WSL n=64 bake | PASS; median total 17,170.9365 ms, 425.5 bytes/ms | `1794809908-test-cx-gaiaarena1:round2-global-lock-bake64.log`, wall 51.60 s |
| Requested lock-free Gaia fix | NOT RUN; no authorized Gaia fork worktree | Requires the owner decision below |

The single-worker n=64 comparison is approximately 20.8% lower median bake time without the global lock (13.600 s vs 17.171 s), but it must not be read as a concurrency throughput result. The target’s two-worker path is the failing concurrent case.

### Platform witnesses

- WSL no-lock target, 20 repeats: failed at iteration 2 with the arena assertion (`1794810162...`); the same target under the global-lock reference passed 20/20 (`1794809812...`).
- Native no-lock target, 20 repeats: assertion on iteration 1, then the direct PowerShell relay required validated-PID cleanup; queue result was exit 143 (`1794810838-test-cx-gaiaarena1:round2-native-no-lock-20x.log`). Native Round 1 global-lock reference passed 20/20 (`1794809061-test-cx-gaiaarena1:native-20x.log`).
- WSL no-lock CTest: one invocation passed, queue log `1794811210-test-cx-gaiaarena1:round2-no-lock-wsl-ctest`; this is consistent with a timing race and is not a safety witness. The 20x target run is the stronger reproduction.
- Native no-lock CTest: failed test 143 with the Gaia assertion, queue log `1794811286-test-cx-gaiaarena1:round2-no-lock-native-ctest-retry3.log`.
- Native and WSL Round 1 global-lock CTest both passed test 143: `1794809049-test-cx-gaiaarena1:native-ctest.log` and `1794806606-test-cx-gaiaarena1:wsl-ctest.log`.
- No new lock-free implementation exists, so new-fix CTest witnesses cannot truthfully be reported as run.

The WSL CTest queue log is the `round2-no-lock-wsl-ctest` record in `/home/liory/.local/state/undertow/undertow-box-done.log`; the payload passed once. The native CTest failure supplies the direct no-lock CTest reproduction. The earlier native relay hang was cleaned up only by killing validated PID `1390229`; no lane process remained.

### Gaia fork handoff and upstream PR text

Latest upstream was checked before this handoff (`5fca8a09…`); it retains the same process-wide allocator design. The pinned Lint111 revision is `84ba7f10dfabd376a862c6fc54d3d43ed365ae54`. The recommended Gaia change is an owner-scoped allocator context: make the relevant SmallBlock/Chunk/Paged allocator state belong to a materialization worker or World, pass that context through World/entity-container allocation, and ensure destruction/free returns to the owning context. A TLS-only singleton is insufficient if a World is destroyed on another thread. An atomic lock-free shared arena is an alternative, but must preserve page-list/chunk ownership and free safety without `GAIA_ALLOC_ARENA_LOCK`.

Falling reproduction for the Gaia fork:

```text
build/.../libraries/SVO/tests/test_voxel_injection --gtest_filter=BulkMaterializationIntegrationTest.CpuRecipeOneVsNWorkerCanonicalHashParity --gtest_repeat=20 --gtest_break_on_failure
```

Expected unpatched result: Gaia assertion at `single_include/gaia.h:6396` on WSL or `:6399` on native Windows with `GAIA_ALLOC_ARENA_LOCK=0`. The fork should add a concurrent two-World test, retain canonical-hash parity, run it 20x on WSL and native, and compare the n=64 bake wall time/allocation throughput against the two references above. No upstream PR was opened.

Proposed upstream PR text (not submitted):

> **Make Gaia allocation arenas owner-scoped for concurrent Worlds**
>
> Reproducer: run `BulkMaterializationIntegrationTest.CpuRecipeOneVsNWorkerCanonicalHashParity` 20 times with `GAIA_ALLOC_ARENA_LOCK=0`; two independent Worlds overlap and trip the allocator ownership assertion at `single_include/gaia.h:6396/6399`. Enabling `GAIA_ALLOC_ARENA_LOCK=1` makes the test pass by serializing all Gaia allocations, which is not acceptable for lock-free runtime/data handling.
>
> The allocator singletons are process-wide, so independent Worlds do not own independent arena state. This change introduces an owner-scoped allocator context (per materialization worker/World) and routes allocation and destruction through that context, preserving ownership when work migrates between threads. Add native and WSL repeated concurrent-World coverage and report bake throughput before/after.

### Recovery and decision

- A: the documented live queue route and native `vixen-native.sh` route were available; the target and benchmark rebuilt from the no-lock tree. One queued benchmark build hit a queue `tee.rs` spare-pipe/SIGPIPE failure (`1794809827`) and succeeded unchanged on retry (`1794809857`); this is tooling, not a source diagnostic.
- B: no generated output, pin, or schema input was stale or changed; regeneration was not applicable to this CMake/Gaia allocator issue.
- C: the target failure is isolated to the required Gaia-dependent CPU scope, but no narrower VIXEN check can validate a lock-free fix without changing Gaia or replacing the scheduler. The single-worker bake passing without the lock is not a valid concurrent baseline.

### STOP / owner action

`REFER-TO-ORCHESTRATOR`: assign a worktree and authority for the pinned Lint111 Gaia fork, then land the Gaia owner-scoped allocator fix and update the consuming pin. Alternatives are (A) per-materialization/World allocator context, recommended; (B) a genuinely thread-safe atomic/owner-routed Gaia arena; or (C) the process-wide lock, explicitly rejected by R760. Until A or B is implemented and pinned, the requested native/WSL 20x and CTest “new fix” witnesses cannot be green.

## CONSOLIDATION ISSUES

- proposed: Box queue tee can abort a queued VIXEN build before payload completion
- proposed: Native direct-test PowerShell relay does not reap an aborted Windows test
