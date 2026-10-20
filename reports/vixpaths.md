# vixpaths lane

Branch `lane-vixpaths` (off `origin/wave/authoring-convergence`; already up to date with `origin/main`).
Raw logs: `/home/liory/codeman-cases/undertow/reports/vixpaths-evidence/`.

## Changes

| Task | Commit | Where | Test |
|---|---|---|---|
| T-2678 | 8c1f6348, 0e97b3f7 | `VIXEN/cmake/ProvisionWslVulkan.cmake:23` | `Native.WslDozenHostGate` |
| T-2833 | 16ca88b0, 46ee83b7 | `VIXEN/CMakeLists.txt:30,615`, `VIXEN/dependencies/CMakeLists.txt:363`, `VIXEN/cmake/VixenWindowsGitTrust.cmake` | `Native.WindowsGitTrust` + FetchContent witness |
| T-2626 | 7d4185a4 | `VIXEN/cmake/VixenKernelSnapshot.cmake:55` | kernel-snapshot witness |
| T-2646 | 1546d335 | `build.bat:73` (UNC mapping), `build.bat:125-135` (stale tree) | end-to-end `build.bat configure vixen-ninja` via UNC |

## Measured

- Windows ctest on the native tree: `Codegen.LauncherArguments`, `Native.WslDozenHostGate`, `Native.WindowsGitTrust` 3/3 pass.
- The same three scripts pass under WSL Linux.
- `build.bat configure vixen-ninja` through `\\wsl.localhost\...` configures to completion under VS CMake 3.31.6; a UNC-keyed stale tree is moved to `build/ninja.stale-1`.
- Red-to-green witnesses exist for the Dozen gate, FetchContent cache trust and kernel snapshot trust.

## Inferred / not run

- No full Linux VIXEN configure or build (Vulkan SDK and Mesa provisioning); the touched tests are `cmake -P` scripts.
- No GPU run; none of the changes touch rendering.
- T-4641 (shared WSL/Windows fetch cache) was not touched.

## SPT DISPOSITION

- T-2626: CLOSE
- T-2833: CLOSE
- T-2646: CLOSE
- T-2678: CLOSE
