using Yeroket.Util.KernelFramework;

// Canonical Light / LightingConfig — one source for C++ (Vixen::Gpu) + GLSL.
// Sampled Lighting Inc0 M1: lights become generated, drift-guarded data types.
//
// kind: 0 = directional (direction_or_position is a normalized direction,
// range unused), 1 = point (direction_or_position is a world-space position,
// range is the falloff radius). Both kinds feed the shared shading path.
//
// std430 offsets (see LightingConfig.g.h for the emitted static_asserts):
// Light keeps its original 32-byte stride so Lambert+GGX addresses every
// existing light exactly as before. LightingConfig stores the per-light cel
// spill-purpose scales after the lights array at byte 144, then cel settings.
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

    // 1 is a regular source, lower values spread softly, and higher values
    // produce stronger, more regional spill. Kept parallel to lights[] so its
    // addition does not change the legacy Light array stride.
    [GpuArray(LightingConfigLimits.kMaxLightsInc0)] public float celSpillPurposeScales;

    // Runtime node parameters for the selectable Lambert+GGX and cel modes.
    public uint shadingMode;
    public uint celBandCount;
    public float celShadowThreshold;
    public float celLitThreshold;
    public float celRampSoftness;
    public float celLitHueShiftDegrees;
    public float celShadowHueShiftDegrees;
    public float celBandFalloffStart;
    public float celBandFalloffEnd;
    public float celLightSpillScale;
    // Round the struct size to std430's 16-byte structure alignment.
    public float _celTailPadding0;
    public float _celTailPadding1;
}
