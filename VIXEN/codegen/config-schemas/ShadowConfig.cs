using Yeroket.Util.KernelFramework;

// Canonical ShadowConfig — one source for C++ (Vixen::Gpu) + GLSL.
// Sampled Lighting Inc1 M4: shadow-pass compute budget as drift-guarded data, not
// magic numbers. Mirrors LightingConfig.cs's precedent exactly (see that file for
// the [GpuStruct]/std430 codegen contract).
//
// enabled: 0/1 master switch. When 0, DirectLighting's shadow term is skipped
// entirely and the pass reduces EXACTLY to the pre-shadow unshadowed direct term
// (the A/B lever the M4 byte-identity gate exercises).
// raysPerLight: 1 for Inc1 hard shadows. The field exists (rather than a bare
// bool) so soft/area shadows later are a DATA change, not a shader rewrite.
// maxShadowDistance: caps shadow-ray traversal cost — the same "cut Tmax short"
// lever roughness-based Tmax-cutting already applies to primary rays, applied to
// shadow rays. Large (whole-scene) by default.
// biasEpsilon: self-intersection guard (shadow acne) — the offset TraceWorldShadow's
// caller applies per that function's own documented convention (origin + normal*eps,
// tmin left near 0).
[GpuStruct]
public struct ShadowConfig
{
    public uint enabled;
    public uint raysPerLight;
    public float maxShadowDistance;
    public float biasEpsilon;
}
