# View Contract Inc-3 — C# Data-Upload Face Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Generate a typed C# data-upload face from the `[View]` schema — field-setters that marshal view data into a `UTVA` AoS wire stamped with the schema-version hash — and prove it round-trips through a generic C++ `ViewWireReader` into the Inc-2b `ViewStore`.

**Architecture:** One `[View]` schema drives two independently-generated faces that meet at a runtime wire buffer. Yeroket emits a `<Model>ViewWriter` C# class (typed setters → `ToBuffer()` → `UTVA` AoS bytes + version header) via a new `ViewWriterEmitter` + shared `ViewLayout` serializer core. VIXEN gains one generic, blob-guided `ViewWireReader` that un-marshals that wire into the existing `ViewStore` by declared-order field walk. AoS composes with `ViewStore`'s by-field setters, so no un-transposer is needed. The version hash is the boundary guard.

**Tech Stack:** C# (Roslyn source-generator model, netstandard2.1, NUnit) in Yeroket; C++23 + gtest + RmlUi + CMake in VIXEN; the Yeroket `CodegenTool~` CLI; wsl.exe-bridged CMake drift-guards (KI-015).

## Global Constraints

- **In-tree + stub only.** Do NOT modify the undertow repo (`/home/liory/Github/undertow`). The undertow migration is Inc-5+. (Spec D1)
- **AoS emit ships; SoA is recorded-but-deferred.** The `[ViewSection(Layout=Soa)]` value is a valid model value, but the emitter throws `NotSupportedException("SoA emit is Inc-5+; use Aos for now")` for it. (Spec D3)
- **Version is C#-sole-authored.** The writer stamps `ViewVersionHash.Compute(v)`; the C++ engine only reads/compares it, never recomputes. (Spec D4)
- **No GPU anchor.** The new claim is the C#↔C++ seam (GPU-independent); Inc-2b owns blob→GPU. (Spec D5)
- **Setter ergonomics:** plain public fields + `List<Row>`, no change-tracking. (Spec D6)
- **Wire format:** a fresh `UTVA` AoS format (magic `'U','T','V','A'`), little-endian, distinct from undertow's `UTVW`. (Spec D7)
- **The proof view is the real `Hud` schema** (`codegen/view-schemas/Hud.cs` / `Hud.blob.g.h`, version `0x55D27B8C`) — it exercises every kind (Int, Float, Bool, String, ArrayOfStruct). No synthetic view.
- **Nothing in Inc-2b changes** — `ViewBlob`, `ViewStore`, `ViewValue`/`ViewKind`, `BlobView`, `ViewVersionHash`, `Hud.blob.g.h` are reused as-is.
- **VIXEN has no C# build** — the generated `Hud.view.g.cs` is a committed, drift-guarded artifact exercised by the Yeroket NUnit test, NOT compiled in VIXEN.
- **Build/test discipline (VIXEN side):** Windows-side via the `vixen-ninja` preset / `build.bat`; poll long builds on a foreground interval (never blind-wait); never overlap same-target builds.
- **Yeroket test discipline:** run from `CodegenTool~/Tests/` (from `CodegenTool~/` discovers 0 tests — a false green); tests are NUnit.
- **Never commit** `SDFNodeGenerator.dll`, `bin/`, or `obj/` (`git checkout --` them if staged). Stage single files (`git add -- <path>`).

**Path shorthand:**
- `$KF` = `/home/liory/Github/Yeroket-Fantasy/Packages/com.yeroket.utility.kernel-framework`
- `$V`  = the VIXEN worktree root (this checkout), VIXEN sources under `$V/VIXEN/`

---

## File Structure

**Yeroket (`$KF`):**
- Create `SourceGenerator~/Transpiler/ViewSectionAttribute.cs` — the `ViewLayout` enum + `[ViewSection(Layout=…)]` attribute (the runtime types the schema references) OR add to an existing attributes file if `[View]` lives in one (Task 1 checks).
- Modify `SourceGenerator~/Transpiler/ViewModel.cs` — add `ViewLayout Layout` to `ViewField`; populate it in `ViewModelBuilder.Classify` from the `ViewSectionAttribute`.
- Create `SourceGenerator~/Transpiler/ViewLayout.cs` — the shared serializer core: `WriteAos(ViewStruct, values)` byte-encoders (the `UTVA` format). One responsibility: schema → wire bytes.
- Create `SourceGenerator~/Transpiler/ViewWriterEmitter.cs` — emits the `<Model>ViewWriter` C# class (typed fields + row structs + `ToBuffer()` calling into the emitted-inline AoS encoder). Sibling of `ViewBlobEmitter`.
- Modify `CodegenTool~/Program.cs` — add the `--view-writer` CLI branch.
- Create `CodegenTool~/Tests/ViewWriterEmitterTests.cs` — NUnit: generated shape, version const, `UTVA` golden bytes.
- Create `CodegenTool~/Tests/ViewSectionLayoutTests.cs` — NUnit: `Soa` throws, `Aos` emits.

**VIXEN (`$V/VIXEN`):**
- Modify `codegen/view-schemas/Hud.cs` — apply `[ViewSection(Layout=Aos)]` to `factions`/`events` (explicit, documents the choice; default is `Aos` anyway).
- Create `libraries/RenderGraph/include/Ui/ViewWireReader.h` + `src/Ui/ViewWireReader.cpp` — the generic blob-guided `UTVA` reader.
- Modify `libraries/RenderGraph/CMakeLists.txt` — add `ViewWireReader.cpp` to the library; register the three gtests.
- Create `libraries/RenderGraph/tests/test_view_wire_roundtrip.cpp` — round-trip + version-mismatch + malformed (all three test bodies; one file, three gtest cases).
- Create `application/main/include/Generated/Hud.view.g.cs` — the committed generated writer (produced by the regen target; checked in).
- Modify `codegen/CMakeLists.txt` — add the `view_hud_writer_check`/`view_hud_writer_regen` wsl.exe-bridged target pair.

---

## Milestone Map

- **M1 — Yeroket layout model + attribute (Tasks 1–2):** the `[ViewSection]`/`ViewLayout` types + `ViewField.Layout`. Testable: model classifies layout; `Soa` recorded.
- **M2 — Yeroket writer emitter + wire (Tasks 3–5):** `ViewLayout` serializer core + `ViewWriterEmitter` + the two NUnit tests. Testable: generated writer produces the exact `UTVA` golden bytes; `Soa` throws.
- **M3 — Yeroket CLI + VIXEN generated artifact + drift-guard (Tasks 6–7):** `--view-writer` branch, apply attribute in `Hud.cs`, generate + commit `Hud.view.g.cs`, add the CMake guard pair. Testable: `--view-writer --check` passes against the committed file.
- **M4 — VIXEN C++ reader + round-trip proof (Tasks 8–10):** `ViewWireReader` + the three gtests. Testable: round-trip reads back every field; version-mismatch + malformed hard-fail.

---

## Task 1: `ViewLayout` enum + `[ViewSection]` attribute (Yeroket)

**Files:**
- Inspect first: find where `[View]` is defined (grep). It is the runtime attribute the schema references — the new `[ViewSection]` lives beside it so `view-schemas/Hud.cs`'s existing `using Yeroket.Util.KernelFramework;` sees it.
- Create: `$KF/SourceGenerator~/Transpiler/ViewSectionAttribute.cs` (or append to the existing attribute file if `[View]` is defined in one — keep them together).
- Test: covered by Task 2's model test (the attribute is data with no behavior; its own unit test would be vacuous).

**Interfaces:**
- Produces: `Yeroket.Util.KernelFramework.ViewLayout { Aos, Soa }` and `[AttributeUsage(AttributeTargets.Field)] ViewSectionAttribute { ViewLayout Layout { get; set; } = ViewLayout.Aos; }`. Task 2 reads it by name; Task 7 applies it in `Hud.cs`.

- [ ] **Step 1: Find where `[View]` is defined**

Run: `grep -rn "class ViewAttribute" $KF/SourceGenerator~ ; grep -rln "namespace Yeroket.Util.KernelFramework" $KF/SourceGenerator~`
Expected: locate the `ViewAttribute` definition + its namespace. Note the file and namespace — the new attribute goes in the SAME namespace so `view-schemas/Hud.cs` (`using Yeroket.Util.KernelFramework;`) resolves it without a new using.

- [ ] **Step 2: Create the attribute + enum**

