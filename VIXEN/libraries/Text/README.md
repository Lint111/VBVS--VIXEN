# VIXEN Text

This library owns RmlUi's application font-engine boundary, bundled-face
FreeType metrics, and the MSDF glyph atlas. Its first font is the existing
`RenderGraph/assets/ui/LatoLatin-Regular.ttf`; it never enumerates system fonts.

The atlas uses the core library from [msdfgen](https://github.com/Chlumsky/msdfgen)
at `v1.13` (MIT). The upstream license is retained in `licenses/msdfgen-LICENSE.txt`.
FreeType remains the existing VIXEN dependency and RmlUi remains the UI layout
engine. Each font-size handle stores FreeType's hinted outline, pixel bearings,
and full glyph bounds in its MSDF page. The UI renderer samples these pages with
a median-distance shader path.

The RmlUi adapter accepts explicitly loaded TrueType/OpenType faces from
packaged `assets/ui` directories; it does not enumerate system font locations.

## Shaping, bidi, and line breaking

The module fetches HarfBuzz `14.5.0` (Old MIT), SheenBidi `v3.0.0`
(Apache-2.0), and libunibreak `libunibreak_5_1` (zlib). Their upstream notices
are retained in `licenses/`. ICU and FriBidi are not used. HarfBuzz is built
with its FreeType adapter; FreeType's own HarfBuzz integration remains disabled.

`ResolveBidi` returns visual-order directional runs and paired-character mirror
substitutions. `ShapeUtf8` shapes the visual runs in that order; HarfBuzz applies
Unicode mirroring for RTL glyphs and reports clusters in original UTF-8 byte
offsets.
The required ISO 15924 script tags are supplied with the copied `PresetDesc`;
the API reports whether their count matches the preset and whether each run's
script is required. `FindLineBreaks` returns libunibreak UAX #14 opportunities
as UTF-8 byte offsets and consumes the preset's word, phrase, or dictionary
class. Language-specific phrase and dictionary tailoring needs the corresponding
preset data from the content loader.

`RmlFontEngine::SetPresetDesc` opts the RmlUi adapter into bidi-aware shaping
for both measurement and glyph-atlas generation. With no active preset, the
lane-7 FreeType path remains the default, so existing Latin HUD output is
unchanged until the content loader supplies a preset. The native fixture uses
the existing bundled Lato face and a test-only Noto Sans Arabic variable font;
its SIL OFL notice is in `licenses/NotoSansArabic-OFL.txt`.
