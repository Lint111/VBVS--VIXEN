# VIXEN tracked kernel pin refresh

## LANDABLE NOW

`8d68e9839ac3af02b937b2b419c27f829e90cccc` — VIXEN now tracks the current Yeroket `origin/main` kernel commit for its codegen checks.

## Base and cause

Base: `fc69e0c33be2321f52c0c1fb3c260d42a7de88a4` on `lane-vixkpin`, off `origin/wave/authoring-convergence`.

The old tracked pin was `a2b2d6b9e1e93b37b1adc0d7dd99e1c63bf96bad`. P692 changed Undertow's `Leveraged.Source` schema index from `unique: true` to `unique: false` in `core/src/Undertow.Authoring/Schema/schemas.json:166`; the row's `F` and `R` faction references can repeat for multiple leverage occurrences. The old CodegenTool rejected that current catalogue during `appflow_check` with `RuntimeState 'Undertow.Content.Core.Systems.Diplomacy.Leveraged' index 'Source' must be unique with owner visibility.`

Fresh old-pin baseline: configure exited 0 and printed the discovered Undertow catalogue plus the old tracked SHA. The queued `appflow_check` command exited 1:

```text
bash /home/liory/.local/bin/with-test-lock.sh --agent vixkpin --resource light --label appflow-old-pin -- nice -n 10 cmake --build ../build/wsl --target appflow_check -j4
```

The failure log is `/home/liory/.local/state/undertow/undertow-box-logs/1791411063-light-appflow-old-pin.log`. It reaches `CodegenModuleDefinition.Run` at `ModuleRegistry.cs:177`, `ModuleRegistry.TryDispatch` at line 309, and `Program.RunDispatch` at line 929 before aborting. This reproduces the supplied editorbase log against this lane's base and current Undertow catalogue.

## Change

Changed only `_vixen_tracked_yeroket_kernel_sha` in `VIXEN/codegen/CMakeLists.txt:132`, from `a2b2d6b9e1e93b37b1adc0d7dd99e1c63bf96bad` to `8d68e9839ac3af02b937b2b419c27f829e90cccc`. The remote `Yeroket-Fantasy` `origin/main` resolves to that same SHA. The derived `YEROKET_KERNEL_SHA` assignment was left untouched.

A fresh reconfigure printed `[codegen] using tracked Yeroket kernel pin: 8d68e9839ac3af02b937b2b419c27f829e90cccc` and the current Undertow schema catalogue path. The updated `appflow_check` passed. All 22 distinct `*_check` targets passed, and the fresh full ALL build completed all 1118 steps with exit 0. After the old-pin capture comparison, all 22 checks passed again on the restored tracked pin. No generated VIXEN source outputs changed.

## Witness

- `ctest -L RenderGraph`: 1339 tests passed, 0 failed; 11 configured skips.
- `ctest -L SVO`: 752 labeled tests; 751 passed, 8 skipped, 1 disabled. The sole failure was `RecipeSimdParity.AllCorpusProgramsAreBitIdenticalAcrossFourLanes`, with `M4d_Output_IsPassthrough: recipe gradient capability mismatch: 94`. This is the established opcode-94/T-1449 failure also recorded in `reports/vixpin.md`; the other runnable SVO tests passed.
- The focused capture-related selection passed 11/11 with caller `DISPLAY`, `WAYLAND_DISPLAY`, `VK_ICD_FILENAMES`, `VK_LAYER_PATH`, and `LD_LIBRARY_PATH` unset. `vixen_wsl_capture_witness` passed twice on the tracked pin. For a direct old/new pin-selection comparison, I selected the parent pin through `VIXEN_YEROKET_KERNEL_SHA_OVERRIDE`, captured to `build/wsl/runtime-captures/baseline-parent-pin`, cleared the override, reconfigured to the tracked `8d68e9839ac3af02b937b2b419c27f829e90cccc` pin, and captured to `build/wsl/runtime-captures/native`. The runtime executables were the same in both captures because the old-pin `appflow_check` is the failing regression and prevents a successful full old-pin build. `tools/compare-capture-pixels.py --require-byte-identical` found all 7 PNGs byte-identical with zero pixel differences.
- The final tracked-pin full ALL build exited 0; Ninja reported `no work to do` after its directory-glob recheck, with zero compile/link units. A separate no-op rebuild also exited 0 with `ninja: no work to do`.
- Configure provisioned the Linux Vulkan SDK and X11 dependencies in this worktree. The SDK's lowercase `include/glslang` required the documented local `Include -> include` symlink because VIXEN checks an uppercase path. The first configure submitted as a build resource waited over two minutes for admission and was canceled before starting; the same configure completed through the light queue. The configure used the required worktree-local `VIXEN_FETCHCONTENT_CACHE` environment value.
- The CodeGraph query and `tools/codegraph-vixen.sh` reported that this disposable worktree has no CodeGraph index. I did not initialize one.
- KFR follow-up: not applicable; this CMake codegen pin change does not alter renderer graph assembly or runtime behavior.

The new CodegenTool bootstrap emitted .NET `NETSDK1188` package-locale warnings from Microsoft.CodeAnalysis 4.8.0. The full C++ build also reported warnings in unchanged third-party and test sources (Gaia, `test_gpu_query_manager.cpp`, and SVO scene-generator tests); the changed CMake line produced no compiler warning.

## STOPs

None in the requested scope. The updated pin clears the AppFlow abort. The one SVO opcode-94 failure is the already documented T-1449 baseline result.

## SPT DISPOSITION

No existing SPT task was closed. Four consolidation proposals were appended to `.spt-proposals/vixkpin.jsonl`.

## CONSOLIDATION ISSUES

- proposed: Track VIXEN's codegen kernel pin with kernel landings
- proposed: Make the Vulkan SDK glslang include lookup case-tolerant
- proposed: Classify CMake configure jobs separately from full builds in the box queue
- proposed: Default VIXEN FetchContent to a worktree-local cache
