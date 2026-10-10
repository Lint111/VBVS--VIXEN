# Vulkan version capabilities and Windows provisioning (R481/R482)

## LANDABLE NOW

### Scope and implementation

- Worktree branch: `lane-winprovision`, starting HEAD `62ea02944d4dbb0f653d756242645da7d972b157`. The current `origin/wave/authoring-convergence` (`742fddb69d9093673f0942845e880b55df257757`) is already an ancestor, so the required wave merge was present before the final witness.
- The required `codegraph explore` query was attempted first, but this worktree has no `.codegraph` index; the helper said not to initialize one. Source inspection continued manually.
- The single settings file is [`VIXEN/cmake/vulkan-sdk-settings.env`](../VIXEN/cmake/vulkan-sdk-settings.env). It declares Windows SDK default `1.4.350.0`, WSL default `1.4.350.1`, other-platform default `1.4.350.1`, minimum Vulkan API `1.2.0`, synchronization2 core-promotion threshold `1.3.0`, and minimum SPIR-V target `1.4.0`. The process environment variable `VIXEN_VULKAN_SDK_VERSION` overrides the platform default.
- CMake, WSL provisioning, and the Windows PowerShell provisioner read that file. Configure logs show expected/found SDK, API header, glslang, and SPIR-V target versions. The Windows provisioning report prints the declared setting and capability floors.
- `CapabilityGraph` now carries Vulkan API, SDK, glslang, and SPIR-V version inputs. Its synchronization2 node checks the required API floor, the feature bit, and either Vulkan 1.3 core promotion or `VK_KHR_synchronization2` on Vulkan 1.2. `VulkanDevice` queries/enables the matching feature struct and resolves promoted entry points. `RayQueryLighting` depends on the SPIR-V floor and continues to resolve through `ResolveOptionalPath` to its independent twin when the target is too old.
- CMake checks the installed SDK release and API headers, requires the synchronization2 extension below the shared core threshold, and reports a ray-query fallback when the SPIR-V target is below its floor. The matrix is updated at [`capability-requirement-matrix.md`](../VIXEN/Vixen-Docs/01-Architecture/capability-requirement-matrix.md).

### Windows tool inventory on ws

The resumed queued `build.bat provision` inventory found:

| Tool | Version | Method | Status on ws |
|---|---:|---|---|
| Git for Windows | 2.55.0.5 | Exact `winget Git.Git` pin | Present; skipped |
| Python | 3.13.15 | Exact `winget Python.Python.3.13` pin | Present; skipped |
| Ninja | 1.13.1 | Exact `winget Ninja-build.Ninja` pin; project cache | Present; skipped |
| CMake | 4.2.3 | Exact `winget Kitware.CMake` pin; cached MSI | Missing; MSI reused, install not started because elevation is required |
| Visual Studio Build Tools + Windows SDK | 17.14.41 + 10.0.26100.0 | Exact `winget Microsoft.VisualStudio.2022.BuildTools` pin | Missing; not reached before the CMake elevation boundary |
| Vulkan SDK | 1.4.350.0 | LunarG official installer with SHA-256 validation | Missing on Windows; not reached before the CMake elevation boundary |

### Owner step and pending Windows witness

The provisioner printed this exact command and did not start the CMake MSI:

```powershell
& '\\wsl.localhost\Ubuntu\home\liory\projects\VBVS--VIXEN\.claude-worktrees\winprovision\VIXEN\cmake\provision-windows-native.ps1'
```

The owner has not completed that elevated step in this run, so an all-present/no-installs rerun is still pending. After it is run, resume `build.bat provision`; then run the Windows-native `build.bat configure vixen-ninja` and `build.bat build vixen-ninja` witnesses, the RenderGraph and SVO suites on the AMD driver (opcode 94 only), and one editor capture and open it. Those Windows-native results remain pending the owner step.

### Verification

- **Baseline before semantic edits:** queued WSL configure and full build passed (`1791636256-build-baseline-wsl-configure.log`, `1791636267-build-baseline-wsl-build.log`). Final main-tree configure also passed with expected/found SDK `1.4.350.1`, API headers `1.4.0`, glslang `16.3.0`, and SPIR-V target `1.6.0` (`1791638331-build-version-configure-after-contract.log`). The existing non-fatal `Include/glslang` case diagnostic appeared in both configure runs.
- **Focused C++ witness:** queued `test_vulkanresources_basic` target build passed (`1791638424-build-version-target-build-after-contract.log`). The filtered `VersionRequirementsSelectIndependentPathsByToolchainAndApi` test passed, covering API 1.1 rejection, API 1.2 with and without the KHR extension, API 1.3 core, and old/new SPIR-V paths (`1791638431-test-version-capgraph-after-contract.log`).
- **SDK setting probes:** expected `1.4.350.0` with installed `1.4.350.1` was accepted and validated, including the nested LunarG loader (`1791638143-build-version-patch-override-retry.log`). Expected `1.4.351.0` with found `1.4.350.1` failed with the expected/found versions and override instructions (`1791638352-build-version-sdk-mismatch-probe.log`).
- **Capability static probes:** SDK headers exposing API 1.1 failed configure with the explicit API 1.2 no-twin message (`1791638263-build-version-no-twin-probe.log`). A glslang probe reporting SPIR-V 1.3.0 configured successfully and reported “Fallback in use” for `RayQueryLighting` (`1791638407-build-version-fallback-probe-final.log`).
- **Final WSL build:** queued `cmake --build build/wsl --parallel 4` passed all 260 Ninja steps, including the no-new-mutex gate. That gate passed with the same two unclassified warnings recorded at baseline (`1791638437-build-version-full-wsl-build-final.log`).
- **22 codegen checks:** queued `cmake --build build/vulkan-version-probes/patch-override --target codegen/all --parallel 4` passed all 22 golden checks (`1791639543-build-version-codegen-checks-fresh.log`). This fresh check tree used the SPIR-V 1.3 probe; the schema/codegen checks do not depend on the simulated Vulkan tool version. The real WSL SDK configure and full build were separately verified in `build/wsl`.
- **Syntax and hygiene:** Windows PowerShell parser accepted `provision-windows-native.ps1`; `bash -n` accepted the WSL provisioner; `CMakePresets.json` parsed as JSON; `git diff --check` passed. The SPT inbox JSONL is valid.
- The first queued Windows invocation used bare `cmd.exe` and exited 127 because this SSH shell omits Windows System32 from `PATH`. Retrying with `/mnt/c/Windows/System32/cmd.exe` reached the intended CMake elevation stop (`1791638019-build-resume-win-provision-explicit-cmd.log`).
- The first external-SDK configure probe found the headers but not the LunarG 1.4.350 nested loader path. `ProvisionVulkan.cmake` now resolves that existing SDK layout before `find_package(Vulkan)`; the retry passed. The incidental manual steps are recorded in the SPT inbox.

## CONSOLIDATION ISSUES (non-facade deltas this lane needed)

- proposed: WSL provisioning needs Windows command discovery from SSH shells
- proposed: External Vulkan SDK loader lookup misses LunarG's nested Linux loader
