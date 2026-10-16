using Undertow.Modding.Schema;
using Yeroket.Util.KernelFramework;
using static Undertow.Modding.Schema.Schema;

namespace Vixen.Codegen.Gpu;

public readonly struct FrameTargets { }

[KernelSystem("vixen:frame-targets")]
[Declare(typeof(FrameTargets))]
public sealed class FrameTargetsSystem { }

public static class FrameKinds
{
    public static readonly KindSchema FrameTargets = Simple(
        F<Texture2D<Rgba16f>>("SceneRadianceHistoryOutput").Dynamic(),
        F<Texture2D<Rgba8>>("OutputImage").Dynamic()).Singleton();
}
