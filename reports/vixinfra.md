# VIXEN infrastructure lane: vixinfra

Base SHA before edits: `e0cf083a476e5f0b2df1c36d5a69bebef8e39dbd`.

## Task results

- **T-1461 — FetchContent default:** Fresh configure with `VIXEN_FETCHCONTENT_CACHE` and `FETCHCONTENT_BASE_DIR` unset selected `build/vixinfra-fetchcontent-default-check/_deps`. Reconfigure of that same tree preserved the path. Both configure runs completed successfully. Commit: `c80953c0`.
- **T-1460 — XCB keysyms:** Added `libxcb-keysyms1` and its development package to windowing provisioning. The provisioned cache contains `VIXEN/.windowing-deps/usr/lib/x86_64-linux-gnu/libxcb-keysyms.so.1`. Commit: `034cb265`.
- **T-1480 — CTest GPU environment:** CTest cases receive the configure-selected Dozen ICD, Vulkan layer directory, Dozen/windowing/SDK loader directories, and the configured `LD_LIBRARY_PATH`. The full RenderGraph run passed with caller-side `VK_ICD_FILENAMES`, `VK_LAYER_PATH`, and `LD_LIBRARY_PATH` removed. Commit: `6dc14bee`.
- **T-1465 — Aggregate build target:** Added `rendergraph_svo_tests`, including RenderGraph/SVO test targets and the app/editor capture producers. `nice -n 10 cmake --build build/wsl --target rendergraph_svo_tests -j8` completed successfully. Commit: `f7e3b04c`.
- **T-1466/T-1504 — Stable suite labels:** Registered suite labels on discovered and direct tests. Selection changed from the recorded numeric ranges in `reports/vixbase.md` (RenderGraph IDs 493–1807: 1,315 cases; SVO IDs 1941–2682: 742 cases) to `ctest -L RenderGraph` (1,319 selected) and `ctest -L SVO` (748 selected). Both are independent of index ranges. Commit: `f7e3b04c`.
- **T-1505/T-1524 — Capture fixtures:** Editor and HUD producers are CTest fixture setup tests with absolute directories under `build/wsl/runtime-captures`. Both use the offscreen path on Linux. Full RenderGraph execution scheduled each producer before its capture assertions. A focused run with `DISPLAY`, `VK_ICD_FILENAMES`, `VK_LAYER_PATH`, and `LD_LIBRARY_PATH` unset passed all 9 tests. Commit: `8bebc0fb`.
- **T-1469 — Runtime manifest:** Routed shader, device, RenderGraph session, and SVO bake caches through the build-local runtime cache, with `VIXEN_CACHE_DIR` as an override. The tracked `VIXEN/cache/global/manifest.txt` remained clean after the full app fixtures. Commit: `bb45bc77`.
- **T-1503 — Asset staging:** Apps with matching asset source/destination now depend on one shared staging target. The aggregate app build completed without concurrent staging collisions. Commit: `d770c6b2`.
- **T-1459 — AttributeStorage:** Classified the unchanged-base behavior as an engine bug: allocation used the reserved vector size as the next ID, yielding 1024 and 2048 instead of 0 and 1; `ReserveCapacity` was also killed after 45.76 seconds. A dedicated next-slot index separates reserved capacity from allocation order. Post-fix `ctest -L VoxelData` passed all 6 cases. Commit: `e928f3d7`.

## Witnesses

- Full RenderGraph selection, run serially with the caller’s Vulkan variables unset: **1,319 selected, 0 failed, 11 skipped**. Log: `build/wsl/vixinfra-rendergraph-final.log`.
- Full SVO selection, run serially with the caller’s Vulkan variables unset: **748 labeled cases; 1 disabled, 8 skipped, 1 failure**. The sole failure is `RecipeSimdParity.AllCorpusProgramsAreBitIdenticalAcrossFourLanes`, test 1930, with `M4d_Output_IsPassthrough: recipe gradient capability mismatch: 94`. This matches the known T-1449 failure in `reports/vixbase.md`. Log: `build/wsl/vixinfra-svo-final.log`.
- Focused capture selection with no `DISPLAY` or caller-side Vulkan variables: **9/9 passed**, including both fixture producers. Log: `build/wsl/vixinfra-capture-no-display.log`.
- Fresh configure command: `env -u VIXEN_FETCHCONTENT_CACHE -u FETCHCONTENT_BASE_DIR cmake --preset vixen-wsl -B ../build/vixinfra-fetchcontent-default-check -DVIXEN_SCHEMA_CATALOG=/home/liory/projects/undertow/core/src/Undertow.Authoring/Schema/schemas.json`. Reconfigure of the same tree also succeeded; `CMakeCache.txt` retains `FETCHCONTENT_BASE_DIR=.../build/vixinfra-fetchcontent-default-check/_deps`.
- `git diff --check` passed, and `git status --short VIXEN/cache/global/manifest.txt` was empty after the app runs.

## Findings and scope

- The unchanged base for T-1459 was `e0cf083a476e5f0b2df1c36d5a69bebef8e39dbd`; its two failing cases were reproduced before editing. That baseline output was captured in the lane run but not saved to a dedicated log file.
- CMake configure still prints the documented, non-fatal `Include/glslang` case-sensitive path diagnostic from `VIXEN/CMakeLists.txt`; configure and generation both exit successfully. The issue is described in `VIXEN/Vixen-Docs/05-Progress/features/consumer-feedback-undertow.md` and is outside this lane’s changes.
- The VIXEN remote has no `origin/main-wave` ref (`git fetch origin main-wave` reports no such remote ref); it exposes `origin/wave/authoring-convergence`. This lane has only the VIXEN worktree, with Undertow read-only, so no cross-repository merge was applicable.
- The required relative `bash tools/with-test-lock.sh` entry point was absent (exit 127). Tests were run through `/home/liory/.local/bin/with-test-lock.sh`, which resolves to the configured Undertow queue. The missing VIXEN-local queue entry point was proposed for consolidation.
- No design decision remains open. The known SVO opcode-94 failure is the only suite failure; no task-level STOP remains.

## CONSOLIDATION ISSUES

- proposed: VIXEN worker worktree needs a discoverable box-queue entry point
