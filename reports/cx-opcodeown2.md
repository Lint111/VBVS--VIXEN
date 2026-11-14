# cx-opcodeown2

## STATUS

PARTIAL / STOPPED ON THE NATIVE-HUD REFERENCE ROUTE. The d9 opcode coupling is unchanged and remains green. The published pin chain booted in both modes; the d9 content row intentionally changes the requested world/save digests. The full 16-image look-dev witness passed. The GPU starfield red is environmental and is registered as row 73. The supported Linux/WSLg capture produced seven images, but the requested vx8 HUD hashes are native Windows Release references, so this lane does not claim cross-platform byte identity.

## VIXEN COMMITS

- Implementation: `f9230d0b1eafcc3979631c375fe8cdbdd6aea844` (`feat(vixen): couple recipe codegen to d9 kernel row`).
- Report/wave tip: `002a5f6b6adf53644e53ef947c16538440497411`.
- Kernel pin: `_vixen_tracked_yeroket_kernel_sha = 597757236569835a7b40ed01ec04c06e286af723`.
- Normal generation regenerated the recipe opcode mirror and VoxelDocument faces; no generated `*.g.*` file was hand-edited. Prior VIXEN codegen checks were 119/119, Release build passed, CPU CTest 1738/1738 passed, and row 51 passed.

## PIN-CHAIN COMMITS

- KFR `lane-d9kfr`: `3d36f1d82931b0f9568687f1ad8347ef0af4673a`; `vixen.pin -> 002a5f6b6adf53644e53ef947c16538440497411`.
- Launcher `lane-d9l`: `6f53334e28a0db824494c5a497055c39a539d127`; `kfr.pin -> 3d36f1d82931b0f9568687f1ad8347ef0af4673a`.
- Undertow `lane-d9brow`: `50ec5098e84d75b2c2fb008196c093c6fa96ccdf`; pins kernel `597757236569835a7b40ed01ec04c06e286af723`, launcher `6f53334e28a0db824494c5a497055c39a539d127`, VIXEN `002a5f6b6adf53644e53ef947c16538440497411`.

## NATIVE BOOT

- Provider resolution used the local launcher row `6f53334e2` and exact d9 kernel root `597757236`; the published VIXEN/KFR waves were reachable after controller fast-forward.
- Initial stage red: the exact clean d9 row had stale batch-owned generated content (`Codec.g.cs` v181 versus a generated `BakedRenderRecipeDef` requiring `renderJob`), producing `CS7036` at `core/src/Undertow.Content/Codec.g.cs:7690`. Log: `/home/liory/.local/state/undertow/undertow-box-logs/1794686031-build-cx-opcodeown2-r2:native-boot-stage.log`.
- Recovery: normal queued `check-content-codegen.sh --write` regenerated 246/246 jobs (`OK 246 / STALE 0 / FAILED 0 / SKIPPED 0`), then the boot graph passed. The generated materialization was restored afterward; Undertow is clean at exact `50ec5098e8` and no follow-up Undertow commit is claimed.
- Final graph: native stage 25/25; native Release build 835/835; products 73 files digest `05d45236bbefdb70`; default CTest 12/12; composition CTest 13/13; fixtures 15/15; default and composition each ran 3 frames with no blocked lifecycle arms. Summary log: `/home/liory/.local/state/undertow/undertow-box-logs/1794687610-short-cx-opcodeown2-r2:native-boot-summary.log`.
- Observed state digest in both modes: world `07b009ea59ade130`, save `881bf4fa425320f6`.
- The requested vx8 digest is world `be65faec23dd5d32`, save `2add7bfb30580525`. The difference is intentional in the d9 row: landed Undertow commit `2cb3188a8` changed the render-schema inputs and paired generated content to add `lighting_profile`, `material`, and `render_job`; normal materialization produced ContentCodec v184/core.pack 71,776 bytes versus vx8 v181/core.pack 71,505 bytes. The opcode-owner change did not alter those content declarations.

## CAPTURES

