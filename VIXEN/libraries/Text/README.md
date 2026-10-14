# VIXEN Text

This library owns RmlUi's application font-engine boundary, bundled-face
FreeType metrics, and the MSDF glyph atlas. Its first font is the existing
`RenderGraph/assets/ui/LatoLatin-Regular.ttf`; it never enumerates system fonts.

The atlas uses the core library from [msdfgen](https://github.com/Chlumsky/msdfgen)
at `v1.13` (MIT). The upstream license is retained in `licenses/msdfgen-LICENSE.txt`.
FreeType remains the existing VIXEN dependency and RmlUi layout remains the UI
layout engine. Each font-size handle stores FreeType's hinted outline, pixel
bearings, and full glyph bounds in its MSDF page. The UI renderer samples these
pages with a median-distance shader path. Shaping, bidi, line breaking, and
rich-text hooks are provided by later text lanes.

The RmlUi adapter accepts explicitly loaded TrueType/OpenType faces from
packaged `assets/ui` directories; it does not enumerate system font locations.
