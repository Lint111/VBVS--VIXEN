# cx-vixtrain2: VIXEN train classification and capped witness

## LANDABLE NOW

- VIXEN: `47f8af7d` merge of `origin/wave/authoring-convergence` into the train, plus the regenerated SDI and FrameTargets artifacts and this report/proposal inbox.
- Crop artifacts are outside the repository at `/home/liory/codeman-cases/undertow/reports/aadither-crops/`.

The wave merge was resolved in `VIXEN/codegen/CMakeLists.txt` by retaining the lane's WSL/home-root resolver and the wave's current tracked kernel pin `b1576418a1ca4cf29dd22853ff3cd1aba6372c83`; both intents remain present. The generated artifacts are `VIXEN/generated/sdi/merged/ExposureTonemapSystem-SDI.g.h:32-140`, `VIXEN/codegen/generated/gpu/FrameTargets.g.cs:1`, and `VIXEN/codegen/generated/gpu/frame-targets.schemas.json:1`.

### Train classification

Evidence is the requested `git log --first-parent origin/wave/authoring-convergence..HEAD`, ancestor checks for each listed commit, and `/home/liory/codeman-cases/undertow/reports/dropaudit/vixen-train.txt:1-24`.

| queue | lane / commit | verdict | evidence |
|---:|---|---|---|
| 64 | vixburn1 `09e8c0ea` | LANDED-IN-TRAIN | ancestor of HEAD; landed by merge `91636f2a` |
| 72 | vixburn2 `f01bee96` | LANDED-IN-TRAIN | ancestor of HEAD; landed by merge `660b75d5` |
| 99 | lookdevleak `9f30c6d8` | SUPERSEDED | older follow-up is parent of `160c3a4d`; the glyph-SDF fix remains RED/unlanded |
| 99 | lookdevleak `160c3a4d` | RED | absent from HEAD; current `LookdevCapture.RendersGlyphSdfLocalizedSign` is the known 60+ GiB runaway and is owned by the active glyph-leak fix; excluded from every witness |
| 125 | vixtestrun `16e2fda0` | LANDED-IN-TRAIN | ancestor of HEAD; landed by merge `bd34450f` |
| 125 | vixpaths `af662b6b` | LANDED-IN-TRAIN | ancestor of HEAD; landed by merge `b57645dc` |
| 132 | vixcmake `1dea3c3f` | LANDED-IN-TRAIN | ancestor of HEAD; landed by merge `f9985bfc` |
| 132 | vkimpl `bbc31a4b` | LANDED-IN-TRAIN | ancestor of HEAD; landed by merge `2daf64d5` |
| 132 | vprov `0bc47df8` | SUPERSEDED | ancestor of later same-lane `b44e8eaa`, which is in the train via `1ba66cd2` |
| 132 | winprov `b31f929d` | LANDED-IN-TRAIN | ancestor of HEAD; landed by merge `897f2581` |
| 132 | vprov `b44e8eaa` | LANDED-IN-TRAIN | ancestor of HEAD; landed by merge `1ba66cd2` |
| 132 | capevid `cbe4e61f` | LANDED-IN-TRAIN | ancestor of HEAD; landed by merge `e18e8e82` |
| 132 | shaderparity `ae0fb707` | LANDED-IN-TRAIN | ancestor of HEAD; landed by merge `276bbde8` |
| 134 | gpufix `779c14e5` | LANDED-IN-TRAIN | ancestor of HEAD; landed by merge `c7696181` |
| 137 | presred `fd46df8e` | SUPERSEDED | contained by later `ringidx` `57899200`, landed by merge `7d398529` |
| 137 | ringidx `57899200` | LANDED-IN-TRAIN | ancestor of HEAD; landed by merge `7d398529` |
| 152 | vctestser `fdbf3c7d` | SUPERSEDED | contained by later `mutexidx` `6208e90d`, landed by merge `c59dda40` |
| 155 | vnatconf `632b3e1a` | SUPERSEDED | contained by later `wslroute` `82b65a1a`, landed by merge `f77f34d4` |
| 159 | mutexidx `6208e90d` | LANDED-IN-TRAIN | ancestor of HEAD; landed by merge `c59dda40` |
| 163 | vgoldens `9cf987ab` | LANDED-IN-TRAIN | ancestor of HEAD; landed by merge `45109ce1` |
| 166 | covmaps `a6575109` | LANDED-IN-TRAIN | ancestor of HEAD; landed by merge `3818b496` |
| 167 | wslroute `82b65a1a` | LANDED-IN-TRAIN | ancestor of HEAD; landed by merge `f77f34d4` |
| 170 | yroot `296afe2a` | LANDED-IN-TRAIN | ancestor of HEAD; landed by merge `f77f34d4` |
| 213 | stbpin `c1f1cf358` | ALREADY-IN | equivalent STB pin is already in the merged wave at `f20e97c2`; the listed commit itself is not an ancestor |

