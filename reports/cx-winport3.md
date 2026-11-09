# cx-winport3

## STATUS

Landable. The lane started at `a4bc503f` (`lane-landvx1`) and is committed as `5df0b8e3` (`fix VIXEN native MSVC build targets`). The required `origin/wave/authoring-convergence` merge was already up to date before the final witness.

## FIXES

- `VIXEN/libraries/RenderGraph/tests/test_shader_source_dir.cpp:28`: pass `dir.string().c_str()` to the narrow `ScopedEnvironment` API, fixing MSVC's `path::c_str()` wide-string mismatch. Owner: RenderGraph test portability.
- `VIXEN/CMakeLists.txt:474-479`: set C++23, required, with extensions disabled at the project level. This gives closure and run-data targets the standard they previously lacked. Owner: VIXEN project CMake configuration.
- `VIXEN/application/editor/CMakeLists.txt:54-56`: add `/bigobj` to the shared MSVC `vixen_editor` target options. Owner: editor target configuration.
- `VIXEN/application/main/tests/test_lookdev_capture.cmake:498-541`: keep the ELF `dlfcn`/`popen` closure recorder, manifest, and closure targets behind `if(NOT WIN32)`. The look-dev capture targets remain native; the POSIX-only closure machinery is not presented to MSVC. Owner: look-dev closure CMake graph.
- The added ShaderManagement `<unistd.h>` issue was already resolved in the base by `6c23635e` using `RuntimeCachePaths.h` and `Vixen::CurrentProcessId()`. This lane made no duplicate change and verified both SDI sources in the native and Linux builds.
- No goldens were re-recorded.

## WITNESS

- Native Debug: queued `tools/vixen-native.sh ... build all vixen-ninja` with `VIXEN_BUILD_LOCK_TIMEOUT=14400`; 1364/1364 targets built successfully. Evidence: `/home/liory/codeman-cases/undertow/reports/cx-winport3-evidence/native-debug-all.log`.
- Native Release: queued `tools/vixen-native.sh ... build all vixen-ninja-release` with `VIXEN_BUILD_LOCK_TIMEOUT=14400`; 1364/1364 targets built successfully. Evidence: `/home/liory/codeman-cases/undertow/reports/cx-winport3-evidence/native-release-all.log`.
- Linux: queued `bash build-wsl.sh -j4`; 1368/1368 steps built successfully, including the repaired RenderGraph, ShaderManagement, editor, and run-data targets. Codegen and golden checks passed.
- CPU suite: queued `ctest -L "^cpu$" --output-on-failure`; 1832/1832 passed, 0 failed. Evidence: `/home/liory/codeman-cases/undertow/reports/cx-winport3-evidence/ctest-cpu.log`. The 1288 named passed tests match the lander after normalizing the checkout-specific `vixen_asset_bundle_verify_<hash>` name.
- Proposal inbox validation passed for both lane proposals.

## LANDABLE NOW

Yes. Commit `5df0b8e3` contains the product fixes. The follow-up report and `.spt-proposals/cx-winport3.jsonl` contain the witness record and incidental tooling findings and are ready to commit with this lane.

## REDS

No required-scope red remains. The first native attempt stopped before compilation when pinned Ninja provisioning received a `winget` HTTP 504; recovery staged the official Ninja 1.13.1 archive in the provisioner's documented cache. The next `build build vixen-ninja` attempt stopped before compilation because the native binary directory did not exist; recovery used the wrapper's configure-plus-build action, `build all vixen-ninja`. These recoveries are recorded as consolidation proposals.

The Windows closure manifest remains intentionally excluded because its recorder is ELF/POSIX-only; Linux builds and verifies that path. Native full builds and the required CPU suite are green.

## STOPs

None. Recovery was successful, the required scoped baseline and final witnesses are available, and no external design decision is needed.

## CONSOLIDATION ISSUES (non-facade deltas this lane needed)

- proposed: Make native Ninja provisioning retry or fall back from winget gateway failures
- proposed: Make vixen-native build configure an unconfigured native tree
