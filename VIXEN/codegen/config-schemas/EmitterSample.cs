using Yeroket.Util.KernelFramework;

namespace Vixen.Gpu;

// Canonical EmitterSample — one source for C++ (Vixen::Gpu) + GLSL.
// Count-agnostic lighting (R502, lightdesign §2.1): the one explicit emitter sample that
// sampleEmitter() returns and the S1 reservoir stores. 32 B std430 (vec3 + uint share a
// 16 B slot, so there is no padding).
//
//   position -- world-space point on the emitter surface.
//   key      -- pipeline(2b) | origin id | sub-seed; the normal is re-derived from it, not stored.
//   radiance -- colour x intensity (W sr^-1 m^-2) at the sample.
//   pdf      -- density w.r.t. area over ALL emitters (pipeline mixture already folded in).
[GpuStruct]
public struct EmitterSample
{
    public Float3 position; // @0
    public uint key;        // @12
    public Float3 radiance; // @16
    public float pdf;       // @28
}
