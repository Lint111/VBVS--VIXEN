using Undertow.Modding.Schema;
using Yeroket.Util.KernelFramework;
using static Undertow.Modding.Schema.Schema;

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
        F<Float>("targetLog2Exposure"));
}

public static class CameraKinds
{
    public static readonly KindSchema PrevCameraConfig = Simple().Singleton();
}
