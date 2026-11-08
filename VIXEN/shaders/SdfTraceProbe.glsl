// ============================================================================
// SdfTraceProbe.glsl - reusable per-pixel SDF ray trace probe (T-2895, R464).
// 1:1 MIRROR of libraries/SVO/include/SdfTraceProbe.h (the CPU decoder). Keep the
// word layout identical.
//
// Opt-in: a test or debug harness defines VIXEN_SDF_TRACE_PROBE (e.g. right after the
// #version line) and binds a storage buffer of width*height*SDF_TRACE_PROBE_WORDS uint32
// at SDF_TRACE_PROBE_BINDING. Without the define this file compiles to nothing and the
// production shaders carry no probe code or binding.
//
// Requires imageSize(outputImage) in scope (BodyInstanceRayMarch.comp binding 0); it is
// included from SdfRecipes.glsl, which that shader pulls in after declaring outputImage.
//
// Record (16 words per pixel, row-major):
//    0 steps        sphere-trace iterations used
//    1 termination  SDF_TRACE_TERM_* below
//    2 recipeId
//    3 gridDim      occupancy acceleration grid dimension (0 = no grid)
//    4 t            ray parameter at the final sample (float bits)
//    5 sdf          field value at the final sample
//    6-8 position   final sample position
//    9-11 gradient  field gradient at the final sample (zero when not computed)
//   12 gridBound    last occupancy-grid empty-space bound applied
//   13 tNear        entry of the ray/bound interval
//   14 tFar         exit of the ray/bound interval
//   15 lastStep     last march step length taken
// ============================================================================
#ifndef SDF_TRACE_PROBE_GLSL
#define SDF_TRACE_PROBE_GLSL

#ifdef VIXEN_SDF_TRACE_PROBE

#define SDF_TRACE_PROBE_BINDING 48
#define SDF_TRACE_PROBE_WORDS   16u

#define SDF_TRACE_TERM_BOUND_MISS 0u  // ray misses the recipe bound
#define SDF_TRACE_TERM_HIT        1u  // surface hit
#define SDF_TRACE_TERM_BUDGET     2u  // step budget exhausted
#define SDF_TRACE_TERM_EXIT       3u  // marched past tFar

layout(std430, binding = SDF_TRACE_PROBE_BINDING) buffer SdfTraceProbeBuffer {
    uint sdfTraceProbeWords[];
};

void sdfTraceProbeRecord(ivec2 pixel, uint steps, uint termination, uint recipeId,
                         uint gridDim, float t, float sdf, vec3 position, vec3 gradient,
                         float gridBound, float tNear, float tFar, float lastStep) {
    ivec2 size = imageSize(outputImage);
    if (any(lessThan(pixel, ivec2(0))) || any(greaterThanEqual(pixel, size))) return;
    uint base = (uint(pixel.y) * uint(size.x) + uint(pixel.x)) * SDF_TRACE_PROBE_WORDS;
    sdfTraceProbeWords[base + 0u] = steps;
    sdfTraceProbeWords[base + 1u] = termination;
    sdfTraceProbeWords[base + 2u] = recipeId;
    sdfTraceProbeWords[base + 3u] = gridDim;
    sdfTraceProbeWords[base + 4u] = floatBitsToUint(t);
    sdfTraceProbeWords[base + 5u] = floatBitsToUint(sdf);
    sdfTraceProbeWords[base + 6u] = floatBitsToUint(position.x);
    sdfTraceProbeWords[base + 7u] = floatBitsToUint(position.y);
    sdfTraceProbeWords[base + 8u] = floatBitsToUint(position.z);
    sdfTraceProbeWords[base + 9u] = floatBitsToUint(gradient.x);
    sdfTraceProbeWords[base + 10u] = floatBitsToUint(gradient.y);
    sdfTraceProbeWords[base + 11u] = floatBitsToUint(gradient.z);
    sdfTraceProbeWords[base + 12u] = floatBitsToUint(gridBound);
    sdfTraceProbeWords[base + 13u] = floatBitsToUint(tNear);
    sdfTraceProbeWords[base + 14u] = floatBitsToUint(tFar);
    sdfTraceProbeWords[base + 15u] = floatBitsToUint(lastStep);
}

#endif // VIXEN_SDF_TRACE_PROBE
#endif // SDF_TRACE_PROBE_GLSL
