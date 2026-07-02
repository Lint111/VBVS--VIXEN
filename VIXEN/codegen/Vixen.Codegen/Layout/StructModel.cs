using System;
using System.Collections.Generic;
using System.Linq;
using Microsoft.CodeAnalysis;

namespace Vixen.Codegen.Layout;

public enum ScalarKind { U32, I32, F32 }

public static class ScalarKindExt
{
    public static int SizeBytes(this ScalarKind k) => 4;      // P0: all scalars 4B
    public static int AlignBytes(this ScalarKind k) => 4;
}

public sealed record FieldLayout(string Name, ScalarKind Kind, int Offset);

public sealed record StructModel(string Name, IReadOnlyList<FieldLayout> Fields, int SizeBytes);

public static class StructLayout
{
    public static StructModel Build(INamedTypeSymbol structType)
    {
        var fields = new List<FieldLayout>();
        int cursor = 0;
        foreach (var f in structType.GetMembers().OfType<IFieldSymbol>().Where(f => !f.IsStatic && !f.IsConst))
        {
            var kind = MapScalar(f.Type, f.Name);
            int align = kind.AlignBytes();
            cursor = RoundUp(cursor, align);
            fields.Add(new FieldLayout(f.Name, kind, cursor));
            cursor += kind.SizeBytes();
        }
        int size = RoundUp(cursor, 4);
        return new StructModel(structType.Name, fields, size);
    }

    static ScalarKind MapScalar(ITypeSymbol t, string field) => t.SpecialType switch
    {
        SpecialType.System_UInt32 => ScalarKind.U32,
        SpecialType.System_Int32  => ScalarKind.I32,
        SpecialType.System_Single => ScalarKind.F32,
        _ => throw new NotSupportedException(
            $"P0 supports scalar fields only; '{field}' is {t.ToDisplayString()}"),
    };

    static int RoundUp(int v, int a) => (v + a - 1) / a * a;
}
