# rjs2 — S2 exposure tone map and native parity

## LANDABLE NOW

- Tip: this lane's stop commit; the structured final report records its SHA.
- The `FrameTargets` schema declaration and wizard-generated references have passing drift checks. They are a verified partial delta, not a complete S2 implementation.
- Parity sheet: not produced. The generated tone-map system GLSL/header are blocked, so no valid reference/system pair was captured.
- Native startup, per-frame time, and GPU timing: not measured. No native capture or benchmark jobs ran.
- The render-graph wiring and 32 one-frame capture cases are committed as unfinished continuation work; they were not built or witnessed.

## STOPs

**STOPPED: pinned GPU-system codegen cannot lower the tone-map's singleton SSBO inputs.** The queued invocation of the pinned `CodegenTool.dll` with `--gaia-gpu-system ExposureTonemapSystem` returned:

```text
GLSL translation failed: GlslAstVisitor: unsupported type '' — no GLSL mapping.
```

The authored body takes `in float[] meter` and `in LightingConfig[] LightingConfigSSBO`, then reads element zero from each. In the pinned kernel at `7a2b1f1e83310b7129fdba19975235ef94198757`, `GaiaGpuSystemModel.GlslParameterType` maps scalar types and otherwise returns `ITypeSymbol.Name`; Roslyn array symbols have no simple name, yielding the empty type reported by the visitor. The GPU entrypoint also passes storage-buffer elements by `linearId`, so the current body contract cannot express these pass-wide element-zero reads. This is in the required kernel dependency and cannot be excluded from the S2 gate. Lane rjs1's loop and `[Relaxed]` work does not cover this gap, and no kernel workaround was added here.

Exact failed command (queue resource `light`, exit 1; log `/home/liory/.local/state/undertow/undertow-box-logs/1791888420-light-rjs2:direct-exposure-system-regeneration-import.log`):

```bash
dotnet build/wsl/codegen/tool/CodegenTool.dll --schema VIXEN/codegen/gpu-systems --schema VIXEN/codegen/generated/gpu --schema /home/liory/.cache/vixen/kernel-codegen/sources/yeroket-7a2b1f1e83310b7129fdba19975235ef94198757/Packages/com.yeroket.utility.kernel-framework/Runtime --schema /home/liory/projects/undertow/core/src/Undertow.Sim.Framework --schema VIXEN/codegen/config-schemas --schema-json VIXEN/codegen/generated/gpu/frame-targets.schemas.json --gaia-gpu-system ExposureTonemapSystem --out-glsl VIXEN/shaders/Generated/ExposureTonemapSystem.comp --out-header VIXEN/application/main/include/Generated/ExposureTonemapSystem.g.h
```

## Required-scope recovery

- Base SHA: `2685b1e980a052f85b938d7ac31b47f5930e015f` (`lane-rjs2`, already at `origin/wave/authoring-convergence`). A fresh queued `vixen_lookdev_capture` target build passed before semantic edits.
- Baseline build command: `cmake --build build/wsl --target vixen_lookdev_capture --parallel 4`, through the global queue, exit 0 (`1791886709-build-rjs2:baseline-renderer-build.log`).
- First red: queued `cmake --build build/wsl --target frame_targets_schema_regen --parallel 2` used only VIXEN's local schema directory and failed to resolve `Undertow.Authoring.KindSchemas.RuntimeStateFormats` (`1791887603-light-rjs2:regen-frame-targets-schema.log`).
- **A — invocation and source recovery:** the global queue and pinned CodegenTool build were available. Staging the full `core/src` tree exposed an unrelated newer `.Message(... visibility: ...)` form unsupported by this VIXEN-pinned source reader (`1791887829-light-rjs2:regen-frame-targets-schema-recovered.log`). A minimal authoring-only root also lacked source for `SpectralType` (`1791887999-light-rjs2:regen-frame-targets-schema-minimal.log`); excluding only the Faction AI schema file then left its paired `[Declare]` without a schema (`1791888150-light-rjs2:regen-frame-targets-schema-filtered.log`). The scoped overlay now stages Undertow and VIXEN source into the build tree and omits the unrelated Faction AI directory. Its schema generation and check pass. This source-root workaround is recorded as a consolidation proposal.
- **A — documented `dotnet run` route:** `dotnet run --project <pinned CodegenTool.csproj> -c Release --artifacts-path build/wsl/codegen/tool-artifacts -nodeReuse:false -- ...` built into the artifacts directory, then failed to launch the missing project-local apphost (Win32Exception 2; `1791888043-build-rjs2:dotnet-run-pinned-schema-generator.log`). Continuing through the CMake-published `CodegenTool.dll` reached the same source/codegen checks. The launcher workaround is recorded as a consolidation proposal.
- **B — fresh generation:** `frame_targets_schema_regen` emitted the `FrameTargets` schema JSON and `frame_targets_ref_regen` emitted its references from source. `frame_targets_schema_check` and `frame_targets_ref_check` both pass after moving staging into the build tree. `exposure_tonemap_gpu_regen` first found no kernel candidate while the source used the wrong attribute form; after applying R499.6's parameterless `[KernelSystem(Emit = KernelEmitTargets.Glsl)]` form and importing `Undertow.Sim.Systems`, generation reached body translation and hit the exact array-type failure above (`1791888420-light-rjs2:direct-exposure-system-regeneration-import.log`).
- **C — scope:** the failing generator is the required producer for the new shader and C++ binding header. There is no supported narrower build or native witness that can validate the pass without those outputs. This is not classified as an unrelated pre-existing red; the implementation is stopped at the kernel capability boundary.

## Witnesses

- Passed: queued fresh base target build `vixen_lookdev_capture`; CMake configure; `frame_targets_schema_check`; `frame_targets_ref_check`.
- Failed: `exposure_tonemap_gpu_regen`, at GLSL translation as described above.
- Not run: VIXEN application build after edits, split test suites, GPU correctness captures, native parity captures, and five-run native benchmark. The body gate must be resolved before these can witness S2. No WSL performance result is reported.

## CONSOLIDATION ISSUES (non-facade deltas this lane needed)

- proposed: Support scoped schema overlays for pinned source readers
- proposed: Launch dotnet run output from artifacts-path
- proposed: Expose source-resolvable GLSL math intrinsics
- proposed: Derive GPU dispatch extents from texture facets
