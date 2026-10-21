using Yeroket.Util.KernelFramework;

namespace Vixen.Codegen.Gpu;

// Render-bridge S2: cold, schema-owned settings records.  The host settings
// write is the producer; these records are the component-region sources used
// by TransferPlan/Fill, which uploads a record only when its generation changes.
[GpuStruct]
public struct LightingSettings
{
    public uint lightCount;
    public float ambientIntensity;
    [GpuArray(LightingConfigLimits.kMaxLightsInc0)] public Light lights;
    [GpuArray(LightingConfigLimits.kMaxLightsInc0)] public float celSpillPurposeScales;
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
    public float dimBandFloor;
    public Float3 skyColor;
    public uint skyMode;
    public float skyGradientSlope;
}

[GpuStruct]
public struct SceneLookSettings
{
    public float exposureCompensationEV;
    public float exposureKeyValue;
    public float exposureMinEV100;
    public float exposureMaxEV100;
    public float exposureLowPercent;
    public float exposureHighPercent;
    public float exposureSpeedUp;
    public float exposureSpeedDown;
    public float exposureDeltaSeconds;
    public float lookSaturation;
    public uint presentationMode;
}

[GpuStruct]
public struct ShadowSettings
{
    public uint enabled;
    public uint raysPerLight;
    public float maxShadowDistance;
    public float biasEpsilon;
}

[GpuStruct]
public struct AccumulationSettings
{
    public uint enabled;
    public float alpha;
    public uint maxFrames;
    public uint resetOnMotion;
    public uint reprojectionEnabled;
}

[GpuStruct]
public struct ReservoirSettings
{
    public uint reservoirEnabled;
    public uint candidateCount;
    public float spatialRadius;
    public uint spatialCount;
    public uint temporalCap;
    public uint biasedModeEnabled;
    public float lightTreeCutThreshold;
}

[GpuStruct]
public struct ProbeGridSettings
{
    public uint probeGridEnabled;
    public float originX;
    public float originY;
    public float originZ;
    public float spacingX;
    public float spacingY;
    public float spacingZ;
    public uint countX;
    public uint countY;
    public uint countZ;
    public uint raysPerProbe;
    public float hysteresisRate;
    public uint amortizationFactor;
}
