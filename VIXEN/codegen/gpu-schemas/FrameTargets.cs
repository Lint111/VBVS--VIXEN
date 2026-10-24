using Undertow.Modding.Schema;
using Yeroket.Util.KernelFramework;
using static Undertow.Modding.Schema.Schema;

namespace Vixen.Codegen.Gpu;

public readonly struct FrameTargets { }

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

public static class FrameKinds
{
    public static readonly KindSchema FrameTargets = Simple(
        F<Texture2D<Rgba16f>>("SceneRadianceHistoryOutput").Dynamic(),
        F<Texture2D<Rgba8>>("OutputImage").Dynamic(),
        F<UInt>("PresentationMode").Default(0).Dynamic()).Singleton();
}
