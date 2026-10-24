using Undertow.Modding.Schema;
using Yeroket.Util.KernelFramework;
using static Undertow.Modding.Schema.Schema;
using System.Collections.Generic;

namespace Vixen.Codegen.Gpu;

public readonly struct FrameTargets { }

// Schema identity for the per-pixel hit buffer. Its std430 layout remains in
// shaders/HitRecord.glsl until the hand raymarch/readback consumers convert.
public readonly struct HitRecord { }

// The zero value is deliberately the capture/parity-safe mode. The current
// kernel schema surface carries enum domains separately from numeric default
// facets, so this field uses the uint backing while retaining one closed enum
// vocabulary for all producers.
public enum PresentationMode : byte
{
    Canonical = 0,
    Presentation = 1,
}

[KernelSystem("vixen:frame-targets")]
[Declare(typeof(FrameTargets))]
public sealed class FrameTargetsSystem { }

[KernelSystem("vixen:hit-record")]
[Declare(typeof(HitRecord))]
public sealed class HitRecordSystem { }

[KernelSystem("vixen:reservoir-record")]
[Declare(typeof(ReservoirRecord))]
public sealed class ReservoirRecordSystem { }

[KernelSystem("vixen:exposure-meter")]
[Declare(typeof(ExposureMeterState))]
public sealed class ExposureMeterSystem { }

[KernelSystem("vixen:camera")]
[Declare(typeof(PrevCameraConfig))]
public sealed class CameraSystem { }

[KernelSystem("vixen:lighting-settings")]
[Declare(typeof(LightingSettings))]
public sealed class LightingSettingsSystem { }

[KernelSystem("vixen:scene-look-settings")]
[Declare(typeof(SceneLookSettings))]
public sealed class SceneLookSettingsSystem { }

[KernelSystem("vixen:shadow-settings")]
[Declare(typeof(ShadowSettings))]
public sealed class ShadowSettingsSystem { }

[KernelSystem("vixen:accumulation-settings")]
[Declare(typeof(AccumulationSettings))]
public sealed class AccumulationSettingsSystem { }

[KernelSystem("vixen:reservoir-settings")]
[Declare(typeof(ReservoirSettings))]
public sealed class ReservoirSettingsSystem { }

[KernelSystem("vixen:probe-grid-settings")]
[Declare(typeof(ProbeGridSettings))]
public sealed class ProbeGridSettingsSystem { }

public static class FrameKinds
{
    public static readonly KindSchema FrameTargets = Simple(
        F<Texture2D<Rgba16f>>("SceneRadianceHistoryOutput").Dynamic(),
        F<Texture2D<Rgba8>>("OutputImage").Dynamic(),
        F<UInt>("PresentationMode").Default(0).Dynamic()).Singleton();
}

public static class HitRecordKinds
{
    public static readonly KindSchema HitRecord = Simple();
}

public static class ReservoirRecordKinds
{
    public static readonly KindSchema ReservoirRecord = Simple(
        F<UInt>("y"),
        F<Float>("weightSum"),
        F<UInt>("sampleCount"),
        F<Float>("targetPdf"));
}

public static class ExposureMeterKinds
{
    public static readonly KindSchema ExposureMeterState = Simple(
        F<Float>("log2Exposure"),
        F<UInt>("state"),
        F<Float>("meteredEV100"),
        F<Float>("targetLog2Exposure"),
        F<UInt>("histogram").Bucketed(128).Dynamic());
}

public static class CameraKinds
{
    public static readonly KindSchema PrevCameraConfig = Simple().Singleton();
}

public static class LightingSettingsKinds
{
    public static readonly KindSchema LightingSettings = Simple().Singleton();
}

public static class SceneLookSettingsKinds
{
    public static readonly KindSchema SceneLookSettings = Simple().Singleton();
}

public static class ShadowSettingsKinds
{
    public static readonly KindSchema ShadowSettings = Simple().Singleton();
}

public static class AccumulationSettingsKinds
{
    public static readonly KindSchema AccumulationSettings = Simple().Singleton();
}

public static class ReservoirSettingsKinds
{
    public static readonly KindSchema ReservoirSettings = Simple().Singleton();
}

public static class ProbeGridSettingsKinds
{
    public static readonly KindSchema ProbeGridSettings = Simple().Singleton();
}

