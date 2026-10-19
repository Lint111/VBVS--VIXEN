# vnatconf lane report (vixen-native-configure)

Branch `lane-vnatconf`, on top of `origin/wave/authoring-convergence` plus lane-vixpaths.

## Per task

- T-2813 DONE d2d34f63. `VIXEN/cmake/VixenKernelSnapshot.cmake` `vixen_select_kernel_sha`, called from `VIXEN/codegen/CMakeLists.txt`. Test `Native.KernelPinOverride` (6 configures of a stub project).
- T-2868 ALREADY FIXED by lane-vixpaths (merged): `build.bat` UNC `pushd` mapping and stale-tree move-aside, `vixen_trust_wsl_git_directory`. Measured: native configure from the UNC worktree completed.
- T-2677 DONE 5d8462ea. `VIXEN/cmake/VixenChecks.cmake` `vixen_ninja_visible_launcher`, used in `VIXEN/codegen/CMakeLists.txt`. Test `Native.NinjaVisibleLauncher`. The native run printed no swap line (INFERRED: the WSL_EXE found there was already Ninja-visible), so the swap itself is proven by the unit test and the earlier Ninja stat experiment only.
- T-2818 DONE 3c00e234. `vixen_kernel_snapshot` publishes through a per-writer stage directory and `file(RENAME)` with no `file(LOCK)`. Tests `Native.KernelSnapshotPublish` (concurrent writers) and `Native.KernelSnapshotReuse` (now registered). MEASURED on Windows over UNC: the native configure published the snapshot into a worktree-local cache.
- T-2956 DONE 80219049 (PARTIAL for the root cause). `build.bat` and `VIXEN/scripts/build/run_configure_locked.ps1` take `VIXEN_BINARY_DIR`, which selects a fresh binary directory. MEASURED: native configure into `C:\vixen-native\vnatconf\build\fresh2` completed. The recompaction "Permission denied" itself was not reproduced; the fresh directory is a supported route, not an automatic recovery.

## Tests

`Native.KernelPinOverride`, `Native.NinjaVisibleLauncher`, `Native.KernelSnapshotReuse`, `Native.KernelSnapshotPublish`: 4/4 pass after. Before: three did not exist and `test_kernel_snapshot_reuse.cmake` was unregistered. Registered in `VIXEN/tests/CMakeLists.txt` (not `VixenTesting.cmake`, owned by sn-vctestser).

## SPT DISPOSITION

- T-2813: CLOSE
- T-2868: CLOSE (fixed by lane-vixpaths)
- T-2677: CLOSE
- T-2818: CLOSE
- T-2956: PARTIAL (fresh binary directory route added; root cause of recompaction failure open)

## CONSOLIDATION ISSUES

- This branch contains unlanded lane-vixpaths commits; land that lane first or together.
- `origin/main-wave` does not exist on the remote; only `origin/wave/authoring-convergence` and `origin/main` were merged (both already up to date).
- Native Windows kernel-root discovery (`VIXEN/codegen/CMakeLists.txt`, `_home_candidates`) probes `<home>/Github/Yeroket-Fantasy`, but the clone is at `~/projects/Yeroket-Fantasy` and HOME is empty on Windows. Native configure needs `-DYEROKET_ROOT` (T-4148/T-4174). I proved native configure with an untracked `CMakeUserPresets.json`, since removed.
- `VIXEN/cmake/VixenBuildCodegen.cmake` still uses `file(LOCK)` at build time and can hang on WSL paths.
- A killed configure can leave `<snapshot>.partial-<random>` stage directories behind.
- Re-passing the same `-DVIXEN_YEROKET_KERNEL_SHA_OVERRIDE` after the tracked pin moves is cleared as stale.
