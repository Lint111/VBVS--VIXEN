# cx-hudkit2

## STATUS

PASS. The HUD kit migration is byte-identical to the legacy HUD at frames 5, 45, and 75. The lane started at `19b12a02a`, merged offscreen capture support from `e8c320753` as merge commit `6b0dc09e8`, and landed the kit fix in `dc337e449` (`fix(ui): match legacy round button text rasterization`).

## CAUSE

The legacy controls were plain `div` elements with the legacy/default line-height. The shared kit fragment adds an inner `.ut-label` span, and `ut-button[variant=round]` forced `line-height: 48px`. That changed the label glyph line box and moved the four HUD glyphs by a fraction of a pixel. The border width, border colour, radius, background colour/opacity, dimensions, and box sizing already matched; the ellipse hit-mask was also already forced by the kit behavior.

## CHANGE

Removed `line-height: 48px` from `VIXEN/libraries/RenderGraph/assets/ui/kit/kit.rcss` (`ut-button.round`). The round variant now inherits the same line-height behavior as the old plain element, so the fix is in the shared kit and applies equally to editor consumers. No HUD-only rule was added.

## HASH TABLE

| frame | base hash | final hash | result |
|---:|---|---|---|
| 5 | `754bcd8e7e3e433dfc42b5635283611d55c4b8821094699b19a70fb04c19a157` | `754bcd8e7e3e433dfc42b5635283611d55c4b8821094699b19a70fb04c19a157` | byte-identical |
| 45 | `7c4d3688aad930e0af47e93ce76bed92595255e4413c6be082fc5cde1b793801` | `7c4d3688aad930e0af47e93ce76bed92595255e4413c6be082fc5cde1b793801` | byte-identical |
| 75 | `cdb9b3932d7c516695690956cc6b9702667ade3a54c31427da31dfba761052a3` | `cdb9b3932d7c516695690956cc6b9702667ade3a54c31427da31dfba761052a3` | byte-identical |

The queued final capture was `cx-hudkit2:final-hud-capture`; the pixel comparator reported `byte_diff=0`, `pixel_diff=0`, `max_channel_delta=0`, and `bbox=none` for all three frames. The pre-fix capture reproduced the hudkit1 mismatch: 240 changed pixels, bbox `x=37..213,y=442..467`, max channel delta 23.

## WITNESS

- Native Release VIXEN build: `cx-hudkit2:native-release-vixen`, exit 0, 687/687 build targets.
- Targeted UI closure: `cx-hudkit2:ui-cpu-guards-rerun`, 39/39 passed. This includes all 16 hit-mask tests, all 6 HUD smoke tests, all 6 composition-host tests, all UI-kit rule/contract/tests, `HudViewTest`, and `ViewHudGolden.IViewSeamIsViewAgnostic`.
- HUD render assertions: `cx-hudkit2:hud-render-assertions`, 3/3 passed (`BaselineIsNonEmpty`, payload swap produces a real pixel difference, same-frame deterministic).
- Native offscreen capture: `cx-hudkit2:baseline-hud-capture` recorded the hudkit1 mismatch before the change; `cx-hudkit2:final-hud-capture` produced the table above.
- `git diff --check` passed. The canonical-shape review found no new manual declaration/map/shim/state-guard workaround in the kit change.

## EDITOR GUARD

The current queued editor producer `cx-hudkit2:editor-ui-gpu-guards` passed 1/1. The seven `EditorToggleUndoCapture.*` assertions are CTest-disabled on this DZN environment, so they are not claimed as current passes. The accepted unchanged shared-UI editor evidence is `sn-uikit1.md` and `sn-uikit1-evidence/witness/migrate/`: that guard/readback witness recorded 88/89 GPU guards with only the registered `ViewHudGolden.GeneratedBindFunctionCompilesAndBinds` baseline red, plus the final editor selector/readback sequence. This lane only changes `ut-button.round`; the editor UI does not use that variant, so its shared-UI readbacks remain unchanged.

## REDS

- Registered pre-existing baseline red: `ViewHudGolden.GeneratedBindFunctionCompilesAndBinds`; excluded from the 39-test targeted closure and not caused by this CSS change.
- Native logs retain pre-existing third-party CMake warnings and the runtime cache-save warning; all required native build/capture commands exited 0.
- Native CMake reports the existing no-new-mutex gate skipped because `bash/python3` are unavailable in the native environment.
- The first empty CTest selector exited 0 without selecting tests; the corrected 39-test selector is the attested result. A consolidation proposal for that silent no-op is committed in `.spt-proposals/cx-hudkit2.jsonl`.

## STOPs

None. Exact reproduction succeeded, so the requested 4x before/after crop for the impossible-reproduction path was not needed.

## CONSOLIDATION ISSUES (non-facade deltas this lane needed)

- proposed: Native UI test selector should reject an empty CTest match

