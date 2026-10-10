# Windows-native provisioning (R476)

## LANDABLE NOW

### Scope and implementation

- Base: `5da61a1897c40ae2f5b7affc1a44d594c4976d24`; branch `lane-winprovision`. Merged `origin/wave/authoring-convergence` before the witness; it was already up to date.
- Read the lane rules, engine-slice common rules, and Undertow R475/R476 at `ac2875435`. R476 directs Windows tool installation through VIXEN's existing self-provisioning/build flow.
- Ran `codegraph explore` before source reads. The VIXEN worktree has no CodeGraph index; `tools/codegraph-vixen.sh` could not supply one.
- Added `VIXEN/cmake/provision-windows-native.ps1`, called by the tracked root `build.bat provision` action before CMake/VS discovery. It reads the Vulkan pin from `ProvisionVulkan.cmake`, inventories pinned versions before installing, uses exact winget package IDs/versions or LunarG's official installer, caches downloads, logs transcripts, skips matching tools, and stops with one elevated PowerShell command when admin rights are needed.
- Installer state and verified executable locations are kept under ignored `VIXEN/.win-native-toolchain/`. The launcher adds tool directories only to its own process environment. CMake MSI is explicitly told not to edit PATH (`ADD_CMAKE_TO_PATH=0`).
- Extended the existing configure/build helper scripts so a Windows shell can stay on a drvfs working directory while CMake receives the active worktree's UNC source and preset-derived binary paths.
- Updated `commands.md`, `session-setup`, and related VIXEN project rules to say “provision, then build” and derive the current worktree path instead of using `C:\cpp\...`.

### Pinned tool inventory on ws

| Tool | Version | Method | Status on ws after rerun |
|---|---:|---|---|
| Git for Windows | 2.55.0.5 | `winget Git.Git` | Present (`2.55.0.windows.5`, normalized); skipped |
| Python | 3.13.15 | `winget Python.Python.3.13`; quiet per-user install, no PATH edit | Present; installed on first successful run, skipped on reruns |
| Ninja | 1.13.1 | `winget Ninja-build.Ninja`; extract official zip to project cache | Present; installed on first successful run, skipped on reruns |
| CMake | 4.2.3 | `winget Kitware.CMake`; cached MSI, quiet install with `ADD_CMAKE_TO_PATH=0` | Missing; MSI is cached and winget verified its hash; install was not started because elevation is required |
| Visual Studio 2022 Build Tools + Windows SDK | 17.14.41 + 10.0.26100.0 | `winget Microsoft.VisualStudio.2022.BuildTools`; quiet bootstrapper with Native Desktop workload and SDK component | Missing; not reached before the CMake elevation boundary |
| Vulkan SDK | 1.4.350.1 | LunarG official installer with SHA metadata verification | Missing; the WSL pin has no matching Windows download in LunarG's Windows version feed. Windows lists 1.4.350.0 for this release. The provisioner stops for an owner pin decision. ([LunarG version/download API](https://vulkan.lunarg.com/content/view/latest-sdk-version-api), [Windows version feed](https://vulkan.lunarg.com/sdk/versions/windows.json)) |

