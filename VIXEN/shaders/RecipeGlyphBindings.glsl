// R631: GlyphOutline bytecode operands are indices into a runtime SSBO, not shader literals.
// This include must precede the generated recipe functions so GLSL sees the declaration while
// parsing their bodies. The generated struct guard keeps it compatible with SdfCoreKernels.
#ifndef VIXEN_RECIPE_GLYPH_BINDINGS_GLSL
#define VIXEN_RECIPE_GLYPH_BINDINGS_GLSL

#extension GL_EXT_control_flow_attributes : enable

// Keep the canonical row declaration visible to SDI/codegen and specialized shaders. The
// production uber source removes the unreachable generated helper at assembly time; the live
// glyph walk below uses the packed binding and never reads this typed aggregate.
#include "recipe/GlyphOutlineInstance.g.glsl"

layout(std430, binding = 51) readonly buffer GlyphOutlineBuffer {
    GlyphOutlineInstance glyphOutlineInstances[];
};

// R768: production recipes use a compact row/segment stream instead of exposing the fixed
// 3,616-byte aggregate to every glyph call site. One row has ten words (offset/count/fill/
// bounds); each segment has fourteen words (four float3 control points plus two uints).
layout(std430, binding = 52) readonly buffer GlyphOutlineFlatBuffer {
    uint glyphOutlineWords[];
};

// The generated SdfCore_GlyphOutline helper is retained for standalone recipe shaders, but
// the production uber path uses this bounded SSBO walk.  Keeping the loop bound data-driven is
// important here: a fixed SegmentCapacity loop is multiplied by every glyph call site in the
// large ray-march shader and can make the driver's first pipeline compile superlinear in the
// number of recipes/layouts.
float SdfCore_GlyphOutlineBounded(vec3 p, uint glyphIndex) {
    precise float minimumDistance = uintBitsToFloat(1621981420u);
    uint crossings = 0u;
    int winding = 0;
    const uint kHeaderWords = 10u;
    const uint kSegmentWords = 14u;
    uint rowCount = glyphOutlineWords[0];
    if (glyphIndex >= rowCount) return minimumDistance;
    uint header = 1u + glyphIndex * kHeaderWords;
    uint segmentOffset = glyphOutlineWords[header + 0u];
    int segmentCount = int(min(glyphOutlineWords[header + 1u], uint(SegmentCapacity)));
    vec2 boundsMin = vec2(uintBitsToFloat(glyphOutlineWords[header + 4u]),
                          uintBitsToFloat(glyphOutlineWords[header + 5u]));
    vec2 boundsMax = vec2(uintBitsToFloat(glyphOutlineWords[header + 7u]),
                          uintBitsToFloat(glyphOutlineWords[header + 8u]));
    bool checkWinding = p.x >= boundsMin.x && p.x <= boundsMax.x &&
        p.y >= boundsMin.y && p.y <= boundsMax.y;
    uint segmentDataBase = 1u + rowCount * kHeaderWords;
    [[dont_unroll]] for (int segmentIndex = 0; segmentIndex < segmentCount; ++segmentIndex) {
        uint segmentBase = segmentDataBase + (segmentOffset + uint(segmentIndex)) * kSegmentWords;
        precise vec2 p0 = vec2(uintBitsToFloat(glyphOutlineWords[segmentBase + 0u]),
                               uintBitsToFloat(glyphOutlineWords[segmentBase + 1u]));
        precise vec2 p1 = vec2(uintBitsToFloat(glyphOutlineWords[segmentBase + 3u]),
                               uintBitsToFloat(glyphOutlineWords[segmentBase + 4u]));
        precise vec2 p2 = vec2(uintBitsToFloat(glyphOutlineWords[segmentBase + 6u]),
                               uintBitsToFloat(glyphOutlineWords[segmentBase + 7u]));
        precise vec2 p3 = vec2(uintBitsToFloat(glyphOutlineWords[segmentBase + 9u]),
                               uintBitsToFloat(glyphOutlineWords[segmentBase + 10u]));
        uint segmentKind = glyphOutlineWords[segmentBase + 12u];
        precise vec2 previous = p0;
        [[dont_unroll]] for (int sampleIndex = 1; sampleIndex <= 16; ++sampleIndex) {
            precise float t = float(sampleIndex) / uintBitsToFloat(1098907648u);
            precise float u = uintBitsToFloat(1065353216u) - t;
            precise vec2 current = vec2(uintBitsToFloat(0u), uintBitsToFloat(0u));
            if (segmentKind == 0u) {
                current = p0 * u + p1 * t;
            } else if (segmentKind == 1u) {
                current = p0 * (u * u) + p1 * (uintBitsToFloat(1073741824u) * u * t) + p2 * (t * t);
            } else {
                current = p0 * (u * u * u) + p1 * (uintBitsToFloat(1077936128u) * u * u * t) +
                    p2 * (uintBitsToFloat(1077936128u) * u * t * t) + p3 * (t * t * t);
            }
            precise vec2 edge = current - previous;
            precise float edgeLengthSquared = edge.x * edge.x + edge.y * edge.y;
            precise float projection = edgeLengthSquared > uintBitsToFloat(0u)
                ? ((p.x - previous.x) * edge.x + (p.y - previous.y) * edge.y) / edgeLengthSquared
                : uintBitsToFloat(0u);
            projection = clamp(projection, uintBitsToFloat(0u), uintBitsToFloat(1065353216u));
            precise vec2 closest = previous + edge * projection;
            precise float distance = length(vec2(p.x - closest.x, p.y - closest.y));
            minimumDistance = min(minimumDistance, distance);
            if (checkWinding && ((previous.y <= p.y && current.y > p.y) ||
                                 (previous.y > p.y && current.y <= p.y))) {
                precise float crossingX = previous.x + (p.y - previous.y) *
                    (current.x - previous.x) / (current.y - previous.y);
                if (crossingX > p.x) {
                    crossings += 1u;
                    winding += current.y > previous.y ? 1 : -1;
                }
            }
            previous = current;
        }
    }
    bool inside = checkWinding && (glyphOutlineWords[header + 2u] == 1u
        ? (crossings & 1u) != 0u
        : winding != 0);
    return inside ? -minimumDistance : minimumDistance;
}

// Rename the fixed-capacity generated helper below.  The emitter routes the production uber
// recipe calls to SdfCore_GlyphOutlineBounded, leaving the legacy helper available to the
// specialized shader path without making it the uber shader's reachable implementation.
#define SdfCore_GlyphOutline VIXEN_SdfCore_GlyphOutlineLegacy

#endif