- Queued `Application.LookdevCapture.RendersFourStatesAtFourAnglesDeterministically`: PASS, 1/1 in 437.59 s. All 16 state/angle captures and all 16 fresh per-state repeats were byte-identical. Evidence: `/home/liory/.local/state/undertow/captures/cx-opcodeown2-r2/20261114T222608.293540/lookdev`.
- Queued `tools/run-vixen-windowed-captures.sh`: PASS, all seven HUD/editor images produced. Evidence: `/home/liory/.local/state/undertow/captures/cx-opcodeown2-r2/20261114T222608.293540/native`.
- The current Linux/WSLg HUD hashes are frame 5 `8ef581c78331f95049ebdc421687ce36993082ceb948067f75bba7b5a8fcab50`, frame 45 `1707ebe27c537dd43704c7760030f7359616eb95264739d790cef475c4e9a02b`, frame 75 `6ac8a82e172accd62508a38e805af76e63e0389c866d7cbd15ce9dd96b682640`.
- The required vx8 native Release reference hashes are frame 5 `754bcd8e7e3e433dfc42b5635283611d55c4b8821094699b19a70fb04c19a157`, frame 45 `7c4d3688aad930e0af47e93ce76bed92595255e4413c6be082fc5cde1b793801`, frame 75 `cdb9b3932d7c516695690956cc6b9702667ade3a54c31427da31dfba761052a3`. The Linux route is not byte-identical to those native Windows references; no HUD golden was changed.

## GPU CLASSIFICATION

- Row 73: `HeadlessStarfieldGraph.CompareIndependentCaptures` failed 3/3 on vx8 base `dc324d0f582e00bd96e07e25565703a5e92d28b1` and 3/3 on row `002a5f6b6adf53644e53ef947c16538440497411`; base reported `first disabled capture was empty`, row reported `second disabled capture was empty`. No isolated run emitted `VK_ERROR_DEVICE_LOST`. This is environmental and must not be attributed to the opcode coupling.
- Prior full GPU split selected 1,620 tests: 551 passed, 83 declared skips, row-73 starfield was the named assertion red, and four Presentation shards were stopped by the GPU guard after no output; `GlyphSdf.RtlProof.Pair` also emitted `VK_ERROR_DEVICE_LOST` and passed on the queue's exclusive retry. Logs: `/home/liory/.local/state/undertow/undertow-box-logs/1794680888-gpu-cx-opcodeown2:split-gpu-1.log`, `1794680883...split-gpu-2.log`, `1794680894...split-gpu-3.log`, `1794680874...split-gpu-4.log`, and `1794680900...split-gpu-5.log`.
- Fresh serial isolation `cx-opcodeown2-r2:gpu-red-isolation` passed 5/5: Presentation night-service 455.78 s, late-afternoon 383.75 s, midday 369.98 s, overcast 354.53 s, and GlyphSdf RtlProof.Pair 89.40 s. No device-loss or assertion red occurred. Log: `/home/liory/.local/state/undertow/undertow-box-logs/1794688987-gpu-cx-opcodeown2-r2:gpu-red-isolation.log`. The full-split Presentation/Glyph failures were shared-device/guard environment effects; row 73 remains the only GPU assertion red.

## FOR THE LANDER

- Kernel: `597757236569835a7b40ed01ec04c06e286af723`
- Undertow: `50ec5098e84d75b2c2fb008196c093c6fa96ccdf`
- Launcher: `6f53334e28a0db824494c5a497055c39a539d127`
- VIXEN: `002a5f6b6adf53644e53ef947c16538440497411` (published wave)
- KFR: `3d36f1d82931b0f9568687f1ad8347ef0af4673a` (published wave)

## SPT DISPOSITION

Existing VIXEN proposals remain recorded in `.spt-proposals/cx-opcodeown2.jsonl`. This run added `56b69f833e0e` (paired generated outputs stale after a clean witness) and `081db6014a26` (content-codegen wrapper self-admits when nested under the queue).

## CONSOLIDATION ISSUES (non-facade deltas this lane needed)

- proposed: Recipe service codegen must discover kernel recipe element declarations
- proposed: Recipe SIMD codegen must emit evaluator dispatch when configured
- proposed: Recipe graph emitter must initialize appended socket metadata
- proposed: Batch-owned generated outputs leave paired rows stale after a clean witness
- proposed: Content codegen wrapper self-admits when nested under the queue

## STOPs

- Native boot itself is complete, with the digest difference explained by intentional d9 content changes.
- The requested HUD byte identity is not claimable from the available Linux/WSLg route because the vx8 hashes are native Windows Release references; rerun the same native Windows capture route on the lander if this gate is mandatory.
- GPU row 73 is environmental and registered. The serial Presentation/Glyph isolation passed 5/5, so no additional GPU STOP remains.
