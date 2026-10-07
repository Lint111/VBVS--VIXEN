# Starlight lane report — T-1139

**Date:** 2026-10-07
**Branch:** `lane-starlight`
**Base:** `7f4db6877bac7062502b1cb603b68464cf42da21`

## Summary

The existing generated `LightingConfig` already carries four lights. `LightingConfigNode::SetLights` now replaces that shared set up to its generated capacity. Directional entries keep the existing direction and radiance behavior; point entries evaluate direction from each shaded world position, apply a finite-range fade, and cap shadow rays at the light. Point-light shadow rays skip the matching emissive source instance so the star does not occlude itself. The directional default remains unchanged.

The direct body path is `computeLightingWithShadows()` in `VIXEN/shaders/SpatialReuseShade.comp`, with shadow visibility built in `ShadowVisibilityWave.comp`. `LightingConfigNode::TypedExecuteImpl` uploads the set used by the shared shader path. KFR's `SessionRenderer::BuildRenderGraph()` only builds the graph and UI wiring; it does not replace lighting configuration.

`VIXEN_STARLIGHT_DEMO` seeds an emissive procedural star and three planets, then adds that star to the existing shared light set. No new light-set abstraction was needed. The existing Lambert+GGX model was preserved. The referenced R419.2/.3 cel model is not present in the inspected VIXEN shader path; that remains a separate pre-existing design mismatch.

## Verification scope and results

- Fresh baseline configure and full build passed through the WSL `vixen-wsl` preset and the global queue.
- The first post-change RenderGraph run found a shader include-context error (`PROVIDER_PROCEDURAL` was unavailable in one RTQuery include). The source skip was changed to match the emissive instance by position without depending on that macro. The focused RTQuery compile check then passed.
- Final RenderGraph suite: **1338/1338 passed**, including `ShadowCorrectnessTest.EmissivePointLightFacesThreeBodiesTowardTheStar`.
- Final SVO suite: one failure. `RecipeSimdParity.AllCorpusProgramsAreBitIdenticalAcrossFourLanes` failed with `M4d_Output_IsPassthrough: recipe gradient capability mismatch: 94`, matching the recorded pre-change baseline failure; the other runnable checks passed, with the same skipped/disabled tests. This CPU recipe parity check is outside the point-light path.
- Focused point-light witness: **1/1 passed** after aligning test coordinates with the capture. It logged normalized star-minus-body directions `(0.6384, -0.3331, 0.6939)`, `(-0.6384, -0.3331, 0.6939)`, and `(-0.6260, 0.3810, 0.6804)`. Facing-surface luma values were 222, 222, and 220.
- The dedicated star capture shows the star and all three planets with their lit sides facing it: [HUD capture](../.tmp/starlight-star-witness2/hud/hud_capture_45.png).
- The final no-emitter capture set is **byte-identical** to baseline: all seven PNGs have zero byte and pixel differences. Captures are in `.tmp/starlight-before` and `.tmp/starlight-after-default-final`.
- `dotnet run --project <pinned CodegenTool> ... --check` completed successfully using isolated artifacts under `.tmp/starlight-codegen-artifacts`. A separate queued `dotnet build` of the pinned tool with `-nodeReuse:false` also passed (0 warnings, 0 errors). The 22 CMake check targets were invoked and reported `ninja: no work to do`, because their input signatures and stamps were already current. The final full rebuild was also a no-op.
- The focused Dzn/Vulkan test passed while emitting validation diagnostics for SPIR-V 1.6 under a Vulkan 1.2 target and a `sceneRadianceHistory` image format mismatch. These diagnostics were not test failures; this run did not establish their baseline attribution.

## Baseline recovery and findings

- At base `7f4db6877bac7062502b1cb603b68464cf42da21`, the queued Windows-native configure (`cmd.exe /c build.bat configure vixen-ninja`) exited 1 from the WSL UNC worktree (`UNC paths are not supported`; `vswhere.exe` was not found; log `.tmp/starlight-baseline-configure.log`). The documented queued WSL `vixen-wsl` configure and fresh full build both succeeded (`.tmp/starlight-baseline-wsl-configure.log`, `.tmp/starlight-baseline-build.log`).
- The first baseline capture attempt used relative output `.tmp/starlight-before` and failed because the capture app runs from `build/wsl/binaries`; using the absolute output directory succeeded (`.tmp/starlight-baseline-captures.log`, `.tmp/starlight-baseline-captures-absolute.log`).
- The requested `SPT show T-1139` lookup could not load the declared scope's `yeroket-stack.local.json`. The inline task brief supplied the acceptance criteria, and the work continued.
- The baseline SVO failure above is proven pre-existing by the same test and diagnostic on the recorded base. It is outside this lane's verification target.

## Stops

None. The existing shared light set provided the required extension point, so no new abstraction decision was needed.

## CONSOLIDATION ISSUES (non-facade deltas this lane needed)

- proposed: Windows-native VIXEN configure cannot resolve the WSL worktree
- proposed: Capture runner interprets relative output paths from the binary directory
- proposed: SPT task lookup requires an unavailable workspace config
