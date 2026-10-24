// R631: GlyphOutline bytecode operands are indices into a runtime SSBO, not shader literals.
// This include must precede the generated recipe functions so GLSL sees the declaration while
// parsing their bodies. The generated struct guard keeps it compatible with SdfCoreKernels.
#ifndef VIXEN_RECIPE_GLYPH_BINDINGS_GLSL
#define VIXEN_RECIPE_GLYPH_BINDINGS_GLSL

#ifndef GLYPHOUTLINEINSTANCE_GLSL
#include "recipe/GlyphOutlineInstance.g.glsl"
#endif

layout(std430, binding = 51) readonly buffer GlyphOutlineBuffer {
    GlyphOutlineInstance glyphOutlineInstances[];
};

#endif
