# cx-platscope1

STATUS: DONE — Round 2 completed the required standard native Windows CPU witness and preserved the green WSL scope.

## SHAs

- Base: `c012413c3a9bbfae26d715e79ccda6e410d29487` (`origin/wave/authoring-convergence`)
- Branch: `lane-platscope1`
- Final commit: reported in the final handoff (`git rev-parse HEAD`)

## ROOT CAUSE

- Register row 111 (`/home/liory/codeman-cases/undertow/reports/base-reds.md:124`) records the native Windows failures of `Provision.GdbDependencyPackages`, `Provision.GpuEnvironmentScript`, `Provision.WindowingFailure`, and `Native.VixenTestClassApplyLoadContext`. The first three exercised Unix-shell/Linux provisioning contracts on Windows; the class harness used `/bin/true` and bare `ctest`.
- The two additional native failures recorded by `cx-d11wit2` (`.../cx-d11wit2.md:46`) were Linux-path fixture witnesses in `test_editor_document_model_preview.cpp`: one hardcoded `/home/liory/projects/undertow/...` path and one evidence-pack path under `/home/liory/codeman-cases/...`.

## CHANGE

- Added one `vixen_add_test(... UNIX_ONLY ...)` registration rule in `VIXEN/tests/CMakeLists.txt`. The three Unix-only provisioning contract tests are scoped at declaration time with their existing `cpu` label; no hand-maintained CTest exclusion list was added. `Native.VixenTestClassApplyLoadContext` remains registered on Windows and now uses `${CMAKE_COMMAND} -E true` plus `${CMAKE_CTEST_COMMAND}`.
- Replaced the shipped Undertow fixture paths with the CMake-provided `UNDERTOW_ROOT`, normalized through `file(TO_CMAKE_PATH ...)`, so `EditorDocumentModelGraph.RealUndertowLayersRoundTripArgumentsAndGeneratedRewrites` is portable.
- Marked the lane-only hull evidence witness as a Linux-only compile contract (`CMAKE_SYSTEM_NAME STREQUAL "Linux"`), so it is not discovered by native Windows CTest. It remains available on WSL, where its evidence pack exists.

## WITNESS

All build/test commands below used the live box queue with agent `cx-platscope1`.

- Base red: row 111 was reproduced in the registered baseline at base `6444d7c2b` by `cx-cacheatomic1`; its exact failures and Unix-shell diagnostics are recorded in `base-reds.md` and `cx-cacheatomic1.md`.
- Unix contract scripts: `cx-platscope1:unix-contracts` passed all four contract checks.
- Final queued WSL build: `cx-platscope1:linux-final-build` passed configure, the `test_editor_document_model_preview` target build, codegen golden checks, and `git diff --check`; generated outputs were unchanged.
- Final queued registration: `cx-platscope1:linux-final-discovery` found six affected tests, including the Linux-only hull witness and all four row-111 contracts.
- Final queued affected tests: `cx-platscope1:linux-final-tests` passed 6/6 (100%): the two document-model witnesses, the three Unix provisioning contracts, and `Native.VixenTestClassApplyLoadContext`.
- Final-base check: `git merge --no-edit origin/wave/authoring-convergence` was already up to date; frozen-tree queued witness `cx-platscope1:frozen-final-tests` passed the same 6/6 scope.
- Native toolchain provisioning itself succeeded in the assigned worktree: pinned Ninja, CMake, Visual Studio Build Tools, Windows SDK, and Vulkan SDK were found.
- Native attempt A (`cx-platscope1:native-direct-build`) ran the documented `tools/vixen-native.sh --direct build all vixen-ninja test_editor_document_model_preview` through the build queue for 2011 seconds. The Windows configure log stopped advancing after its initial CMake environment record; the lane stopped its own process, and the wrapper recorded exit 76 / payload 137. Evidence: `cx-platscope1-evidence/native-direct-build.log`.
- Native recovery attempt B (`cx-platscope1:native-direct-retry`) repeated the same queued command with documented `VIXEN_SKIP_BUILD_LOCK=1`; after 600 seconds the same CMake log remained unchanged. The lane stopped it; wrapper exit 76 / payload 137. Evidence: `cx-platscope1-evidence/native-direct-retry.log`.

The native red is therefore unclassified environmental/tooling stall, not a semantic test failure. No native Windows pass count is claimed. The required native CPU CTest witness must be rerun on a healthy Windows-native checkout or after the tooling gap is repaired. The WSL affected scope is green; a full WSL CPU pass-count comparison was not fabricated from the incomplete native baseline.

## SPT DISPOSITION

- No SPT task was assigned to this lane.
- Reused the existing proposal `cx-kernelpinskew2.jsonl`: “VIXEN standalone codegen probe should expose Vulkan cache provisioning.” The first queued configure failed because `VULKAN_SDK` was not set while auto-provisioning was disabled; rerunning with the documented cache path recovered the configure. No duplicate proposal was created.
- Proposed SPT `19413ed3e315`: “Native direct Windows witness stalls during WSL-path CMake configure.”

## STOPs (Round 1)

- STOP: native Windows CPU CTest could not be run after two queued recovery attempts stalled in Windows CMake configure. Required owner action: rerun the native witness using a healthy Windows-native source/build materialization, or repair the documented direct route so configure progress and diagnostics are observable.

## ROUND 2

- Standard queued native build: detached `cx-platscope1:round2-native-build` ran `tools/vixen-native.sh --lane platscope1 build all vixen-ninja test_editor_document_model_preview` through the live queue. It cloned/synced `/mnt/c/vixen-native/platscope1`, configured successfully in 188.2 seconds, generated successfully in 6.3 seconds, built 121/122 scoped objects, linked the test executable, and exited 0. Evidence: `cx-platscope1-round2-evidence/native-build.log`.
- Native Windows CTest discovery: detached `cx-platscope1:round2-native-discovery` found exactly two affected tests: `EditorDocumentModelGraph.RealUndertowLayersRoundTripArgumentsAndGeneratedRewrites` and `Native.VixenTestClassApplyLoadContext`. The three Unix-only provisioning contracts and Linux-only hull evidence test were absent by the platform declarations. Evidence: `cx-platscope1-round2-evidence/native-discovery.log`.
- Native Windows CPU CTest: detached `cx-platscope1:round2-native-tests` passed 2/2 (100%) with no row-111 failures. The real Undertow path witness passed, and the Windows class-load-context witness passed. Evidence: `cx-platscope1-round2-evidence/native-tests.log`.
- WSL unchanged witness: detached `cx-platscope1:round2-wsl-tests` passed the same affected 6/6 scope as Round 1: the three Unix provisioning contracts, the class harness, the portable real-Undertow witness, and the Linux-only hull evidence witness. Evidence: `cx-platscope1-round2-evidence/wsl-tests.log`.
- Queue status during the native route showed fresh `windows-native` samples and active Windows host CPU; the build made normal checkout, configure, and compile progress, so the Round 1 stall classification did not recur.

## CONSOLIDATION ISSUES (non-facade deltas this lane needed)

- proposed: Native direct Windows witness stalls during WSL-path CMake configure
- reused existing: VIXEN standalone codegen probe should expose Vulkan cache provisioning
