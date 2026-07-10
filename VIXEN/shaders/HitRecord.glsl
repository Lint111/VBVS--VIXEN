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
// ============================================================================

#ifndef HITRECORD_GLSL
#define HITRECORD_GLSL

#define HITRECORD_FLAG_HIT 0x1u

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
