# rjs2 — S2 exposure tone map and native parity

## LANDABLE NOW

- Continuation tip: see the final response for the commit SHA. This is a partial checkpoint; S2 is not complete or acceptance-ready.
- The per-build kernel override `a97c273ff4312dc09de16ce33dc8f1a06dcedd14` emits `ExposureTonemapSystem.comp` and `ExposureTonemapSystem.g.h`. The tracked VIXEN kernel pin remains unchanged.
- `FrameTargets` schema/reference checks and the tone-map codegen drift check pass. The old reference shader's SDI header was also generated.
- No parity sheet or native timing was produced. The render-graph wiring and 32 one-frame A/B cases remain unbuilt and unwitnessed; no look-dev A/B captures or split suites ran.

## STOPs

**STOPPED: the pinned kernel GPU emitter emits an invalid GLSL definition for the `LightingConfig` storage-buffer element.** Its shader contains `struct Light` inside the `LIGHTINGCONFIG_GLSL` block and emits `struct Light` again under `LIGHT_GLSL`. The SDI merger compiles the emitted GLSL and rejects the second definition:

```text
ERROR: shader:43: 'Light' : redefinition struct
ERROR: shader:48: '' : missing #endif
ERROR: shader:48: '' : compilation terminated
```

The array lowering gap from the previous stop is resolved: GPU-system generation now succeeds and emits the shader and C++ binding header. The new stop is downstream. `sdi_tool` cannot emit `ExposureTonemapSystem-SDI.g.h` because it cannot compile the system shader; consequently `BuildRenderGraph.cpp` cannot compile. This generation behavior is in the pinned kernel (`CodegenTool~/Wizard/GaiaGpuSystemModule.cs`, `GaiaGpuSystemEmitter.EmitGlsl` collects both a struct and its nested structs). The kernel worktree was used as the requested source for a build-tree snapshot; its tracked VIXEN pin was not changed.

The shader compile failure came from the queued command below, run from the VIXEN root (queue resource `light`, exit 4; log `/home/liory/.local/state/undertow/undertow-box-logs/1791894020-light-rjs2:sdi-merge-regenerate-with-local-root.log`):

```bash
../build/wsl/bin/sdi_tool merge-variants shaders/sdi-variants.json
```

The first merger attempt failed to resolve `Generated/LightingConfig.glsl` because the manifest did not include its local shader root (`1791893978-light-rjs2:sdi-merge-regenerate.log`). Adding `.` to the manifest's `includePaths` fixed that lookup: the reference shader compiled and its SDI header was generated, then the system shader hit the duplicate `Light` error. The manifest include-root change and its consolidation proposal are committed with this checkpoint.

The follow-up application build also fails at the missing system SDI header (queued build, exit 1; log `/home/liory/.local/state/undertow/undertow-box-logs/1791894181-build-rjs2:wsl-lookdev-build-after-sdi.log`):

```text
fatal error: merged/ExposureTonemapSystem-SDI.g.h: No such file or directory
```

**Required-scope recovery:**

- Base SHA: `2685b1e980a052f85b938d7ac31b47f5930e015f`. `origin/wave/authoring-convergence` was fetched and merged; it was already up to date. A fresh queued baseline build of `vixen_lookdev_capture` passed before semantic edits (`1791886709-build-rjs2:baseline-renderer-build.log`).
- **A — invocation and provisioning:** the first continuation configure used an incorrect full SHA and failed `git cat-file` (`1791893659-light-rjs2:configure-gpuarray-override.log`). Resolving `a97c273f` in the named kernel worktree gave `a97c273ff4312dc09de16ce33dc8f1a06dcedd14`; reconfiguring with `-DVIXEN_YEROKET_KERNEL_SHA_OVERRIDE=<full SHA>` and `-DYEROKET_ROOT=<kernel worktree>` passed (`1791893710-light-rjs2:configure-gpuarray-override-retry.log`). No tracked pin was edited.
- **B — fresh generation:** queued `exposure_tonemap_gpu_regen` passed and emitted the system GLSL/header (`1791893727-light-rjs2:regen-tonemap-system-gpuarray.log`). `frame_targets_schema_check`, `frame_targets_ref_check`, and `exposure_tonemap_gpu_check` passed (`1791893821-light-rjs2:gpuarray-codegen-checks.log`). The merger's first include-root failure was recovered as described above; the remaining duplicate-type compile failure is not an invocation or provisioning issue.
- **C — scope:** the missing system SDI header is a required input to the graph build and to the native parity harness. There is no supported narrower native capture or timing witness for the pass without it. This is a new required-scope kernel-emitter failure, not an unrelated baseline red. The shader must emit each nested GLSL struct exactly once before S2 can continue.

## Witnesses

- Passed: baseline `vixen_lookdev_capture` build; override configure; tone-map system generation; `frame_targets_schema_check`, `frame_targets_ref_check`, and `exposure_tonemap_gpu_check`.
- Passed: SDI generation for `ExposureTonemapReference` after adding the local shader root to the manifest.
- Failed: `sdi_tool merge-variants` compiling `ExposureTonemapSystem` due to the duplicate `Light` struct; subsequent `vixen_lookdev_capture` build fails because the system SDI header is absent.
- Not run: native Vulkan build/capture, byte parity, startup/per-frame timing, five-run `--gpu-benchmark`, look-dev A/B, and split test suites. No WSL performance numbers are reported.

## SPT DISPOSITION

- No SPT task IDs were dispatched in this brief. The proposals below are consolidation issues, not task IDs.

## CONSOLIDATION ISSUES

- proposed: Support scoped schema overlays for pinned source readers
- proposed: Launch dotnet run output from artifacts-path
- proposed: Expose source-resolvable GLSL math intrinsics
- proposed: Derive GPU dispatch extents from texture facets
- proposed: Default the SDI merger to the manifest shader root