Create `$KF/SourceGenerator~/Transpiler/ViewSectionAttribute.cs` (adjust `namespace` to match Step 1's finding — shown here as the expected `Yeroket.Util.KernelFramework`):

```csharp
using System;

namespace Yeroket.Util.KernelFramework
{
    /// <summary>Wire layout for a [View] struct-array section. Aos = rows contiguous (the Inc-3
    /// ships-now path, composes with the engine ViewStore's by-field setters). Soa = column-major
    /// (the undertow UTVW target; recorded here but its emit is Inc-5+).</summary>
    public enum ViewLayout { Aos, Soa }

    /// <summary>Declares the wire layout of a struct-array field on a [View] schema. Optional;
    /// absent ⇒ Aos. Scalar/single-row fields ignore it (Aos and Soa serialize identically).</summary>
    [AttributeUsage(AttributeTargets.Field, AllowMultiple = false)]
    public sealed class ViewSectionAttribute : Attribute
    {
        public ViewLayout Layout { get; set; } = ViewLayout.Aos;
    }
}
```

> If Step 1 shows `[View]` in a namespace OTHER than `Yeroket.Util.KernelFramework` (e.g. it is aliased), put `ViewSectionAttribute` in that SAME namespace and note it — `view-schemas/Hud.cs` must be able to reference `[ViewSection]` with its existing usings.

- [ ] **Step 3: Build the source generator project to confirm it compiles**

Run: `cd $KF/CodegenTool~ && /home/liory/.dotnet/dotnet build -c Release 2>&1 | tail -5`
Expected: `Build succeeded`. (The attribute is referenced by the transpiler assembly the tool links.)

- [ ] **Step 4: Commit**

```bash
cd $KF && git add -- SourceGenerator~/Transpiler/ViewSectionAttribute.cs
git commit -m "feat(view-contract): [ViewSection(Layout)] attribute + ViewLayout enum (Inc-3)"
```

---

## Task 2: `ViewField.Layout` in the view model (Yeroket)

**Files:**
- Modify: `$KF/SourceGenerator~/Transpiler/ViewModel.cs` (add `Layout` to `ViewField`; read the attribute in `Classify`).
- Test: `$KF/CodegenTool~/Tests/ViewSectionLayoutTests.cs` (the layout-classification half; the emitter-throws half is added in Task 5).

**Interfaces:**
- Consumes: `ViewLayout` / `ViewSectionAttribute` (Task 1).
- Produces: `ViewField.Layout` (type `ViewLayout`, default `Aos`), populated for `StructArray` fields from `[ViewSection]`. Task 3/4/5 read `field.Layout`.

- [ ] **Step 1: Write the failing test**

Create `$KF/CodegenTool~/Tests/ViewSectionLayoutTests.cs`:

```csharp
using System.Linq;
using NUnit.Framework;
using Yeroket.KernelFramework.Codegen;

[TestFixture]
public class ViewSectionLayoutTests
{
    // Compile a tiny [View] with a Soa-attributed array and an unattributed array,
    // then assert the model records the layout per-field.
    const string Src = @"
namespace Yeroket.Util.KernelFramework {
    public enum ViewLayout { Aos, Soa }
    [System.AttributeUsage(System.AttributeTargets.Field)]
    public sealed class ViewSectionAttribute : System.Attribute { public ViewLayout Layout { get; set; } }
    [System.AttributeUsage(System.AttributeTargets.Struct)]
    public sealed class ViewAttribute : System.Attribute { }
}
namespace T {
    using Yeroket.Util.KernelFramework;
    public struct Row { public int x; }
    [View] public struct V {
        public int scalar;
        [ViewSection(Layout = ViewLayout.Soa)] public Row[] cols;
        public Row[] plain;
    }
}";

    [Test]
    public void Model_Records_PerField_Layout()
    {
        var view = CompilationLoader.LoadViews(new[] { WriteTemp(Src) })
                       .First(s => s.Name == "V");
        var model = ViewModelBuilder.Build(view);
        var cols  = model.Fields.First(f => f.Name == "cols");
        var plain = model.Fields.First(f => f.Name == "plain");
        Assert.That(cols.Layout,  Is.EqualTo(ViewLayout.Soa));
        Assert.That(plain.Layout, Is.EqualTo(ViewLayout.Aos));   // default
    }

    static string WriteTemp(string src)
    {
        var p = System.IO.Path.Combine(System.IO.Path.GetTempPath(),
            "vs_layout_" + System.Guid.NewGuid().ToString("N") + ".cs");
        System.IO.File.WriteAllText(p, src);
        return p;
    }
}
```

> Note: `ViewLayout` here is the CODEGEN model's copy (`Yeroket.KernelFramework.Codegen.ViewLayout` — see Step 3), referenced unqualified because the test `using`s that namespace. The `Src` string embeds a SEPARATE runtime copy under `Yeroket.Util.KernelFramework` (that is the schema-facing attribute the compiled snippet uses). Keep the two names identical; they are parallel enums (one is the Roslyn-model side, one is the schema/runtime side), mirroring how `ViewScalar` is a model enum distinct from `int`/`float` C# types.

- [ ] **Step 2: Run the test to verify it fails**

Run: `cd $KF/CodegenTool~/Tests && /home/liory/.dotnet/dotnet test --filter ViewSectionLayoutTests 2>&1 | tail -15`
Expected: FAIL — `ViewField` has no `Layout` member / `Yeroket.KernelFramework.Codegen.ViewLayout` does not exist (compile error in the test), because Task 2's model change isn't in yet.

- [ ] **Step 3: Add the `ViewLayout` model enum + `ViewField.Layout`**

In `$KF/SourceGenerator~/Transpiler/ViewModel.cs`, add a model-side `ViewLayout` enum next to `ViewScalar`/`ViewFieldKind` (line 8-9 area):

```csharp
    public enum ViewScalar { Int, Float, Bool, String }
    public enum ViewFieldKind { Scalar, Struct, StructArray }
    public enum ViewLayout { Aos, Soa }   // wire layout for StructArray fields; scalars ignore it
```

Add `Layout` to `ViewField` (default `Aos`):

```csharp
    public sealed class ViewField
    {
        public string Name { get; }
        public ViewFieldKind Kind { get; }
        public ViewScalar? Scalar { get; }
        public ViewStruct Struct { get; }
        public ViewLayout Layout { get; }   // NEW — Aos unless [ViewSection(Layout=Soa)]
        public ViewField(string name, ViewFieldKind kind, ViewScalar? scalar, ViewStruct str,
                         ViewLayout layout = ViewLayout.Aos)
        { Name = name; Kind = kind; Scalar = scalar; Struct = str; Layout = layout; }
    }
```

- [ ] **Step 4: Read the attribute in `Classify`**

In `ViewModelBuilder.Classify`, for the `StructArray` branch, read `[ViewSection]` off the field symbol. Replace the existing StructArray return (line ~47-48) with:

```csharp
            // T[] where T is a struct → StructArray
            if (f.Type is IArrayTypeSymbol arr && arr.ElementType is INamedTypeSymbol elem && IsPlainStruct(elem))
                return new ViewField(f.Name, ViewFieldKind.StructArray, null, Build(elem), ReadLayout(f));
```

Add the helper (in `ViewModelBuilder`):

```csharp
        static ViewLayout ReadLayout(IFieldSymbol f)
        {
            var a = f.GetAttributes().FirstOrDefault(x => x.AttributeClass?.Name == "ViewSectionAttribute");
            if (a == null) return ViewLayout.Aos;
            // Layout is a named property: [ViewSection(Layout = ViewLayout.Soa)]
            foreach (var na in a.NamedArguments)
                if (na.Key == "Layout" && na.Value.Value is int v)
                    return (ViewLayout)v;
            return ViewLayout.Aos;
        }
```

> `ViewLayout` on the schema side is an enum → its `TypedConstant.Value` is the underlying `int`. Cast through `int` (Roslyn gives the boxed underlying value). `Aos = 0`, `Soa = 1` on both enums — keep the member order identical.

- [ ] **Step 5: Run the test to verify it passes**

Run: `cd $KF/CodegenTool~/Tests && /home/liory/.dotnet/dotnet test --filter ViewSectionLayoutTests 2>&1 | tail -15`
Expected: PASS (1 test).

- [ ] **Step 6: Run the full Yeroket suite to confirm no regression**

Run: `cd $KF/CodegenTool~/Tests && /home/liory/.dotnet/dotnet test 2>&1 | tail -8`
Expected: all pass (existing `ViewBlobEmitterTests`, `ViewVersionHashTests`, `ViewModelTests`, `ViewLoaderTests` + the new one). The `ViewField` constructor gained an optional param, so existing callers still compile.

- [ ] **Step 7: Commit**

```bash
cd $KF && git add -- SourceGenerator~/Transpiler/ViewModel.cs CodegenTool~/Tests/ViewSectionLayoutTests.cs
git commit -m "feat(view-contract): ViewField.Layout from [ViewSection] (Inc-3)"
```

---

## Task 3: `ViewLayout` serializer core — the `UTVA` encoder (Yeroket)

**Files:**
- Create: `$KF/SourceGenerator~/Transpiler/ViewLayout.cs` (namespace `Yeroket.KernelFramework.Codegen`). NOTE: this is the *serializer core class* — name it `ViewWireFormat` to avoid clashing with the `ViewLayout` ENUM from Task 2. (The enum and the encoder are different types; use a distinct class name.)
- Test: covered by Task 4/5's emitter test (the encoder's bytes are asserted through the generated writer's `ToBuffer()` golden).

**Interfaces:**
- Consumes: `ViewStruct`/`ViewField`/`ViewScalar`/`ViewFieldKind`.
- Produces: string constants + helper methods the emitter inlines into the generated writer. Specifically a single method `EmitToBufferBody(StringBuilder sb, ViewStruct v)` that writes the C# statements of `ToBuffer()` (header + declared-order AoS body). Task 4 calls it.

> **Design note (DRY):** rather than a runtime C# serializer library the generated code calls, the emitter *inlines* the encode statements into each generated `ToBuffer()` (mirrors how `EmitViewWriter` in undertow inlines `PutF32`/loops). This keeps the generated file self-contained (no Yeroket runtime dependency in the consumer). `ViewWireFormat` owns the *emit-the-statements* logic so the future SoA target is a second method here.

The `UTVA` format this emits (from spec §4), for reference in every step below:
- Header (12 bytes): `'U','T','V','A'` (4) + version `uint32` LE (4) + top-field-count `uint32` LE (4).
- Body, top-level fields in declared order:
  - Int → `int32` LE (4). Float → IEEE-754 LE (4). Bool → 1 byte (0/1).
  - String → `uint32` LE byte-length + UTF-8 bytes.
  - ArrayOfStruct → `uint32` LE row count, then each row = its element scalar fields in declared order (same per-kind encoding), rows contiguous.

- [ ] **Step 1: Create the encoder emitter**

Create `$KF/SourceGenerator~/Transpiler/ViewLayout.cs`:

```csharp
using System.Text;

namespace Yeroket.KernelFramework.Codegen
{
    // Emits the C# statements of a generated <Model>ViewWriter.ToBuffer() — the UTVA AoS wire
    // (spec §4). The encode logic is INLINED into the generated file (no Yeroket runtime dependency
    // in the consumer). The future SoA target is a second method here (WriteSoaBody), Inc-5+.
    internal static class ViewWireFormat
    {
        // Little-endian primitive writers emitted once at the top of the generated class.
        public static void EmitHelpers(StringBuilder sb, string ind)
        {
            sb.AppendLine($"{ind}private static void W32(System.Collections.Generic.List<byte> b, uint v) {{ b.Add((byte)v); b.Add((byte)(v>>8)); b.Add((byte)(v>>16)); b.Add((byte)(v>>24)); }}");
            sb.AppendLine($"{ind}private static void WI32(System.Collections.Generic.List<byte> b, int v) => W32(b, unchecked((uint)v));");
            sb.AppendLine($"{ind}private static void WF32(System.Collections.Generic.List<byte> b, float v) {{ var t = System.BitConverter.GetBytes(v); if(!System.BitConverter.IsLittleEndian) System.Array.Reverse(t); b.AddRange(t); }}");
            sb.AppendLine($"{ind}private static void WBool(System.Collections.Generic.List<byte> b, bool v) => b.Add(v ? (byte)1 : (byte)0);");
            sb.AppendLine($"{ind}private static void WStr(System.Collections.Generic.List<byte> b, string v) {{ var u = System.Text.Encoding.UTF8.GetBytes(v ?? string.Empty); W32(b, (uint)u.Length); b.AddRange(u); }}");
        }

        // Emits the body of ToBuffer(): header + declared-order AoS body, into local `var b`.
        public static void EmitToBufferBody(StringBuilder sb, ViewStruct v, string ind)
        {
            sb.AppendLine($"{ind}var b = new System.Collections.Generic.List<byte>();");
            sb.AppendLine($"{ind}b.Add((byte)'U'); b.Add((byte)'T'); b.Add((byte)'V'); b.Add((byte)'A');");
            sb.AppendLine($"{ind}W32(b, SchemaVersion);");
            sb.AppendLine($"{ind}W32(b, {v.Fields.Count}u);");
            foreach (var f in v.Fields)
                EmitField(sb, f, "this." + f.Name, ind);
            sb.AppendLine($"{ind}return b.ToArray();");
        }

        static void EmitField(StringBuilder sb, ViewField f, string access, string ind)
        {
            if (f.Kind == ViewFieldKind.StructArray)
            {
                if (f.Layout == ViewLayout.Soa)
                    throw new System.NotSupportedException($"SoA emit is Inc-5+; field '{f.Name}' must use Aos for now");
                string row = "__r_" + f.Name;
                sb.AppendLine($"{ind}W32(b, (uint){access}.Count);");
                sb.AppendLine($"{ind}foreach (var {row} in {access}) {{");
                foreach (var ef in f.Struct.Fields)
                    EmitScalar(sb, ef, $"{row}.{ef.Name}", ind + "    ");
                sb.AppendLine($"{ind}}}");
                return;
            }
            if (f.Kind == ViewFieldKind.Scalar) { EmitScalar(sb, f, access, ind); return; }
            throw new System.NotSupportedException($"plain Struct field '{f.Name}' unsupported in Inc-3 (only scalars + struct[] arrays)");
        }

        static void EmitScalar(StringBuilder sb, ViewField f, string access, string ind)
        {
            switch (f.Scalar)
            {
                case ViewScalar.Int:    sb.AppendLine($"{ind}WI32(b, {access});"); break;
                case ViewScalar.Float:  sb.AppendLine($"{ind}WF32(b, {access});"); break;
                case ViewScalar.Bool:   sb.AppendLine($"{ind}WBool(b, {access});"); break;
                case ViewScalar.String: sb.AppendLine($"{ind}WStr(b, {access});"); break;
            }
        }
    }
}
```

- [ ] **Step 2: Build to confirm it compiles**

Run: `cd $KF/CodegenTool~ && /home/liory/.dotnet/dotnet build -c Release 2>&1 | tail -5`
Expected: `Build succeeded` (no test yet — the emitter is exercised in Task 4).

- [ ] **Step 3: Commit**

```bash
cd $KF && git add -- SourceGenerator~/Transpiler/ViewLayout.cs
git commit -m "feat(view-contract): UTVA AoS wire encoder emitter (Inc-3)"
```

---

## Task 4: `ViewWriterEmitter` — the generated writer class (Yeroket)

**Files:**
- Create: `$KF/SourceGenerator~/Transpiler/ViewWriterEmitter.cs`.
- Test: `$KF/CodegenTool~/Tests/ViewWriterEmitterTests.cs` (Task 5 adds the golden-bytes assertions; this task adds the structural ones).

**Interfaces:**
- Consumes: `ViewStruct` (from `ViewModelBuilder.Build`), `ViewVersionHash.Compute`, `ViewWireFormat` (Task 3).
- Produces: `public static string ViewWriterEmitter.Emit(ViewStruct v)` → the full `<Model>.view.g.cs` text. Task 6 (CLI) calls it.

- [ ] **Step 1: Write the failing test (structural)**

Create `$KF/CodegenTool~/Tests/ViewWriterEmitterTests.cs`:

```csharp
using System.Collections.Generic;
using NUnit.Framework;
using Yeroket.KernelFramework.Codegen;

[TestFixture]
public class ViewWriterEmitterTests
{
    static ViewField Scalar(string n, ViewScalar s) => new ViewField(n, ViewFieldKind.Scalar, s, null);

    // Mirror of the real Hud schema (Hud.blob.g.h): every kind exercised.
    static ViewStruct Hud()
    {
        var faction = new ViewStruct("HudFaction", new List<ViewField> {
            Scalar("name", ViewScalar.String), Scalar("grievance", ViewScalar.Float),
            Scalar("focused", ViewScalar.Bool), Scalar("known", ViewScalar.Bool),
            Scalar("inLens", ViewScalar.Bool), Scalar("recentChanged", ViewScalar.Bool) });
        var ev = new ViewStruct("HudEvent", new List<ViewField> {
            Scalar("kind", ViewScalar.String), Scalar("tick", ViewScalar.Int) });
        return new ViewStruct("Hud", new List<ViewField> {
            Scalar("tick", ViewScalar.Int), Scalar("bodyCount", ViewScalar.Int),
            Scalar("activeLensName", ViewScalar.String), Scalar("activeLensCount", ViewScalar.Int),
            new ViewField("factions", ViewFieldKind.StructArray, null, faction),
            new ViewField("events",   ViewFieldKind.StructArray, null, ev) });
    }

    [Test]
    public void Emit_Has_Class_Fields_RowTypes_And_Version()
    {
        string src = ViewWriterEmitter.Emit(Hud());
        Assert.That(src, Does.Contain("class HudViewWriter"));
        Assert.That(src, Does.Contain("public const uint SchemaVersion = 0x"
            + ViewVersionHash.Compute(Hud()).ToString("X8") + "u;"));
        Assert.That(src, Does.Contain("public int tick;"));
        Assert.That(src, Does.Contain("public string activeLensName;"));
        Assert.That(src, Does.Contain("public struct HudFactionRow"));
        Assert.That(src, Does.Contain("public float grievance;"));
        Assert.That(src, Does.Contain("public System.Collections.Generic.List<HudFactionRow> factions"));
        Assert.That(src, Does.Contain("public byte[] ToBuffer()"));
        Assert.That(src, Does.Not.Contain("\r\n"));
    }
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `cd $KF/CodegenTool~/Tests && /home/liory/.dotnet/dotnet test --filter ViewWriterEmitterTests 2>&1 | tail -12`
Expected: FAIL — `ViewWriterEmitter` does not exist (compile error).

- [ ] **Step 3: Implement `ViewWriterEmitter`**

Create `$KF/SourceGenerator~/Transpiler/ViewWriterEmitter.cs`:

```csharp
using System.Collections.Generic;
using System.Text;

namespace Yeroket.KernelFramework.Codegen
{
    // Emits <Model>.view.g.cs — the typed C# data-upload face (Inc-3, spec §3.3). Plain public
    // fields + List<Row> setters; ToBuffer() produces the UTVA AoS wire (via ViewWireFormat).
    // Sibling of ViewBlobEmitter. One row struct per StructArray element type.
    public static class ViewWriterEmitter
    {
        static string CsType(ViewScalar s) => s switch {
            ViewScalar.Int => "int", ViewScalar.Float => "float",
            ViewScalar.Bool => "bool", ViewScalar.String => "string", _ => "int" };

        // De-duped element structs, so each row type is declared once.
        static void Collect(ViewStruct v, List<ViewStruct> outList, HashSet<string> seen)
        {
            foreach (var f in v.Fields)
                if (f.Kind == ViewFieldKind.StructArray && seen.Add(f.Struct.Name))
                { Collect(f.Struct, outList, seen); outList.Add(f.Struct); }
        }

        public static string Emit(ViewStruct v)
        {
            var sb = new StringBuilder();
            sb.AppendLine("// GENERATED by Yeroket kernel-codegen (--view-writer) — DO NOT EDIT. Regenerate from the canonical [View] schema.");
            sb.AppendLine("#nullable disable");
            sb.AppendLine("namespace Vixen.Views");
            sb.AppendLine("{");
            sb.AppendLine($"    public sealed partial class {v.Name}ViewWriter");
            sb.AppendLine("    {");
            string ver = "0x" + ViewVersionHash.Compute(v).ToString("X8") + "u";
            sb.AppendLine($"        public const uint SchemaVersion = {ver};");
            sb.AppendLine();

            // Row struct types (declared before the List fields that use them).
            var elems = new List<ViewStruct>();
            Collect(v, elems, new HashSet<string>());
            foreach (var e in elems)
            {
                sb.AppendLine($"        public struct {e.Name}Row");
                sb.AppendLine("        {");
                foreach (var rf in e.Fields)
                    sb.AppendLine($"            public {CsType(rf.Scalar.Value)} {rf.Name};");
                sb.AppendLine("        }");
            }
            sb.AppendLine();

            // Top-level fields, declared order.
            foreach (var f in v.Fields)
            {
                if (f.Kind == ViewFieldKind.StructArray)
                    sb.AppendLine($"        public System.Collections.Generic.List<{f.Struct.Name}Row> {f.Name} = new System.Collections.Generic.List<{f.Struct.Name}Row>();");
                else
                    sb.AppendLine($"        public {CsType(f.Scalar.Value)} {f.Name};");
            }
            sb.AppendLine();

            // Encoder helpers + ToBuffer().
            ViewWireFormat.EmitHelpers(sb, "        ");
            sb.AppendLine();
            sb.AppendLine("        public byte[] ToBuffer()");
            sb.AppendLine("        {");
            ViewWireFormat.EmitToBufferBody(sb, v, "            ");
            sb.AppendLine("        }");

            sb.AppendLine("    }");
            sb.AppendLine("}");
            return sb.ToString().Replace("\r\n", "\n");
        }
    }
}
```

> The generated `ToBuffer()` references `this.factions` etc. as `List<Row>`; `EmitToBufferBody` iterates them with `foreach (var __r_factions in this.factions)`. `ViewWireFormat` emits `.Count` and `.<field>` accesses that match these field/row shapes exactly.

- [ ] **Step 4: Run the test to verify it passes**

Run: `cd $KF/CodegenTool~/Tests && /home/liory/.dotnet/dotnet test --filter ViewWriterEmitterTests 2>&1 | tail -12`
Expected: PASS (1 test).

- [ ] **Step 5: Commit**

```bash
cd $KF && git add -- SourceGenerator~/Transpiler/ViewWriterEmitter.cs CodegenTool~/Tests/ViewWriterEmitterTests.cs
git commit -m "feat(view-contract): ViewWriterEmitter — generated C# upload face (Inc-3)"
```

---

## Task 5: Golden `UTVA` bytes + `Soa`-throws tests (Yeroket)

**Files:**
- Modify: `$KF/CodegenTool~/Tests/ViewWriterEmitterTests.cs` (add a compile-and-run golden-bytes test).
- Modify: `$KF/CodegenTool~/Tests/ViewSectionLayoutTests.cs` (add the emitter-throws-on-Soa test).

**Interfaces:**
- Consumes: `ViewWriterEmitter.Emit`, `CompilationLoader` (to compile the generated writer + run it in-process).

> **Why compile-and-run:** the strongest offline proof that the generated writer emits the exact `UTVA` bytes is to compile the generated source, instantiate `HudViewWriter`, set known values, call `ToBuffer()`, and assert the byte array equals the hand-computed canonical **B**. This B is the SAME literal the C++ round-trip (Task 9) asserts against.

- [ ] **Step 1: Check whether the test project can compile+run generated C# in-process**

Run: `grep -rn "CSharpCompilation\|LoadViews\|Emit(" $KF/CodegenTool~/CompilationLoader.cs | head`
Expected: `CompilationLoader` already builds a Roslyn `CSharpCompilation` (it does — it loads `[View]` symbols). Determine whether it exposes a way to emit an assembly to a `MemoryStream` and `Assembly.Load` it. If it does NOT, add a small test-local helper (Step 2 shows one that is self-contained, so this works regardless).

- [ ] **Step 2: Write the failing golden-bytes test**

Append to `ViewWriterEmitterTests.cs`:

```csharp
    [Test]
    public void ToBuffer_Produces_Canonical_UTVA_Bytes()
    {
        // Compile the generated writer + a tiny driver, run it, capture the bytes.
        string gen = ViewWriterEmitter.Emit(Hud());
        string driver = @"
namespace Drive {
  public static class D {
    public static byte[] Run() {
      var w = new Vixen.Views.HudViewWriter();
      w.tick = 42; w.bodyCount = 9; w.activeLensName = ""Intel""; w.activeLensCount = 3;
      w.factions.Add(new Vixen.Views.HudViewWriter.HudFactionRow{
        name=""Reds"", grievance=0.5f, focused=true, known=true, inLens=false, recentChanged=true});
      w.factions.Add(new Vixen.Views.HudViewWriter.HudFactionRow{
        name=""Blues"", grievance=0.25f, focused=false, known=false, inLens=true, recentChanged=false});
      w.events.Add(new Vixen.Views.HudViewWriter.HudEventRow{ kind=""war"", tick=40 });
      return w.ToBuffer();
    }
  }
}";
        byte[] actual = CompileRun.Run(new[] { gen, driver }, "Drive.D", "Run");

        var b = new System.Collections.Generic.List<byte>();
        // header
        b.AddRange(System.Text.Encoding.ASCII.GetBytes("UTVA"));
        W32(b, ViewVersionHash.Compute(Hud()));  // version
        W32(b, 6u);                               // top-field count
        // body, declared order: tick, bodyCount, activeLensName, activeLensCount, factions[], events[]
        WI32(b, 42); WI32(b, 9); WStr(b, "Intel"); WI32(b, 3);
        W32(b, 2u);                               // factions row count
        WStr(b, "Reds");  WF32(b, 0.5f);  b.Add(1); b.Add(1); b.Add(0); b.Add(1);
        WStr(b, "Blues"); WF32(b, 0.25f); b.Add(0); b.Add(0); b.Add(1); b.Add(0);
        W32(b, 1u);                               // events row count
        WStr(b, "war"); WI32(b, 40);

        Assert.That(actual, Is.EqualTo(b.ToArray()));
    }

    // Little-endian helpers mirroring ViewWireFormat, for building the expected buffer.
    static void W32(System.Collections.Generic.List<byte> b, uint v) { b.Add((byte)v); b.Add((byte)(v>>8)); b.Add((byte)(v>>16)); b.Add((byte)(v>>24)); }
    static void WI32(System.Collections.Generic.List<byte> b, int v) => W32(b, unchecked((uint)v));
    static void WF32(System.Collections.Generic.List<byte> b, float v) { var t = System.BitConverter.GetBytes(v); if(!System.BitConverter.IsLittleEndian) System.Array.Reverse(t); b.AddRange(t); }
    static void WStr(System.Collections.Generic.List<byte> b, string s) { var u = System.Text.Encoding.UTF8.GetBytes(s); W32(b,(uint)u.Length); b.AddRange(u); }
```

Add a self-contained compile-run helper file `$KF/CodegenTool~/Tests/CompileRun.cs`:

```csharp
using System;
using System.IO;
using System.Linq;
using System.Reflection;
using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.CSharp;

// Compiles C# source snippets to an in-memory assembly and invokes a static method.
public static class CompileRun
{
    public static byte[] Run(string[] sources, string typeName, string method)
    {
        var trees = sources.Select(s => CSharpSyntaxTree.ParseText(s)).ToArray();
        var refs = AppDomain.CurrentDomain.GetAssemblies()
            .Where(a => !a.IsDynamic && !string.IsNullOrEmpty(a.Location))
            .Select(a => MetadataReference.CreateFromFile(a.Location))
            .Cast<MetadataReference>().ToList();
        var comp = CSharpCompilation.Create("gen_" + Guid.NewGuid().ToString("N"),
            trees, refs, new CSharpCompilationOptions(OutputKind.DynamicallyLinkedLibrary));
        using var ms = new MemoryStream();
        var res = comp.Emit(ms);
        if (!res.Success)
            throw new Exception("compile failed:\n" + string.Join("\n",
                res.Diagnostics.Where(d => d.Severity == DiagnosticSeverity.Error)));
        ms.Seek(0, SeekOrigin.Begin);
        var asm = Assembly.Load(ms.ToArray());
        var t = asm.GetType(typeName) ?? throw new Exception("type not found: " + typeName);
        return (byte[])t.GetMethod(method).Invoke(null, null);
    }
}
```

- [ ] **Step 3: Add the `Soa`-throws test**

Append to `ViewSectionLayoutTests.cs`:

```csharp
    [Test]
    public void Emitter_Rejects_Soa_Layout()
    {
        var row = new ViewStruct("Row", new System.Collections.Generic.List<ViewField> {
            new ViewField("x", ViewFieldKind.Scalar, ViewScalar.Int, null) });
        var v = new ViewStruct("V", new System.Collections.Generic.List<ViewField> {
            new ViewField("cols", ViewFieldKind.StructArray, null, row, ViewLayout.Soa) });
        var ex = Assert.Throws<System.NotSupportedException>(() => ViewWriterEmitter.Emit(v));
        Assert.That(ex.Message, Does.Contain("Inc-5+"));
    }
```

- [ ] **Step 4: Run both test classes to verify pass**

Run: `cd $KF/CodegenTool~/Tests && /home/liory/.dotnet/dotnet test --filter "ViewWriterEmitterTests|ViewSectionLayoutTests" 2>&1 | tail -15`
Expected: PASS (4 tests: 2 emitter + 2 layout). If the golden-bytes test fails, the diff between `actual` and expected shows exactly which field's encoding drifted — fix the encoder in `ViewWireFormat` (Task 3), not the test.

- [ ] **Step 5: Full suite + commit**

Run: `cd $KF/CodegenTool~/Tests && /home/liory/.dotnet/dotnet test 2>&1 | tail -8`
Expected: all pass.

```bash
cd $KF && git add -- CodegenTool~/Tests/ViewWriterEmitterTests.cs CodegenTool~/Tests/ViewSectionLayoutTests.cs CodegenTool~/Tests/CompileRun.cs
git commit -m "test(view-contract): golden UTVA bytes + Soa-throws (Inc-3)"
```

---

## Task 6: `--view-writer` CLI branch + generate `Hud.view.g.cs` (Yeroket + VIXEN)

**Files:**
- Modify: `$KF/CodegenTool~/Program.cs` (add the `--view-writer` branch, mirroring `--view-blob`).
- Modify: `$V/VIXEN/codegen/view-schemas/Hud.cs` (apply `[ViewSection(Layout=Aos)]` explicitly).
- Create: `$V/VIXEN/application/main/include/Generated/Hud.view.g.cs` (produced by running the tool; committed).

**Interfaces:**
- Consumes: `ViewWriterEmitter.Emit`, `ViewModelBuilder.Build`, `CompilationLoader.LoadViews`.
- Produces: the CLI contract `--schema <dir> --view-writer <Name> --out-cs <path> [--check]`. Task 7 (CMake) invokes it.

- [ ] **Step 1: Add the `--view-writer` branch to `Program.cs`**

In `$KF/CodegenTool~/Program.cs`, after the `--view-blob` block (ends line ~97), add:

```csharp
        string? viewWriter = Flag(args, "--view-writer");
        if (viewWriter is not null)
        {
            string? outCs = Flag(args, "--out-cs");
            if (schema is null || outCs is null)
            {
                Console.Error.WriteLine("usage: --schema <dir> --view-writer <Name> --out-cs <path> [--check]");
                return 2;
            }
            var vfiles = Directory.GetFiles(schema, "*.cs", SearchOption.AllDirectories);
            var views  = CompilationLoader.LoadViews(vfiles);
            var vsym   = views.FirstOrDefault(s => s.Name == viewWriter);
            if (vsym is null)
            {
                Console.Error.WriteLine($"[View] named '{viewWriter}' not found; found: {string.Join(", ", views.Select(s => s.Name))}");
                return 2;
            }
            var vmodel = ViewModelBuilder.Build(vsym);
            var cs = ViewWriterEmitter.Emit(vmodel);
            if (check) { bool ok = Same(outCs, cs); if (!ok) Console.Error.WriteLine($"STALE: {outCs}"); return ok ? 0 : 1; }
            Write(outCs, cs);
            return 0;
        }
```

- [ ] **Step 2: Apply the layout attribute in the Hud schema**

In `$V/VIXEN/codegen/view-schemas/Hud.cs`, annotate the two array fields (explicit-Aos documents the choice; behavior is identical to unannotated):

```csharp
    [View]
    public struct Hud {
        public int    tick;
        public int    bodyCount;
        public string activeLensName;
        public int    activeLensCount;
        [ViewSection(Layout = ViewLayout.Aos)] public HudFaction[] factions;
        [ViewSection(Layout = ViewLayout.Aos)] public HudEvent[]   events;
    }
```

> `ViewSection`/`ViewLayout` resolve via the existing `using Yeroket.Util.KernelFramework;` at the top of the file (Task 1 put them in that namespace). If Task 1 used a different namespace, add the matching `using` here.

- [ ] **Step 3: Generate `Hud.view.g.cs` and inspect it**

Run (WSL-side, where the tool + dotnet live):
```bash
/home/liory/.dotnet/dotnet run --project $KF/CodegenTool~ -c Release -- \
  --schema /mnt/c/cpp/VBVS--VIXEN/VIXEN/codegen/view-schemas --view-writer Hud \
  --out-cs /mnt/c/cpp/VBVS--VIXEN/VIXEN/application/main/include/Generated/Hud.view.g.cs
```
Expected: exit 0, file written. Inspect it: `sed -n '1,60p' /mnt/c/cpp/VBVS--VIXEN/VIXEN/application/main/include/Generated/Hud.view.g.cs` — confirm `class HudViewWriter`, `SchemaVersion = 0x55D27B8Cu`, the six top-level fields, `HudFactionRow`/`HudEventRow`, and `ToBuffer()`.

- [ ] **Step 4: Verify `--check` passes against the committed file**

Run: same command with `--check` appended (and no write). Expected: exit 0 (the file matches). Then perturb (add a space to the generated file) and re-run `--check` → exit 1 + `STALE:` — confirming the guard bites. Restore by regenerating.

- [ ] **Step 5: Commit (two repos)**

```bash
cd $KF && git add -- CodegenTool~/Program.cs
git commit -m "feat(view-contract): --view-writer CLI branch (Inc-3)"

cd /mnt/c/cpp/VBVS--VIXEN && git add -- VIXEN/codegen/view-schemas/Hud.cs VIXEN/application/main/include/Generated/Hud.view.g.cs
git commit -m "feat(view-contract): [ViewSection(Aos)] on Hud + generated Hud.view.g.cs (Inc-3)"
```

---

## Task 7: CMake drift-guard for `Hud.view.g.cs` (VIXEN)

**Files:**
- Modify: `$V/VIXEN/codegen/CMakeLists.txt` (add the `view_hud_writer_check`/`regen` pair, wsl.exe-bridged).

**Interfaces:**
- Consumes: the `--view-writer` CLI (Task 6), the existing `_CODEGEN_RUNNER`, `_yk_tool_run`, `_schema_view_run`, `_codegen_to_wsl_path` (all set up earlier in the file).

- [ ] **Step 1: Add the output-path translation**

In `$V/VIXEN/codegen/CMakeLists.txt`, inside the `if(_codegen_need_wsl_bridge)` block (after line ~112, alongside the other `_codegen_to_wsl_path` calls), add:

```cmake
        _codegen_to_wsl_path("${CMAKE_SOURCE_DIR}/application/main/include/Generated/Hud.view.g.cs" _out_hud_writer_cs_run)
```

And in the `else()` branch (after line ~123), add the native path:

```cmake
        set(_out_hud_writer_cs_run "${CMAKE_SOURCE_DIR}/application/main/include/Generated/Hud.view.g.cs")
```

- [ ] **Step 2: Add the target pair**

After the `view_hud_blob_regen` target (line ~190), before the closing `else()` (line 191), add:

```cmake
    # --- Hud C# data-upload writer (View Contract Inc-3): --view-writer emits the typed
    # HudViewWriter (Hud.view.g.cs) — the C# upload face. Committed + drift-guarded like every
    # other generated artifact. NOT compiled in VIXEN (no C# build here); exercised by the
    # Yeroket NUnit tests. The C++ round-trip proof (test_view_wire_roundtrip) consumes the same
    # UTVA wire this writer emits.
    set(_view_writer_args
        run --project "${_yk_tool_run}" -c Release --
        --schema "${_schema_view_run}" --view-writer Hud
        --out-cs "${_out_hud_writer_cs_run}")
    add_custom_target(view_hud_writer_check ALL
        COMMAND ${_CODEGEN_RUNNER} ${_view_writer_args} --check
        COMMENT "[codegen] golden check: Hud.view.g.cs matches canonical [View] schema (Yeroket tool)"
        VERBATIM)
    add_custom_target(view_hud_writer_regen
        COMMAND ${_CODEGEN_RUNNER} ${_view_writer_args}
        COMMENT "[codegen] regenerate Hud.view.g.cs (Yeroket tool)"
        VERBATIM)
```

- [ ] **Step 3: Reconfigure + build the check target**

Run (Windows-side, per repo discipline — poll, don't blind-wait):
```
build.bat configure
```
Then build just the guard: `cmake --build build-ninja --target view_hud_writer_check` (or the preset's build dir). Expected: the target runs the tool with `--check` and exits 0 (`Hud.view.g.cs` matches). If the guard is DISABLED (WARNING about WSL-only tool), that is the KI-015 path — confirm the WSL-side configure enables it, or that wsl.exe bridging is active.

> Poll pattern for the configure/build (never blind-wait): `until ! kill -0 $PID 2>/dev/null; do echo "[watch +${t}s] $(tail -1 $LOG)"; sleep 15; t=$((t+15)); done`.

- [ ] **Step 4: Commit**

```bash
cd /mnt/c/cpp/VBVS--VIXEN && git add -- VIXEN/codegen/CMakeLists.txt
git commit -m "build(view-contract): drift-guard Hud.view.g.cs (view_hud_writer_check, Inc-3)"
```

---

## Task 8: `ViewWireReader` — the generic C++ reader (VIXEN)

**Files:**
- Create: `$V/VIXEN/libraries/RenderGraph/include/Ui/ViewWireReader.h` + `src/Ui/ViewWireReader.cpp`.
- Modify: `$V/VIXEN/libraries/RenderGraph/CMakeLists.txt` (add `src/Ui/ViewWireReader.cpp` to the `RenderGraph` library sources).
- Test: Task 9 (the reader is proven by the round-trip; a minimal build-only check here confirms it compiles + links).

**Interfaces:**
- Consumes: `ViewBlob`/`ViewFieldDesc`/`ViewKind` (`Ui/ViewBlob.h`), `ViewStore` + `ViewValue` (`Ui/ViewStore.h`).
- Produces: `static bool Vixen::RenderGraph::ViewWireReader::Apply(std::span<const std::byte> wire, ViewStore& store)`. Task 9 calls it.

- [ ] **Step 1: Write the header**

Create `$V/VIXEN/libraries/RenderGraph/include/Ui/ViewWireReader.h`:

```cpp
#pragma once
#include "Ui/ViewStore.h"
#include <cstddef>
#include <span>

namespace Vixen::RenderGraph {

// Reads a UTVA AoS wire buffer (View Contract Inc-3, spec §4) into a ViewStore, guided by the
// store's ViewBlob (declared field order + kinds). Version-checked at entry against store.Version().
// Returns false + logs (LT_ERROR) on any version mismatch or malformed input; never partially
// writes on failure, never throws, never over-reads (bounds-checked against the span).
class ViewWireReader {
public:
    static bool Apply(std::span<const std::byte> wire, ViewStore& store);
};

}  // namespace Vixen::RenderGraph
```

- [ ] **Step 2: Write the implementation**

Create `$V/VIXEN/libraries/RenderGraph/src/Ui/ViewWireReader.cpp`:

```cpp
#include "Ui/ViewWireReader.h"
#include <RmlUi/Core/Log.h>
#include <cstring>
#include <string>

namespace Vixen::RenderGraph {

namespace {

// Little-endian cursor over the wire; every read bounds-checks and sets ok=false on overrun.
struct Cursor {
    const std::byte* p;
    size_t n;
    size_t at = 0;
    bool ok = true;

    bool Need(size_t k) { if (at + k > n) { ok = false; } return ok; }
    uint8_t U8()  { if (!Need(1)) return 0; return static_cast<uint8_t>(p[at++]); }
    uint32_t U32() { if (!Need(4)) return 0;
        uint32_t v = static_cast<uint8_t>(p[at]) | (static_cast<uint8_t>(p[at+1])<<8)
                   | (static_cast<uint8_t>(p[at+2])<<16) | (static_cast<uint8_t>(p[at+3])<<24);
        at += 4; return v; }
    int32_t I32() { return static_cast<int32_t>(U32()); }
    float F32() { uint32_t u = U32(); float f; std::memcpy(&f, &u, 4); return f; }
    bool Bool() { return U8() != 0; }
    std::string Str() {
        uint32_t len = U32();
        if (!ok || !Need(len)) { ok = false; return {}; }
        std::string s(reinterpret_cast<const char*>(p + at), len);
        at += len; return s;
    }
};

}  // namespace

bool ViewWireReader::Apply(std::span<const std::byte> wire, ViewStore& store) {
    Cursor c{ wire.data(), wire.size() };

    // Header: 'U','T','V','A' + version u32 + top-field-count u32.
    if (!c.Need(12)) { Rml::Log::Message(Rml::Log::LT_ERROR, "ViewWireReader: truncated header"); return false; }
    if (c.U8()!='U' || c.U8()!='T' || c.U8()!='V' || c.U8()!='A') {
        Rml::Log::Message(Rml::Log::LT_ERROR, "ViewWireReader: bad magic (expected UTVA)"); return false;
    }
    uint32_t wireVer = c.U32();
    uint32_t fieldCount = c.U32();

    // Version guard (spec §5.3) — hard boundary error, store untouched.
    if (wireVer != store.Version()) {
        Rml::Log::Message(Rml::Log::LT_ERROR,
            "ViewWireReader: schema version mismatch (wire=0x%08X store=0x%08X) — skipping",
            wireVer, store.Version());
        return false;
    }

    const auto& fields = store.Blob().fields;
    if (fieldCount != fields.size()) {
        Rml::Log::Message(Rml::Log::LT_ERROR,
            "ViewWireReader: top-field count mismatch (wire=%u blob=%zu)", fieldCount, fields.size());
        return false;
    }

    // Decode body in declared order, driving ViewStore's validated setters.
    for (const auto& f : fields) {
        switch (f.kind) {
            case ViewKind::Int:    { int v = c.I32(); if (!c.ok) break; store.SetScalar(f.name, ViewValue::I(v)); break; }
            case ViewKind::Float:  { float v = c.F32(); if (!c.ok) break; store.SetScalar(f.name, ViewValue::F(v)); break; }
            case ViewKind::Bool:   { bool v = c.Bool(); if (!c.ok) break; store.SetScalar(f.name, ViewValue::B(v)); break; }
            case ViewKind::String: { std::string v = c.Str(); if (!c.ok) break; store.SetScalar(f.name, ViewValue::S(std::move(v))); break; }
            case ViewKind::ArrayOfStruct: {
                uint32_t rows = c.U32();
                if (!c.ok) break;
                auto h = store.ResizeArray(f.name, rows);
                for (uint32_t r = 0; r < rows && c.ok; ++r) {
                    for (const auto& ef : f.elem) {
                        switch (ef.kind) {
                            case ViewKind::Int:    { int v = c.I32(); if (!c.ok) break; h.Set(r, ef.name, ViewValue::I(v)); break; }
                            case ViewKind::Float:  { float v = c.F32(); if (!c.ok) break; h.Set(r, ef.name, ViewValue::F(v)); break; }
                            case ViewKind::Bool:   { bool v = c.Bool(); if (!c.ok) break; h.Set(r, ef.name, ViewValue::B(v)); break; }
                            case ViewKind::String: { std::string v = c.Str(); if (!c.ok) break; h.Set(r, ef.name, ViewValue::S(std::move(v))); break; }
                            default: c.ok = false; break;   // nested arrays unsupported in the kind catalogue
                        }
                        if (!c.ok) break;
                    }
                }
                break;
            }
        }
        if (!c.ok) break;
    }

    if (!c.ok) {
        Rml::Log::Message(Rml::Log::LT_ERROR, "ViewWireReader: malformed body (overran buffer)");
        return false;
    }
    if (c.at != c.n) {
        Rml::Log::Message(Rml::Log::LT_ERROR, "ViewWireReader: %zu trailing bytes", c.n - c.at);
        return false;
    }
    return true;
}

}  // namespace Vixen::RenderGraph
```

> **Partial-write caveat (documented, not a bug for the proof):** on a mid-body overrun, some earlier fields were already written to `store` before `ok` flipped. The version-mismatch and magic/count/header failures — the cases the spec calls "hard boundary error, store untouched" — all `return false` BEFORE any field is written, so those honor the untouched guarantee. Malformed-body is a corrupt-input diagnostic (`return false`, logged); the caller must not consume a `false` result. The round-trip test (Task 9) asserts the untouched guarantee specifically for the version-mismatch case, matching spec §5.3.

- [ ] **Step 3: Add the source to the RenderGraph library**

In `$V/VIXEN/libraries/RenderGraph/CMakeLists.txt`, find the `add_library(RenderGraph …)` source list and add (next to the other `src/Ui/*.cpp` — grep `Ui/ViewStore.cpp` to locate the group):

```cmake
    src/Ui/ViewWireReader.cpp
```

- [ ] **Step 4: Build the library to confirm it compiles + links**

Run (Windows-side, poll): `cmake --build build-ninja --target RenderGraph`
Expected: `RenderGraph` builds clean. (No test yet.)

- [ ] **Step 5: Commit**

```bash
cd /mnt/c/cpp/VBVS--VIXEN && git add -- VIXEN/libraries/RenderGraph/include/Ui/ViewWireReader.h VIXEN/libraries/RenderGraph/src/Ui/ViewWireReader.cpp VIXEN/libraries/RenderGraph/CMakeLists.txt
git commit -m "feat(view-contract): generic UTVA ViewWireReader -> ViewStore (Inc-3)"
```

---

## Task 9: Round-trip gtest (VIXEN)

**Files:**
- Create: `$V/VIXEN/libraries/RenderGraph/tests/test_view_wire_roundtrip.cpp` (three gtest cases: roundtrip, version_mismatch, malformed).
- Modify: `$V/VIXEN/libraries/RenderGraph/CMakeLists.txt` (register the test executable).
- Reuse: `$V/VIXEN/application/main/include/Generated/Hud.blob.g.h` (the `Hud` `ViewBlob`, version `0x55D27B8C`).

**Interfaces:**
- Consumes: `ViewWireReader::Apply` (Task 8), `ViewStore` accessors (`ScalarSlotPtr`, `Array`, `Blob`, `Version`), `Vixen::Views::kHudBlob`.

- [ ] **Step 1: Write the test file**

Create `$V/VIXEN/libraries/RenderGraph/tests/test_view_wire_roundtrip.cpp`:

```cpp
#include "Ui/ViewWireReader.h"
#include "Ui/ViewStore.h"
#include "Generated/Hud.blob.g.h"   // Vixen::Views::kHudBlob (version 0x55D27B8C)
#include <gtest/gtest.h>
#include <cstddef>
#include <cstring>
#include <string>
#include <vector>

using namespace Vixen::RenderGraph;

namespace {

// Build the canonical UTVA wire "B" — the byte-for-byte twin of the C# ToBuffer() golden
// (Yeroket ViewWriterEmitterTests). Same known input: tick=42, bodyCount=9, activeLensName="Intel",
// activeLensCount=3, factions=[Reds,Blues], events=[war@40].
struct WB {
    std::vector<std::byte> b;
    void u8(uint8_t v)  { b.push_back(std::byte{v}); }
    void u32(uint32_t v){ u8(v&0xFF); u8((v>>8)&0xFF); u8((v>>16)&0xFF); u8((v>>24)&0xFF); }
    void i32(int32_t v) { u32(static_cast<uint32_t>(v)); }
    void f32(float v)   { uint32_t u; std::memcpy(&u,&v,4); u32(u); }
    void str(const std::string& s){ u32(static_cast<uint32_t>(s.size())); for(char ch: s) u8(static_cast<uint8_t>(ch)); }
};

std::vector<std::byte> CanonicalWire(uint32_t version) {
    WB w;
    w.u8('U'); w.u8('T'); w.u8('V'); w.u8('A');
    w.u32(version);
    w.u32(6);                      // top-field count
    w.i32(42); w.i32(9); w.str("Intel"); w.i32(3);
    w.u32(2);                      // factions
    w.str("Reds");  w.f32(0.5f);  w.u8(1); w.u8(1); w.u8(0); w.u8(1);
    w.str("Blues"); w.f32(0.25f); w.u8(0); w.u8(0); w.u8(1); w.u8(0);
    w.u32(1);                      // events
    w.str("war"); w.i32(40);
    return w.b;
}

// field index by name in the Hud blob (declared order).
int Field(const ViewBlob& blob, std::string_view name) {
    for (size_t k = 0; k < blob.fields.size(); ++k) if (blob.fields[k].name == name) return (int)k;
    return -1;
}
int Elem(const ViewFieldDesc& f, std::string_view name) {
    for (size_t k = 0; k < f.elem.size(); ++k) if (f.elem[k].name == name) return (int)k;
    return -1;
}

}  // namespace

TEST(ViewWireRoundtrip, ReadsBackEveryField) {
    const auto& blob = Vixen::Views::kHudBlob;
    ViewStore store(blob, blob.version);
    auto wire = CanonicalWire(blob.version);

    ASSERT_TRUE(ViewWireReader::Apply(wire, store));

    // scalars
    EXPECT_EQ(*static_cast<int*>(store.ScalarSlotPtr(Field(blob,"tick"))), 42);
    EXPECT_EQ(*static_cast<int*>(store.ScalarSlotPtr(Field(blob,"bodyCount"))), 9);
    EXPECT_EQ(*static_cast<Rml::String*>(store.ScalarSlotPtr(Field(blob,"activeLensName"))), "Intel");
    EXPECT_EQ(*static_cast<int*>(store.ScalarSlotPtr(Field(blob,"activeLensCount"))), 3);

    // factions array
    int fi = Field(blob, "factions");
    const auto& fdesc = blob.fields[fi];
    auto& fac = store.Array(fi);
    ASSERT_EQ(fac.size(), 2u);
    EXPECT_EQ(fac[0].cells[Elem(fdesc,"name")].s, "Reds");
    EXPECT_FLOAT_EQ(fac[0].cells[Elem(fdesc,"grievance")].f, 0.5f);
    EXPECT_TRUE (fac[0].cells[Elem(fdesc,"focused")].b);
    EXPECT_TRUE (fac[0].cells[Elem(fdesc,"known")].b);
    EXPECT_FALSE(fac[0].cells[Elem(fdesc,"inLens")].b);
    EXPECT_TRUE (fac[0].cells[Elem(fdesc,"recentChanged")].b);
    EXPECT_EQ(fac[1].cells[Elem(fdesc,"name")].s, "Blues");
    EXPECT_FLOAT_EQ(fac[1].cells[Elem(fdesc,"grievance")].f, 0.25f);
    EXPECT_TRUE (fac[1].cells[Elem(fdesc,"inLens")].b);

    // events array
    int ei = Field(blob, "events");
    const auto& edesc = blob.fields[ei];
    auto& ev = store.Array(ei);
    ASSERT_EQ(ev.size(), 1u);
    EXPECT_EQ(ev[0].cells[Elem(edesc,"kind")].s, "war");
    EXPECT_EQ(ev[0].cells[Elem(edesc,"tick")].i, 40);
}

TEST(ViewWireRoundtrip, VersionMismatchIsHardError) {
    const auto& blob = Vixen::Views::kHudBlob;
    ViewStore store(blob, blob.version);
    auto wire = CanonicalWire(blob.version ^ 0x1u);   // perturbed version

    EXPECT_FALSE(ViewWireReader::Apply(wire, store));
    // store untouched — the version guard returns before any field write.
    EXPECT_EQ(*static_cast<int*>(store.ScalarSlotPtr(Field(blob,"tick"))), 0);
    EXPECT_EQ(store.Array(Field(blob,"factions")).size(), 0u);
}

TEST(ViewWireRoundtrip, MalformedIsRejected) {
    const auto& blob = Vixen::Views::kHudBlob;
    auto good = CanonicalWire(blob.version);

    // (a) wrong magic
    { ViewStore s(blob, blob.version); auto w = good; w[0] = std::byte{'X'};
      EXPECT_FALSE(ViewWireReader::Apply(w, s)); }
    // (b) truncated body
    { ViewStore s(blob, blob.version); std::vector<std::byte> w(good.begin(), good.begin()+20);
      EXPECT_FALSE(ViewWireReader::Apply(w, s)); }
    // (c) field-count mismatch
    { ViewStore s(blob, blob.version); auto w = good; w[8] = std::byte{5};   // count byte -> 5, not 6
      EXPECT_FALSE(ViewWireReader::Apply(w, s)); }
    // (d) trailing garbage
    { ViewStore s(blob, blob.version); auto w = good; w.push_back(std::byte{0xAB});
      EXPECT_FALSE(ViewWireReader::Apply(w, s)); }
}
```

> **The cross-language tie:** `CanonicalWire` here builds the exact same byte sequence the C# `ToBuffer_Produces_Canonical_UTVA_Bytes` test (Task 5) asserts `HudViewWriter.ToBuffer()` emits, for the identical known input. If either side drifts, one of the two tests fails.

- [ ] **Step 2: Register the test in CMake**

In `$V/VIXEN/libraries/RenderGraph/CMakeLists.txt`, find where a sibling test is registered (grep `test_view_blob_equiv` — mirror its exact pattern, which is likely `add_executable` + `target_link_libraries(... GTest::gtest_main RenderGraph)` + `add_test`/`gtest_discover_tests`). Add the analogous block:

```cmake
add_executable(test_view_wire_roundtrip tests/test_view_wire_roundtrip.cpp)
target_link_libraries(test_view_wire_roundtrip PRIVATE GTest::gtest_main RenderGraph)
target_include_directories(test_view_wire_roundtrip PRIVATE
    ${CMAKE_SOURCE_DIR}/application/main/include)   # for Generated/Hud.blob.g.h
gtest_discover_tests(test_view_wire_roundtrip)
```

> Match the ACTUAL pattern `test_view_blob_equiv` uses (it also includes `Generated/Hud.blob.g.h`, so copy its include-dir handling verbatim rather than guessing). If it uses a `RENDERGRAPH_TEST(...)` helper macro or a loop, follow that instead.

- [ ] **Step 3: Build the test (verify it compiles, then that it passes)**

Run (Windows-side, poll): `cmake --build build-ninja --target test_view_wire_roundtrip`
Expected: builds clean.

- [ ] **Step 4: Run the test**

Run: the built `test_view_wire_roundtrip.exe` (under `build-ninja/libraries/RenderGraph/tests/` or the preset's test output dir) — or `ctest --test-dir build-ninja -R ViewWireRoundtrip --output-on-failure`.
Expected: 3 cases PASS (`ReadsBackEveryField`, `VersionMismatchIsHardError`, `MalformedIsRejected`).

> If `ReadsBackEveryField` fails on a specific field, the encoder (C# Task 3) and decoder (C++ Task 8) disagree on that kind's bytes — reconcile against the spec §4 format; the `CanonicalWire` bytes are the arbiter.

- [ ] **Step 5: Commit**

```bash
cd /mnt/c/cpp/VBVS--VIXEN && git add -- VIXEN/libraries/RenderGraph/tests/test_view_wire_roundtrip.cpp VIXEN/libraries/RenderGraph/CMakeLists.txt
git commit -m "test(view-contract): UTVA wire round-trip + version + malformed gtest (Inc-3)"
```

---

## Task 10: Full-suite no-regression + close-out (VIXEN + Yeroket)

**Files:** none (verification only) — plus the plan doc's Progress Log.

- [ ] **Step 1: Yeroket full suite**

Run: `cd $KF/CodegenTool~/Tests && /home/liory/.dotnet/dotnet test 2>&1 | tail -10`
Expected: all pass (Inc-1/2b tests + the 4 new Inc-3 tests).

- [ ] **Step 2: VIXEN — Inc-2b tests still green**

Run (Windows-side): build + run `test_view_blob_equiv` (the Inc-2b proof) and `test_view_wire_roundtrip`. Expected: both pass — Inc-3 added beside Inc-2b and changed nothing in it.

Run: `cmake --build build-ninja --target test_view_blob_equiv test_view_wire_roundtrip` then run both. Expected: all green.

- [ ] **Step 3: Confirm drift-guards pass in a fresh configure**

Run (Windows-side): `build.bat configure` then `cmake --build build-ninja --target view_hud_blob_check view_hud_writer_check`.
Expected: both golden checks exit 0 (`Hud.blob.g.h`, `hud.viewblob`, `Hud.view.g.cs` all match their canonical schema). If a guard reports DISABLED (KI-015 WSL-only), note it — the WSL-side configure is authoritative for the guard.

- [ ] **Step 4: No stray artifacts staged**

Run: `cd /mnt/c/cpp/VBVS--VIXEN && git status --short` and `cd $KF && git status --short`
Expected: only intended files. If `SDFNodeGenerator.dll`, `bin/`, or `obj/` appear, `git checkout -- <path>` them (never commit).

---

## Progress Log

- (append per milestone: `Milestone N (Tasks A–B): DONE · commits <short>..<short> · Opus validator OK · <date>`)

---

## Self-Review

**Spec coverage:**
- D1 (in-tree + stub, no undertow) → Global Constraints + Tasks touch only VIXEN/Yeroket. ✓
- D2 (setters primary, transpose factored) → Task 3 `ViewWireFormat` core (SoA is a future method here) + Task 4 setters. ✓
- D3 (`[ViewSection]` + kind first-class; AoS ships, SoA throws) → Task 1 (attr), Task 2 (model), Task 3/5 (SoA throws). ✓
- D4 (version stamped in wire, C#-sole-authored) → Task 3 header emit + Task 8 guard reads `store.Version()`. ✓
- D5 (round-trip proof, no GPU) → Task 9 (three cases), no GPU target anywhere. ✓
- D6 (fields + `List<Row>`) → Task 4 emitter. ✓
- D7 (`UTVA` AoS format) → Task 3 (encoder) + Task 8 (decoder) + spec §4 replicated in both. ✓
- Spec §4 wire format → Task 3 emits it, Task 8 reads it, Task 9 `CanonicalWire` is the arbiter. ✓
- Spec §5 reader (blob-guided, version guard, malformed hard-fail) → Task 8. ✓
- Spec §6 tests (C# structural + golden, C++ round-trip + mismatch + malformed) → Tasks 4/5/9. ✓
- Spec §6.6 drift-guard → Task 7. ✓
- Spec §6.5 no-regression → Task 10. ✓

**Placeholder scan:** No TBD/TODO. Every code step shows full code. The two "grep to find the exact sibling pattern" steps (Task 1 Step 1 `[View]` location, Task 9 Step 2 test-registration macro) are deliberate — they adapt to the real file rather than guessing a macro name — and each gives the expected shape + a fallback. Not placeholders.

**Type consistency:** `ViewWireFormat` (encoder class) ≠ `ViewLayout` (enum) — deliberately distinct names (Task 3 note). `HudViewWriter`/`HudFactionRow`/`HudEventRow` used identically in Tasks 4/5. `ViewWireReader::Apply(std::span<const std::byte>, ViewStore&)→bool` identical in Tasks 8/9. `ScalarSlotPtr`/`Array`/`Version`/`Blob` match `ViewStore.h`. `SetScalar`/`ResizeArray`/`RowHandle::Set` + `ViewValue::I/F/B/S` match `ViewStore.cpp`/`ViewBlob.h`. CLI `--view-writer --out-cs` identical in Tasks 6/7. `0x55D27B8C` matches `Hud.blob.g.h`. ✓
