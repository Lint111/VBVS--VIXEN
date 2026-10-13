# rjs2 — S2 exposure tone map and native parity

## LANDABLE NOW

- Reusable artifact checkpoint: VIXEN commit **0e0f98ca96ff25fcb89300bd7d9881c28ce4b922**. The per-build kernel override is **a99311ce7cb7e8aa5b9896c6fbb36257c5d7ed9d**; the tracked VIXEN pin remains **7a2b1f1e83310b7129fdba19975235ef94198757**.
- CMake regeneration ran for both the tone-map GLSL and binding header. The shader now emits one Light definition before LightingConfig. The generated binding header is present and its drift check passes. The frame-target schema/reference checks and tone-map drift check pass.
- The render-graph wiring and 32 one-frame parity capture registrations are committed. The application cannot compile until the generated system SDI header exists.
- Native Visual Studio/Vulkan configuration and the native sdi_tool target build pass. The native compiler then exposes a remaining GLSL signature error described below.
- S2 is not acceptance-ready: no native parity sheet, benchmark samples, look-dev A/B captures, or split-suite results are available.

## STOPs

**STOPPED: the a99311ce GPU-system emitter produces an illegal GLSL function signature for the tone-map storage-buffer inputs.** The generated shader body declares:

    void ExposureTonemapSystem_Body(in float meter[], in LightingConfig LightingConfigSSBO[], int i) {

Both the WSL SDI merger and the native SDI merger reject line 60 with:

    ERROR: shader:60: '' : array size required

The native VIXEN configure found Vulkan SDK 1.4.350.0 and glslang 16.2.0. The native sdi_tool compiled the reference pass and the preceding shaders, then rejected ExposureTonemapSystem with that diagnostic. This confirms that the duplicate-struct fix works; the new failure is the unsized array formals.

Standalone `glslangValidator` is absent from the WSL environment. The native Vulkan SDK's `sdi_tool` merge path is the shader compile witness for this lane.

The app build stops earlier at the missing merged/ExposureTonemapSystem-SDI.g.h include in BuildRenderGraph.cpp. The SDI merger cannot generate that header while the system shader fails compilation. The source experiment that changed these inputs to scalar LightingConfig and float parameters was rejected by YK_KF_4022 because LightingConfig is not a Gaia component in the schema type manifest; that experiment was restored. A separate hand-authored Gaia kind or shader patch would duplicate the declaration and is not a valid lane workaround.

The required kernel-side action is to lower singleton storage-buffer inputs into a legal GLSL body interface while preserving the element-zero reads and the per-element linearId behavior. The VIXEN tracked pin remains unchanged. The assigned kernel worktree was used only as the a99311ce build input.

## Required scope and recovery

- Base SHA: **2685b1e980a052f85b938d7ac31b47f5930e015f**. A fresh queued baseline build of vixen_lookdev_capture passed before the S2 semantic edits (log 1791886709).
- Required scope: generate the system shader and binding header from the a99311ce snapshot; compile the system SDI header and render-graph consumer; then run native byte parity, five native timing rounds with medians and spread, look-dev A/B captures, and the RenderGraph|SVO|Application split suites.
- **A — invocation and provisioning:** CodeGraph exploration was attempted first but no .codegraph index existed in this worktree, so source inspection continued with rg. The native WSL kernel UNC path was rejected by Windows Git as dubious ownership. A GIT_CONFIG_COUNT retry broke FetchContent with “bogus count in GIT_CONFIG_COUNT.” Recovery: bundle the exact a99311ce commit into the assigned native checkout, create a detached local kernel snapshot there, set VIXEN_KERNEL_CACHE_DIR inside the native checkout, and configure through the Visual Studio developer environment. Native configure then passed (log 1791901198); no global Git config or tracked kernel pin was changed.
- **B — fresh generation:** queued configure with the a99311ce override, exposure_tonemap_gpu_regen, frame_targets_schema_check, frame_targets_ref_check, and exposure_tonemap_gpu_check passed (logs 1791899041, 1791899081, and 1791899469). The generated Light dependency is emitted once and in dependency order. Native sdi_tool build passed (log 1791902255), but native and WSL SDI compilation both fail on the unsized body parameters (logs 1791902300 and 1791899182). The native application build fails because the system SDI header is consequently absent (log 1791901475).
- **C — scope:** this red is inside the requested tone-map shader and blocks the render-graph build and every native parity/capture/timing witness. It is not an unrelated baseline red: the pre-edit renderer build passed. No narrower supported scope can prove the S2 gates without a compilable system SDI header.

## Verification

- Passed: a99311ce override configure, tone-map shader/header regeneration, schema/reference/codegen drift checks, native Vulkan configure, and native sdi_tool build.
- Failed: WSL and native SDI merge of ExposureTonemapSystem at shader line 60; native app build at the missing system SDI header.
- Test inventory only: WSL CTest `-N` selected 12 RenderGraph, 0 SVO, and 44 Application entries; the requested `RenderGraph|SVO|Application` union selected 56. Native Windows inventory registered 253 total tests, including 8 RenderGraph, 0 SVO, and 44 Application entries. The inventory logs warn that some executables are absent. No suite ran, so there is no failing-name comparison or “no new failing names” result.
- Not run: native byte comparison against the old SDI pass; native startup/per-frame timing; five --gpu-benchmark rounds; look-dev A/B captures; split correctness suites. No benchmark queue-load sample, median, or spread was measured.

## SPT disposition

No SPT tasks were dispatched. The proposals below are consolidation issues, not task IDs.

## CONSOLIDATION ISSUES

- proposed: Support scoped schema overlays for pinned source readers
- proposed: Launch dotnet run output from artifacts-path
- proposed: Expose source-resolvable GLSL math intrinsics
- proposed: Derive GPU dispatch extents from texture facets
- proposed: Default the SDI merger to the manifest shader root
- proposed: Lower singleton SSBO inputs into legal GLSL body parameters
- proposed: Make native kernel snapshots work with WSL-owned sources
- proposed: Make native multi-label CTest filters select label unions
- proposed: Make CodeGraph indexes available in VIXEN lane worktrees