// Existing SystemSchema is the declaration face for host writers and native
// GPU stages.  Every path is a schema kind plus an exact member; the scheduler
// derives the CPU-to-GPU transfer rows from these footprints.
public static class KindSchemas
{
    public static readonly IReadOnlyList<SystemSchema> Systems = new[]
    {
        new SystemSchema("vixen:lighting-settings-write", writes: new[] {
            "state:lighting-settings.lightCount", "state:lighting-settings.ambientIntensity",
            "state:lighting-settings.lights", "state:lighting-settings.celSpillPurposeScales",
            "state:lighting-settings.shadingMode", "state:lighting-settings.celBandCount",
            "state:lighting-settings.celShadowThreshold", "state:lighting-settings.celLitThreshold",
            "state:lighting-settings.celRampSoftness", "state:lighting-settings.celLitHueShiftDegrees",
            "state:lighting-settings.celShadowHueShiftDegrees", "state:lighting-settings.celBandFalloffStart",
            "state:lighting-settings.celBandFalloffEnd", "state:lighting-settings.celLightSpillScale",
            "state:lighting-settings.dimBandFloor", "state:lighting-settings.skyColor",
            "state:lighting-settings.skyMode", "state:lighting-settings.skyGradientSlope" }),
        new SystemSchema("vixen:scene-look-settings-write", writes: new[] {
            "state:scene-look-settings.exposureCompensationEV", "state:scene-look-settings.exposureKeyValue",
            "state:scene-look-settings.exposureMinEV100", "state:scene-look-settings.exposureMaxEV100",
            "state:scene-look-settings.exposureLowPercent", "state:scene-look-settings.exposureHighPercent",
            "state:scene-look-settings.exposureSpeedUp", "state:scene-look-settings.exposureSpeedDown",
            "state:scene-look-settings.exposureDeltaSeconds", "state:scene-look-settings.lookSaturation",
            "state:scene-look-settings.presentationMode" }),
        new SystemSchema("vixen:shadow-settings-write", writes: new[] {
            "state:shadow-settings.enabled", "state:shadow-settings.raysPerLight",
            "state:shadow-settings.maxShadowDistance", "state:shadow-settings.biasEpsilon" }),
        new SystemSchema("vixen:accumulation-settings-write", writes: new[] {
            "state:accumulation-settings.enabled", "state:accumulation-settings.alpha",
            "state:accumulation-settings.maxFrames", "state:accumulation-settings.resetOnMotion",
            "state:accumulation-settings.reprojectionEnabled" }),
        new SystemSchema("vixen:reservoir-settings-write", writes: new[] {
            "state:reservoir-settings.reservoirEnabled", "state:reservoir-settings.candidateCount",
            "state:reservoir-settings.spatialRadius", "state:reservoir-settings.spatialCount",
            "state:reservoir-settings.temporalCap", "state:reservoir-settings.biasedModeEnabled",
            "state:reservoir-settings.lightTreeCutThreshold" }),
        new SystemSchema("vixen:probe-grid-settings-write", writes: new[] {
            "state:probe-grid-settings.probeGridEnabled", "state:probe-grid-settings.originX",
            "state:probe-grid-settings.originY", "state:probe-grid-settings.originZ",
            "state:probe-grid-settings.spacingX", "state:probe-grid-settings.spacingY",
            "state:probe-grid-settings.spacingZ", "state:probe-grid-settings.countX",
            "state:probe-grid-settings.countY", "state:probe-grid-settings.countZ",
            "state:probe-grid-settings.raysPerProbe", "state:probe-grid-settings.hysteresisRate",
            "state:probe-grid-settings.amortizationFactor" }),
        new SystemSchema("vixen:exposure-meter", reads: new[] {
            "state:frame-targets.SceneRadianceHistoryOutput" }, writes: new[] {
            "state:exposure-meter.histogram" }),
        new SystemSchema("vixen:exposure-reduce", reads: new[] {
            "state:exposure-meter.histogram",
            "state:scene-look-settings.exposureKeyValue",
            "state:scene-look-settings.exposureMinEV100",
            "state:scene-look-settings.exposureMaxEV100",
            "state:scene-look-settings.exposureLowPercent",
            "state:scene-look-settings.exposureHighPercent",
            "state:scene-look-settings.exposureSpeedUp",
            "state:scene-look-settings.exposureSpeedDown",
            "state:scene-look-settings.exposureDeltaSeconds" }, writes: new[] {
            "state:exposure-meter.log2Exposure", "state:exposure-meter.state",
            "state:exposure-meter.meteredEV100", "state:exposure-meter.targetLog2Exposure" }),
        new SystemSchema("vixen:direct-lighting", reads: new[] {
            "state:lighting-settings.lightCount", "state:lighting-settings.lights",
            "state:shadow-settings.enabled", "state:shadow-settings.raysPerLight",
            "state:shadow-settings.maxShadowDistance", "state:shadow-settings.biasEpsilon",
            "state:reservoir-settings.reservoirEnabled", "state:reservoir-settings.candidateCount",
            "state:reservoir-settings.temporalCap", "state:reservoir-settings.lightTreeCutThreshold" }),
        new SystemSchema("vixen:body-instance-ray-march", reads: new[] {
            "state:accumulation-settings.enabled", "state:accumulation-settings.alpha",
            "state:accumulation-settings.maxFrames", "state:accumulation-settings.resetOnMotion",
            "state:accumulation-settings.reprojectionEnabled" }),
        new SystemSchema("vixen:probe-update", reads: new[] {
            "state:probe-grid-settings.probeGridEnabled", "state:probe-grid-settings.originX",
            "state:probe-grid-settings.originY", "state:probe-grid-settings.originZ",
            "state:probe-grid-settings.spacingX", "state:probe-grid-settings.spacingY",
            "state:probe-grid-settings.spacingZ", "state:probe-grid-settings.countX",
            "state:probe-grid-settings.countY", "state:probe-grid-settings.countZ",
            "state:probe-grid-settings.raysPerProbe", "state:probe-grid-settings.hysteresisRate",
            "state:probe-grid-settings.amortizationFactor" }),
    };
}
