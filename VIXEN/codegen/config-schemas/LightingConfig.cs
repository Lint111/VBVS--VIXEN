using Yeroket.Util.KernelFramework;

// Canonical Light / LightingConfig — one source for C++ (Vixen::Gpu) + GLSL.
// Sampled Lighting Inc0 M1: lights become generated, drift-guarded data types.
//
// kind: 0 = directional (direction_or_position is a normalized direction,
// range unused), 1 = point (direction_or_position is a world-space position,
// range is the falloff radius) — point is unused by any consumer yet, but the
// field is shaped so the buffer layout survives adding it later without a
// re-layout.
//
// std430 offsets (see LightingConfig.g.h for the emitted static_asserts):
// Light: direction_or_position@0, kind@12, radiance@16, range@28 (size 32).
// LightingConfig: lightCount@0, ambientIntensity@4, lights@16 (4 * 32 = 128
// -> ends 144); the gap from @8 to @16 is implicit std430 padding — lights[]
// is an array of a 16-byte-aligned nested struct, so the array itself is
// 16-aligned.
[GpuStruct]
public struct Light
{
    public Float3 direction_or_position;
    public uint kind;
    public Float3 radiance;
    public float range;
}

// UBO-friendly fixed array (kMaxLightsInc0 lights) rather than an SSBO
// variable-length array — Inc0 scope is a small fixed light set.
public static class LightingConfigLimits
{
    public const int kMaxLightsInc0 = 4;
}

[GpuStruct]
public struct LightingConfig
{
    public uint lightCount;
    public float ambientIntensity;

    [GpuArray(LightingConfigLimits.kMaxLightsInc0)] public Light lights;
}
