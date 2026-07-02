using System.IO;
using Xunit;

public class CliTests
{
    static string WriteSchema(string dir)
    {
        Directory.CreateDirectory(dir);
        var p = Path.Combine(dir, "SkeletonConfig.cs");
        File.WriteAllText(p,
            "using Vixen.Codegen.Attributes;\n" +
            "[GpuStruct] public struct SkeletonConfig { public uint a; public int b; }\n");
        return p;
    }

    [Fact]
    public void Generate_ThenCheck_IsClean()
    {
        var tmp = Path.Combine(Path.GetTempPath(), "vcg_" + System.Guid.NewGuid().ToString("N"));
        var schema = Path.Combine(tmp, "schemas");
        WriteSchema(schema);
        var cpp = Path.Combine(tmp, "gen", "SkeletonConfig.g.h");
        var glsl = Path.Combine(tmp, "gen", "SkeletonConfig.glsl");

        int gen = Vixen.Codegen.Program.Main(new[]
            { "--schema", schema, "--out-cpp", cpp, "--out-glsl", glsl });
        Assert.Equal(0, gen);
        Assert.Contains("uint32_t a;", File.ReadAllText(cpp));
        Assert.Contains("uint a;", File.ReadAllText(glsl));

        int check = Vixen.Codegen.Program.Main(new[]
            { "--schema", schema, "--out-cpp", cpp, "--out-glsl", glsl, "--check" });
        Assert.Equal(0, check);

        File.WriteAllText(cpp, "stale");
        int check2 = Vixen.Codegen.Program.Main(new[]
            { "--schema", schema, "--out-cpp", cpp, "--out-glsl", glsl, "--check" });
        Assert.Equal(1, check2);
    }
}
