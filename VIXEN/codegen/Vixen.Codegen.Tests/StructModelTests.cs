using System.Linq;
using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.CSharp;
using Vixen.Codegen.Layout;
using Xunit;

public class StructModelTests
{
    // Compiles a snippet and returns the named struct symbol.
    static INamedTypeSymbol Symbol(string src, string name)
    {
        var tree = CSharpSyntaxTree.ParseText(src);
        var refs = new[] { MetadataReference.CreateFromFile(typeof(object).Assembly.Location) };
        var comp = CSharpCompilation.Create("t", new[] { tree }, refs);
        return (INamedTypeSymbol)comp.GetSymbolsWithName(name).Single();
    }

    [Fact]
    public void TwoScalars_HaveOffsets0And4_Size8()
    {
        var s = Symbol("struct Foo { public uint a; public int b; }", "Foo");
        var m = StructLayout.Build(s);
        Assert.Equal("Foo", m.Name);
        Assert.Equal(new[] { ("a", 0), ("b", 4) },
            m.Fields.Select(f => (f.Name, f.Offset)).ToArray());
        Assert.Equal(ScalarKind.U32, m.Fields[0].Kind);
        Assert.Equal(8, m.SizeBytes);
    }

    [Fact]
    public void NonScalar_Throws()
    {
        var s = Symbol("struct Bad { public double d; }", "Bad");
        Assert.Throws<System.NotSupportedException>(() => StructLayout.Build(s));
    }
}
