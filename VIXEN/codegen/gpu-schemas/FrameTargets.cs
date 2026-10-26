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
        // Engine motion vectors (previous UV minus current UV, unjittered cameras), written by
        // the march from camera and instance transforms (ReconstructionMotion.glsl).
        F<Texture2D<Rg32f>>("MotionVectors").Dynamic(),
        // Camera-axis view depth of each pixel's primary hit (a miss is far), written from the
        // hit records by the reconstruction depth pass (ReconstructionDepth.comp). The sign
        // follows the depth convention, so readers compare magnitudes.
        F<Texture2D<R32f>>("ViewDepth").Dynamic(),
        // The anti-aliasing module's linear-HDR resolve: the exposure meter and the tone map read
        // it. With the `none` provider it is the scene radiance itself.
        F<Texture2D<Rgba16f>>("ResolvedRadiance").Dynamic(),
        F<Texture2D<Rgba8>>("OutputImage").Dynamic(),
        F<UInt>("PresentationMode").Default(0).Dynamic()).Singleton();
}
