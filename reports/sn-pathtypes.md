# sn-pathtypes: native Windows PathDistance/text/glyph witness (R601.3)

Status: **SCOPED WITNESS COMPLETE; FULL NATIVE RELEASE GATE PARTIAL.** This report records the Windows-native evidence for the PathDistance path-type adoption at VIXEN `4111b8c0485015dc4af1c673d796d2f20b9d616b` (the lane HEAD). No VIXEN source or generated product file was changed by this lane.

## Scope and provenance

- Native checkout: `C:\vixen-native\vixenpin`, checked out at `4111b8c0485015dc4af1c673d796d2f20b9d616b`.
- Pinned kernel snapshot: `f16b9aa1193ec646a78e2d16578ed23ad19cd0e4`, confirmed by the native configure output and the owned snapshot under `.tmp/native-kernel-cache`.
- Paired Undertow root: `/home/liory/projects/undertow`, SHA `7e8b4e689bb03cd7432c4260ae8a5b7531f11ccb`.
- Required ignored FidelityFX tools were copied from the assigned `fsr3` native checkout into the lane clone: `FidelityFX_SC.exe` and `glslangValidator.exe`.
- Native build was direct through the documented Windows batch route, with the lane cap of four compiler jobs. Every GPU capture below ran as a separate job through the live `/home/liory/.local/bin/with-test-lock.sh` queue with `--resource gpu`.
- CodeGraph was queried first, but this worktree has no `.codegraph` index; the CLI reported that the owner must provide one. Standard source lookup was used afterward.

## Native Release build

The first configure completed, but the first build stopped before compilation because the fresh checkout lacked the gitignored `binary_store/glslangValidator.exe`. After copying that tool, the fresh Release graph built 1,350 of 1,351 targets. The sole remaining failure was `codegen/checks/recipe_simd_check.stamp`: `RecipeSimd.g.hpp` and `RecipeGlslLiterals.g.h` were reported stale because native regeneration produced CRLF while the checked-in blobs are LF. A byte diff showed line-ending-only differences and no semantic delta.

The documented `recipe_simd_regen` recovery target completed successfully (1/1 target). The focused consumer targets `test_text_shader_layout` and `vixen_lookdev_capture` also built successfully. The full native graph is therefore not claimed green; the scoped consumer binaries used below are fresh and built from the pinned snapshot.

After copying all raw evidence, the lane-owned `C:\vixen-native\vixenpin\build` directory measured 11G and was removed. The RTL watchdog had left a lane-owned CTest/renderer descendant alive; those exact Windows PIDs were terminated before cleanup. No other lane process was touched.

Raw build evidence is outside the repository at `/home/liory/codeman-cases/undertow/reports/sn-pathtypes-evidence/` (`build-initial.log`, `build-retry.log`, `recipe-simd-regen.log`, and the focused-build transcripts).

## CPU text witnesses

| Witness | Result |
|---|---:|
| `TextShaderLayout.GeneratedStd430FieldsMatchCppOffsets` | 1/1 passed |
| `TextShaderLayout.GeneratedContentEnumsMatchCppValues` | 1/1 passed |
| `TextGlyphMetrics.*` and `TextShapingFixture.*` | 11/11 passed |

The CPU CTest logs are `text-layout-cpu-ctest.log` and `text-cpu-ctest.log` in the raw evidence directory.

## Native captures

All successful glyph and text PNGs were 1920x1080. There is no matched 1920x1080 before/reference image for the text-shader or localized-glyph outputs in the supplied evidence set, so those rows claim CTest completion and artifact production, not a pixel-diff result.

| CTest witness | Result | Capture/reference evidence |
|---|---:|---|
| `vixen_text_shader_capture_producer` | 1/1 passed | `textshader/`; `hud_capture_5.png` (88,183 bytes) and `hud_capture_45.png` (88,234 bytes); no matched reference |
| `GlyphSdf.latin-carved` | 1/1 passed | `glyph-latin-carved/`; PNG 99,675 bytes |
| `GlyphSdf.latin-embossed` | 1/1 passed | `glyph-latin-embossed/`; PNG 102,330 bytes |
| `GlyphSdf.qps-rtl-carved` | 1/1 passed | `glyph-qps-rtl-carved/`; PNG 99,499 bytes |
| `GlyphSdf.qps-rtl-embossed` | 1/1 passed | `glyph-qps-rtl-embossed/`; PNG 102,619 bytes |
| `GlyphSdf.AnalyticVsVoxel128` | 1/1 passed | `glyph-analytic-vs-voxel128/`; `trilinear_max_delta=0.261464`, `unallocated_samples=0` |
| `GlyphSdf.AnalyticVsVoxel128.Yaw30` | 1/1 passed | `glyph-analytic-vs-voxel128-yaw30/`; `trilinear_max_delta=0.261464`, `unallocated_samples=0` |
| `GlyphSdf.AnalyticVsVoxel` (256) | 1/1 passed | `glyph-analytic-vs-voxel256/`; `trilinear_max_delta=0.117659`, `unallocated_samples=0` |
| `GlyphSdf.AnalyticVsVoxel256.Yaw30` | 1/1 passed | `glyph-analytic-vs-voxel256-yaw30/`; `trilinear_max_delta=0.117659`, `unallocated_samples=0` |
| `LookdevCapture.RendersFourStatesAtFourAnglesDeterministically` | 1/1 passed | `lookdev-standard/`; all 16 primary PNGs matched their 16 fresh `.repeat` PNGs byte-for-byte |

The standard lookdev fixture explicitly reported all 16 selected captures byte-identical across fresh per-state process runs. The four localized variants and four analytic-vs-voxel variants produced one PNG each; the analytic fixtures also emitted their numeric comparison tables and native performance CSVs.

## STOPs

- **Full native Release gate remains partial:** `recipe_simd_check` is still red on CRLF-only generated-file differences after the supported regeneration recovery. This blocks a claim of a fully green native graph, but not the fresh focused binaries and scoped capture witnesses above.
- **RTL proof pair is unverified:** `Application.LookdevCapture.GlyphSdf.RtlProof.Pair` was admitted through the live queue but was killed by the queue idle watchdog with exit 75 (`STALL_KILLED`) after 201 seconds, while its native log remained at `test_compute_pipeline`. It produced only `glyphsdf-shaping.txt`, not a PNG. The partial log is in `glyph-rtl-proof-pair/`.
- **External visual-reference limitation:** no matched 1920x1080 reference was available for the text-shader or localized-glyph PNGs, so no external pixel-diff pass/fail is asserted for those captures.

## SPT DISPOSITION

No new SPT proposal was filed because each incidental workaround matched an existing proposal; near-duplicates were intentionally avoided. The R601.3 scoped native witness is otherwise complete.

## CONSOLIDATION ISSUES

- Existing `1359197…` — Native Windows VIXEN witness build needed a hand-built C: clone with custom configure/build/capture .bat files (also covers copying gitignored `FidelityFX_SC.exe` and `glslangValidator.exe`).
- Existing `8a221c…` — Preserve generated recipe output line endings in native VIXEN checkouts.
- Existing `aefa3d…` — Native capture needs heartbeat for long Vulkan pipeline calls (the RTL proof pair reproduced this queue-watchdog hole).
- Existing `e4e488…` — Provide a CodeGraph index for VIXEN worktrees.
