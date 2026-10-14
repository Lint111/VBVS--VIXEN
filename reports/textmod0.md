# Text module corrective witness

Final corrective commit: `c3d1631d483e2fb81d6eb650b407c915e892b4c7` (`Match text atlas glyphs to hinted FreeType metrics`). The msdfgen MSVC CRT fix from `be3ebb57` remains in the lane. The native HUD capture producer registration is in `5385a6fa` and is included in the final tree.

## Changes

- Glyph outlines are loaded at the requested pixel size with FreeType hinting. Atlas cells include the glyph's complete bounds and distance padding; quads use the FreeType bearing and advances in that same pixel coordinate system.
- The Lato fixture checks advances for ten strings and checks atlas bounds and metrics for `g`, `y`, `p`, and `j` against FreeType.
- `hud.rml` contains `✓`, `●`, `▸`, `🔍`, and `◉`; `hud.rcss` selects `LatoLatin`. The only bundled face is `LatoLatin-Regular.ttf`, which covers none of those five codepoints. No bundled icon face exists, so missing characters use the original hollow-box fallback instead of a question mark.
- `vixen_hud_capture_producer` is registered whenever tests and the `VIXEN` target are enabled, including native Windows. The native CTest label is `VIXEN_HUD_CAPTURE`.

## Verification

- WSL CMake build passed for `Text`, `test_text_glyph_metrics`, `test_ui_hud_smoke`, and `VIXEN`; no warnings were reported in new code.
- Focused glyph metrics and HUD smoke tests passed: 3 and 6 tests respectively.
- Pinned `OctreeConfig` codegen check passed.
- Dozen `vixen_hud_capture_producer` passed. Matching before/after captures are 500×500; any changed RGB channel counts as a changed pixel:

  | Frame | Changed pixels | Max channel delta | Changed pixels ≤8 | Changed pixels ≤16 | Changed pixels >16 |
  | --- | ---: | ---: | ---: | ---: | ---: |
  | 5 | 4,732 | 51 | 3,407 | 4,452 | 280 |
  | 45 | 5,398 | 50 | 3,732 | 4,958 | 440 |
  | 75 | 5,111 | 52 | 3,689 | 4,797 | 314 |

  Visual inspection shows the `g` and `y` descenders intact and no line clipping. The remaining Dozen differences are confined to text rendering; the counts and maxima above are reported without treating them as zero-delta matches.

- Native Windows build passed for `VIXEN`, look-dev capture, and planet capture targets. Native `vixen_hud_capture_producer` passed on the shared `gpu-native` queue (no exclusive reservation) and emitted frames 5, 45, and 75.
- The available native `native-before` images are not a matched scene baseline: their background is black, while the final native capture background is lavender. Full-frame native comparison therefore reports 246,484 / 246,309 / 246,407 changed pixels and max channel delta 232 for frames 5 / 45 / 75, but these figures do not measure text parity. The native word crops show the descenders, but a same-scene pre-change native capture is still needed to establish native pixel parity.

Raw images, logs, pixel diffs, and crops are stored outside the repository under `/home/liory/codeman-cases/undertow/reports/textmod0-evidence/`.

## CONSOLIDATION ISSUES

- Track a generator for the embedded UI SPIR-V header.
- Align msdfgen’s Windows CRT with VIXEN.
- Provide a dependency-free PNG pixel-diff tool for capture evidence.
- Document the VIXEN build working directory for direct CMake commands.
