// ============================================================================
// HitRecord.glsl - per-pixel hit record (G-buffer-equivalent)
// ============================================================================
// Sampled Lighting Inc1 M3: the record a primary pass writes and a shade pass
// reads, splitting "what did the ray hit" from "how do we light it". This
// milestone keeps march and shade in the SAME shader (BodyInstanceRayMarch.comp)
// and proves the SSBO round-trip is lossless; Task 4 moves the shade into a
// separate DirectLighting.comp pass that only reads this buffer.
//
// Hand-written (not [GpuStruct]-codegenned): this struct has NO C++ consumer
// this milestone (no host upload, no CPU-side struct in engine code — the only
// C++ reader is the SDI-parity test's own local mirror struct, exactly the way
// test_lightingconfig_sdi_parity.cpp checks OctreeConfig/LightingConfig without
// requiring every reflected shader struct to be an engine type). Running the
// Yeroket CodegenTool for a struct nothing in C++ includes would emit a
// Generated/HitRecord.g.h with zero consumers — disproportionate for this
// milestone's scope. If Task 4 or later grows a real C++-side consumer (e.g. a
// CPU debug inspector), promote this to [GpuStruct] then, mirroring the
// LightingConfig precedent.
//
// std430 layout (see test_hitrecord_sdi_parity.cpp for the SPIR-V-reflection-
// vs-C++-mirror drift-guard proof). vec3's base alignment is 16 B but its SIZE
// is only 12 B, so a scalar immediately following a vec3 packs into that vec3's
// trailing 4 B instead of starting a new 16 B slot — every field below is
// tightly packed on that rule, not naively 16-aligned per field:
//   0  albedo       vec3   (12 B)
//   12 roughness    float  (fills albedo's trailing 4 B)
//   16 worldNormal  vec3   (12 B; starts fresh at 16 since offset 16 already
//                           satisfies vec3's 16 B alignment)
//   28 hitT         float  (fills worldNormal's trailing 4 B)
//   32 worldPos     vec3   (12 B; offset 32 already 16-aligned)
//   44 flags        uint   (fills worldPos's trailing 4 B)
//   48 _pad0        uint[3]  (explicit pad -> 60 B, then struct-tail-aligned
//                             to 64 B for the array stride, since the largest
//                             member alignment in this struct is 16 B)
// Total: 64 bytes/element.
//
// flags: bit0 (0x1) = hit/miss (1 = TraceWorld found a hit; 0 = sky/miss).
// bit1 (0x2) = CELL_RESOLVED (W-LEAN L1): the pixel's blend weight saturates
// (footprint ≥ 2·detailSize0 ⇔ the resolve replaces its ENTIRE color) — the
// shadow wave classifies + stamps it, then skips per-pixel work the composite
// would discard; later lean slices (transport skip / resolve fold) read the
// SAME bit so the classification has one owner. Only ever set when the lean
// switch is on (params camForward.w > 0).
//
// _pad0 usage (MASTER ledger — ShadowVisibilityWave.comp carries a mirror):
// [0] = winning instance index (M3 round 3; the Cornell diagnostics read it
// back, and W2's recipe-bucketed shade resolves pixel->instance->recipe
// through it). [1] = M11.2: floatBitsToUint(WorldHit.emission) -- the winning
// instance's emission intensity, so SpatialReuseShade.comp can add a self-lit
// term at the primary hit without re-deriving it from bodyInstances[] a
// second time. [2] has split ownership: bits 0..3 = ShadowVisibilityWave's
// analytic-light bitmask (bit i = light i unoccluded), bit 4 = its ReSTIR
// reservoir-ray answer, bits 5..7 reserved, and bits 8..15 = the primary
// policy stencil (regime bits 8..10, VIRTUAL bit 11, MATERIALIZED bit 12,
// bits 13..15 reserved). The march owns bits 8..15; both shadow phases use
// masked RMWs and own only bits 0..4. (VIXEN_SHADOW_DBG's
// debug packing reuses all three under #ifdef, but that path never coexists
// with a real frame's [0]/[1]/[2] content.)
// ============================================================================

#ifndef HITRECORD_GLSL
#define HITRECORD_GLSL

#define HITRECORD_FLAG_HIT 0x1u
#define HITRECORD_FLAG_CELL_RESOLVED 0x2u
// round-9: set iff this pixel's FINAL (terminal) winning hit came off the
// far-field mip-resolve path (TraceWorld's WorldHit.wasFarField), not merely
// "some instance's far-field candidate won its own per-instance compare" --
// see WorldHit.wasFarField's comment in TraceWorld.glsl for why those differ.
// Rides the bestHit selection (overwritten/cleared whenever a later,
// non-far-field instance wins), never a sticky global.
#define HITRECORD_FLAG_FAR_FIELD 0x4u

struct HitRecord {
    vec3  albedo;
    float roughness;
    vec3  worldNormal;
    float hitT;
    vec3  worldPos;
    uint  flags;
    uint  _pad0[3];
};

#endif // HITRECORD_GLSL
