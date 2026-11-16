# cx-gaiaarena1

## STATUS

COMPLETED / LANDABLE. Round 3 implements and witnesses the owner-scoped, lock-free Gaia allocator on upstream and the Lint111 pin, then points VIXEN at the pin. No process-wide lock is enabled. The Gaia commits are intentionally unpushed for the controller to land.

## SHAs

- Base before implementation: `c012413c3a9bbfae26d715e79ccda6e410d29487`.
- `origin/wave/authoring-convergence` was fetched and merged; it was already at the base SHA.
- Round 1 reference commit: `7bff05f9b881` (`fix: enable Gaia arena locking for parallel materialization`), rejected by R760.
- Round 2 revert: `6901112639b0` (`revert: remove rejected process-wide Gaia lock`). The current VIXEN tree does not define `GAIA_ALLOC_ARENA_LOCK=1`.
- Upstream Gaia base checked from `origin-upstream/main`: `4eaf6f208ee693779e8b1c261a0c6bdfb278de78f`; upstream fix: `8bd1cbff407ec3f0ed36df6892d0072baa9d570f` (`fix(mem): use thread-local allocation arenas`).
- Lint111 pin base: `84ba7f10dfabd376a862c6fc54d3d43ed365ae54`; pin fix: `34cf2534f838fb44259a5b3cafd04befacbcdb09` (`fix(mem): use thread-local allocation arenas`).
- VIXEN pin commit: `0fa3e62fdd1ecbb4d84ca21e08d691b69757df3c` (`deps(gaia): pin owner-scoped allocators`).

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

## STOPS (HISTORICAL ROUND 1/2)

Round 1 provisioning/invocation failures were recoverable. Round 2 had a design/authority stop until the owner assigned the Gaia worktrees; that stop was cleared by the Round 3 dispatch. The rejected process-wide lock was reverted and is not reported as a completed fix.

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

## ROUND 3 — OWNER-SCOPED GAIA ALLOCATOR

### Upstream workflow and known issue search

