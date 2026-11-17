# Font import for the glyph walk (R892.14, R892.18, R892.19).
#
# The kernel tool `glyph-bake` (GlyphBake~ of the pinned Yeroket kernel) bakes each font below once
# per exact band: `<font>.band<band>.glyphbake` is the compressed file that ships with a pack, and
# `<font>.band<band>.glyphimage` is the decompressed image the runtime reads (no Brotli decoder in
# VIXEN). The commands live in codegen/CMakeLists.txt, next to the kernel tool they build; this
# file only names the outputs, so targets configured before codegen can refer to them.
#
# The band is a bake parameter: recipe registration derives the band a glyph row needs and uses
# the narrowest bake that covers it. Add a band here when a recipe reports that none does.
include_guard(GLOBAL)

set(VIXEN_GLYPH_BAKE_BANDS_EM "0.03" CACHE STRING
    "Exact bands (em) each font is baked with; a list, e.g. 0.03;0.08")
set(VIXEN_GLYPH_BAKE_DIR "${CMAKE_BINARY_DIR}/glyph-bake")
set(VIXEN_GLYPH_BAKE_FONT_NAMES LatoLatin-Regular NotoSansArabic-VF)
set(VIXEN_GLYPH_BAKE_FONT_LatoLatin-Regular
    "${VIXEN_ROOT}/libraries/RenderGraph/assets/ui/LatoLatin-Regular.ttf")
set(VIXEN_GLYPH_BAKE_FONT_NotoSansArabic-VF
    "${VIXEN_ROOT}/libraries/Text/tests/data/NotoSansArabic-VF.ttf")

# Every font is baked by this target; consumers that read an image at run time depend on it.
if(NOT TARGET vixen_glyph_bake)
    add_custom_target(vixen_glyph_bake ALL)
    set_target_properties(vixen_glyph_bake PROPERTIES FOLDER "Codegen")
endif()

# vixen_glyph_bake_file(<out> <font name> <band> <glyphbake|glyphimage|glyphsamples>)
function(vixen_glyph_bake_file out_var font_name band extension)
    set(${out_var} "${VIXEN_GLYPH_BAKE_DIR}/${font_name}.band${band}.${extension}" PARENT_SCOPE)
endfunction()

# vixen_glyph_bake_images(<out> <font name>): the images of every band, joined by '|', for a
# compile definition; the runtime offers all of them to registration.
function(vixen_glyph_bake_images out_var font_name)
    set(_images "")
    foreach(_band IN LISTS VIXEN_GLYPH_BAKE_BANDS_EM)
        vixen_glyph_bake_file(_image "${font_name}" "${_band}" glyphimage)
        list(APPEND _images "${_image}")
    endforeach()
    list(JOIN _images "|" _joined)
    set(${out_var} "${_joined}" PARENT_SCOPE)
endfunction()
