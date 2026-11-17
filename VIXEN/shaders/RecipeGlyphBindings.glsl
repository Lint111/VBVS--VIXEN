// R631: GlyphOutline bytecode operands are indices into a runtime SSBO, not shader literals.
// This include must precede the generated recipe functions so GLSL sees the declaration while
// parsing their bodies. The generated struct guard keeps it compatible with SdfCoreKernels.
#ifndef VIXEN_RECIPE_GLYPH_BINDINGS_GLSL
#define VIXEN_RECIPE_GLYPH_BINDINGS_GLSL

#extension GL_EXT_control_flow_attributes : enable

// Keep the canonical row declaration visible to SDI/codegen and specialized shaders. The
// production uber source removes the unreachable generated helper at assembly time; the live
// glyph walk below reads the packed binding and never reads this typed aggregate.
#include "recipe/GlyphOutlineInstance.g.glsl"

layout(std430, binding = VXB_GlyphOutlineBuffer) readonly buffer GlyphOutlineBuffer {
    GlyphOutlineInstance glyphOutlineInstances[];
};

// R892.14: the packed glyph buffer holds baked glyph rows (Recipe/GlyphBakeStream.h packs them):
//   [0] row count
//   rows        2 words each: first placement (word index), placement count
//   placements  5 words each: glyph block (word index), offset x, offset y (row units),
//               font units per row unit, row units per font unit
//   blocks      one per glyph, as the kernel tool glyph-bake wrote it at font import (font units):
//               [0] chord count [1] columns [2] rows [3] list entries [4..7] bounds
//               [8..11] grid origin and cells per unit; cells, 2 words each (list offset << 12 |
//               count; a lower bound of the distance with the sign bit set when the cell centre
//               is inside); chords, 4 floats each; lists, 16-bit chord indices, two per word
layout(std430, binding = VXB_GlyphOutlineFlatBuffer) readonly buffer GlyphOutlineFlatBuffer {
    uint glyphOutlineWords[];
};

