using Vixen.Codegen.Emit;
using Vixen.Codegen.Layout;
using Xunit;

public class CppStructEmitterTests
{
    [Fact]
    public void EmitsStructWithGuardAsserts()
    {
        var m = new StructModel("SkeletonConfig", new[]
        {
            new FieldLayout("a", ScalarKind.U32, 0),
            new FieldLayout("b", ScalarKind.I32, 4),
        }, 8);
        var h = CppStructEmitter.Emit(m);
        Assert.Contains("struct SkeletonConfig {", h);
        Assert.Contains("uint32_t a;", h);
        Assert.Contains("int32_t b;", h);
        Assert.Contains("static_assert(sizeof(SkeletonConfig) == 8", h);
        Assert.Contains("static_assert(offsetof(SkeletonConfig, b) == 4", h);
        Assert.Contains("#pragma once", h);
    }
}
