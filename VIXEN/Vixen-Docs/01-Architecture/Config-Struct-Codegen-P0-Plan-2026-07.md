# Config-Struct Codegen — P0 (Walking Skeleton) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Prove the whole config-struct codegen pipeline on the minimum content — one trivial `[GpuStruct]` with two scalar fields is authored in canonical C#, and a standalone callable tool emits byte-identical **C++** and **GLSL** headers, wired as a CMake regen step with a byte-identical golden gate.

**Architecture:** A new **VIXEN-side .NET console tool** (`VIXEN/codegen/`) builds a Roslyn `Compilation` from canonical C# schema files, finds `[GpuStruct]` types, computes an **std430 layout model**, and runs pluggable emitters (**C++ + GLSL** in P0) that write committed artifacts. Regen is a `dotnet`-gated CMake pre-build step (VIXEN's normal C++/shader build stays .NET-free); a `--check` mode fails the build if a committed artifact is stale.

**Tech Stack:** .NET 8 (`~/.dotnet/dotnet`) · Roslyn (`Microsoft.CodeAnalysis.CSharp` 4.8.0) · xUnit · CMake/Ninja · glslc (bundled Vulkan SDK) · g++ (WSL).

## Global Constraints

- **Canonical stays C#; generated C++/GLSL are committed artifacts; VIXEN's normal build stays .NET-free** — only *regeneration* runs `dotnet`. (Inc4 D8)
- **Generated `.g.h` / `.glsl` are VERBATIM** — never hand-edit; only the tool writes them.
- **Vixen attributes, VIXEN-side** — the `[GpuStruct]` attribute + schemas live in `VIXEN/codegen/`, never in Yeroket. (user steer 2026-07-02)
- **std430** is the only layout mode in scope.
- **P0 uses scalar fields only.** vec/mat/nested-struct/array/explicit-pad are **P1** — do not implement them here; the model must *throw* on an unsupported field kind (so P1 is forced to extend deliberately).
- WSL: use `~/.dotnet/dotnet` (never the Windows dotnet). Commit trailers on every commit:
  `Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>` and `Claude-Session: https://claude.ai/code/session_01FyfX5aZWhF1kakkUE98u4c`.

---

## Milestone Map (post-brainstorm-context-manager — segmented 2026-07-02)

Run on branch `feat/config-struct-codegen` (main checkout, sequential milestones). Implementer = Sonnet, Validator = Opus, per milestone. Controller runs the gates.

- [x] **M1 — The codegen tool** · Tasks 1–5: `[GpuStruct]` attribute → std430 scalar layout model → C++ emitter → GLSL emitter → Roslyn `Compilation` loader + CLI (generate/`--check`). Gate: `~/.dotnet/dotnet test VIXEN/codegen/Vixen.Codegen.Tests` all green.
- [x] **M2 — Wire into VIXEN + golden gate** · Tasks 6–7: `SkeletonConfig` canonical + committed artifacts → CMake dotnet-gated golden gate (must BITE on tamper) + glslc/g++ compile-smoke of both backends. Gate: `cmake --build --preset vixen-wsl --target codegen_check` + the compile-smokes.

### Progress Log
- Milestone M1 (Tasks 1–5): DONE · commits `01d7f65f`..`3b36c824` (attr `01d7f65f` / model `8301333b` / C++ emit `eedc377b` / GLSL emit `04594a58` / loader+CLI `3b36c824`) · gate `~/.dotnet/dotnet test` = **5/5 green** (controller-run) · Opus review **APPROVED** (controller-direct — validator `p0-m1-val` stalled idle without delivering; std430 model / both emitters / CLI-`--check` verified correct; 3 benign plan-omission deviations: `using System`, `using System.Linq`, stub `Program.cs` replaced in Task 5) · 2026-07-02
  - Validator `p0-m1-val` APPROVED (delivered after a ping) + surfaced findings:
    - **[A] FIX-BEFORE-MERGE** — both emitters use `StringBuilder.AppendLine` → `Environment.NewLine`, so emitted bytes are host-dependent (`\r\n` vs `\n`). Undermines the byte-identical golden guarantee (benign in the WSL flow — both `\n` + `* text=auto` — so it does NOT disturb M2). Fix: emit `'\n'` explicitly. Apply as a hardening pass after M2 (byte-identical on WSL → golden stays green).
    - **[B]** emitter default switch arms (`_ => "uint32_t"` / `"uint"`) should `throw` to match `StructModel`'s fail-loud; unreachable in P0. (Fold into the [A] pass.)
    - **[C]** `StructModel` should add `!f.IsImplicitlyDeclared` when P1 adds properties (auto-prop backing fields). **[D]** `Program.Write` bare-filename guard (cosmetic). `CompilationLoader` should surface schema compile diagnostics. → all **P1**.
