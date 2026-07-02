using Vixen.Codegen.Emit;
using Vixen.Codegen.Layout;
using Xunit;

public class GlslStructEmitterTests
{
    [Fact]
    public void EmitsGlslStructWithGuard()
    {
        var m = new StructModel("SkeletonConfig", new[]
        {
            new FieldLayout("a", ScalarKind.U32, 0),
            new FieldLayout("b", ScalarKind.I32, 4),
        }, 8);
        var g = GlslStructEmitter.Emit(m);
        Assert.Contains("#ifndef SKELETONCONFIG_GLSL", g);
        Assert.Contains("struct SkeletonConfig {", g);
        Assert.Contains("uint a;", g);
        Assert.Contains("int b;", g);
    }
}
