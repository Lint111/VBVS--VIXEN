# vixwit: wave witness for fsroptional + textures1 + capdir (FSR3 OFF)

Branch `lane-vixwit`, based on `origin/wave/authoring-convergence` at `f750d6e9`. Raw logs and captures are outside the repo, in `~/codeman-cases/undertow/reports/vixwit-evidence/`.

## What was done

- Merged `lane-fsroptional`, `lane-textures1` and `lane-capdir` with `--no-ff`, in that order. No conflicts in any of the three merges.
- Configured and built Release with `VIXEN_WITH_FSR3=OFF` through the queue. One new commit was needed: `VulkanGraphApplication.cpp:103` now defines `GLM_ENABLE_EXPERIMENTAL` before `glm/gtx/norm.hpp`. textures1 deleted gli, whose bundled old glm used to hide this gate, so the merged tree failed to compile without it. The merges alone were not buildable.
- Zero new warnings. The one warning in a touched file (`DeviceNode.cpp:683`) is pre-existing.

## Measured results

- Capdir: `vixen_editor_capture_producer` passed. `HudRenderCapture.*` passed 5/5. `EditorToggleUndoCapture.*` passed 9/9.
- Suites: core group (Textures 5/5, RenderGraph 42/42) passed. VulkanResources ran 17 tests: 16 pass, 1 skip.
- FSR3 OFF: no FSR3 or FidelityFX defined symbols. The default boot passes. `VIXEN_UPSCALER=fsr3` boots with one fallback warning.
- Capture landing: the deployed queue does not set `UT_QUEUE_CAPTURE_ROOT` (it was unset in the queued payload). The lane wrapper set a private root itself, and captures landed under it. With the variable unset, they landed in `build/wsl/runtime-captures/<editor|hud>`.

## Not obtained

The owed instancing before/after capture could not be taken. `VIXEN_INSTANCING_DEMO=1` exits 255 on both the wave base and the assembly with "Prepare failed: Compiled upscaler provider is absent from its candidate set". The demo is already broken at `f750d6e9`, independent of the three lanes. Cause (inferred): the post-compile check from `395e441b` at `VulkanGraphApplication.cpp:334` throws because the demo graph never selects an upscaler provider. The demo also has no capture hook. No golden was recaptured.

## SPT DISPOSITION

- One proposal filed in `.spt-proposals/vixwit.jsonl`: fix the instancing demo's Prepare failure on the wave.
- Raw-queue note: the deployed queue lacks `UT_QUEUE_CAPTURE_ROOT` provisioning; the implementation lives on the undeployed `lane-queuegpu` branch.

## CONSOLIDATION ISSUES

- Instancing demo broken on the wave base (see above); textures1's before/after witness stays owed until it is fixed.
- Effective queue provisioning of `UT_QUEUE_CAPTURE_ROOT` is not yet witnessed; only the consumer side was.
- stb is pinned to `2c980bb5`, which is not fetchable fresh; configure needed `FETCHCONTENT_SOURCE_DIR_STB`.
