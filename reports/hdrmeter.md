# HDR exposure meter lane report

Base: `5dd78d8675160314f5831e4b3d10b3fdefd8c164` (`lane-hdrmeter`, VIXEN Windows native checkout at `/mnt/c/vixen-native/hdrmeter`). The fetched `origin/wave/authoring-convergence` tip is the same commit; the required merge was already up to date.

## LANDABLE NOW

- Added optional per-pass CSV columns for `exposure_meter`, `exposure_reduce`, and `exposure_tonemap` in `VulkanGraphApplication.cpp`. The app translation unit compiled in the base Release build, but the app did not link because required dependencies failed. These columns are needed to locate the reported 1.1 ms meter cost once the native app is buildable.
- All 22 VIXEN CMake codegen checks passed on the pinned kernel snapshot after the documented regeneration recovery. The first run had 21 passes and a stale `RecipeSimd.g.hpp`; `recipe_simd_regen` reported zero semantic lines changed, and the fresh `recipe_simd_check` passed. The generated blob hash matches the base blob; the initial stale result was caused by CRLF/LF handling on the `/mnt/c` checkout.
- The requested exposure mirror test has not run. Its source uses `assert()`, while its CMake target does not undefine `NDEBUG`; the native Release compile would disable those assertions. An assertion-live target configuration is needed for that witness.
- The requested meter optimization, default-on switch, native timings, after contact sheets, and assertion-live exposure mirror witness are not landable from this baseline. No semantic HDR edits were made because the required native Release app baseline could not be established.

## STOPs

**STOPPED: baseline unobtainable: the native Release `VixenApp` target fails in its required SVO dependency.** The checked-in output generated from pinned kernel commit `c3fb3847979e3f9eef04783510c42b70d7058ae7` declares local `value` variables that shadow function parameters at `RecipeSimd.g.hpp:2831, 2849, 2858`. MSVC 14.44 emits C4457, promoted to C2220 by `/W4 /WX`. This repeats after the declared generator runs and the 22 generation checks pass. `VixenApp` depends on SVO, so this failure cannot be excluded from the native timing scope. The emitter lives in the pinned kernel CodegenTool, outside the two assigned VIXEN worktrees.

Required verification scope is: native Release `VixenApp` and dependencies; per-pass GPU timings with exposure enabled at 500×500 and 1920×1080; the exposure mirror test with assertions live; and the 22 CMake generation checks. The 22 checks are green. The build, native timing, look-dev-after, and mirror-test witnesses are incomplete.

### Recovery record

- **Base and first red:** base SHA `5dd78d8675160314f5831e4b3d10b3fdefd8c164`. The full queued native Release build (`hdrmeter:base-full-build-retry`) was stopped by the box idle watchdog at 199 seconds (queue exit 75); its detached Windows child advanced to 811/1,152 targets and recorded six failures before it was terminated. Logs: `/home/liory/.local/state/undertow/undertow-box-logs/1791708489-build-hdrmeter:base-full-build-retry.log` and native `.tmp/native-temp/vixen_build_hdrmeter-base.log`. Failures included the SVO C4457, the RenderGraph aligned-storage C2338, a VoxelDocument translation unit, and three RenderGraph test translation units.
- **A — invocation and provisioning:** the assigned native checkout did not exist, so it was created at the assigned path from the base. The first queued `cmd.exe` attempt used a WSL UNC working directory and an unquoted Windows path; it exited 1 before CMake. `build.bat provision; build.bat all vixen-ninja` then reported no VS C++ toolchain despite VS 2022 being installed. The native helper also rejected an unsupported `-StreamProgress` option. Recovery used the installed `vcvars64.bat`, a worktree-local native preset, the supported `run_configure_locked.ps1`/`run_build_with_summary.ps1`, a quoted `C:\` batch path, and the WSL-visible Yeroket root `//wsl.localhost/Ubuntu/home/liory/projects/Yeroket-Fantasy`. A cleanup configure initially looked for presets from the repository root and exited 1; rerunning from `VIXEN/` succeeded. The helper’s silent log polling caused the queue watchdog to detach its Windows child; subsequent scoped CMake builds streamed progress directly through the box queue. The final native cache has the default `/EHsc` flags restored.
- **B — fresh generation:** the first scoped run of the 22 codegen targets found only `recipe_simd_check` stale. `recipe_simd_regen` completed and reported no semantic line changes in its four declared outputs. The full target set was rerun: the other 21 checks had passed in the first run, and the regenerated SIMD check passed on the second run. No generated semantic churn is included in the primary worktree.
- **C — base proof and scope:** a fresh scoped `VixenApp` Release build on the unchanged base reproduced SVO C4457 and RenderGraph C2338 (`hdrmeter:base-app-target`, exit 2; log `/home/liory/.local/state/undertow/undertow-box-logs/1791709954-build-hdrmeter:base-app-target.log`). The app’s `VulkanGraphApplication.cpp` translation unit compiled, then Ninja stopped because dependencies failed. `clang-cl.exe` is not installed (`where clang-cl.exe` found none). A local attempt to apply MSVC’s documented `_ENABLE_EXTENDED_ALIGNED_STORAGE` option through `CMAKE_CXX_FLAGS` replaced the default `/EHsc` and introduced C4530; that retry was stopped at 128/469 targets. The override was removed from the local preset, and a queued configure is restoring the base flags. This does not change the reproduced C4457 blocker. No warning was suppressed and no generated file was hand-edited.
- **Owner action needed:** update the pinned `RecipeSimdEmitter` to emit non-shadowing local names, then regenerate and build the native VixenApp dependency graph. If the aligned-storage C2338 remains after that, enable the compiler-documented extended alignment in the supported Windows configuration without replacing the default exception flags.

The initial native look-dev references and pre-existing contact sheets were opened. The midday sheet reads flat and bright against the reference; the night sheet keeps the machine, worker, and water faint where the reference shows stronger sodium-lit pools. Existing captures measured in `reports/lookdev.md` include midday p95 0.859, late-afternoon p95 0.816, overcast p95 0.893, and night p95 0.284; they are pre-change context only. No new after contact sheet was produced, so there is no visual after result to describe.

R494’s 0.5 ms tone-map/exposure cap was read. R505 was not present in `/home/liory/projects` or `/home/liory/scripts/codex-briefs`; this brief supplies the required scope and behavior.

## SPT DISPOSITION

Thirteen proposals were appended to `.spt-proposals/hdrmeter.jsonl` for incidental instrumentation, Windows-native provisioning, queue supervision, DrvFS metadata, generated line endings, compiler-flag setup, the Release assertion gap, and preset-directory selection. They are committed with this report.

## CONSOLIDATION ISSUES

- Expose HDR passes in the native performance CSV
- Keep silent native builds alive under the box queue
- Make the native build entry point use the installed Visual Studio toolchain
- Resolve the Yeroket kernel root in native WSL-backed checkouts
- Provision the assigned native checkout with the lane base
- Keep native build helper parameters compatible across wave bases
- Quote Windows paths in queued native build invocations
- Ignore DrvFS executable-bit noise in Windows native worktrees
- Preserve canonical line endings in generated native outputs
- Enable MSVC extended aligned storage in the native preset
- Preserve MSVC exception flags when adding native CXX options
- Keep the exposure mirror assertions live in Release builds
- Run native CMake presets from the VIXEN source directory