### Witnesses

All VIXEN builds/tests/captures used the live queue. GPU tests and captures used `systemd-run --user --scope -p MemoryMax=12G -p MemorySwapMax=0`; `LookdevCapture.RendersGlyphSdfLocalizedSign` was excluded from every CTest selection. Raw logs are in `/home/liory/codeman-cases/undertow/reports/cx-vixtrain2-evidence/`.

| scope | before / wave-base evidence | after / merged-tip evidence |
|---|---|---|
| Release build + codegen | prior train had the known SDI/FrameTargets generated-input drift | PASS: `build-wsl.sh -j4`, `-DVIXEN_WITH_FSR3=OFF`; all 142 codegen/golden checks passed after regenerating one SDI header and the FrameTargets artifacts |
| RenderGraph | `reports/vixkpin.md` records 15 known failures and the same names on the wave base | 1,416 passed / 11 skipped / 15 failed from 1,442 selected; the failing-name set is exactly the known 15: 10 procedural recipe renders, 3 editor-document renders, AppFlow toggle undo, and `vixen_text_shader_capture_producer` timeout |
| SVO | `reports/vixkpin.md`: 789/799 pass, 10 known HLSL/SPIR-V failures | 780 passed / 9 skipped / 2 disabled / 10 failed from 801 registered (799 counted by CTest); the same 10 HLSL/SPIR-V names, no new failure |
| Textures | 3/3 | 3/3 |
| VulkanResources | 17/17 in the prior report | 16 passed / 1 skipped / 0 failed out of 17; the skipped device-identity case is not a new failure |
| CashSystem | 117/117 | 117/117 |
| Text | 15/15 in the prior report | 15/15; the `-L Text` witness also matched 3 Textures and four non-text glyph captures, all green |
| GaiaVoxelWorld | not recorded in the prior train report | 98 passed / 2 disabled / 0 failed out of 100 registered |
| capture producers | editor PASS, HUD PASS, instancing PASS, text timeout | editor 65.50s PASS; HUD 47.38s PASS; instancing 2.81s PASS; text timeout 184.50s, matching the wave-base red |

The SVO failures are the known kernel HLSL-emitter red, not a VIXEN-side regression. The current build log also contains the existing generated `gaia:true` fixed-capacity string warnings; no new hand-written warning was introduced by this train merge or regeneration.

### Dither crops

The detached `lane-aadither` worktree was built and captured at 1000² and 2000² in presentation mode, then at 2000² in canonical mode. The saved crops are deterministic CPU PNG crops of the upper 25% sky band:

- `night-service-presentation-1000-sky.png` — 1000×250, SHA-256 `6d8c5efd7b7b909523de6e9bd12c1731c971ed40054c9398e5321f12428bdf9f`.
- `night-service-presentation-2000-sky.png` — 2000×500, SHA-256 `dabb343502e763556eeceeed971e5ced711b3e25b5e0c9b6208fa95d9347ab12`.
- `night-service-canonical-2000-sky.png` — 2000×500, SHA-256 `bf66c9cda4b0b330b71960935d98229c2610823d515b03046ee77775ebe3d090`.

The presentation crops show the expected blue-noise texture; canonical mode is the smooth comparison. The detached capture worktree is clean and its build/fetch trees were deleted.

## STOPs

- The 15 RenderGraph and 10 SVO failures remain accepted wave-base reds; their failing names are unchanged, so no merge was reverted.
- `LookdevCapture.RendersGlyphSdfLocalizedSign` remains excluded because the active glyph-leak lane is still fixing its unbounded memory growth.
- Kernel SourceGenerator/CodegenTool suites were not rerun: this lane changes no kernel source and the required scope is the VIXEN train plus its paired Undertow schema consumer. No before/after kernel result is claimed.

## SPT DISPOSITION

No existing SPT task was closed or changed. Four new consolidation proposals were appended to `.spt-proposals/cx-vixtrain2.jsonl`.

## CONSOLIDATION ISSUES

- proposed: VIXEN build gates stale FrameTargets schema after full compilation
- proposed: VIXEN build discovers stale merged SDI header only after full compilation
- proposed: Queued witness wrapper can stall on shared history lock after payload completion
- proposed: Lookdev capture requires GPU-resource admission and explicit shared-driver environment
