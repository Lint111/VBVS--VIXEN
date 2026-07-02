using System;
using System.IO;
using System.Linq;
using Vixen.Codegen.Emit;
using Vixen.Codegen.Layout;

namespace Vixen.Codegen;

public static class Program
{
    public static int Main(string[] args)
    {
        string? schema = Flag(args, "--schema"), outCpp = Flag(args, "--out-cpp"), outGlsl = Flag(args, "--out-glsl");
        bool check = args.Contains("--check");
        if (schema is null || outCpp is null || outGlsl is null)
        { Console.Error.WriteLine("usage: --schema <dir> --out-cpp <path> --out-glsl <path> [--check]"); return 2; }

        var files = Directory.GetFiles(schema, "*.cs", SearchOption.AllDirectories);
        var structs = CompilationLoader.LoadGpuStructs(files);
        if (structs.Count != 1)
        { Console.Error.WriteLine($"P0 expects exactly one [GpuStruct]; found {structs.Count}"); return 2; }

        var model = StructLayout.Build(structs[0]);
        var cpp = CppStructEmitter.Emit(model);
        var glsl = GlslStructEmitter.Emit(model);

        if (check)
        {
            if (!Same(outCpp, cpp)) { Console.Error.WriteLine($"STALE: {outCpp}"); return 1; }
            if (!Same(outGlsl, glsl)) { Console.Error.WriteLine($"STALE: {outGlsl}"); return 1; }
            return 0;
        }
        Write(outCpp, cpp); Write(outGlsl, glsl);
        return 0;
    }

    static string? Flag(string[] a, string name)
    { int i = Array.IndexOf(a, name); return i >= 0 && i + 1 < a.Length ? a[i + 1] : null; }
    static bool Same(string path, string text) => File.Exists(path) && File.ReadAllText(path) == text;
    static void Write(string path, string text)
    { Directory.CreateDirectory(Path.GetDirectoryName(path)!); File.WriteAllText(path, text); }
}
