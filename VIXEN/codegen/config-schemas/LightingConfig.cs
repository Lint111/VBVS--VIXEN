using Yeroket.Util.KernelFramework;

// Canonical Light — one source for C++ (Vixen::Gpu) + GLSL, consumed by the
// LightingSettings record (RenderSettings.cs).
//
// kind: 0 = directional (direction_or_position is a normalized direction,
// range unused), 1 = point (direction_or_position is a world-space position,
// range is the falloff radius). Both kinds feed the shared shading path.
//
// Light keeps its original 32-byte stride so Lambert+GGX addresses every
// existing light exactly as before.
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
