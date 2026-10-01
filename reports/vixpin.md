# VIXEN shared TBB and kernel pin refresh

Lane: `vixpin`; branch: `lane-vixpin`; base: `204c22d64767953f0d611034f5eb0e92f50d0c4e`.

## Changes

- oneTBB now sees `BUILD_SHARED_LIBS=ON` only while its FetchContent project is configured. The previous value is restored immediately afterwards, so the later dependencies continue to use `BUILD_SHARED_LIBS=OFF`.
- The tracked kernel SHA is assigned as a normal configure variable on every run. The old generic `YEROKET_KERNEL_SHA` cache entry is removed. A deliberate per-tree override is available as `VIXEN_YEROKET_KERNEL_SHA_OVERRIDE`, and configure prints whether it selected the tracked pin or that override.
- The existing `TBB::tbb` copy steps now resolve the built shared library on this Linux witness.

## Baseline and focused proofs

Before editing, I configured the unchanged base twice in the same fresh tree. Both commands exited 0; the logs and extracted compile databases are in `build/vixpin-evidence/`.

- First configure: `baseline-configure-first.log`. Its 33 TBB translation-unit commands contain `-flto=auto`, the shared oneTBB shape.
- Reconfigure: `baseline-configure-reconfigure.log`. oneTBB warns that it is building static; the 33 commands instead define `__TBB_DYNAMIC_LOAD_ENABLED` and `__TBB_SOURCE_DIRECTLY_INCLUDED`. The TBB command diff has 170 lines. `BUILD_SHARED_LIBS` is `OFF` at the end of both configures.

After the change, a fresh configure of `build/vixpin` produced 33 shared-shape TBB commands. I changed the tracked pin in `VIXEN/codegen/CMakeLists.txt` from `b3621c23621f2fe3f8b0dc49dbbe42f602ae3a27` to `94dd8df925fb9a35877c66a688ccdf0a681529a4` and reconfigured that same tree without `-U` or cache cleanup. `fixed-pin-bump-reconfigure.log` shows that CMake selected the new SHA and archived the matching kernel snapshot. I restored the tracked pin and configured the same tree again.

The TBB compile-command sets from the first configure, the pin-bump reconfigure, and the restored-pin configure are byte-identical: all three filtered JSON files have SHA-256 `0fadb5c72ea4f002fc1f7073cc177b959ffc833d6bff2be412f00912cf9b9c51`. They contain shared-build LTO flags and no static-build macros; the cache remains `BUILD_SHARED_LIBS=OFF`. The full build linked `libtbb.so.12.11` and copied it beside VIXEN, RenderGraph tests, and SVO tests.

The final cache has no `YEROKET_KERNEL_SHA` entry. `VIXEN_YEROKET_KERNEL_SHA_OVERRIDE` is empty, and the restored configure log selects the tracked `b3621c23621f2fe3f8b0dc49dbbe42f602ae3a27` pin.

## Witness

The fresh Release/Ninja tree was configured with the worktree-local FetchContent directory and built through `tools/with-test-lock.sh`, using `nice -n 10` and four build jobs. The full build completed all 1,134 steps with exit 0. Logs are under `build/vixpin-evidence/`.

- All 22 `*_check` targets completed successfully. The fresh full build created 21 codegen check stamps plus `no_new_mutex_check.stamp`; the explicit 22-target invocation then exited 0 with no work because those fresh stamps were current.
- `ctest -L RenderGraph`: 1,337 selected, 0 failures, 12 skipped.
- `ctest -L SVO`: only `RecipeSimdParity.AllCorpusProgramsAreBitIdenticalAcrossFourLanes` failed, with the known opcode 94 / T-1449 mismatch. Eight tests were skipped and one was disabled.
- Captures: 11/11 passed with `DISPLAY` and `WAYLAND_DISPLAY` unset and `VK_ICD_FILENAMES` set to the Dzn ICD.
- Post-test no-op rebuild: Ninja reported `no work to do`; there were zero compile/link units (CMake performed its two directory-glob rechecks).

The CodeGraph-first query could not run because this isolated worktree has no `.codegraph` index. I used the allowed source-reading fallback; the same index gap is already covered by the vixcache proposal, so I did not duplicate it here. This witness covers the Linux/GCC/WSL configuration; Windows/MSVC was not exercised.

## STOPs

None in the requested scope. The only failing test is the task-approved opcode 94 / T-1449 case.

## CONSOLIDATION ISSUES

- proposed: Standalone VIXEN configure needs a manual Undertow schema catalogue path
- proposed: VIXEN codegen does not locate the kernel repo in isolated project worktrees
- proposed: VIXEN kernel snapshot cache needs a manual worktree-local path for isolated lanes
