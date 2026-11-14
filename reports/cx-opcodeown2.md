# cx-opcodeown2

## STATUS

STOPPED / REFER-TO-ORCHESTRATOR for the required native boot and complete look-dev/HUD witness. The d9 VIXEN opcode-owner change is committed and its codegen, Release build, CPU suite, and targeted row-51 repair are green. The remaining STOPs are provider reachability and GPU/environmental/unregistered-test findings; no boot digest delta is claimed.

## VIXEN COMMITS

- `f9230d0b1eafcc3979631c375fe8cdbdd6aea844` — `feat(vixen): couple recipe codegen to d9 kernel row`.
- Pin: `_vixen_tracked_yeroket_kernel_sha = 597757236569835a7b40ed01ec04c06e286af723`.
- Normal codegen regenerated `SdfOpCodes.g.h`, `SdfRecipeEvalDispatch.g.inc`, and `VoxelDocument.g.h`; `vixen_codegen_checks` and `recipe_opcode_mirror_check` passed. No generated `*.g.*` file was hand-edited.
- Minimal handwritten consumer changes were required by the generated API: recipe-service discovery now includes the kernel `RecipeElementKinds.cs`; recipe SIMD passes its configured evaluator-dispatch output; `SdfInstruction.h` suppresses the generated aggregate-initializer warning; the recipe registry recognizes typed `SampleSurface`; and `EditorDocumentModel` validates the generated layer-op table and preserves the loaded VDC1 format when serializing.

## PIN-CHAIN COMMITS

- KFR `lane-d9kfr`: `1113413f64cdc4030d9c878e5fee3142f32883d6` from `0e1e9a1380d38a9d6a7d7b31f0aaf24be6c5ef3a`; `vixen.pin -> f9230d0b1eafcc3979631c375fe8cdbdd6aea844`.
- Launcher `lane-d9l`: `4d7c0161ccb6106d8965754cb6c457497547d666` from `8cdb9c8cb5041c8a2fa2e823387d92db7f5bddfe`; `kfr.pin -> 1113413f64cdc4030d9c878e5fee3142f32883d6`.
- Undertow `lane-d9brow` pin commit: `9714cb3aef4525708d848eda0ea07fc5ea0d32e5`; `kernel.pin -> 597757236569835a7b40ed01ec04c06e286af723`, `launcher.pin -> 4d7c0161ccb6106d8965754cb6c457497547d666`, `vixen.pin -> f9230d0b1eafcc3979631c375fe8cdbdd6aea844`.

## WITNESS

- Fresh queued d9 recipe regeneration passed after wiring the missing kernel element-kind input and evaluator-dispatch output. Final queued `vixen_codegen_checks` passed; explicit `recipe_opcode_mirror_check` passed. The checks covered 119/119 VIXEN codegen checks.
- Queued native Release build passed: all 682 build tasks completed, including VIXEN and look-dev targets. Final log: `/home/liory/.local/state/undertow/undertow-box-logs/1794683480-build-cx-opcodeown2:final-release-build.log`.
- Full CPU CTest passed: 1,738/1,738 selected tests, 0 failed, 4 declared skips. Log: `/home/liory/.local/state/undertow/undertow-box-logs/1794680225-test-cx-opcodeown2:d9-full-cpu-final.log`.
- Row 51 (VDC1 v2->v3 writer) now PASSes: the regenerated d9 VoxelDocument face and the minimal editor format-preservation change passed the focused VoxelDocument/editor set 3/3. The prior v2 fixture no longer promotes on save.
- Full GPU split selected 1,620 tests. It recorded 551 passed, 1 real test failure, and 83 declared skips before four shards were stopped by the GPU guard: three Presentation cases stalled without output, and `Application.LookdevCapture.GlyphSdf.RtlProof.Pair` hit `VK_ERROR_DEVICE_LOST` during `FrameSyncNode::FrameEnd`. The remaining named failure was `HeadlessStarfieldGraph.CompareIndependentCaptures`; a single-case rerun reproduced `second disabled capture was empty`. This has no d9 register-row attribution in the current register and is therefore an unregistered finding, not silently accepted as a green witness.
- Look-dev focused capture/golden run was stopped at the checkpoint after two serial passes (`Application.LookdevCapture.Presentation.night-service` and `Application.LookdevCapture.RendersFourStatesAtFourAnglesDeterministically`, both about 412 s). The full look-dev/golden matrix did not complete; no new golden was recaptured.
- The accepted vx8 HUD reference hashes are frame 5 `754bcd8e7e3e433dfc42b5635283611d55c4b8821094699b19a70fb04c19a157`, frame 45 `7c4d3688aad930e0af47e93ce76bed92595255e4413c6be082fc5cde1b793801`, and frame 75 `cdb9b3932d7c516695690956cc6b9702667ade3a54c31427da31dfba761052a3`. This lane did not complete the supported native HUD capture because boot staging stopped at provider reachability, so byte identity is not claimed.

## NATIVE BOOT / STOPs

The documented d9 `tools/check-native-boot.sh --kernel-root ... --agent cx-opcodeown2` route was run with the live queue-aware boot graph. Kernel pairing matched `597757236569835a7b40ed01ec04c06e286af723`, but staging failed closed before build or either boot mode:

- `provider-root: launcher pin UNREACHABLE -- FAILED CLOSED (undertow.provider.pin-unreachable)`.
- Run: `/home/liory/projects/undertow/.claude/worktrees/d9brow/.tmp/launcher-native-boot/runs/20261114T191320-2863527`.
- Log: `/home/liory/.local/state/undertow/undertow-box-logs/1794683599-build-cx-opcodeown2:native-boot-stage.log`.
- Default and composition were skipped; world `be65faec23dd5d32` and save `2add7bfb30580525` were not fabricated.

The controller must fast-forward/publish these exact tips on the provider waves before the boot can resolve the chain:

- VIXEN wave: `f9230d0b1eafcc3979631c375fe8cdbdd6aea844`.
- KFR wave: `1113413f64cdc4030d9c878e5fee3142f32883d6`.

After reachability is repaired, rerun both default and composition boots and the complete native look-dev/HUD witness. The exact Undertow pin commit to use is `9714cb3aef4525708d848eda0ea07fc5ea0d32e5`.

Recovery record: A resolved the global queue path and paired d9 kernel root; B regenerated all affected VIXEN faces through normal targets plus the documented d9 VoxelDocument generator; C isolated the remaining native failure to provider reachability and the GPU guard/device environment. The generated VoxelDocument output contains an emitter-provided trailing-whitespace line; it was preserved rather than hand-edited.

## SPT DISPOSITION

Proposals were recorded and validated in `.spt-proposals/cx-opcodeown2.jsonl`: `0155f5613e47` (recipe element declarations), `6f1af382b880` (recipe SIMD evaluator dispatch), and `94c2487d5586` (generated socket metadata initialization).

## CONSOLIDATION ISSUES (non-facade deltas this lane needed)

- proposed: Recipe service codegen must discover kernel recipe element declarations
- proposed: Recipe SIMD codegen must emit evaluator dispatch when configured
- proposed: Recipe graph emitter must initialize appended socket metadata