I checked `origin-upstream/main` before editing; this checkout resolved the latest upstream base to `4eaf6f208ee6` (the older `5fca8a09` recorded in the Round 2 report was stale). GitHub issue/PR searches found no allocator-specific issue or PR. Gaia issue [#39](https://github.com/richardbiely/gaia-ecs/issues/39) is a known, broader restriction on concurrent World/query use, but it does not describe this allocator-arena defect.

The upstream branch `fix/owner-scoped-allocators` has one focused commit, `8bd1cbff407e`:

- `dyn_singleton<T>::get()` now returns a `thread_local` instance, so each materialization worker owns independent SmallBlock, Chunk, and Paged allocator state.
- The default `ArenaLock` path no longer carries process-wide owner/assertion state; `test_violations()` remains an opt-in compatibility hook and is zero for owner-scoped arenas. `GAIA_ALLOC_ARENA_LOCK=1` remains available only as an explicit compatibility serialization mode and is not enabled by VIXEN.
- Added `ECS - Independent worlds do not share allocation arenas` to `src/test/src/test_mt.cpp`: two threads create independent Worlds and allocate 2,048 entities each. The obsolete old detector expectation in `test_mutation_runtime.cpp` was updated to assert no cross-thread violation.
- Regenerated `single_include/gaia.h` with the documented generator; the generated diff is included in the commit.

The ownership contract is worker/thread scoped: a World and its allocations must remain on the creating materialization worker. This is the ownership model used by VIXEN's CPU-TBB materialization path; independent Worlds on different workers no longer share arena lists. It is not a process-wide lock and does not make arbitrary migration of a live World between threads safe.

### Gaia witnesses

The pre-fix repro failed with the SmallBlockPage header assertion (exit 134), proving the concurrent arena collision: `/home/liory/.local/state/undertow/undertow-box-logs/1794811994-test-cx-gaiaarena1:round3-upstream-repro-before.log`. The same focused test passed with 3/3 assertions after the fix: `/home/liory/.local/state/undertow/undertow-box-logs/1794812442-test-cx-gaiaarena1:round3-upstream-repro-after.log`.

The first full CTest attempt was a provisioning red because only `gaia_test` had been built while CTest registered 51 executables (`1794812509...`, exit 8, missing executables). Recovery built the complete graph (`1794812693...`, exit 0), then the full Gaia suite passed 51/51 and 793/793 doctest cases (`1794813104-test-cx-gaiaarena1:round3-upstream-full-suite-final.log`). The suite initially exposed one old assertion expecting shared-arena violations; after changing that test contract, the suite was green. The final amended commit contains that test update.

The pin branch `pin/soa31-arena-2026-11-16` cleanly cherry-picked the upstream change as one commit, `34cf2534f838`. Its focused repro passed 1/1 with 3/3 assertions: `/home/liory/.local/state/undertow/undertow-box-logs/1794813412-test-cx-gaiaarena1:round3-pin-repro.log`.

### VIXEN pin and platform witnesses

VIXEN consumes the pin from `VIXEN/dependencies/native/gaia.pin`; that file now names `sha: 34cf2534f838fb44259a5b3cafd04befacbcdb09` and `branch: pin/soa31-arena-2026-11-16`, committed in `0fa3e62fdd1e`. The WSL configure log verified the exact Gaia SHA, and the native configure log independently verified the same SHA from the revision-keyed cache. Undertow and kernel copies at `toolchain/providers/gaia.pin` were deliberately not changed in this lane; the controller must update those byte-identical copies to the same pin in the landing batch. No KFR/launcher pin is changed here.

| Witness | Result | Evidence |
|---|---|---|
| WSL CPU parity test, new pin, 20 repeats | PASS, 20/20 | `1794813722-test-cx-gaiaarena1:round3-vixen-wsl-20x.log` |
| WSL CPU CTest | PASS, 1/1 | `1794813740-test-cx-gaiaarena1:round3-vixen-wsl-ctest.log` |
| Native Windows CPU parity test, new pin, 20 repeats | PASS, 20/20 | `1794814207-test-cx-gaiaarena1:round3-vixen-native-20x.log` |
| Native Windows CPU CTest | PASS, 1/1 | `1794814243-test-cx-gaiaarena1:round3-vixen-native-ctest.log` |

The native test target first hit `LNK1104` because a stale same-lane `test_voxel_injection.exe` was still running and held the output. I verified the exact path and PID through `vixen-native.sh ps`, stopped only that validated PID, and the unchanged target build then passed (`1794814122-build-cx-gaiaarena1:round3-vixen-native-build-pin-retry.log`).

### Timing and throughput

The no-lock and global-lock references are the stored Round 2 history; the no-lock run is a serial bake reference because the concurrent target crashed. The new owner-scoped run is fresh WSL n=64 data:

| Configuration | n=64 median bake | Allocation/generation throughput | Evidence |
|---|---:|---:|---|
| No lock, serial reference | 13,600.1609 ms | 537.2 bytes/ms | Round 2 `1794810173-test-cx-gaiaarena1:round2-no-lock-bake64.log` |
| Global `GAIA_ALLOC_ARENA_LOCK=1` | 17,170.9365 ms | 425.5 bytes/ms | Round 2 `1794809908-test-cx-gaiaarena1:round2-global-lock-bake64.log` |
| Owner-scoped thread-local arenas | 14,656.2187 ms | 498.5 bytes/ms | `1794813757-test-cx-gaiaarena1:round3-vixen-wsl-bake64.log` |

The global-lock reference is 26.3% slower than the no-lock serial reference when normalized to the no-lock time (equivalently, no-lock is 20.8% lower than global). The owner-scoped result is about 14.7% faster than the global-lock reference and about 7.8% slower than the single-worker no-lock reference in this run; it preserves concurrent correctness without the rejected process-wide serialization. The bake output remained `nodeCount=581`, `brickCount=508`, and `poolBytes=7,305,928`.

The parity test itself has no allocation-throughput counter; it is the safety witness. Its stored WSL queue wall was 0.56 s for the crashing no-lock attempt and 6.31 s for the historical global-lock 20x run. The owner-scoped 20x runs completed in about 7 s on WSL and 13 s on native Windows. Those test wall times include platform and queue variance, so the n=64 medians above are the comparable throughput measurement.

### Upstream PR text (not submitted)

Two independent Worlds allocating concurrently could corrupt Gaia's process-wide arena lists and trip the SmallBlockPage header assertion with `GAIA_ALLOC_ARENA_LOCK=0`. Enabling `GAIA_ALLOC_ARENA_LOCK=1` makes the repro pass only by serializing every Gaia allocation, including runtime allocations; the n=64 bake measured 17,170.9365 ms versus 13,600.1609 ms for the no-lock serial reference.

This change makes Gaia's allocation singletons thread-local, giving each materialization worker its own arena state, and adds a two-World concurrent allocation regression test. The owner/thread contract is explicit: a World stays on its owning worker. The generated single header is updated with the source headers. The global lock is intentionally retained only as an opt-in compatibility mode and is not the VIXEN fix. The Gaia suite passes 51/51 CTest targets. No PR was opened and no branch was pushed.

## REDS / RECOVERY — ROUND 3

- The initial full-suite missing-executable red was recovered by building the complete registered test graph; it was not a source failure.
- The first full suite after that recovery found the obsolete shared-arena detector expectation; the test contract was updated as part of the focused Gaia change and the final 51/51 suite passed.
- The native first link red was a stale same-lane test process locking the output; exact-PID cleanup and an unchanged rebuild cleared it. The underlying direct-test relay issue is already represented by the Round 2 SPT proposal.
- No new product red remains. The original row-110 no-lock failure is the target this change fixes; both platforms now pass the required 20x and CTest witnesses.

## UPSTREAM / PIN HANDOFF

- Controller should push/open the upstream PR from `8bd1cbff407e` and carry the focused pin commit `34cf2534f838` onto the synchronized Lint111 pin branch as appropriate.
- Controller should update Undertow and kernel `toolchain/providers/gaia.pin` copies to `34cf2534f838...` in the same batch; this lane intentionally changed only VIXEN's `VIXEN/dependencies/native/gaia.pin`.
- The generated header is part of both Gaia commits; do not recapture or hand-edit it.

## CONSOLIDATION ISSUES

- proposed: Box queue tee can abort a queued VIXEN build before payload completion
- proposed: Native direct-test PowerShell relay does not reap an aborted Windows test
- proposed: Gaia single-header generation needs a provisioned formatter
- proposed: VIXEN needs a first-class local Gaia source override for unpushed pin witnesses
