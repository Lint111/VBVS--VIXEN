#ifndef PROBE_UPDATE_COMMON_GLSL
#define PROBE_UPDATE_COMMON_GLSL
// ============================================================================
// W1a (wavefront epoch): the ProbeUpdate family's shared contract.
// ============================================================================
// ProbeUpdate.comp's single megakernel split into ProbeGather.comp (primary
// rays + light pick + shadow-ray request emission) and ProbeApply.comp
// (visibility consumption + reduction + atlas writes), bridged by the
// ShadowRayTrace.comp traversal wave. EVERYTHING both halves must agree on —
// the workgroup shape, the probe-index remap, grid indexing, the queue slot
// function, and the per-ray payload layout — lives HERE exactly once, so the
// two passes structurally cannot diverge on the contract that makes their
// split bit-identical to the merged shader.
// ============================================================================

// Upper bound on ProbeGridConfig.raysPerProbe: local_size_x is a SHADER
// COMPILE-TIME constant (GLSL has no dynamic workgroup size), so the
// per-workgroup-per-probe shape needs a fixed ceiling. 256 matches this
// codebase's other compute passes' largest declared local_size (comfortably
// inside the 1024-invocation minimum-guaranteed core Vulkan limit). Rays
// beyond this ceiling are silently NOT cast (the rayIndex < raysPerProbe
// gate in both mains) — raising the budget is a recompile, no data-layout
// change. Also the STRIDE of the shadow-ray queue: slot addressing below
// bakes this constant in, so gather, wave sizing (CPU-side), and apply all
// inherit the same queue geometry from this one definition.
#define PROBE_UPDATE_MAX_RAYS_PER_PROBE 256u

#include "Generated/ProbeGridConfig.glsl"

layout(std430, binding = 28) readonly buffer ProbeGridConfigSSBO {
    ProbeGridConfig probeGridConfig;
};

uint ProbeUpdateProbeCount() {
    return probeGridConfig.countX * probeGridConfig.countY * probeGridConfig.countZ;
}

// Sparse-dispatch amortization stride-remap (Sampled Lighting Inc6 M1,
// mechanism B): the CPU dispatches ONLY ceil(probeCount/amortizationFactor)
// workgroups per pass; this recovers which probe THIS workgroup owns this
// tick. At F=1 it degenerates exactly to probeIndex = gl_WorkGroupID.x.
// GATHER and APPLY run in the SAME tick with the SAME frameCounter (the
// config SSBO is written once per tick CPU-side), so both halves resolve the
// SAME probe set — the split's slot-ownership invariant. Callers must still
// bound-check the result against ProbeUpdateProbeCount() (ceil-division
// over-dispatch produces out-of-range indices for the last rotation slot).
// `max(amortizationFactor, 1u)` guards divide/mod-by-zero defensively —
// upstream never produces 0, but the shader assumes nothing about CPU code
// it cannot see at compile time.
uint ProbeUpdateProbeIndex() {
    uint amortizationFactor = max(probeGridConfig.amortizationFactor, 1u);
    return gl_WorkGroupID.x * amortizationFactor +
           (probeGridConfig.frameCounter % amortizationFactor);
}

// Probe grid indexing: X fastest, then Y, then Z (matches
// BuildRenderGraph.cpp's atlas layout comment: "columns sweep the grid's X
// axis, rows sweep Y, and Z-slices tile across the texture width").
uvec3 ProbeUpdateProbeCoords(uint probeIndex) {
    uint px = probeIndex % probeGridConfig.countX;
    uint py = (probeIndex / probeGridConfig.countX) % probeGridConfig.countY;
    uint pz = probeIndex / (probeGridConfig.countX * probeGridConfig.countY);
    return uvec3(px, py, pz);
}

vec3 ProbeUpdateProbeWorldPos(uvec3 pc) {
    return vec3(
        probeGridConfig.originX + float(pc.x) * probeGridConfig.spacingX,
        probeGridConfig.originY + float(pc.y) * probeGridConfig.spacingY,
        probeGridConfig.originZ + float(pc.z) * probeGridConfig.spacingZ);
}

// Fixed-slot queue addressing: slot identity is a pure function of
// (probe, ray) over the FULL grid — F-independent buffer sizing
// (probeCount * PROBE_UPDATE_MAX_RAYS_PER_PROBE slots), no atomics, and a
// stable slot<->ray mapping for debugging. Slots of probes not dispatched
// this tick go STALE; the wave re-traces them wastefully but apply never
// reads them (it only consumes slots its own remap resolves this tick) —
// that waste is bounded by the amortization fraction and measured by the
// wave's own CSV column before any compaction scheme is considered.
uint ProbeUpdateShadowSlot(uint probeIndex, uint rayIndex) {
    return probeIndex * PROBE_UPDATE_MAX_RAYS_PER_PROBE + rayIndex;
}

// Per-ray intermediate carried from gather to apply (16 B under std430).
// contributionIfVisible = the ray's full direct-lighting term ASSUMING the
// shadow ray finds no occluder (apply multiplies by the wave's 0/1 answer —
// x*1.0 and x*0.0 are bit-exact, preserving numeric identity with the
// merged shader). depth encodes the primary hit: hit <=> depth > 0
// (TraceWorld's tmin is 0.001, so a real hit distance can never be 0), and
// misses/idle lanes write 0 — exactly the zero contribution both give the
// merged shader's reduction.
struct ProbeRayPayload {
    vec3 contributionIfVisible; float depth;
};

#endif // PROBE_UPDATE_COMMON_GLSL