The VS package pin was confirmed on ws with `winget show --id Microsoft.VisualStudio.2022.BuildTools --exact --versions`; 17.14.41 is available. The provisioner requires both the Native Desktop workload and SDK 26100 and checks VS with an exact version range. ([vswhere version-range syntax](https://github.com/microsoft/vswhere/wiki/Versions))

### Owner steps and STOPs

1. **Elevation:** the first successful `build.bat provision` run installed Python and Ninja, cached the CMake MSI, then stopped before starting that installer. In an elevated Windows PowerShell, run the exact command printed by the provisioner:

   ```powershell
   & '\\wsl.localhost\Ubuntu\home\liory\projects\VBVS--VIXEN\.claude-worktrees\winprovision\VIXEN\cmake\provision-windows-native.ps1'
   ```

   The next run skips Git, Python, and Ninja, reuses the CMake installer, and resumes from the first missing machine-wide tool.

2. **Vulkan pin decision:** the WSL route pins 1.4.350.1, while LunarG does not list a Windows 1.4.350.1 installer. The provisioner does not substitute 1.4.350.0. Owner must choose whether Windows may use 1.4.350.0, or whether to select a shared version available on both platforms. The WSL pin was left unchanged.

3. **Native witness stopped at provisioning:** Windows-native configure/build, Windows RenderGraph and SVO suites (opcode 94 only), and the AMD-driver editor capture were not run. ws had no Visual Studio Build Tools, CMake, Ninja, or Vulkan SDK at baseline; provisioning stopped at the first admin-required installer. Capture and suite evidence therefore remain pending the owner steps above.

This is the task's starting red and an expected owner boundary, not an unrelated pre-existing failure or an unobtainable baseline.

### Verification evidence

- **Unchanged native baseline:** queued `build.bat configure vixen-ninja` on base SHA `5da61a1` exited 1 because `vswhere.exe` was absent at `C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe`. Log: `/home/liory/.local/state/undertow/undertow-box-logs/1791632361-build-winprovision:baseline-native-configure-powershell.log`.
- **Provision run and resume:** transcript `VIXEN/.win-native-toolchain/logs/provision-20261010-144602.log` shows Python and Ninja installed, the CMake MSI downloaded and hash-verified, and the admin stop before installation. `provision-20261010-144656.log` and `provision-20261010-151442.log` show Git/Python/Ninja present and skipped, and the cached CMake MSI reused. A full “all present/no installs” second run is not yet possible because the owner step remains.
- **PowerShell syntax:** Windows PowerShell parser accepted the provisioner and both changed launcher helpers after the final edits.
- **WSL configure:** queued `cmake --preset vixen-wsl` from `VIXEN/` completed with exit 0. Log: `/home/liory/.local/state/undertow/undertow-box-logs/1791633089-build-winprovision:wsl-configure-correct-cwd.log`. A preliminary invocation from the repository root failed to find `CMakePresets.json`; rerunning from the documented `VIXEN/` source directory succeeded.
- **WSL build:** queued `cmake --build build/wsl --parallel 4` from the worktree root completed with exit 0 (`UT_JOB_DONE build winprovision:wsl-build exit=0 state=OK`). Log: `/home/liory/.local/state/undertow/undertow-box-logs/1791633265-build-winprovision:wsl-build.log`.
- The full WSL build log contains exactly **22** `[codegen] golden check` steps, matching the 22 `vixen_add_codegen_check(... ALL ...)` targets in `VIXEN/codegen/CMakeLists.txt`; all passed. The separate no-new-mutex gate passed with two unclassified-declaration warnings. The build compiled test binaries but did not run the WSL test suite.
- `git diff --check` passed. The changed PowerShell files parsed cleanly. A scan of the relevant `.claude` rules found no `C:\cpp` paths or duplicate `repo_win` assignments.

### Recovery ladder record

- **A — Invocation/provisioning:** checked the required global queue path and the Windows-native launcher route. Preliminary malformed shell-quoted baseline calls were corrected before recording the native baseline. The task-provided provision action now reaches the official installers and stops at the required admin boundary.
- **B — Regeneration:** the WSL configure regenerated its build tree successfully; the fresh WSL build completed. Windows-native generation was not attempted without the required compiler/toolchain.
- **C — Isolation:** not applicable. The missing Windows toolchain is within this task's verification scope and is not being classified as an unrelated or pre-existing red.

## SPT DISPOSITION

Created proposal `3c4f45b77113` in `.spt-proposals/winprovision.jsonl` and included it for landing. It records the manual source/build path handling needed because the Windows launcher must remain on drvfs while the worktree is reached through WSL UNC.

## CONSOLIDATION ISSUES

- proposed: Native launcher depends on UNC current directory for worktree builds