- Milestone M2 (Tasks 6–7): DONE · commits `0bba1ec6` (SkeletonConfig canonical + generated C++/GLSL) / `aa3bbf73` (CMake golden gate + regen + wiring) · gate (controller-run): `codegen_check` rc 0, tamper rc 1 + `STALE:` (gate **bites**), C++ g++ + GLSL glslc compile-smokes OK, main-build configure rc 0 (new subdir doesn't break it) · artifacts verified (`SkeletonConfig.g.h` sizeof 8/version@0/payload@4; `.glsl` guard+struct) · CMake gate dotnet-gated (`return()` when absent — D8) · 2026-07-02
  - Deviation (doc-only): the preset runs from `VIXEN/` (CMakePresets.json location), not repo root — no code change.
  - **★ P0 milestones (M1+M2) COMPLETE.** The [A]/[B] newline+fail-loud hardening landed in
    `70165c13`; the later kernel-core migration superseded and removed this parallel tool in
    `1b075391`.

---

## File Structure

| File | Responsibility |
|---|---|
| `VIXEN/codegen/Vixen.Codegen.Attributes/GpuStructAttributes.cs` | The `[GpuStruct]` marker attribute (the Vixen attribute consumers mark schemas with). |
| `VIXEN/codegen/Vixen.Codegen.Attributes/Vixen.Codegen.Attributes.csproj` | net8.0 classlib; schemas + tool reference it. |
| `VIXEN/codegen/Vixen.Codegen/Layout/StructModel.cs` | Parse a Roslyn `INamedTypeSymbol` → ordered `FieldLayout[]` with std430 offsets + total size. Throws on non-scalar (P0). |
| `VIXEN/codegen/Vixen.Codegen/Emit/CppStructEmitter.cs` | `StructModel` → C++ header string (struct + `static_assert`s). |
| `VIXEN/codegen/Vixen.Codegen/Emit/GlslStructEmitter.cs` | `StructModel` → GLSL struct string. |
| `VIXEN/codegen/Vixen.Codegen/CompilationLoader.cs` | Build a `CSharpCompilation` from schema `.cs` files + the attributes ref; enumerate `[GpuStruct]` symbols. |
| `VIXEN/codegen/Vixen.Codegen/Program.cs` | CLI: `--schema <dir> --out-cpp <path> --out-glsl <path> [--check]`. |
| `VIXEN/codegen/Vixen.Codegen/Vixen.Codegen.csproj` | net8.0 exe; refs Roslyn + the attributes project. |
| `VIXEN/codegen/Vixen.Codegen.Tests/*.cs` | xUnit tests for the model + both emitters + the CLI. |
| `VIXEN/codegen/schemas/SkeletonConfig.cs` | The P0 canonical struct (2 `uint` fields). |
| `VIXEN/codegen/generated/SkeletonConfig.g.h` | Committed C++ artifact (tool output). |
| `VIXEN/codegen/generated/SkeletonConfig.glsl` | Committed GLSL artifact (tool output). |
| `VIXEN/codegen/CMakeLists.txt` | Regen custom target (dotnet-gated) + `--check` golden gate + a compile-smoke of both artifacts. |
| `VIXEN/CMakeLists.txt` (modify) | `add_subdirectory(codegen)` guarded so it is opt-in / skipped when dotnet or the toggle is absent. |

---

### Task 1: Attributes assembly

**Files:**
- Create: `VIXEN/codegen/Vixen.Codegen.Attributes/GpuStructAttributes.cs`
- Create: `VIXEN/codegen/Vixen.Codegen.Attributes/Vixen.Codegen.Attributes.csproj`

**Interfaces:**
- Produces: `namespace Vixen.Codegen.Attributes` with `[AttributeUsage(AttributeTargets.Struct)] class GpuStructAttribute : Attribute { public GpuLayout Layout {get;set;} = GpuLayout.Std430; }` and `enum GpuLayout { Std430 }`.

- [ ] **Step 1: Create the csproj**

```xml
<!-- VIXEN/codegen/Vixen.Codegen.Attributes/Vixen.Codegen.Attributes.csproj -->
<Project Sdk="Microsoft.NET.Sdk">
  <PropertyGroup>
    <TargetFramework>net8.0</TargetFramework>
    <Nullable>enable</Nullable>
    <LangVersion>latest</LangVersion>
  </PropertyGroup>
</Project>
```

- [ ] **Step 2: Write the attribute**

```csharp
// VIXEN/codegen/Vixen.Codegen.Attributes/GpuStructAttributes.cs
namespace Vixen.Codegen.Attributes;

/// <summary>Layout convention a GPU struct is packed with.</summary>
public enum GpuLayout { Std430 }

/// <summary>Marks a C# struct as the single source of a GPU config struct.
/// The codegen tool emits byte-identical C++ and GLSL from it.</summary>
[AttributeUsage(AttributeTargets.Struct, AllowMultiple = false)]
public sealed class GpuStructAttribute : System.Attribute
{
    public GpuLayout Layout { get; set; } = GpuLayout.Std430;
}
```

- [ ] **Step 3: Build it**

Run: `~/.dotnet/dotnet build VIXEN/codegen/Vixen.Codegen.Attributes -c Release`
Expected: `Build succeeded. 0 Error(s)`.

- [ ] **Step 4: Commit**

```bash
git add VIXEN/codegen/Vixen.Codegen.Attributes
git commit -m "feat(codegen): [GpuStruct] attribute assembly (P0)"
```

---

### Task 2: std430 layout model (scalars)

**Files:**
- Create: `VIXEN/codegen/Vixen.Codegen/Layout/StructModel.cs`
- Create: `VIXEN/codegen/Vixen.Codegen/Vixen.Codegen.csproj`
- Test: `VIXEN/codegen/Vixen.Codegen.Tests/StructModelTests.cs` (+ its csproj, Task 2 Step 1)

**Interfaces:**
- Produces:
  - `record FieldLayout(string Name, ScalarKind Kind, int Offset);`
  - `enum ScalarKind { U32, I32, F32 }` with `SizeBytes => 4` for all (P0).
  - `record StructModel(string Name, IReadOnlyList<FieldLayout> Fields, int SizeBytes);`
  - `static class StructLayout { static StructModel Build(INamedTypeSymbol structType); }` — walks instance fields in source order; maps `uint→U32`, `int→I32`, `float→F32`; each scalar has align 4; offset = running cursor rounded up to align; struct size = cursor rounded up to 4. Throws `NotSupportedException($"P0 supports scalar fields only; '{name}' is {type}")` on anything else.

- [ ] **Step 1: Create the tool + test csprojs**

```xml
<!-- VIXEN/codegen/Vixen.Codegen/Vixen.Codegen.csproj -->
<Project Sdk="Microsoft.NET.Sdk">
  <PropertyGroup>
    <OutputType>Exe</OutputType>
    <TargetFramework>net8.0</TargetFramework>
    <Nullable>enable</Nullable>
    <LangVersion>latest</LangVersion>
    <RootNamespace>Vixen.Codegen</RootNamespace>
  </PropertyGroup>
  <ItemGroup>
    <PackageReference Include="Microsoft.CodeAnalysis.CSharp" Version="4.8.0" />
    <ProjectReference Include="../Vixen.Codegen.Attributes/Vixen.Codegen.Attributes.csproj" />
  </ItemGroup>
</Project>
```
```xml
<!-- VIXEN/codegen/Vixen.Codegen.Tests/Vixen.Codegen.Tests.csproj -->
<Project Sdk="Microsoft.NET.Sdk">
  <PropertyGroup>
    <TargetFramework>net8.0</TargetFramework>
    <Nullable>enable</Nullable>
    <IsPackable>false</IsPackable>
  </PropertyGroup>
  <ItemGroup>
    <PackageReference Include="Microsoft.NET.Test.Sdk" Version="17.9.0" />
    <PackageReference Include="xunit" Version="2.7.0" />
    <PackageReference Include="xunit.runner.visualstudio" Version="2.5.7" />
    <PackageReference Include="Microsoft.CodeAnalysis.CSharp" Version="4.8.0" />
    <ProjectReference Include="../Vixen.Codegen/Vixen.Codegen.csproj" />
    <ProjectReference Include="../Vixen.Codegen.Attributes/Vixen.Codegen.Attributes.csproj" />
  </ItemGroup>
</Project>
```

- [ ] **Step 2: Write the failing test**

```csharp
// VIXEN/codegen/Vixen.Codegen.Tests/StructModelTests.cs
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
```

- [ ] **Step 3: Run it, verify it fails**

Run: `~/.dotnet/dotnet test VIXEN/codegen/Vixen.Codegen.Tests`
Expected: FAIL — `StructLayout` does not exist.

- [ ] **Step 4: Implement the model**

```csharp
// VIXEN/codegen/Vixen.Codegen/Layout/StructModel.cs
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
```

- [ ] **Step 5: Run tests, verify pass**

Run: `~/.dotnet/dotnet test VIXEN/codegen/Vixen.Codegen.Tests`
Expected: PASS (2 tests).

- [ ] **Step 6: Commit**

```bash
git add VIXEN/codegen/Vixen.Codegen VIXEN/codegen/Vixen.Codegen.Tests
git commit -m "feat(codegen): std430 scalar layout model + tests (P0)"
```

---

### Task 3: C++ emitter

**Files:**
- Create: `VIXEN/codegen/Vixen.Codegen/Emit/CppStructEmitter.cs`
- Test: `VIXEN/codegen/Vixen.Codegen.Tests/CppStructEmitterTests.cs`

**Interfaces:**
- Consumes: `StructModel`, `FieldLayout`, `ScalarKind` (Task 2).
- Produces: `static class CppStructEmitter { static string Emit(StructModel m); }` — returns a `#pragma once` header: `#include <cstdint>` + `namespace Vixen::Gpu { struct <Name> { <cpptype> <field>; … }; static_assert(sizeof==Size); static_assert(offsetof==…) per field; }`. Scalar→C++: U32→`uint32_t`, I32→`int32_t`, F32→`float`.

- [ ] **Step 1: Write the failing test**

```csharp
// VIXEN/codegen/Vixen.Codegen.Tests/CppStructEmitterTests.cs
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
```

- [ ] **Step 2: Run it, verify it fails** — `~/.dotnet/dotnet test VIXEN/codegen/Vixen.Codegen.Tests`; FAIL (`CppStructEmitter` missing).

- [ ] **Step 3: Implement**

```csharp
// VIXEN/codegen/Vixen.Codegen/Emit/CppStructEmitter.cs
using System.Text;
using Vixen.Codegen.Layout;

namespace Vixen.Codegen.Emit;

public static class CppStructEmitter
{
    static string Cpp(ScalarKind k) => k switch
    {
        ScalarKind.U32 => "uint32_t",
        ScalarKind.I32 => "int32_t",
        ScalarKind.F32 => "float",
        _ => "uint32_t",
    };

    public static string Emit(StructModel m)
    {
        var sb = new StringBuilder();
        sb.AppendLine("#pragma once");
        sb.AppendLine("// GENERATED by Vixen.Codegen — DO NOT EDIT. Regenerate from the canonical [GpuStruct].");
        sb.AppendLine("#include <cstdint>");
        sb.AppendLine("#include <cstddef>");
        sb.AppendLine("namespace Vixen::Gpu {");
        sb.AppendLine($"struct {m.Name} {{");
        foreach (var f in m.Fields) sb.AppendLine($"    {Cpp(f.Kind)} {f.Name};");
        sb.AppendLine("};");
        sb.AppendLine($"static_assert(sizeof({m.Name}) == {m.SizeBytes}, \"{m.Name} std430 size\");");
        foreach (var f in m.Fields)
            sb.AppendLine($"static_assert(offsetof({m.Name}, {f.Name}) == {f.Offset}, \"{f.Name}@{f.Offset}\");");
        sb.AppendLine("} // namespace Vixen::Gpu");
        return sb.ToString();
    }
}
```

- [ ] **Step 4: Run tests, verify pass** — PASS.

- [ ] **Step 5: Commit** — `git add VIXEN/codegen/Vixen.Codegen/Emit/CppStructEmitter.cs VIXEN/codegen/Vixen.Codegen.Tests/CppStructEmitterTests.cs && git commit -m "feat(codegen): C++ struct emitter (P0)"`

---

### Task 4: GLSL emitter (the net-new backend)

**Files:**
- Create: `VIXEN/codegen/Vixen.Codegen/Emit/GlslStructEmitter.cs`
- Test: `VIXEN/codegen/Vixen.Codegen.Tests/GlslStructEmitterTests.cs`

**Interfaces:**
- Consumes: `StructModel` (Task 2).
- Produces: `static class GlslStructEmitter { static string Emit(StructModel m); }` — emits a GLSL `struct <Name> { <glsltype> <field>; … };` (no `#pragma once`; GLSL uses include guards via `#ifndef`). Scalar→GLSL: U32→`uint`, I32→`int`, F32→`float`.

- [ ] **Step 1: Write the failing test**

```csharp
// VIXEN/codegen/Vixen.Codegen.Tests/GlslStructEmitterTests.cs
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
```

- [ ] **Step 2: Run it, verify it fails** — FAIL.

- [ ] **Step 3: Implement**

```csharp
// VIXEN/codegen/Vixen.Codegen/Emit/GlslStructEmitter.cs
using System.Text;
using Vixen.Codegen.Layout;

namespace Vixen.Codegen.Emit;

public static class GlslStructEmitter
{
    static string Glsl(ScalarKind k) => k switch
    {
        ScalarKind.U32 => "uint",
        ScalarKind.I32 => "int",
        ScalarKind.F32 => "float",
        _ => "uint",
    };

    public static string Emit(StructModel m)
    {
        string guard = m.Name.ToUpperInvariant() + "_GLSL";
        var sb = new StringBuilder();
        sb.AppendLine($"#ifndef {guard}");
        sb.AppendLine($"#define {guard}");
        sb.AppendLine("// GENERATED by Vixen.Codegen — DO NOT EDIT.");
        sb.AppendLine($"struct {m.Name} {{");
        foreach (var f in m.Fields) sb.AppendLine($"    {Glsl(f.Kind)} {f.Name};");
        sb.AppendLine("};");
        sb.AppendLine($"#endif // {guard}");
        return sb.ToString();
    }
}
```

- [ ] **Step 4: Run tests, verify pass** — PASS.

- [ ] **Step 5: Commit** — `git add … && git commit -m "feat(codegen): GLSL struct emitter (P0)"`

---

### Task 5: Compilation loader + CLI (`--out-cpp/--out-glsl/--check`)

**Files:**
- Create: `VIXEN/codegen/Vixen.Codegen/CompilationLoader.cs`
- Create: `VIXEN/codegen/Vixen.Codegen/Program.cs`
- Test: `VIXEN/codegen/Vixen.Codegen.Tests/CliTests.cs`

**Interfaces:**
- Consumes: `StructLayout.Build`, `CppStructEmitter.Emit`, `GlslStructEmitter.Emit`.
- Produces:
  - `static class CompilationLoader { static IReadOnlyList<INamedTypeSymbol> LoadGpuStructs(IEnumerable<string> schemaFiles); }` — parses the `.cs` files + a reference to the attributes assembly + corelib; returns every struct annotated `[GpuStruct]` (matched by attribute class name `GpuStructAttribute`).
  - `Program.Main(args)` — flags `--schema <dir>` (glob `*.cs`), `--out-cpp <path>`, `--out-glsl <path>`, `--check`. Emits exactly one struct in P0. Without `--check`: `WriteAllText` both artifacts (creating dirs). With `--check`: compare emitted text to the on-disk files; exit `1` + print the first differing path if stale, else `0`.

- [ ] **Step 1: Write the failing test**

```csharp
// VIXEN/codegen/Vixen.Codegen.Tests/CliTests.cs
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
```

- [ ] **Step 2: Run it, verify it fails** — FAIL (`Program.Main` / `CompilationLoader` missing).

- [ ] **Step 3: Implement the loader**

```csharp
// VIXEN/codegen/Vixen.Codegen/CompilationLoader.cs
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
```

- [ ] **Step 4: Implement the CLI**

```csharp
// VIXEN/codegen/Vixen.Codegen/Program.cs
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
```

- [ ] **Step 5: Run tests, verify pass** — PASS (all suites).

- [ ] **Step 6: Commit** — `git add … && git commit -m "feat(codegen): compilation loader + CLI (generate/--check) (P0)"`

---

### Task 6: Canonical schema + committed artifacts

**Files:**
- Create: `VIXEN/codegen/schemas/SkeletonConfig.cs`
- Create (tool output, committed): `VIXEN/codegen/generated/SkeletonConfig.g.h`, `VIXEN/codegen/generated/SkeletonConfig.glsl`

**Interfaces:**
- Consumes: the CLI (Task 5).

- [ ] **Step 1: Write the canonical struct**

```csharp
// VIXEN/codegen/schemas/SkeletonConfig.cs
using Vixen.Codegen.Attributes;

/// <summary>P0 walking-skeleton config — proves the pipeline end to end.</summary>
[GpuStruct]
public struct SkeletonConfig
{
    public uint version;   // offset 0
    public int  payload;   // offset 4
}
```

- [ ] **Step 2: Generate the artifacts**

Run:
```bash
~/.dotnet/dotnet run --project VIXEN/codegen/Vixen.Codegen -c Release -- \
  --schema VIXEN/codegen/schemas \
  --out-cpp VIXEN/codegen/generated/SkeletonConfig.g.h \
  --out-glsl VIXEN/codegen/generated/SkeletonConfig.glsl
```
Expected: exit 0; both files created.

- [ ] **Step 3: Verify `--check` is clean on the committed output**

Run: same command with `--check` appended.
Expected: exit 0 (no `STALE`).

- [ ] **Step 4: Commit schema + generated artifacts**

```bash
git add VIXEN/codegen/schemas VIXEN/codegen/generated
git commit -m "feat(codegen): SkeletonConfig canonical + generated C++/GLSL (P0)"
```

---

### Task 7: CMake regen wiring + golden gate + compile-smoke

**Files:**
- Create: `VIXEN/codegen/CMakeLists.txt`
- Modify: `VIXEN/CMakeLists.txt` (add the guarded `add_subdirectory(codegen)`)

**Interfaces:**
- Consumes: the CLI (Task 5), the committed artifacts (Task 6).

- [ ] **Step 1: Write the codegen CMakeLists**

```cmake
# VIXEN/codegen/CMakeLists.txt
# Regen + golden gate for Vixen.Codegen. Only active when dotnet is available
# (D8: normal builds compile committed artifacts; only regen needs .NET).
find_program(VIXEN_DOTNET NAMES dotnet HINTS "$ENV{HOME}/.dotnet")
if(NOT VIXEN_DOTNET)
    message(STATUS "[codegen] dotnet not found — skipping regen/golden gate (committed artifacts used as-is)")
    return()
endif()

set(_cg  "${CMAKE_CURRENT_SOURCE_DIR}")
set(_gen "${_cg}/generated")

# Golden gate: fail the build if committed artifacts are stale vs. the canonical schema.
add_custom_target(codegen_check ALL
    COMMAND ${VIXEN_DOTNET} run --project "${_cg}/Vixen.Codegen" -c Release --
            --schema "${_cg}/schemas"
            --out-cpp "${_gen}/SkeletonConfig.g.h"
            --out-glsl "${_gen}/SkeletonConfig.glsl"
            --check
    COMMENT "[codegen] golden check: committed artifacts match canonical schema"
    VERBATIM)

# Convenience regen target (manual): cmake --build <dir> --target codegen_regen
add_custom_target(codegen_regen
    COMMAND ${VIXEN_DOTNET} run --project "${_cg}/Vixen.Codegen" -c Release --
            --schema "${_cg}/schemas"
            --out-cpp "${_gen}/SkeletonConfig.g.h"
            --out-glsl "${_gen}/SkeletonConfig.glsl"
    COMMENT "[codegen] regenerate committed artifacts"
    VERBATIM)
```

- [ ] **Step 2: Wire it into the VIXEN build (guarded, opt-in)**

Add near the other `add_subdirectory` calls in `VIXEN/CMakeLists.txt`:
```cmake
# Config-struct codegen golden gate (no-ops without dotnet). Opt-out with -DVIXEN_CODEGEN_GATE=OFF.
option(VIXEN_CODEGEN_GATE "Run the config-struct codegen golden check" ON)
if(VIXEN_CODEGEN_GATE)
    add_subdirectory(codegen)
endif()
```

- [ ] **Step 3: Configure + build the gate**

Run:
```bash
cmake --build --preset vixen-wsl --target codegen_check
```
Expected: `[codegen] golden check` runs, exit 0 (artifacts match).

- [ ] **Step 4: Prove the gate BITES (tamper test)**

Run:
```bash
printf 'stale\n' >> VIXEN/codegen/generated/SkeletonConfig.g.h
cmake --build --preset vixen-wsl --target codegen_check ; echo "rc=$?"
git checkout -- VIXEN/codegen/generated/SkeletonConfig.g.h
```
Expected: non-zero `rc` with `STALE: …/SkeletonConfig.g.h`. (Restore leaves the tree clean.)

- [ ] **Step 5: Compile-smoke both emitted artifacts**

Run (proves the emitted C++ AND GLSL are valid — the walking-skeleton's real point):
```bash
echo '#include "VIXEN/codegen/generated/SkeletonConfig.g.h"
int main(){ return (int)sizeof(Vixen::Gpu::SkeletonConfig); }' > /tmp/cg_smoke.cpp
g++ -std=c++23 -I. /tmp/cg_smoke.cpp -o /tmp/cg_smoke && echo CPP_OK
printf '#version 450\n#include "SkeletonConfig.glsl"\nlayout(local_size_x=1) in;\nvoid main(){ SkeletonConfig c; c.version=0u; }\n' > /tmp/cg_smoke.comp
GLSLC=$(ls VIXEN/.vulkan-sdk/*/x86_64/bin/glslc | head -1)
"$GLSLC" -fshader-stage=compute -I VIXEN/codegen/generated /tmp/cg_smoke.comp -o /tmp/cg_smoke.spv && echo GLSL_OK
```
Expected: `CPP_OK` and `GLSL_OK`.

- [ ] **Step 6: Commit**

```bash
git add VIXEN/codegen/CMakeLists.txt VIXEN/CMakeLists.txt
git commit -m "build(codegen): CMake golden gate + regen target; compile-smoke both backends (P0)"
```

---

## Self-Review

**Spec coverage (P0 row = "extract/package the emitter core as a callable tool; a trivial 2-field [GpuStruct] emits C++ AND GLSL; regen wired in CMake; golden green; proves the mechanism + the GLSL emitter"):**
- Callable tool → Tasks 2–5 (`Vixen.Codegen` console exe). ✓
- Trivial 2-field `[GpuStruct]` → Task 6 (`SkeletonConfig`). ✓
- Emits C++ **and** GLSL → Tasks 3, 4 + Task 6 artifacts. ✓
- Regen wired in CMake → Task 7 (`codegen_regen` + `codegen_check`). ✓
- Golden green + **bites** → Task 7 Steps 3–4. ✓
- Proves the GLSL emitter → Task 4 + Task 7 Step 5 (glslc compile-smoke). ✓
- Extraction-mechanism open question (spec §7) → **resolved**: a standalone console tool that builds a Roslyn `Compilation` and calls emitters directly (not the analyzer-reference path), because it is the cleanest "callable" and reuses the proven `Compilation`+emit pattern. Recorded here as the P0 decision.

**Placeholder scan:** none — every code step has complete code; every run step has an exact command + expected output.

**Type consistency:** `StructModel`/`FieldLayout`/`ScalarKind` defined in Task 2 and consumed unchanged in Tasks 3–5; `CompilationLoader.LoadGpuStructs` / `Program.Main` signatures match their test call-sites; `Vixen::Gpu` C++ namespace + `<Name>.g.h`/`.glsl` output names consistent across Tasks 3, 4, 6, 7.

**Out of scope (correctly deferred to P1):** non-scalar fields (the model throws), `OctreeConfig`, touching real shaders/`ShellOctreeGpu.h`, the reflection drift-guard on generated output, C#/Python emitters.

---

## Execution Handoff

Plan saved to `VIXEN/Vixen-Docs/01-Architecture/Config-Struct-Codegen-P0-Plan-2026-07.md`. Two execution options:

1. **Subagent-Driven (recommended)** — dispatch a fresh subagent per task, Opus review between tasks, fast iteration (this is the post-brainstorm-context-manager milestone pipeline).
2. **Inline Execution** — execute tasks in this session with checkpoints.
