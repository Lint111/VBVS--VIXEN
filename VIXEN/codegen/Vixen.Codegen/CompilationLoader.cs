using System.Collections.Generic;
using System.Linq;
using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.CSharp;

namespace Vixen.Codegen;

public static class CompilationLoader
{
    public static IReadOnlyList<INamedTypeSymbol> LoadGpuStructs(IEnumerable<string> schemaFiles)
    {
        var trees = schemaFiles.Select(f => CSharpSyntaxTree.ParseText(System.IO.File.ReadAllText(f), path: f));
        var refs = new[]
        {
            MetadataReference.CreateFromFile(typeof(object).Assembly.Location),
            MetadataReference.CreateFromFile(typeof(Attributes.GpuStructAttribute).Assembly.Location),
        };
        var comp = CSharpCompilation.Create("schemas", trees, refs);
        var result = new List<INamedTypeSymbol>();
        foreach (var tree in comp.SyntaxTrees)
        {
            var model = comp.GetSemanticModel(tree);
            foreach (var node in tree.GetRoot().DescendantNodes()
                     .OfType<Microsoft.CodeAnalysis.CSharp.Syntax.StructDeclarationSyntax>())
            {
                if (model.GetDeclaredSymbol(node) is INamedTypeSymbol sym &&
                    sym.GetAttributes().Any(a => a.AttributeClass?.Name == "GpuStructAttribute"))
                    result.Add(sym);
            }
        }
        return result;
    }
}
