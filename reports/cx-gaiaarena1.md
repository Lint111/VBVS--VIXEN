# cx-gaiaarena1

## STATUS

DONE. Row 110 is fixed in VIXEN and the required CPU witnesses pass on both WSL and native Windows. No Gaia fork change or upstream PR is warranted: the defect is VIXEN's use of Gaia's documented multi-World mode without enabling Gaia's process-wide arena lock.

## SHAs

- Base before implementation: `c012413c3a9bbfae26d715e79ccda6e410d29487`.
- `origin/wave/authoring-convergence` was fetched and merged; it was already at the base SHA.
- Fix commit: `7bff05f9b881` (`fix: enable Gaia arena locking for parallel materialization`).
- Pinned Gaia revision: `84ba7f10dfabd376a862c6fc54d3d43ed365ae54` from `Lint111/gaia-ecs`.

## ROOT CAUSE

`VIXEN/libraries/SVO/src/BulkMaterialization.cpp:120` calls `BakeSdfWorld`; `VIXEN/libraries/SVO/include/SdfBake.h:303-315` creates a fresh `GaiaVoxelWorld` for each materialization. `BulkMaterialization.cpp:181-213` runs those materializations through `KernelDispatch::Backend::CpuTbb`, including the two-worker path exercised by `VIXEN/libraries/SVO/tests/test_voxel_injection.cpp:214-249`.

The pinned Gaia allocator documents process-wide `SmallBlockAllocator`, `ChunkAllocator`, and `PagedAllocator` singletons at `include/gaia/mem/mem_alloc.h:41-52`. With the default `GAIA_ALLOC_ARENA_LOCK=0`, assertion-enabled builds reject concurrent mutation at `mem_alloc.h:75-88`; the same default is declared in `include/gaia/config/config.h:47-51`. The assertion is an allocator ownership check, not a TLS or alignment failure. Its process-wide static lock/owner state makes the required contract explicit.

This is not actually Windows-only. The registered native Windows run exposed the overlap first because MSVC Debug scheduling made the two independent World mutations overlap, while the unchanged WSL Debug baseline also reproduced the same assertion (iteration 3/20). There is no evidence of a Windows allocator, TLS, or alignment defect.

Latest upstream was checked before classification: `richardbiely/gaia-ecs` currently reports `5fca8a09692bc6b536568d8d45e16dac13948af2` at `main`; the pinned Lint111 branch remains `84ba…`. Gaia already supplies the supported lock mode, so no upstream fork branch or PR was opened.

## CHANGE

Added `target_compile_definitions(gaia INTERFACE GAIA_ALLOC_ARENA_LOCK=1)` at `VIXEN/dependencies/native/CMakeLists.txt:78-81`. This enables Gaia's supported non-recursive process-wide arena spinlock for all VIXEN consumers of the header-only `gaia` target while preserving the existing parallel materialization design.

## WITNESS

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

- Row 110: reproduced on the unchanged WSL baseline and fixed by enabling Gaia's arena lock; native Windows and WSL post-fix witnesses are green.
- No new visible product red was introduced.
- The full VIXEN suite was not required for this isolated CPU case; both platform-specific CTest witnesses and 20x repeat witnesses passed.

## SPT DISPOSITION

No duplicate SPT proposal was created. Searches found existing coverage for the incidental setup issues: T-7559 (queue-core drift repair), T-6206 (native C: clone/custom setup), T-6644 (native Ninja/toolchain provisioning), and closed T-4495 (root-relative VIXEN preset configure/build). The missing CodeGraph index is also already represented by an existing lane proposal. No SPT task IDs were supplied by the brief.

## STOPS

None. The initial native and queue failures were recoverable provisioning/invocation failures; the recovery ladder completed and the required target witnesses passed.

## CONSOLIDATION ISSUES

None proposed by this lane; existing SPT coverage was extended by evidence in this report rather than duplicated.