// The walk over one baked glyph block, in font units. These are the statements of
// GlyphBaker.Evaluate (kernel tool) and of EvaluateGlyphBakeBlock (the CPU face,
// libraries/SVO/src/Recipe/GlyphBakeWalk.cpp), in the same order.
//
// Outside the grid: the distance to the glyph bounds, a lower bound. In a cell farther from the
// outline than the exact band of the bake: the stored lower bound with the stored sign, and no
// chord is read. Otherwise one counted loop over the chords listed for the cell: squared distance
// (one square root at the end), and the sign from the crossings of the path
// sample -> (centre x, sample y) -> centre, which stays inside the cell, so every chord it can
// cross is in the list. The loop bound is data, so the code is not multiplied by a capacity at
// every glyph call site of the ray-march shader.
float VixenGlyphBakeBlock(uint block, float px, float py) {
    int columns = int(glyphOutlineWords[block + 1u]);
    int rows = int(glyphOutlineWords[block + 2u]);
    float gridMinX = uintBitsToFloat(glyphOutlineWords[block + 8u]);
    float gridMinY = uintBitsToFloat(glyphOutlineWords[block + 9u]);
    float cellsPerUnitX = uintBitsToFloat(glyphOutlineWords[block + 10u]);
    float cellsPerUnitY = uintBitsToFloat(glyphOutlineWords[block + 11u]);
    precise float cellX = (px - gridMinX) * cellsPerUnitX;
    precise float cellY = (py - gridMinY) * cellsPerUnitY;
    if (!(cellX >= 0.0 && cellY >= 0.0 && cellX < float(columns) && cellY < float(rows))) {
        precise float outX = max(max(uintBitsToFloat(glyphOutlineWords[block + 4u]) - px,
                                     px - uintBitsToFloat(glyphOutlineWords[block + 6u])), 0.0);
        precise float outY = max(max(uintBitsToFloat(glyphOutlineWords[block + 5u]) - py,
                                     py - uintBitsToFloat(glyphOutlineWords[block + 7u])), 0.0);
        precise float outside = outX * outX + outY * outY;
        return sqrt(outside);
    }
    int ix = int(cellX);
    int iy = int(cellY);
    uint cell = block + 12u + uint(iy * columns + ix) * 2u;
    uint listWord = glyphOutlineWords[cell];
    uint boundWord = glyphOutlineWords[cell + 1u];
    uint count = listWord & 4095u;
    uint offset = listWord >> 12u;
    bool inside = (boundWord & 0x80000000u) != 0u;
    if (count == 0u) {
        float bound = uintBitsToFloat(boundWord & 0x7FFFFFFFu);
        return inside ? -bound : bound;
    }
    precise float centreX = gridMinX + (float(ix) + 0.5) / cellsPerUnitX;
    precise float centreY = gridMinY + (float(iy) + 0.5) / cellsPerUnitY;
    float lowX = min(px, centreX);
    float highX = max(px, centreX);
    float lowY = min(py, centreY);
    float highY = max(py, centreY);
    uint chordBase = block + 12u + uint(columns * rows) * 2u;
    uint listBase = chordBase + glyphOutlineWords[block] * 4u;
    precise float minimumSquared = uintBitsToFloat(0x7F7FFFFFu);
    [[dont_unroll]] for (uint index = 0u; index < count; ++index) {
        uint entry = offset + index;
        uint chord = chordBase +
            ((glyphOutlineWords[listBase + (entry >> 1u)] >> ((entry & 1u) * 16u)) & 0xFFFFu) * 4u;
        float ax = uintBitsToFloat(glyphOutlineWords[chord]);
        float ay = uintBitsToFloat(glyphOutlineWords[chord + 1u]);
        float bx = uintBitsToFloat(glyphOutlineWords[chord + 2u]);
        float by = uintBitsToFloat(glyphOutlineWords[chord + 3u]);
        precise float ex = bx - ax;
        precise float ey = by - ay;
        precise float wx = px - ax;
        precise float wy = py - ay;
        precise float lengthSquared = ex * ex + ey * ey;
        precise float along = wx * ex + wy * ey;
        precise float t = lengthSquared > 0.0 ? clamp(along / lengthSquared, 0.0, 1.0) : 0.0;
        precise float dx = wx - ex * t;
        precise float dy = wy - ey * t;
        precise float squared = dx * dx + dy * dy;
        minimumSquared = min(minimumSquared, squared);
        if ((ay <= py) != (by <= py)) {
            // crossing x > q  <=>  (wy * ex - (q - ax) * ey) has the sign of ey
            precise float low = wy * ex - (lowX - ax) * ey;
            precise float high = wy * ex - (highX - ax) * ey;
            bool beyondLow = ey > 0.0 ? low > 0.0 : low < 0.0;
            bool beyondHigh = ey > 0.0 ? high > 0.0 : high < 0.0;
            if (beyondLow && !beyondHigh) inside = !inside;
        }
        if ((ax <= centreX) != (bx <= centreX)) {
            precise float vx = centreX - ax;
            precise float low = vx * ey - (lowY - ay) * ex;
            precise float high = vx * ey - (highY - ay) * ex;
            bool beyondLow = ex > 0.0 ? low > 0.0 : low < 0.0;
            bool beyondHigh = ex > 0.0 ? high > 0.0 : high < 0.0;
            if (beyondLow && !beyondHigh) inside = !inside;
        }
    }
    float distance = sqrt(minimumSquared);
    return inside ? -distance : distance;
}

// The walk over one row: the least signed distance over the baked glyphs placed in it, in row
// units (EvaluateGlyphBakeRow on the CPU). Exact outside every glyph, and inside while the
// glyphs of the row do not overlap; where they overlap the sign is right and the magnitude is
// a lower bound. A row without placements is empty space.
float SdfCore_GlyphOutlineBounded(vec3 p, uint glyphIndex) {
    float minimumDistance = uintBitsToFloat(1621981420u);
    uint rowCount = glyphOutlineWords[0];
    if (glyphIndex >= rowCount) return minimumDistance;
    uint rowBase = 1u + glyphIndex * 2u;
    uint placementBase = glyphOutlineWords[rowBase];
    uint placementCount = glyphOutlineWords[rowBase + 1u];
    [[dont_unroll]] for (uint index = 0u; index < placementCount; ++index) {
        uint placement = placementBase + index * 5u;
        float fontUnitsPerRowUnit = uintBitsToFloat(glyphOutlineWords[placement + 3u]);
        precise float qx = (p.x - uintBitsToFloat(glyphOutlineWords[placement + 1u])) * fontUnitsPerRowUnit;
        precise float qy = (p.y - uintBitsToFloat(glyphOutlineWords[placement + 2u])) * fontUnitsPerRowUnit;
        precise float distance = VixenGlyphBakeBlock(glyphOutlineWords[placement], qx, qy) *
            uintBitsToFloat(glyphOutlineWords[placement + 4u]);
        minimumDistance = min(minimumDistance, distance);
    }
    return minimumDistance;
}

// Rename the fixed-capacity generated helper below.  The emitter routes the production uber
// recipe calls to SdfCore_GlyphOutlineBounded, leaving the legacy helper available to the
// specialized shader path without making it the uber shader's reachable implementation.
#define SdfCore_GlyphOutline VIXEN_SdfCore_GlyphOutlineLegacy

#endif
