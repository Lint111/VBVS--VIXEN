# View Contract Inc-2b — Reflection Blob + Generic Dynamic Marshaler Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the renderer host any view from a runtime description (generated C++ header AND runtime `.viewblob` file), so a zero-engine-C++ consumer can drive the HUD — proven byte-identical to Inc-2's native `HudView` path.

**Architecture:** One `[View]` schema → a reflection blob (kind-catalogue field descriptors + a C#-authored schema-version hash), delivered as a `constexpr` C++ header and a runtime data file, both producing the same in-memory `ViewBlob`. A new engine-side `BlobView : IView` walks the blob to build RmlUi's dynamic data-model definitions at `Register` time and marshals consumer data into a generic typed `ViewStore`. `UIRenderNode` and native `HudView` are untouched.

**Tech Stack:** C++20 (VIXEN engine, RmlUi vendored), C# (Yeroket kernel-codegen tool, Roslyn), CMake+Ninja, gtest, real-GPU (D3D12/dzn) capture.

**Spec:** `VIXEN/Vixen-Docs/01-Architecture/View-Contract-Inc2b-Reflection-Blob-Design-2026-07.md`.

## Global Constraints

- **Kind catalogue is fixed:** `ViewKind { Int, Float, Bool, String, ArrayOfStruct }` — exactly what `[View]` expresses. No standalone nested-struct (non-array) kind. No byte offsets or strides anywhere.
- **Version hash authored SOLELY in C#** (`ViewVersionHash.Compute`). The C++ engine only *reads* `blob.version` and compares — never recomputes it. Algorithm: **FNV-1a over a canonical UTF-8 string** `model | perField(name '|' kindTag) | forArrays(elem perField(name '|' kindTag))` in declared order, joined by `\n`, producing a `uint32_t` (fold the 64-bit FNV to 32 by `(uint32_t)(h ^ (h >> 32))`). Kind tags: `int`,`float`,`bool`,`string`,`array`.
- **Version mismatch = hard fail:** `BlobView::Register` logs `LT_ERROR` and skips registration (empty view, never garbage). Tested both match and mismatch.
- **All parser/marshal failures hard + logged, never crash/garbage:** malformed/missing file → no blob; unknown kind → reject; bad setter name/kind → logged, no write.
- **`UIRenderNode` and native `HudView`/`BindHudModel` are NOT modified** — the blob path is additive (a second `IView`).
- **RmlUi dynamic API only via public surface:** `DataModelConstructor::GetDataTypeRegister()`, `DataTypeRegister::RegisterDefinition`, `StructDefinition::AddMember`, `DataModelConstructor::BindCustomDataVariable`, `ScalarDefinition<T>`, `ArrayDefinition<Container>`. The one internals touchpoint is a synthetic `FamilyId` (see Task 5).
- **Content hash (proof test only) reuses** `Vixen::Hash::ComputeHash64` from `libraries/Core/include/VixenHash.h` — distinct from the version hash.
- **Generated artifacts are committed** and guarded by `--check` (KI-015: `--check` no-ops on a Windows configure where Yeroket is unreachable — pre-existing, not a new issue).
- **Cross-repo:** Yeroket at `$ENV{HOME}/Github/Yeroket-Fantasy` (`/home/liory/Github/Yeroket-Fantasy`); VIXEN at `/mnt/c/cpp/VBVS--VIXEN`. Yeroket C# tests run from `CodegenTool~/Tests/` (running from `CodegenTool~/` silently runs 0 tests — false green). The Yeroket `-c Release` build rebuilds `RoslynAnalyzers/SDFNodeGenerator.dll` non-deterministically — `git checkout --` it after; never commit it.
- **Build Windows-side** via the preset (`build.bat` / `_ninja_*.bat` → `vixen-ninja` → `build/ninja`); real GPU renders ~50s; first configure ~500s. Poll long builds on a foreground interval; never overlap same-target builds.

---

## Milestone Map

- **Milestone 1 — Codegen (Yeroket):** Tasks 1–3. The `--view-blob` emitter (version hash + header + data-file) + C# tests. Deliverable: `dotnet run … --view-blob Hud …` emits both artifacts; C# suite green.
- **Milestone 2 — Engine blob contract + storage:** Tasks 4–6. `ViewBlob`/`ViewKind`/`ViewValue`, `ViewStore` (typed storage + setters), `ViewBlobFile` parser. Pure-CPU units, gtest each.
- **Milestone 3 — Generic host + wiring:** Tasks 7–8. `BlobView : IView` (dynamic registration + version guard), CMake `view_hud_blob_check`/`regen` + commit generated artifacts.
- **Milestone 4 — Proof gate:** Task 9. `test_view_blob_equiv` (data-model content-hash: native==header==datafile, bad-version empty+logged; parser round-trip) + the reused real-GPU anchor.

---

## Task 1: Yeroket — `ViewVersionHash` (C#)

**Files:**
- Create: `$KF/SourceGenerator~/Transpiler/ViewVersionHash.cs`
- Test: `$KF/CodegenTool~/Tests/ViewVersionHashTests.cs`

where `$KF = /home/liory/Github/Yeroket-Fantasy/Packages/com.yeroket.utility.kernel-framework`.

**Interfaces:**
- Consumes: `ViewStruct`, `ViewField`, `ViewFieldKind`, `ViewScalar` (existing, `ViewModel.cs`, namespace `Yeroket.KernelFramework.Codegen`).
- Produces: `public static uint ViewVersionHash.Compute(ViewStruct v)` — the stable schema-version hash used by both emitters (Task 2/3).

- [ ] **Step 1: Write the failing test**

Create `ViewVersionHashTests.cs`:
```csharp
using Xunit;
using Yeroket.KernelFramework.Codegen;
using System.Collections.Generic;

public class ViewVersionHashTests
{
    static ViewField Scalar(string n, ViewScalar s) => new ViewField(n, ViewFieldKind.Scalar, s, null);

    [Fact]
    public void Deterministic_And_Order_Sensitive()
    {
        var a = new ViewStruct("Hud", new List<ViewField> {
            Scalar("tick", ViewScalar.Int), Scalar("name", ViewScalar.String) });
        var a2 = new ViewStruct("Hud", new List<ViewField> {
            Scalar("tick", ViewScalar.Int), Scalar("name", ViewScalar.String) });
        var reordered = new ViewStruct("Hud", new List<ViewField> {
            Scalar("name", ViewScalar.String), Scalar("tick", ViewScalar.Int) });

        Assert.Equal(ViewVersionHash.Compute(a), ViewVersionHash.Compute(a2));   // deterministic
        Assert.NotEqual(ViewVersionHash.Compute(a), ViewVersionHash.Compute(reordered)); // order matters
        Assert.NotEqual(0u, ViewVersionHash.Compute(a));
    }

    [Fact]
    public void Rename_And_Retype_Change_Hash()
    {
        var baseV = new ViewStruct("Hud", new List<ViewField> { Scalar("tick", ViewScalar.Int) });
        var renamed = new ViewStruct("Hud", new List<ViewField> { Scalar("frame", ViewScalar.Int) });
        var retyped = new ViewStruct("Hud", new List<ViewField> { Scalar("tick", ViewScalar.Float) });
        Assert.NotEqual(ViewVersionHash.Compute(baseV), ViewVersionHash.Compute(renamed));
        Assert.NotEqual(ViewVersionHash.Compute(baseV), ViewVersionHash.Compute(retyped));
    }
}
```

- [ ] **Step 2: Run the test to verify it fails**

Run (from `$KF/CodegenTool~/Tests/`): `/home/liory/.dotnet/dotnet test`
Expected: FAIL — `ViewVersionHash` does not exist.

- [ ] **Step 3: Write minimal implementation**

Create `ViewVersionHash.cs`:
```csharp
using System.Text;

namespace Yeroket.KernelFramework.Codegen
{
    // The [View] schema-version hash: a stable FNV-1a over the view's STRUCTURE (model name +
    // field names/kinds in declared order; array element fields recursed). Data/values are not
    // hashed. This is the SOLE author of the version — the C++ engine only reads+compares it,
    // never recomputes — so there is no cross-language hash-agreement hazard.
    public static class ViewVersionHash
    {
        public static uint Compute(ViewStruct v)
        {
            var sb = new StringBuilder();
            sb.Append(v.Name);
            AppendFields(sb, v);
            return Fold(Fnv1a64(sb.ToString()));
        }

        static void AppendFields(StringBuilder sb, ViewStruct v)
        {
            foreach (var f in v.Fields)
            {
                sb.Append('\n').Append(f.Name).Append('|').Append(KindTag(f));
                if (f.Kind == ViewFieldKind.StructArray || f.Kind == ViewFieldKind.Struct)
                    AppendFields(sb, f.Struct);   // recurse element fields, declared order
            }
        }

        static string KindTag(ViewField f)
        {
            if (f.Kind == ViewFieldKind.StructArray) return "array";
            if (f.Kind == ViewFieldKind.Struct)      return "struct";
            return f.Scalar switch {
                ViewScalar.Int => "int", ViewScalar.Float => "float",
                ViewScalar.Bool => "bool", ViewScalar.String => "string", _ => "?" };
        }

        static ulong Fnv1a64(string s)
        {
            ulong h = 14695981039346656037UL;
            foreach (byte b in Encoding.UTF8.GetBytes(s)) { h ^= b; h *= 1099511628211UL; }
            return h;
        }

        static uint Fold(ulong h) => (uint)(h ^ (h >> 32));
    }
}
```

- [ ] **Step 4: Run the test to verify it passes**

Run (from `$KF/CodegenTool~/Tests/`): `/home/liory/.dotnet/dotnet test`
Expected: PASS (both facts).

- [ ] **Step 5: Commit**

```bash
cd /home/liory/Github/Yeroket-Fantasy
git checkout -- Packages/com.yeroket.utility.kernel-framework/RoslynAnalyzers/SDFNodeGenerator.dll 2>/dev/null || true
git add Packages/com.yeroket.utility.kernel-framework/SourceGenerator~/Transpiler/ViewVersionHash.cs \
        Packages/com.yeroket.utility.kernel-framework/CodegenTool~/Tests/ViewVersionHashTests.cs
git commit -m "feat(view-codegen): ViewVersionHash — FNV-1a schema-version hash over [View] structure"
```

---

## Task 2: Yeroket — `ViewBlobEmitter.EmitHeader` (C#)

**Files:**
- Create: `$KF/SourceGenerator~/Transpiler/ViewBlobEmitter.cs`
- Test: `$KF/CodegenTool~/Tests/ViewBlobEmitterTests.cs`

**Interfaces:**
- Consumes: `ViewStruct`/`ViewField`/`ViewFieldKind`/`ViewScalar`; `ViewVersionHash.Compute` (Task 1).
- Produces: `public static string ViewBlobEmitter.EmitHeader(ViewStruct v)` — a C++ header with `constexpr` descriptors + `constexpr ViewBlob k<Name>Blob`. Returns `\n`-normalized text (mirror `RmlMarkupEmitter`'s trailing `.Replace("\r\n","\n")`).

**Emit shape** (for the Hud schema — `tick:int, bodyCount:int, activeLensName:string, activeLensCount:int, factions:HudFaction[], events:HudEvent[]`; `HudFaction{name:string,grievance:float,focused:bool,known:bool,inLens:bool,recentChanged:bool}`, `HudEvent{kind:string,tick:int}`):
```cpp
#pragma once
// GENERATED by Yeroket kernel-codegen (--view-blob) — DO NOT EDIT. Regenerate from the canonical [View] schema.
#include "Ui/ViewBlob.h"

namespace Vixen::Views {

inline constexpr Vixen::RenderGraph::ViewFieldDesc kHudFaction_fields[] = {
    {"name", Vixen::RenderGraph::ViewKind::String, {}},
    {"grievance", Vixen::RenderGraph::ViewKind::Float, {}},
    {"focused", Vixen::RenderGraph::ViewKind::Bool, {}},
    {"known", Vixen::RenderGraph::ViewKind::Bool, {}},
    {"inLens", Vixen::RenderGraph::ViewKind::Bool, {}},
    {"recentChanged", Vixen::RenderGraph::ViewKind::Bool, {}},
};
inline constexpr Vixen::RenderGraph::ViewFieldDesc kHudEvent_fields[] = {
    {"kind", Vixen::RenderGraph::ViewKind::String, {}},
    {"tick", Vixen::RenderGraph::ViewKind::Int, {}},
};
inline constexpr Vixen::RenderGraph::ViewFieldDesc kHud_fields[] = {
    {"tick", Vixen::RenderGraph::ViewKind::Int, {}},
    {"bodyCount", Vixen::RenderGraph::ViewKind::Int, {}},
    {"activeLensName", Vixen::RenderGraph::ViewKind::String, {}},
    {"activeLensCount", Vixen::RenderGraph::ViewKind::Int, {}},
    {"factions", Vixen::RenderGraph::ViewKind::ArrayOfStruct, kHudFaction_fields},
    {"events", Vixen::RenderGraph::ViewKind::ArrayOfStruct, kHudEvent_fields},
};
inline constexpr Vixen::RenderGraph::ViewBlob kHudBlob = {
    "hud", kHud_fields, 0x9E3779B1u /* generated version */ };

}  // namespace Vixen::Views
```
Notes for the emitter: model name = `v.Name.ToLowerInvariant()`; each array field's `elem` initializer names its element `k<ElemName>_fields` array (emit element arrays before the top-level array, depth-first, de-duplicated by element struct name via a `HashSet<string>`, mirroring `RmlDataModelEmitter.CollectStructs`); the version literal is `"0x" + ViewVersionHash.Compute(v).ToString("X8") + "u"`. `ViewFieldDesc.elem` for scalars is `{}` (empty span).

- [ ] **Step 1: Write the failing test**

Create `ViewBlobEmitterTests.cs`:
```csharp
using Xunit;
using Yeroket.KernelFramework.Codegen;
using System.Collections.Generic;

public class ViewBlobEmitterTests
{
    static ViewField Scalar(string n, ViewScalar s) => new ViewField(n, ViewFieldKind.Scalar, s, null);

    static ViewStruct Hud()
    {
        var faction = new ViewStruct("HudFaction", new List<ViewField> {
            Scalar("name", ViewScalar.String), Scalar("grievance", ViewScalar.Float),
            Scalar("focused", ViewScalar.Bool) });
        return new ViewStruct("Hud", new List<ViewField> {
            Scalar("tick", ViewScalar.Int),
            new ViewField("factions", ViewFieldKind.StructArray, null, faction) });
    }

    [Fact]
    public void Header_Has_ModelName_Fields_And_Version()
    {
        string h = ViewBlobEmitter.EmitHeader(Hud());
        Assert.Contains("#include \"Ui/ViewBlob.h\"", h);
        Assert.Contains("kHudFaction_fields[]", h);           // element array emitted first
        Assert.Contains("kHud_fields[]", h);
        Assert.Contains("\"tick\", Vixen::RenderGraph::ViewKind::Int", h);
        Assert.Contains("\"factions\", Vixen::RenderGraph::ViewKind::ArrayOfStruct, kHudFaction_fields", h);
        Assert.Contains("kHudBlob = {", h);
        Assert.Contains("\"hud\", kHud_fields,", h);          // lowercased model name
        Assert.Contains("0x" + ViewVersionHash.Compute(Hud()).ToString("X8") + "u", h);
        Assert.DoesNotContain("\r\n", h);                      // normalized newlines
    }
}
```

- [ ] **Step 2: Run the test to verify it fails**

Run (from `$KF/CodegenTool~/Tests/`): `/home/liory/.dotnet/dotnet test --filter ViewBlobEmitterTests`
Expected: FAIL — `ViewBlobEmitter` does not exist.

- [ ] **Step 3: Write minimal implementation**

Create `ViewBlobEmitter.cs` with `EmitHeader`:
```csharp
using System.Collections.Generic;
using System.Linq;
using System.Text;

namespace Yeroket.KernelFramework.Codegen
{
    // Emits the reflection-blob faces of a [View] schema: a constexpr C++ header (EmitHeader) and a
    // runtime data file (EmitDataFile, Task 3). Sibling of RmlDataModelEmitter/RmlMarkupEmitter.
    // Both faces carry the same ViewVersionHash. The blob describes the view in the finite kind
    // catalogue (int/float/bool/string scalars, array-of-struct) — no offsets/strides.
    public static class ViewBlobEmitter
    {
        static string KindEnum(ViewField f) => f.Kind switch {
            ViewFieldKind.StructArray => "ArrayOfStruct",
            _ => f.Scalar switch {
                ViewScalar.Int => "Int", ViewScalar.Float => "Float",
                ViewScalar.Bool => "Bool", ViewScalar.String => "String", _ => "Int" }
        };

        // Depth-first collect of element structs (de-duped by name), so element arrays are emitted
        // before the top-level array that references them.
        static void Collect(ViewStruct v, List<ViewStruct> outList, HashSet<string> seen)
        {
            foreach (var f in v.Fields)
                if (f.Kind == ViewFieldKind.StructArray && seen.Add(f.Struct.Name))
                {
                    Collect(f.Struct, outList, seen);
                    outList.Add(f.Struct);
                }
        }

        static void EmitFieldArray(StringBuilder sb, string arrayName, ViewStruct v)
        {
            sb.AppendLine($"inline constexpr Vixen::RenderGraph::ViewFieldDesc {arrayName}[] = {{");
            foreach (var f in v.Fields)
            {
                string elem = f.Kind == ViewFieldKind.StructArray ? $"k{f.Struct.Name}_fields" : "{}";
                sb.AppendLine($"    {{\"{f.Name}\", Vixen::RenderGraph::ViewKind::{KindEnum(f)}, {elem}}},");
            }
            sb.AppendLine("};");
        }

        public static string EmitHeader(ViewStruct v)
        {
            var sb = new StringBuilder();
            sb.AppendLine("#pragma once");
            sb.AppendLine("// GENERATED by Yeroket kernel-codegen (--view-blob) — DO NOT EDIT. Regenerate from the canonical [View] schema.");
            sb.AppendLine("#include \"Ui/ViewBlob.h\"");
            sb.AppendLine();
            sb.AppendLine("namespace Vixen::Views {");
            sb.AppendLine();

            var elems = new List<ViewStruct>();
            Collect(v, elems, new HashSet<string>());
            foreach (var e in elems) { EmitFieldArray(sb, $"k{e.Name}_fields", e); }
            EmitFieldArray(sb, $"k{v.Name}_fields", v);
            sb.AppendLine();
            string ver = "0x" + ViewVersionHash.Compute(v).ToString("X8") + "u";
            sb.AppendLine($"inline constexpr Vixen::RenderGraph::ViewBlob k{v.Name}Blob = {{");
            sb.AppendLine($"    \"{v.Name.ToLowerInvariant()}\", k{v.Name}_fields, {ver} /* generated version */ }};");
            sb.AppendLine();
            sb.AppendLine("}  // namespace Vixen::Views");
            return sb.ToString().Replace("\r\n", "\n");
        }
    }
}
```

- [ ] **Step 4: Run the test to verify it passes**

Run (from `$KF/CodegenTool~/Tests/`): `/home/liory/.dotnet/dotnet test --filter ViewBlobEmitterTests`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
cd /home/liory/Github/Yeroket-Fantasy
git checkout -- Packages/com.yeroket.utility.kernel-framework/RoslynAnalyzers/SDFNodeGenerator.dll 2>/dev/null || true
git add Packages/com.yeroket.utility.kernel-framework/SourceGenerator~/Transpiler/ViewBlobEmitter.cs \
        Packages/com.yeroket.utility.kernel-framework/CodegenTool~/Tests/ViewBlobEmitterTests.cs
git commit -m "feat(view-codegen): ViewBlobEmitter.EmitHeader — constexpr ViewBlob header face"
```

---

## Task 3: Yeroket — `ViewBlobEmitter.EmitDataFile` + `--view-blob` CLI

**Files:**
- Modify: `$KF/SourceGenerator~/Transpiler/ViewBlobEmitter.cs` (add `EmitDataFile`)
- Modify: `$KF/CodegenTool~/Program.cs` (add `--view-blob` branch)
- Modify: `$KF/CodegenTool~/Tests/ViewBlobEmitterTests.cs` (add data-file test)

**Interfaces:**
- Consumes: `EmitHeader` (Task 2), `ViewVersionHash.Compute`.
- Produces: `public static string ViewBlobEmitter.EmitDataFile(ViewStruct v)` — the runtime `.viewblob` text; the `--view-blob` CLI mode (`--out-header`, `--out-datafile`, `--check`) matching the `--view-markup` branch shape.

**Data-file format** (line-oriented, trivially parseable in C++ — see Task 6):
```
# viewblob v1
model hud
version 0x9E3779B1
elem HudFaction
  name string
  grievance float
  focused bool
  known bool
  inLens bool
  recentChanged bool
field tick int
field bodyCount int
field activeLensName string
field activeLensCount int
field factions array HudFaction
field events array HudEvent
```
Rules: `#` comment lines ignored; `model <name>`; `version 0x<hex8>`; `elem <StructName>` opens a block, its `  <name> <kind>` indented lines (two-space) are element scalar fields until the next non-indented line; `field <name> <kind>` for top-level scalars; `field <name> array <ElemName>` for arrays (referencing a previously-declared `elem`). Emit elements before the top-level fields that reference them, depth-first (same order as the header). Kinds: `int|float|bool|string`. `\n`-normalized.

- [ ] **Step 1: Write the failing test**

Append to `ViewBlobEmitterTests.cs`:
```csharp
    [Fact]
    public void DataFile_Has_Model_Version_Elem_And_Fields()
    {
        string d = ViewBlobEmitter.EmitDataFile(Hud());
        Assert.Contains("model hud", d);
        Assert.Contains("version 0x" + ViewVersionHash.Compute(Hud()).ToString("X8"), d);
        Assert.Contains("elem HudFaction", d);
        Assert.Contains("  grievance float", d);          // indented element field
        Assert.Contains("field tick int", d);
        Assert.Contains("field factions array HudFaction", d);
        // elem block must precede the array field that references it
        Assert.True(d.IndexOf("elem HudFaction") < d.IndexOf("field factions array HudFaction"));
        Assert.DoesNotContain("\r\n", d);
    }
```

- [ ] **Step 2: Run the test to verify it fails**

Run (from `$KF/CodegenTool~/Tests/`): `/home/liory/.dotnet/dotnet test --filter ViewBlobEmitterTests`
Expected: FAIL — `EmitDataFile` does not exist.

- [ ] **Step 3: Write the implementation**

Add to `ViewBlobEmitter.cs`:
```csharp
        static string KindTag(ViewField f) => f.Kind switch {
            ViewFieldKind.StructArray => "array",
            _ => f.Scalar switch {
                ViewScalar.Int => "int", ViewScalar.Float => "float",
                ViewScalar.Bool => "bool", ViewScalar.String => "string", _ => "int" }
        };

        public static string EmitDataFile(ViewStruct v)
        {
            var sb = new StringBuilder();
            sb.AppendLine("# viewblob v1");
            sb.AppendLine($"model {v.Name.ToLowerInvariant()}");
            sb.AppendLine($"version 0x{ViewVersionHash.Compute(v):X8}");

            var elems = new List<ViewStruct>();
            Collect(v, elems, new HashSet<string>());
            foreach (var e in elems)
            {
                sb.AppendLine($"elem {e.Name}");
                foreach (var rf in e.Fields)
                    sb.AppendLine($"  {rf.Name} {KindTag(rf)}");
            }
            foreach (var f in v.Fields)
            {
                if (f.Kind == ViewFieldKind.StructArray)
                    sb.AppendLine($"field {f.Name} array {f.Struct.Name}");
                else
                    sb.AppendLine($"field {f.Name} {KindTag(f)}");
            }
            return sb.ToString().Replace("\r\n", "\n");
        }
```
Add the `--view-blob` branch to `Program.cs` (place it right after the `--view-markup` block, before the `--struct` usage check), mirroring `--view-markup`:
```csharp
        string? viewBlob = Flag(args, "--view-blob");
        if (viewBlob is not null)
        {
            string? outHeader   = Flag(args, "--out-header");
            string? outDatafile = Flag(args, "--out-datafile");
            if (schema is null || outHeader is null || outDatafile is null)
            {
                Console.Error.WriteLine("usage: --schema <dir> --view-blob <Name> --out-header <path> --out-datafile <path> [--check]");
                return 2;
            }
            var vfiles = Directory.GetFiles(schema, "*.cs", SearchOption.AllDirectories);
            var views  = CompilationLoader.LoadViews(vfiles);
            var vsym   = views.FirstOrDefault(s => s.Name == viewBlob);
            if (vsym is null)
            {
                Console.Error.WriteLine($"[View] named '{viewBlob}' not found; found: {string.Join(", ", views.Select(s => s.Name))}");
                return 2;
            }
            var vmodel = ViewModelBuilder.Build(vsym);
            var header = ViewBlobEmitter.EmitHeader(vmodel);
            var data   = ViewBlobEmitter.EmitDataFile(vmodel);
            if (check)
            {
                bool hOk = Same(outHeader, header), dOk = Same(outDatafile, data);
                if (!hOk) Console.Error.WriteLine($"STALE: {outHeader}");
                if (!dOk) Console.Error.WriteLine($"STALE: {outDatafile}");
                return (hOk && dOk) ? 0 : 1;
            }
            Write(outHeader, header);
            Write(outDatafile, data);
            return 0;
        }
```

- [ ] **Step 4: Run the tests + smoke the CLI**

Run (from `$KF/CodegenTool~/Tests/`): `/home/liory/.dotnet/dotnet test --filter ViewBlobEmitterTests`
Expected: PASS.
Smoke the CLI (from `$KF/CodegenTool~`), writing to /tmp:
```bash
/home/liory/.dotnet/dotnet run --project . -c Release -- \
  --schema /mnt/c/cpp/VBVS--VIXEN/VIXEN/codegen/view-schemas --view-blob Hud \
  --out-header /tmp/Hud.blob.g.h --out-datafile /tmp/hud.viewblob
```
Expected: both files written; `/tmp/Hud.blob.g.h` contains `kHudBlob`, `/tmp/hud.viewblob` contains `model hud`.

- [ ] **Step 5: Commit**

```bash
cd /home/liory/Github/Yeroket-Fantasy
git checkout -- Packages/com.yeroket.utility.kernel-framework/RoslynAnalyzers/SDFNodeGenerator.dll 2>/dev/null || true
git add Packages/com.yeroket.utility.kernel-framework/SourceGenerator~/Transpiler/ViewBlobEmitter.cs \
        Packages/com.yeroket.utility.kernel-framework/CodegenTool~/Program.cs \
        Packages/com.yeroket.utility.kernel-framework/CodegenTool~/Tests/ViewBlobEmitterTests.cs
git commit -m "feat(view-codegen): ViewBlobEmitter.EmitDataFile + --view-blob CLI (header+datafile)"
```

---

## Task 4: VIXEN — `ViewBlob` contract + `ViewValue`

**Files:**
- Create: `VIXEN/libraries/RenderGraph/include/Ui/ViewBlob.h`
- Test: `VIXEN/libraries/RenderGraph/tests/test_view_blob.cpp`
- Modify: `VIXEN/libraries/RenderGraph/tests/CMakeLists.txt` (register `test_view_blob`)

**Interfaces:**
- Produces:
  - `enum class Vixen::RenderGraph::ViewKind : uint8_t { Int, Float, Bool, String, ArrayOfStruct };`
  - `struct ViewFieldDesc { std::string_view name; ViewKind kind; std::span<const ViewFieldDesc> elem; };`
  - `struct ViewBlob { std::string_view model; std::span<const ViewFieldDesc> fields; uint32_t version; };`
  - `struct ViewValue { enum class Tag { Int, Float, Bool, String } tag; int i; float f; bool b; std::string s; static ViewValue I(int); static ViewValue F(float); static ViewValue B(bool); static ViewValue S(std::string); };`
  - `bool KindAcceptsValue(ViewKind, const ViewValue&);` — scalar kind ↔ value tag agreement (ArrayOfStruct accepts nothing).
- Consumed by: Tasks 5, 6, 7 and the generated header (Task 8).

- [ ] **Step 1: Write the failing test**

Create `test_view_blob.cpp`:
```cpp
#include <gtest/gtest.h>
#include "Ui/ViewBlob.h"
using namespace Vixen::RenderGraph;

TEST(ViewBlob, ConstexprDescriptorHoldsFieldsInOrder) {
    static constexpr ViewFieldDesc elem[] = {
        {"grievance", ViewKind::Float, {}}, {"focused", ViewKind::Bool, {}} };
    static constexpr ViewFieldDesc fields[] = {
        {"tick", ViewKind::Int, {}},
        {"factions", ViewKind::ArrayOfStruct, elem} };
    static constexpr ViewBlob blob = {"hud", fields, 0xABCD1234u};
    EXPECT_EQ(blob.model, "hud");
    EXPECT_EQ(blob.version, 0xABCD1234u);
    ASSERT_EQ(blob.fields.size(), 2u);
    EXPECT_EQ(blob.fields[0].name, "tick");
    EXPECT_EQ(blob.fields[1].kind, ViewKind::ArrayOfStruct);
    ASSERT_EQ(blob.fields[1].elem.size(), 2u);
    EXPECT_EQ(blob.fields[1].elem[0].name, "grievance");
}

TEST(ViewValue, KindAcceptsMatchingTagOnly) {
    EXPECT_TRUE (KindAcceptsValue(ViewKind::Int,    ViewValue::I(3)));
    EXPECT_FALSE(KindAcceptsValue(ViewKind::Int,    ViewValue::S("x")));
    EXPECT_TRUE (KindAcceptsValue(ViewKind::String, ViewValue::S("x")));
    EXPECT_FALSE(KindAcceptsValue(ViewKind::ArrayOfStruct, ViewValue::I(1)));
}
```

- [ ] **Step 2: Register the test + run to verify it fails**

Add to `VIXEN/libraries/RenderGraph/tests/CMakeLists.txt` (mirror an existing small CPU test registration, e.g. `test_view_hud_golden`):
```cmake
add_executable(test_view_blob test_view_blob.cpp)
target_link_libraries(test_view_blob PRIVATE ${RENDERGRAPH_TEST_COMMON_LIBS} GTest::gtest_main)
set_target_properties(test_view_blob PROPERTIES FOLDER "Tests/RenderGraph Tests")
gtest_discover_tests(test_view_blob DISCOVERY_MODE PRE_TEST DISCOVERY_TIMEOUT 60)
```
Build (Windows-side, poll on interval): `build.bat build` then run `build/ninja/.../test_view_blob`.
Expected: FAIL to compile — `Ui/ViewBlob.h` missing.

- [ ] **Step 3: Write the implementation**

Create `Ui/ViewBlob.h`:
```cpp
#pragma once
// The reflection-blob contract for the renderer-agnostic view path (Inc-2b). A ViewBlob describes
// a [View] in the finite kind catalogue; a BlobView builds a live RmlUi data model from it and a
// ViewStore holds the typed data. Delivered as a constexpr header (Generated/Hud.blob.g.h) or a
// runtime .viewblob file (ViewBlobFile) — both produce this struct. No byte offsets/strides.
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace Vixen::RenderGraph {

enum class ViewKind : uint8_t { Int, Float, Bool, String, ArrayOfStruct };

struct ViewFieldDesc {
    std::string_view name;
    ViewKind         kind;
    std::span<const ViewFieldDesc> elem;   // element scalar fields; empty unless ArrayOfStruct
};

struct ViewBlob {
    std::string_view               model;
    std::span<const ViewFieldDesc> fields;   // top-level fields, declared order
    uint32_t                       version;  // C#-authored schema hash; engine reads/compares only
};

struct ViewValue {
    enum class Tag : uint8_t { Int, Float, Bool, String } tag;
    int         i = 0;
    float       f = 0.0f;
    bool        b = false;
    std::string s;
    static ViewValue I(int v)          { ViewValue r; r.tag = Tag::Int;    r.i = v; return r; }
    static ViewValue F(float v)        { ViewValue r; r.tag = Tag::Float;  r.f = v; return r; }
    static ViewValue B(bool v)         { ViewValue r; r.tag = Tag::Bool;   r.b = v; return r; }
    static ViewValue S(std::string v)  { ViewValue r; r.tag = Tag::String; r.s = std::move(v); return r; }
};

inline bool KindAcceptsValue(ViewKind k, const ViewValue& v) {
    switch (k) {
        case ViewKind::Int:    return v.tag == ViewValue::Tag::Int;
        case ViewKind::Float:  return v.tag == ViewValue::Tag::Float;
        case ViewKind::Bool:   return v.tag == ViewValue::Tag::Bool;
        case ViewKind::String: return v.tag == ViewValue::Tag::String;
        case ViewKind::ArrayOfStruct: return false;
    }
    return false;
}

}  // namespace Vixen::RenderGraph
```

- [ ] **Step 4: Build + run to verify it passes**

Build (Windows-side, poll): `build.bat build` (target `test_view_blob`), then run the exe.
Expected: PASS (2 tests).

- [ ] **Step 5: Commit**

```bash
cd /mnt/c/cpp/VBVS--VIXEN
git add VIXEN/libraries/RenderGraph/include/Ui/ViewBlob.h \
        VIXEN/libraries/RenderGraph/tests/test_view_blob.cpp \
        VIXEN/libraries/RenderGraph/tests/CMakeLists.txt
git commit -m "feat(view-contract): ViewBlob/ViewKind/ViewValue reflection-blob contract (Inc-2b)"
```

---

## Task 5: VIXEN — `ViewStore` (typed storage + by-field setters)

**Files:**
- Create: `VIXEN/libraries/RenderGraph/include/Ui/ViewStore.h`, `VIXEN/libraries/RenderGraph/src/Ui/ViewStore.cpp`
- Test: `VIXEN/libraries/RenderGraph/tests/test_view_store.cpp`
- Modify: `VIXEN/libraries/RenderGraph/tests/CMakeLists.txt` (register); `VIXEN/libraries/RenderGraph/CMakeLists.txt` (add `src/Ui/ViewStore.cpp` to the lib sources)

**Interfaces:**
- Consumes: `ViewBlob`, `ViewFieldDesc`, `ViewKind`, `ViewValue` (Task 4). RmlUi `Rml::String`, `Rml::DataModelHandle`.
- Produces:
  - `class ViewStore` with: `explicit ViewStore(const ViewBlob& blob, uint32_t consumerVersion);` `void SetScalar(std::string_view field, ViewValue v);` `struct RowHandle { ViewStore* store; size_t fieldIndex; void Set(size_t row, std::string_view elemField, ViewValue v); };` `RowHandle ResizeArray(std::string_view field, size_t n);` `void Flush(Rml::DataModelHandle& model);` `uint32_t Version() const;` and (for `BlobView`) `void* ScalarSlotPtr(size_t fieldIndex);` `void* ArraySlotPtr(size_t fieldIndex);` `const ViewBlob& Blob() const;`
  - Row storage type used by the array slots: `struct ViewRow { std::vector<int> ints; std::vector<float> floats; std::vector<unsigned char> bools; std::vector<Rml::String> strings; };` — but see the storage note below.

**Storage model (fixed design — critical for RmlUi read-back).** An array-of-struct field's data is a `std::vector<ViewRow>`; each `ViewRow` holds `std::vector<ViewCell> cells` (one cell per element field, in `elem[]` order); `ViewCell` is a fixed-layout tagged cell:
```cpp
struct ViewCell { int i; float f; bool b; Rml::String s; };  // one cell per element field; the
// BlobView member definitions read the cell's active member (by the field's kind).
```
Rationale: RmlUi's `ArrayDefinition<std::vector<ViewRow>>::Child` yields a `ViewRow*` per index, and the element `StructDefinition`'s members (built in Task 7) are custom definitions that read `static_cast<ViewRow*>(ptr)->cells[memberIndex]` then that cell's active member by kind. So the row must be an addressable object (a `ViewRow`), and each member definition captures its own `memberIndex` + kind — no member offsets needed. **`ViewStore` only stores**: it exposes `std::vector<ViewRow>& Array(size_t fieldIndex)` and each `ViewRow` exposes `ViewCell& Cell(size_t memberIndex)`; the RmlUi definition wiring (the custom member definitions over `cells[memberIndex]`) is entirely Task 7's job.

- [ ] **Step 1: Write the failing test**

Create `test_view_store.cpp`:
```cpp
#include <gtest/gtest.h>
#include "Ui/ViewStore.h"
using namespace Vixen::RenderGraph;

static constexpr ViewFieldDesc kElem[] = {
    {"grievance", ViewKind::Float, {}}, {"focused", ViewKind::Bool, {}} };
static constexpr ViewFieldDesc kFields[] = {
    {"tick", ViewKind::Int, {}},
    {"name", ViewKind::String, {}},
    {"factions", ViewKind::ArrayOfStruct, kElem} };
static constexpr ViewBlob kBlob = {"hud", kFields, 0x1111u};

TEST(ViewStore, ScalarSetAndReadBack) {
    ViewStore s(kBlob, 0x1111u);
    s.SetScalar("tick", ViewValue::I(42));
    s.SetScalar("name", ViewValue::S("Reds"));
    EXPECT_EQ(*static_cast<int*>(s.ScalarSlotPtr(0)), 42);
    EXPECT_EQ(*static_cast<Rml::String*>(s.ScalarSlotPtr(1)), "Reds");
    EXPECT_EQ(s.Version(), 0x1111u);
}

TEST(ViewStore, ArrayResizeAndRowSet) {
    ViewStore s(kBlob, 0x1111u);
    auto rows = s.ResizeArray("factions", 2);
    rows.Set(0, "grievance", ViewValue::F(0.7f));
    rows.Set(0, "focused", ViewValue::B(true));
    rows.Set(1, "grievance", ViewValue::F(0.1f));
    auto& arr = s.Array(2);
    ASSERT_EQ(arr.size(), 2u);
    EXPECT_FLOAT_EQ(arr[0].Cell(0).f, 0.7f);
    EXPECT_TRUE(arr[0].Cell(1).b);
}

TEST(ViewStore, RejectsWrongNameAndKind) {
    ViewStore s(kBlob, 0x1111u);
    s.SetScalar("nope", ViewValue::I(1));       // unknown name -> no-op (logged)
    s.SetScalar("tick", ViewValue::S("bad"));   // wrong kind -> no-op (logged)
    EXPECT_EQ(*static_cast<int*>(s.ScalarSlotPtr(0)), 0);  // unchanged
}
```

- [ ] **Step 2: Register + build + run to verify it fails**

Register in tests CMake (mirror Task 4's block, name `test_view_store`, link `ViewStore` via the RenderGraph lib). Add `src/Ui/ViewStore.cpp` to the RenderGraph library's source list in `VIXEN/libraries/RenderGraph/CMakeLists.txt` (find the `target_sources`/`add_library` list and append it).
Build (poll). Expected: FAIL to compile — `Ui/ViewStore.h` missing.

- [ ] **Step 3: Write the implementation**

Create `Ui/ViewStore.h`:
```cpp
#pragma once
#include "Ui/ViewBlob.h"
#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/Types.h>   // Rml::String
#include <cstddef>
#include <string_view>
#include <vector>

namespace Vixen::RenderGraph {

// One fixed-layout cell per element field; BlobView binds a ScalarDefinition to the active member
// by the field's kind. Only the member matching the field kind is ever read.
struct ViewCell { int i = 0; float f = 0.0f; bool b = false; Rml::String s; };

struct ViewRow {
    std::vector<ViewCell> cells;
    ViewCell& Cell(size_t memberIndex) { return cells[memberIndex]; }
};

// Generic typed storage for a described view + the by-field, blob-validated setter API. Setters
// validate field name + kind against the blob; a mismatch is logged and dropped (never a bad write).
class ViewStore {
public:
    ViewStore(const ViewBlob& blob, uint32_t consumerVersion);

    void SetScalar(std::string_view field, ViewValue v);

    struct RowHandle {
        ViewStore* store; size_t fieldIndex;
        void Set(size_t row, std::string_view elemField, ViewValue v);
    };
    RowHandle ResizeArray(std::string_view field, size_t n);

    void Flush(Rml::DataModelHandle& model);   // DirtyVariable every top-level field
    uint32_t Version() const { return consumerVersion_; }
    const ViewBlob& Blob() const { return blob_; }

    // For BlobView binding: raw pointers into the typed slots.
    void* ScalarSlotPtr(size_t fieldIndex);          // &int / &float / &bool / &Rml::String
    std::vector<ViewRow>& Array(size_t fieldIndex);  // the row container for an ArrayOfStruct field
    void* ArraySlotPtr(size_t fieldIndex) { return &Array(fieldIndex); }

private:
    struct ScalarSlot { int i = 0; float f = 0.0f; bool b = false; Rml::String s; };
    int FindField(std::string_view name) const;      // -1 if absent
    int FindElemField(size_t fieldIndex, std::string_view name) const;

    const ViewBlob& blob_;
    uint32_t consumerVersion_;
    std::vector<ScalarSlot> scalars_;                        // one per top-level field (unused for arrays)
    std::vector<std::vector<ViewRow>> arrays_;               // one per top-level field (unused for scalars)
};

}  // namespace Vixen::RenderGraph
```
Create `src/Ui/ViewStore.cpp`:
```cpp
#include "Ui/ViewStore.h"
#include <RmlUi/Core/Log.h>

namespace Vixen::RenderGraph {

ViewStore::ViewStore(const ViewBlob& blob, uint32_t consumerVersion)
    : blob_(blob), consumerVersion_(consumerVersion) {
    scalars_.resize(blob_.fields.size());
    arrays_.resize(blob_.fields.size());
}

int ViewStore::FindField(std::string_view name) const {
    for (size_t k = 0; k < blob_.fields.size(); ++k)
        if (blob_.fields[k].name == name) return static_cast<int>(k);
    return -1;
}

int ViewStore::FindElemField(size_t fieldIndex, std::string_view name) const {
    const auto& elem = blob_.fields[fieldIndex].elem;
    for (size_t k = 0; k < elem.size(); ++k) if (elem[k].name == name) return static_cast<int>(k);
    return -1;
}

static void AssignCell(ViewCell& c, ViewKind kind, const ViewValue& v) {
    switch (kind) {
        case ViewKind::Int:    c.i = v.i; break;
        case ViewKind::Float:  c.f = v.f; break;
        case ViewKind::Bool:   c.b = v.b; break;
        case ViewKind::String: c.s = v.s; break;
        default: break;
    }
}

void ViewStore::SetScalar(std::string_view field, ViewValue v) {
    int idx = FindField(field);
    if (idx < 0) { Rml::Log::Message(Rml::Log::LT_ERROR, "ViewStore: unknown field '%.*s'", (int)field.size(), field.data()); return; }
    ViewKind k = blob_.fields[idx].kind;
    if (!KindAcceptsValue(k, v)) { Rml::Log::Message(Rml::Log::LT_ERROR, "ViewStore: kind mismatch for '%.*s'", (int)field.size(), field.data()); return; }
    auto& slot = scalars_[idx];
    switch (k) {
        case ViewKind::Int:    slot.i = v.i; break;
        case ViewKind::Float:  slot.f = v.f; break;
        case ViewKind::Bool:   slot.b = v.b; break;
        case ViewKind::String: slot.s = v.s; break;
        default: break;
    }
}

ViewStore::RowHandle ViewStore::ResizeArray(std::string_view field, size_t n) {
    int idx = FindField(field);
    if (idx < 0 || blob_.fields[idx].kind != ViewKind::ArrayOfStruct) {
        Rml::Log::Message(Rml::Log::LT_ERROR, "ViewStore: '%.*s' is not an array field", (int)field.size(), field.data());
        return RowHandle{this, static_cast<size_t>(-1)};
    }
    auto& rows = arrays_[idx];
    const size_t members = blob_.fields[idx].elem.size();
    rows.assign(n, ViewRow{});
    for (auto& r : rows) r.cells.resize(members);
    return RowHandle{this, static_cast<size_t>(idx)};
}

void ViewStore::RowHandle::Set(size_t row, std::string_view elemField, ViewValue v) {
    if (fieldIndex == static_cast<size_t>(-1)) return;
    int mi = store->FindElemField(fieldIndex, elemField);
    if (mi < 0) { Rml::Log::Message(Rml::Log::LT_ERROR, "ViewStore: unknown elem field '%.*s'", (int)elemField.size(), elemField.data()); return; }
    ViewKind k = store->blob_.fields[fieldIndex].elem[mi].kind;
    if (!KindAcceptsValue(k, v)) { Rml::Log::Message(Rml::Log::LT_ERROR, "ViewStore: elem kind mismatch for '%.*s'", (int)elemField.size(), elemField.data()); return; }
    auto& rows = store->arrays_[fieldIndex];
    if (row >= rows.size()) return;
    AssignCell(rows[row].cells[mi], k, v);
}

void* ViewStore::ScalarSlotPtr(size_t fieldIndex) {
    auto& slot = scalars_[fieldIndex];
    switch (blob_.fields[fieldIndex].kind) {
        case ViewKind::Int:    return &slot.i;
        case ViewKind::Float:  return &slot.f;
        case ViewKind::Bool:   return &slot.b;
        case ViewKind::String: return &slot.s;
        default: return nullptr;
    }
}

std::vector<ViewRow>& ViewStore::Array(size_t fieldIndex) { return arrays_[fieldIndex]; }

void ViewStore::Flush(Rml::DataModelHandle& model) {
    for (const auto& f : blob_.fields) {
        // f.name is a string_view into constexpr/parser-owned storage; DirtyVariable takes a String.
        model.DirtyVariable(Rml::String(f.name));
    }
}

}  // namespace Vixen::RenderGraph
```

- [ ] **Step 4: Build + run to verify it passes**

Build (poll), run `test_view_store`. Expected: PASS (3 tests).

- [ ] **Step 5: Commit**

```bash
cd /mnt/c/cpp/VBVS--VIXEN
git add VIXEN/libraries/RenderGraph/include/Ui/ViewStore.h \
        VIXEN/libraries/RenderGraph/src/Ui/ViewStore.cpp \
        VIXEN/libraries/RenderGraph/tests/test_view_store.cpp \
        VIXEN/libraries/RenderGraph/tests/CMakeLists.txt \
        VIXEN/libraries/RenderGraph/CMakeLists.txt
git commit -m "feat(view-contract): ViewStore typed storage + blob-validated by-field setters (Inc-2b)"
```

---

## Task 6: VIXEN — `ViewBlobFile` (`.viewblob` parser)

**Files:**
- Create: `VIXEN/libraries/RenderGraph/include/Ui/ViewBlobFile.h`, `VIXEN/libraries/RenderGraph/src/Ui/ViewBlobFile.cpp`
- Test: `VIXEN/libraries/RenderGraph/tests/test_view_blob_file.cpp`
- Modify: tests CMake (register); RenderGraph lib CMake (add source)

**Interfaces:**
- Consumes: `ViewBlob`, `ViewFieldDesc`, `ViewKind` (Task 4).
- Produces: `class ViewBlobFile { public: static std::optional<ViewBlobFile> Parse(std::string_view text); const ViewBlob& Blob() const; };` — the parser OWNS the backing storage (strings + descriptor vectors) that the returned `ViewBlob`'s `string_view`/`span` point into, so the `ViewBlobFile` must outlive any use of its `Blob()`. `Parse` returns `nullopt` on any malformed input (logged). Also `static std::optional<ViewBlobFile> Load(const std::string& path);` (reads file, returns nullopt + logs if missing).

- [ ] **Step 1: Write the failing test**

Create `test_view_blob_file.cpp`:
```cpp
#include <gtest/gtest.h>
#include "Ui/ViewBlobFile.h"
using namespace Vixen::RenderGraph;

static const char* kGood =
    "# viewblob v1\n"
    "model hud\n"
    "version 0xABCD1234\n"
    "elem HudFaction\n"
    "  grievance float\n"
    "  focused bool\n"
    "field tick int\n"
    "field factions array HudFaction\n";

TEST(ViewBlobFile, ParsesModelVersionFieldsAndElems) {
    auto f = ViewBlobFile::Parse(kGood);
    ASSERT_TRUE(f.has_value());
    const ViewBlob& b = f->Blob();
    EXPECT_EQ(b.model, "hud");
    EXPECT_EQ(b.version, 0xABCD1234u);
    ASSERT_EQ(b.fields.size(), 2u);
    EXPECT_EQ(b.fields[0].name, "tick");
    EXPECT_EQ(b.fields[0].kind, ViewKind::Int);
    EXPECT_EQ(b.fields[1].name, "factions");
    EXPECT_EQ(b.fields[1].kind, ViewKind::ArrayOfStruct);
    ASSERT_EQ(b.fields[1].elem.size(), 2u);
    EXPECT_EQ(b.fields[1].elem[0].name, "grievance");
    EXPECT_EQ(b.fields[1].elem[0].kind, ViewKind::Float);
}

TEST(ViewBlobFile, RejectsUnknownKind) {
    auto f = ViewBlobFile::Parse("model hud\nversion 0x1\nfield tick banana\n");
    EXPECT_FALSE(f.has_value());
}

TEST(ViewBlobFile, RejectsArrayReferencingUndeclaredElem) {
    auto f = ViewBlobFile::Parse("model hud\nversion 0x1\nfield x array Ghost\n");
    EXPECT_FALSE(f.has_value());
}
```

- [ ] **Step 2: Register + build + run to verify it fails**

Register (mirror Task 4). Add `src/Ui/ViewBlobFile.cpp` to the RenderGraph lib sources. Build (poll). Expected: FAIL to compile — header missing.

- [ ] **Step 3: Write the implementation**

Create `Ui/ViewBlobFile.h`:
```cpp
#pragma once
#include "Ui/ViewBlob.h"
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include <deque>

namespace Vixen::RenderGraph {

// Runtime .viewblob parser (Inc-2b data-file delivery front-end). Owns the backing storage the
// produced ViewBlob's string_views/spans point into — keep the ViewBlobFile alive while using Blob().
// All malformed/missing input returns nullopt (logged), never throws.
class ViewBlobFile {
public:
    static std::optional<ViewBlobFile> Parse(std::string_view text);
    static std::optional<ViewBlobFile> Load(const std::string& path);
    const ViewBlob& Blob() const { return blob_; }

    ViewBlobFile(ViewBlobFile&&) = default;             // moves keep storage stable (deque/list)
    ViewBlobFile& operator=(ViewBlobFile&&) = default;
    ViewBlobFile(const ViewBlobFile&) = delete;
private:
    ViewBlobFile() = default;
    // Stable-address backing storage: deque never invalidates element addresses on growth, so the
    // spans/string_views into these stay valid.
    std::deque<std::string>        strings_;            // owns every name string
    std::deque<std::vector<ViewFieldDesc>> elemArrays_; // owns each elem[] array
    std::vector<ViewFieldDesc>     topFields_;
    ViewBlob                       blob_{};
};

}  // namespace Vixen::RenderGraph
```
Create `src/Ui/ViewBlobFile.cpp` (implement the line grammar from Task 3's format; return nullopt + `LT_ERROR` on: unknown kind token, `array` referencing an undeclared elem, missing `model`/`version`, malformed `version` hex). Key structure:
```cpp
#include "Ui/ViewBlobFile.h"
#include <RmlUi/Core/Log.h>
#include <charconv>
#include <fstream>
#include <sstream>
#include <unordered_map>

namespace Vixen::RenderGraph {

static bool ScalarKind(std::string_view t, ViewKind& out) {
    if (t == "int") { out = ViewKind::Int; return true; }
    if (t == "float") { out = ViewKind::Float; return true; }
    if (t == "bool") { out = ViewKind::Bool; return true; }
    if (t == "string") { out = ViewKind::String; return true; }
    return false;
}

std::optional<ViewBlobFile> ViewBlobFile::Parse(std::string_view text) {
    ViewBlobFile f;
    std::string model; uint32_t version = 0; bool haveModel = false, haveVersion = false;
    std::unordered_map<std::string, size_t> elemIndex;   // elem name -> index into elemArrays_
    std::string cur;
    std::istringstream in{std::string(text)};
    std::string line;
    std::string curElem; std::vector<ViewFieldDesc>* curElemVec = nullptr;
    auto intern = [&](std::string s) -> std::string_view { f.strings_.push_back(std::move(s)); return f.strings_.back(); };

    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        bool indented = (line[0] == ' ');
        std::istringstream ls{line};
        std::string tok; ls >> tok;
        if (indented) {
            if (!curElemVec) { Rml::Log::Message(Rml::Log::LT_ERROR, "viewblob: indented field outside elem"); return std::nullopt; }
            std::string kindTok; ls >> kindTok; ViewKind k;
            if (!ScalarKind(kindTok, k)) { Rml::Log::Message(Rml::Log::LT_ERROR, "viewblob: bad elem kind '%s'", kindTok.c_str()); return std::nullopt; }
            curElemVec->push_back(ViewFieldDesc{intern(tok), k, {}});
            continue;
        }
        curElemVec = nullptr;
        if (tok == "model") { ls >> model; haveModel = true; }
        else if (tok == "version") {
            std::string hx; ls >> hx;
            if (hx.rfind("0x", 0) != 0) { Rml::Log::Message(Rml::Log::LT_ERROR, "viewblob: bad version"); return std::nullopt; }
            auto r = std::from_chars(hx.data()+2, hx.data()+hx.size(), version, 16);
            if (r.ec != std::errc{}) { Rml::Log::Message(Rml::Log::LT_ERROR, "viewblob: bad version hex"); return std::nullopt; }
            haveVersion = true;
        }
        else if (tok == "elem") {
            std::string name; ls >> name;
            f.elemArrays_.emplace_back();
            elemIndex[name] = f.elemArrays_.size() - 1;
            curElemVec = &f.elemArrays_.back();
        }
        else if (tok == "field") {
            std::string name, kindTok; ls >> name >> kindTok;
            if (kindTok == "array") {
                std::string elemName; ls >> elemName;
                auto it = elemIndex.find(elemName);
                if (it == elemIndex.end()) { Rml::Log::Message(Rml::Log::LT_ERROR, "viewblob: array '%s' references undeclared elem '%s'", name.c_str(), elemName.c_str()); return std::nullopt; }
                std::span<const ViewFieldDesc> elem{f.elemArrays_[it->second]};
                f.topFields_.push_back(ViewFieldDesc{intern(name), ViewKind::ArrayOfStruct, elem});
            } else {
                ViewKind k;
                if (!ScalarKind(kindTok, k)) { Rml::Log::Message(Rml::Log::LT_ERROR, "viewblob: bad field kind '%s'", kindTok.c_str()); return std::nullopt; }
                f.topFields_.push_back(ViewFieldDesc{intern(name), k, {}});
            }
        }
        else { Rml::Log::Message(Rml::Log::LT_ERROR, "viewblob: unknown directive '%s'", tok.c_str()); return std::nullopt; }
    }
    if (!haveModel || !haveVersion) { Rml::Log::Message(Rml::Log::LT_ERROR, "viewblob: missing model/version"); return std::nullopt; }
    f.blob_ = ViewBlob{ intern(model), std::span<const ViewFieldDesc>{f.topFields_}, version };
    return f;
}

std::optional<ViewBlobFile> ViewBlobFile::Load(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) { Rml::Log::Message(Rml::Log::LT_ERROR, "viewblob: cannot open '%s'", path.c_str()); return std::nullopt; }
    std::ostringstream ss; ss << in.rdbuf();
    return Parse(ss.str());
}

}  // namespace Vixen::RenderGraph
```
> **Note on the `elem` span stability:** `f.elemArrays_` is a `std::deque`, so `emplace_back` for a later elem does not invalidate the address of an earlier `elemArrays_[i]` — the `std::span` captured for a `field … array …` stays valid. Same reasoning for `strings_`. The array field must be declared *after* its elem (the emitter guarantees this), so `elemArrays_[it->second]` already exists and won't be reallocated. Verify no `topFields_` reallocation invalidates `blob_.fields`: `topFields_` is fully built before `blob_` captures its span — good.

- [ ] **Step 4: Build + run to verify it passes**

Build (poll), run `test_view_blob_file`. Expected: PASS (3 tests).

- [ ] **Step 5: Commit**

```bash
cd /mnt/c/cpp/VBVS--VIXEN
git add VIXEN/libraries/RenderGraph/include/Ui/ViewBlobFile.h \
        VIXEN/libraries/RenderGraph/src/Ui/ViewBlobFile.cpp \
        VIXEN/libraries/RenderGraph/tests/test_view_blob_file.cpp \
        VIXEN/libraries/RenderGraph/tests/CMakeLists.txt \
        VIXEN/libraries/RenderGraph/CMakeLists.txt
git commit -m "feat(view-contract): ViewBlobFile — runtime .viewblob parser with hard-fail validation (Inc-2b)"
```

---

## Task 7: VIXEN — `BlobView : IView` (dynamic registration + version guard)

**Files:**
- Create: `VIXEN/libraries/RenderGraph/include/Ui/BlobView.h`, `VIXEN/libraries/RenderGraph/src/Ui/BlobView.cpp`
- Test: `VIXEN/libraries/RenderGraph/tests/test_blob_view.cpp`
- Modify: tests CMake (register); RenderGraph lib CMake (add source)

**Interfaces:**
- Consumes: `IView` (`Ui/IView.h`), `ViewBlob`/`ViewKind` (Task 4), `ViewStore`/`ViewRow`/`ViewCell` (Task 5). RmlUi public API: `Rml::DataModelConstructor`, `GetDataTypeRegister`, `Rml::DataTypeRegister::RegisterDefinition`, `Rml::StructDefinition::AddMember`, `BindCustomDataVariable`, `Rml::ScalarDefinition<T>`, `Rml::DataVariable`, `Rml::MakeUnique`.
- Produces: `class BlobView final : public IView { public: BlobView(const ViewBlob& blob, std::string documentPath); const char* ModelName() const override; void Register(Rml::DataModelConstructor& c) override; const char* DocumentPath() const override; ViewStore& Store(); bool Registered() const; };`

**Registration algorithm** (in `Register`):
1. If `store_.Version() != blob_.version` → `Rml::Log::Message(LT_ERROR, "View '%s' version mismatch: engine %u vs consumer %u — skipping register", model, blob_.version, store_.Version())`, set `registered_ = false`, `return`.
2. For each top-level field:
   - **scalar:** `c.BindCustomDataVariable(name, Rml::DataVariable(scalarDefFor(kind), store_.ScalarSlotPtr(idx)))`. Keep the `ScalarDefinition<T>` alive: build once per kind into a member `std::vector<Rml::UniquePtr<Rml::VariableDefinition>> ownedDefs_` and pass the raw pointer to `DataVariable` (RmlUi's `DataVariable` does not take ownership; `RegisterDefinition` does — for scalars bound directly we retain ownership in `ownedDefs_`).
   - **array-of-struct:** build a `StructDefinition` (via `Rml::MakeUnique<Rml::StructDefinition>()`), for each element member `AddMember(memberName, scalarDefForCell(kind, memberIndex))` where the member definition reads `ViewCell`'s active member — use a small `ViewCellScalarDefinition` (a `VariableDefinition` subclass whose `Get/Set` read/write `static_cast<ViewCell*>(ptr)->i/f/b/s` by kind). Register the struct def via `c.GetDataTypeRegister()->RegisterDefinition(familyId, std::move(structDef))`, wrap in an `ArrayDefinition` over `std::vector<ViewRow>`… **but** `ArrayDefinition<Container>::Child` yields `&(*it)` (a `ViewRow*`), and the struct's member defs must read a `ViewRow`'s `cells[memberIndex]`. So the member def is a `ViewRowMemberDefinition(memberIndex, kind)` whose `Get/Set` do `static_cast<ViewRow*>(ptr)->cells[memberIndex]` then the cell's active member. Register the array def and `BindCustomDataVariable(name, DataVariable(arrayDef, &store_.Array(idx)))`.
3. `model_ = c.GetModelHandle(); registered_ = true;`

**Synthetic FamilyId:** allocate via a process-monotonic counter (`static std::atomic<Rml::FamilyId>` seeded high, e.g. `0x7000'0000`, incremented per struct def) so it never collides with RmlUi's type-derived FamilyIds. Confirm the `RegisterDefinition` signature's id type against `DataTypeRegister.h` during implementation; if RmlUi exposes a `Family<T>()` only, fall back to registering per unique synthetic id.

> The custom `VariableDefinition` subclasses (`ViewRowMemberDefinition`, and for the array element a `StructDefinition` assembled from them; the top-level scalar uses stock `ScalarDefinition<T>`) live in `BlobView.cpp`. They mirror what `ScalarDefinition<T>::Get` does (`variant = *typed_ptr`) but index into the `ViewCell`. This is the crux of the increment — implement carefully against the RmlUi `Variant` API (`variant = intValue;` etc.).

- [ ] **Step 1: Write the failing test** (CPU, mirrors `test_view_hud_golden`'s RmlUi fixture — `NullRenderInterface` + system interface + `Rml::Initialise` + `CreateContext` + `CreateDataModel`)

Create `test_blob_view.cpp`:
```cpp
#include <gtest/gtest.h>
#include <RmlUi/Core.h>
#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/RenderInterface.h>
#include "Ui/BlobView.h"
#include "Ui/VixenRmlSystemInterface.h"
using namespace Vixen::RenderGraph;

namespace {
class NullRender final : public Rml::RenderInterface {
public:
    Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex>, Rml::Span<const int>) override { return 0; }
    void RenderGeometry(Rml::CompiledGeometryHandle, Rml::Vector2f, Rml::TextureHandle) override {}
    void ReleaseGeometry(Rml::CompiledGeometryHandle) override {}
    void EnableScissorRegion(bool) override {}
    void SetScissorRegion(Rml::Rectanglei) override {}
    Rml::TextureHandle LoadTexture(Rml::Vector2i&, const Rml::String&) override { return 0; }
    Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte>, Rml::Vector2i) override { return 0; }
    void ReleaseTexture(Rml::TextureHandle) override {}
};
static constexpr ViewFieldDesc kElem[] = { {"grievance", ViewKind::Float, {}}, {"focused", ViewKind::Bool, {}} };
static constexpr ViewFieldDesc kFields[] = {
    {"tick", ViewKind::Int, {}}, {"name", ViewKind::String, {}},
    {"factions", ViewKind::ArrayOfStruct, kElem} };
static constexpr ViewBlob kBlob = {"blobtest", kFields, 0x1234u};

struct RmlFixture {
    VixenRmlSystemInterface sys; NullRender render; Rml::Context* ctx = nullptr;
    RmlFixture() { Rml::SetSystemInterface(&sys); Rml::SetRenderInterface(&render); Rml::Initialise();
                   ctx = Rml::CreateContext("t", Rml::Vector2i(64,64)); }
    ~RmlFixture() { Rml::Shutdown(); }
};
}  // namespace

TEST(BlobView, MatchingVersionRegistersAndBinds) {
    RmlFixture fx;
    BlobView view(kBlob, "assets/ui/hud.rml");
    view.Store();                                   // consumer version defaults to blob.version (matching)
    view.Store().SetScalar("tick", ViewValue::I(7));
    view.Store().SetScalar("name", ViewValue::S("Reds"));
    auto rows = view.Store().ResizeArray("factions", 1);
    rows.Set(0, "grievance", ViewValue::F(0.5f));
    rows.Set(0, "focused", ViewValue::B(true));
    Rml::DataModelConstructor c = fx.ctx->CreateDataModel(view.ModelName());
    view.Register(c);
    EXPECT_TRUE(view.Registered());
    auto handle = c.GetModelHandle();
    // resolved read-back through RmlUi:
    Rml::Variant v;
    // (Use whatever RmlUi read path the golden test uses; at minimum assert Registered()==true and
    //  that a subsequent GetModelHandle is valid — deep value read is covered by test_view_blob_equiv.)
    EXPECT_TRUE((bool)handle);
}

TEST(BlobView, VersionMismatchSkipsRegister) {
    RmlFixture fx;
    ViewBlob bad = kBlob; bad.version = 0x1234u;
    BlobView view(bad, "assets/ui/hud.rml");
    view.SetConsumerVersion(0xDEADu);               // deliberate mismatch
    Rml::DataModelConstructor c = fx.ctx->CreateDataModel(view.ModelName());
    view.Register(c);
    EXPECT_FALSE(view.Registered());                // skipped
}
```
> Add `void SetConsumerVersion(uint32_t)` to `BlobView` (forwards to reconstruct/set the `ViewStore`'s consumer version) so the mismatch test can force a bad version. Default consumer version = `blob.version` (matching) at construction.

- [ ] **Step 2: Register + build + run to verify it fails**

Register (mirror). Add `src/Ui/BlobView.cpp` to lib sources. Build (poll). Expected: FAIL to compile — `Ui/BlobView.h` missing.

- [ ] **Step 3: Write the implementation**

Create `Ui/BlobView.h` and `src/Ui/BlobView.cpp` per the registration algorithm above. `BlobView` owns `ViewStore store_`, `std::string documentPath_`, `std::vector<Rml::UniquePtr<Rml::VariableDefinition>> ownedDefs_`, `Rml::DataModelHandle model_`, `bool registered_ = false`. `ModelName()` returns a cached `std::string` copy of `blob_.model` (so the `const char*` outlives the `string_view`). Implement the custom `ViewRowMemberDefinition` in the .cpp.

- [ ] **Step 4: Build + run to verify it passes**

Build (poll), run `test_blob_view`. Expected: PASS (2 tests) — matching registers, mismatch skips.

- [ ] **Step 5: Commit**

```bash
cd /mnt/c/cpp/VBVS--VIXEN
git add VIXEN/libraries/RenderGraph/include/Ui/BlobView.h \
        VIXEN/libraries/RenderGraph/src/Ui/BlobView.cpp \
        VIXEN/libraries/RenderGraph/tests/test_blob_view.cpp \
        VIXEN/libraries/RenderGraph/tests/CMakeLists.txt \
        VIXEN/libraries/RenderGraph/CMakeLists.txt
git commit -m "feat(view-contract): BlobView — generic IView host, dynamic RmlUi model from a ViewBlob (Inc-2b)"
```

---

## Task 8: VIXEN — codegen wiring + committed generated artifacts

**Files:**
- Modify: `VIXEN/codegen/CMakeLists.txt` (add `view_hud_blob_check`/`view_hud_blob_regen`)
- Create (generated, committed): `VIXEN/application/main/include/Generated/Hud.blob.g.h`, `VIXEN/libraries/RenderGraph/assets/ui/hud.viewblob`

**Interfaces:**
- Consumes: the `--view-blob` CLI (Task 3), the existing `_yk_tool`/`VIXEN_DOTNET` resolution + `view_hud_*` target pattern in `codegen/CMakeLists.txt`.
- Produces: committed `Hud.blob.g.h` (with `Vixen::Views::kHudBlob`) + `hud.viewblob`, both drift-guarded.

- [ ] **Step 1: Generate the artifacts from the canonical schema**

Run the `--view-blob` regen once by hand (from `$KF/CodegenTool~`) to produce the committed files:
```bash
/home/liory/.dotnet/dotnet run --project . -c Release -- \
  --schema /mnt/c/cpp/VBVS--VIXEN/VIXEN/codegen/view-schemas --view-blob Hud \
  --out-header  /mnt/c/cpp/VBVS--VIXEN/VIXEN/application/main/include/Generated/Hud.blob.g.h \
  --out-datafile /mnt/c/cpp/VBVS--VIXEN/VIXEN/libraries/RenderGraph/assets/ui/hud.viewblob
```
Verify `Hud.blob.g.h` has `kHudBlob` and `hud.viewblob` starts with `# viewblob v1` / `model hud`.

- [ ] **Step 2: Add the drift-guard targets**

In `VIXEN/codegen/CMakeLists.txt`, inside the `if(EXISTS "${_yk_tool}/CodegenTool.csproj")` block (after the `view_hud_markup_*` targets), add — mirroring `view_hud_check`/`view_hud_regen`:
```cmake
    # --- Hud reflection blob (View Contract Inc-2b): --view-blob emits a constexpr C++ header
    # (the generic BlobView's compile-time delivery) AND a runtime .viewblob data file. Both are
    # committed + drift-guarded against the canonical [View] schema, like every other generated artifact.
    set(_view_blob_args
        run --project "${_yk_tool}" -c Release --
        --schema "${_cg}/view-schemas" --view-blob Hud
        --out-header  "${CMAKE_SOURCE_DIR}/application/main/include/Generated/Hud.blob.g.h"
        --out-datafile "${CMAKE_SOURCE_DIR}/libraries/RenderGraph/assets/ui/hud.viewblob")
    add_custom_target(view_hud_blob_check ALL
        COMMAND ${VIXEN_DOTNET} ${_view_blob_args} --check
        COMMENT "[codegen] golden check: Hud.blob.g.h + hud.viewblob match canonical [View] schema"
        VERBATIM)
    add_custom_target(view_hud_blob_regen
        COMMAND ${VIXEN_DOTNET} ${_view_blob_args}
        COMMENT "[codegen] regenerate Hud.blob.g.h + hud.viewblob"
        VERBATIM)
```

- [ ] **Step 3: Reconfigure + build to verify the guard passes**

Reconfigure + build Windows-side (poll). The `view_hud_blob_check` ALL target must succeed (exit 0 — artifacts match). Confirm `hud.viewblob` is staged next to `hud.rml` in the build's ui assets (mirror how `hud.rml` is staged; if the assets copy is a glob it is automatic, else add `hud.viewblob` to the staged UI asset list in `libraries/RenderGraph/CMakeLists.txt` or the assets CMake — check the existing `hud.rml` staging and match it).

- [ ] **Step 4: Verify the generated header compiles against `ViewBlob.h`**

Confirm a TU including `Generated/Hud.blob.g.h` compiles (it will be included by Task 9's test). Quick check: `test_view_blob_equiv` (Task 9) includes it; alternatively add a one-line static_assert TU. Expected: compiles; `kHudBlob.version` is a nonzero `uint32_t`.

- [ ] **Step 5: Commit**

```bash
cd /mnt/c/cpp/VBVS--VIXEN
git add VIXEN/codegen/CMakeLists.txt \
        VIXEN/application/main/include/Generated/Hud.blob.g.h \
        VIXEN/libraries/RenderGraph/assets/ui/hud.viewblob
# include any assets-staging CMake edit from Step 3 if one was needed
git commit -m "feat(view-contract): --view-blob codegen wiring + committed Hud.blob.g.h/hud.viewblob (Inc-2b)"
```

---

## Task 9: VIXEN — proof gate (`test_view_blob_equiv` + GPU anchor)

**Files:**
- Create: `VIXEN/libraries/RenderGraph/tests/test_view_blob_equiv.cpp`
- Modify: tests CMake (register); confirm the existing `test_hud_render_capture` GPU anchor still builds/runs unchanged.

**Interfaces:**
- Consumes: native `HudView` (`application/main/include/graph/HudView.h`) via its `Register` + `SetHudView`; `BlobView` (Task 7) + `Hud.blob.g.h` `kHudBlob` (Task 8) + `ViewBlobFile::Load` (Task 6); `Vixen::Hash::ComputeHash64` (`libraries/Core/include/VixenHash.h`); the RmlUi fixture (Task 7's `NullRender` + `VixenRmlSystemInterface`).

> **Include-path note:** the test includes `VixenHash.h` and `Generated/Hud.blob.g.h`. Confirm the test target links/inherits Core's include dir (for `VixenHash.h`) and `application/main/include` (for `Generated/Hud.blob.g.h`) — if `RENDERGRAPH_TEST_COMMON_LIBS` doesn't already surface them, add `target_include_directories(test_view_blob_equiv PRIVATE ${CMAKE_SOURCE_DIR}/libraries/Core/include ${CMAKE_SOURCE_DIR}/application/main/include)`. Also: this test needs the native `HudView` type, which lives in `application/main` — if linking that into a RenderGraph-tests target is awkward, place `test_view_blob_equiv` under `application/main/tests/` instead (where `test_hud_view` already lives) and register it there. The implementer picks whichever keeps the dependency direction clean; note the choice in the Progress Log.
- Produces: `test_view_blob_equiv` — the CPU hash-equivalence gate. (No new production code.)

> **`HashModel` helper** — walk the resolved data model in declared order and fold values into a `uint64_t`. Since reading arbitrary resolved values back out of a `DataModelHandle` is awkward, hash the **source storage** each path binds instead (equivalently: after each path is populated with identical fixture data, its bound storage must contain identical values — which is exactly what makes the render identical). Concretely: define one canonical fixture (`tick=7, bodyCount=3, activeLensName="Grievance", activeLensCount=2, factions=[{...}], events=[{...}]`) and a `HashHudData(...)` that hashes those inputs; assert each path *consumed* them into equal storage by reading each path's storage back:
> - native: `HudView` debug accessors + read `factions_`/`events_` (add minimal const debug getters if needed, mirroring the existing `DebugTick()/DebugLensName()/DebugFactionRecentChanged()`).
> - blob (header + datafile): read `ViewStore` slots via `ScalarSlotPtr`/`Array`.
> Hash the extracted values in declared field order with `ComputeHash64` and compare.

- [ ] **Step 1: Write the failing test**

Create `test_view_blob_equiv.cpp`:
```cpp
#include <gtest/gtest.h>
#include <RmlUi/Core.h>
#include <RmlUi/Core/RenderInterface.h>
#include "Ui/BlobView.h"
#include "Ui/ViewBlobFile.h"
#include "Ui/VixenRmlSystemInterface.h"
#include "Generated/Hud.blob.g.h"          // Vixen::Views::kHudBlob
#include "VixenHash.h"
#include <optional>
using namespace Vixen::RenderGraph;

// --- canonical fixture + a storage hash over declared field order ---
namespace {
struct Fixture { int tick=7, bodyCount=3, lensCount=2; const char* lens="Grievance"; };
uint64_t HashStore(ViewStore& s) {
    std::vector<uint8_t> buf;
    auto put = [&](const void* p, size_t n){ const uint8_t* b=(const uint8_t*)p; buf.insert(buf.end(), b, b+n); };
    // tick, bodyCount, activeLensName, activeLensCount, factions[], events[]
    put(s.ScalarSlotPtr(0), sizeof(int));
    put(s.ScalarSlotPtr(1), sizeof(int));
    { auto* str=(Rml::String*)s.ScalarSlotPtr(2); put(str->data(), str->size()); }
    put(s.ScalarSlotPtr(3), sizeof(int));
    for (auto& row : s.Array(4)) { put(&row.Cell(0).f,sizeof(float)); put(&row.Cell(1).b,sizeof(bool)); /* ...members */ }
    return Vixen::Hash::ComputeHash64(buf);
}
void Populate(ViewStore& s, Fixture fx) {
    s.SetScalar("tick", ViewValue::I(fx.tick));
    s.SetScalar("bodyCount", ViewValue::I(fx.bodyCount));
    s.SetScalar("activeLensName", ViewValue::S(fx.lens));
    s.SetScalar("activeLensCount", ViewValue::I(fx.lensCount));
    auto f = s.ResizeArray("factions", 1);
    f.Set(0, "grievance", ViewValue::F(0.7f)); f.Set(0, "focused", ViewValue::B(true));
    f.Set(0, "name", ViewValue::S("Reds")); f.Set(0, "known", ViewValue::B(true));
    f.Set(0, "inLens", ViewValue::B(false)); f.Set(0, "recentChanged", ViewValue::B(false));
    auto e = s.ResizeArray("events", 0);
}
}  // namespace

TEST(ViewBlobEquiv, HeaderAndDatafileBlobsHashEqual) {
    Fixture fx;
    // header-delivered blob
    BlobView headerView(Vixen::Views::kHudBlob, "assets/ui/hud.rml");
    Populate(headerView.Store(), fx);
    uint64_t hHeader = HashStore(headerView.Store());
    // datafile-delivered blob (resolve the staged asset path used by tests)
    auto file = ViewBlobFile::Load("assets/ui/hud.viewblob");
    ASSERT_TRUE(file.has_value());
    BlobView dataView(file->Blob(), "assets/ui/hud.rml");
    Populate(dataView.Store(), fx);
    uint64_t hData = HashStore(dataView.Store());
    EXPECT_EQ(hHeader, hData);
    // versions match across delivery forms
    EXPECT_EQ(Vixen::Views::kHudBlob.version, file->Blob().version);
}

TEST(ViewBlobEquiv, ParserRoundTripEqualsHeaderStructure) {
    auto file = ViewBlobFile::Load("assets/ui/hud.viewblob");
    ASSERT_TRUE(file.has_value());
    const ViewBlob& d = file->Blob();
    const ViewBlob& h = Vixen::Views::kHudBlob;
    ASSERT_EQ(d.fields.size(), h.fields.size());
    for (size_t i=0;i<h.fields.size();++i) {
        EXPECT_EQ(d.fields[i].name, h.fields[i].name);
        EXPECT_EQ(d.fields[i].kind, h.fields[i].kind);
        EXPECT_EQ(d.fields[i].elem.size(), h.fields[i].elem.size());
    }
    EXPECT_EQ(d.version, h.version);
}

TEST(ViewBlobEquiv, VersionMismatchYieldsEmptyModel) {
    // deliberate desync: consumer version != blob version -> BlobView skips register
    BlobView view(Vixen::Views::kHudBlob, "assets/ui/hud.rml");
    view.SetConsumerVersion(Vixen::Views::kHudBlob.version ^ 0x1u);
    // (register against a real model in the RmlUi fixture — mirror test_blob_view; assert !Registered())
    // Full RmlUi-fixture register here as in test_blob_view.
    EXPECT_NE(view.Store().Version(), Vixen::Views::kHudBlob.version);
}
```
> The native-path equality (`H_native == H_header`) is asserted by extending the test to also populate a `HudView` with the same fixture and hashing its storage identically — add once `HudView` exposes the needed const getters (or reuse the existing debug accessors + add `const std::vector<Vixen::Views::HudFaction>& DebugFactions() const`). Keep the native comparison in the same test file so all three hashes are asserted together: `EXPECT_EQ(hNative, hHeader); EXPECT_EQ(hNative, hData);`.

- [ ] **Step 2: Register + build + run to verify it fails**

Register `test_view_blob_equiv` in tests CMake (CPU test; must be able to resolve the staged `assets/ui/hud.viewblob` — set the test's WORKING_DIRECTORY to the staged-assets dir, matching how `test_ui_hud_smoke` resolves its assets, since that test is CWD-sensitive). Build (poll). Expected: FAIL (assets/impl not all present) then pass once wired.

- [ ] **Step 3: Fill in the native-path comparison + any missing debug getters**

Add the minimal const accessors to `HudView` needed to read its storage for hashing (mirror the existing `Debug*` accessors; do NOT change `HudView`'s behavior). Wire `hNative` and assert `hNative == hHeader == hData`.

- [ ] **Step 4: Build + run to verify it passes; run the GPU anchor**

Build (poll), run `test_view_blob_equiv` → PASS (all hash equalities + parser round-trip + mismatch).
Then run the **existing** `test_hud_render_capture` (Inc-2's native GPU anchor) unchanged on the real GPU (~50s) and confirm it still passes against the Inc-2 baseline — this is the render-truth anchor; Inc-2b does not modify it.
Also confirm no-regression: `test_view_hud_golden`, `test_ui_hud_smoke` still green.

- [ ] **Step 5: Commit**

```bash
cd /mnt/c/cpp/VBVS--VIXEN
git add VIXEN/libraries/RenderGraph/tests/test_view_blob_equiv.cpp \
        VIXEN/libraries/RenderGraph/tests/CMakeLists.txt \
        VIXEN/application/main/include/graph/HudView.h
git commit -m "test(view-contract): test_view_blob_equiv — native==header==datafile hash equivalence + mismatch guard (Inc-2b)"
```

---

## Progress Log

- Milestone 1 (Tasks 1-3, Yeroket codegen): DONE · Yeroket branch `feat/view-contract-inc2b-blob` commits 569be3af..1ba4f2ca · Opus validator APPROVED · 24/24 C# tests green · CLI end-to-end version `0x55D27B8C` identical in header+datafile (single-source guarantee) · SDFNodeGenerator.dll excluded · 2026-07-07
  - Deviation (sound): tests written in NUnit (csproj is NUnit-only, no xUnit ref), faithful 1:1 translation of the plan's xUnit literals.
- Milestone 2 (Tasks 4-6, VIXEN engine contract+storage): DONE · commits cc7905a8..dd2b7f1f · Opus validator APPROVED · real-toolchain gtests test_view_blob 2/2 + test_view_store 3/3 + test_view_blob_file 3/3 · setter guards drop bad writes (confirmed no slot mutation) · parser hard-fails each failure class · fixed storage model (ViewRow=vector<ViewCell>) honored · std::deque span-stability sound · 2026-07-07
  - CMake note (sound): `RENDERGRAPH_TEST_COMMON_LIBS` (in test_critical_nodes.cmake) is device-linked-test-only; pure-CPU tests correctly mirror `test_barrier_types` (`GTest::gtest_main RenderGraph` + gtest_discover_tests). UI sources added to RENDERGRAPH_UI_HEADERS/SOURCES.
  - Minor gap (non-blocking): no dedicated missing-model/version negative test; the end-guard `if(!haveModel||!haveVersion) return nullopt` covers it correctly.
- Milestone 3 (Tasks 7-8, VIXEN BlobView host + codegen wiring — the CRUX): DONE · commits 48301c7f (T7) + 6f203a67 (T8) · Opus validator APPROVED · test_blob_view 2/2 real-toolchain · validator TRACED the ViewRow* ptr chain through RmlUi source (ArrayDefinition::Child→StructDefinition::Child→member def gets ViewRow*, cast+cell-by-kind correct); FamilyId synthetic 0x70000000 can't collide (RmlUi Family ids start at 0); definition-ownership split correct (scalars in ownedDefs_, struct/array owned by RegisterDefinition — no dangle/double-free); version-guard-first confirmed · generated Hud.blob.g.h + hud.viewblob both version 0x55D27B8C (single-source end-to-end from M1) · direct-WSL --check exit 0 · Yeroket untouched (1ba4f2ca, DLL clean) · 2026-07-07
  - KI-015 (pre-existing, NOT M3): Windows-side ninja can't exec `\\wsl$\...\dotnet` for the `--check` custom targets (same for pre-existing view_hud_check); guard LOGIC verified via direct WSL run. **Being fixed platform-agnostically via a wsl.exe bridge (user request 2026-07-07) — see the WSL-bridge task below.**
  - Validator note (covered by M4): test_blob_view asserts Registered() true/false but doesn't read a value back through the model; M4's test_view_blob_equiv reads bound storage back + hash-compares, hardening the array-marshaling proof end-to-end.
- KI-015 platform-agnostic fix (user request 2026-07-07, inserted between M3 and M4): DONE · commit 34ea4ff1 · Opus validator APPROVED · codegen drift-guards now EXECUTE cross-OS — on a Windows configure where the Yeroket tool is WSL-only, all 5 guard/regen target pairs route through `wsl.exe -e <wsl-dotnet>` with `wslpath`-translated path args (one shared `_CODEGEN_RUNNER` + `_codegen_to_wsl_path()` helper); native/Linux path byte-identical to pre-fix; wsl.exe-absent falls back to loud-WARNING skip. `view_hud_blob_check` AND pre-existing `view_hud_check` now build GREEN Windows-side (both were failing). Validator PROVED the guard genuinely catches drift: perturbed hud.viewblob +1 byte → guard exit 1 `STALE`, then restored byte-exact. KI-015 flipped to RESOLVED (execution fix). Yeroket untouched. This retro-fixes the whole codegen program's Windows drift-detection, not just the blob guard.
- Milestone 4 (Task 9, the PROOF GATE): DONE · commit afc9cb6c · Opus validator APPROVED · **the increment's claim HOLDS** — test_view_blob_equiv 3/3: H_native == H_header == H_datafile (native `vector<HudFaction>` vs blob `vector<ViewRow>`/ViewCell — genuinely distinct storage, all 6 faction + 2 event members hashed in declared order, so a marshaling divergence WOULD change the hash → equivalence is non-vacuous), version-mismatch → empty model, parser round-trip loads the real on-disk hud.viewblob (v0x55D27B8C). GPU anchor test_hud_render_capture FRESHLY re-rendered on real GPU (VIXEN.exe, ~75s, 3/3 PASS, wholeFrameDiff 9814px ≈245× the kMinHudDiffPixels=40 threshold). No-regression: golden 3/3, hud_smoke 6/6, hud_view 1/1, blob_view 2/2. HudView.h change = read-only const getters only. test placed in application/main/tests/ (mirrors test_hud_view; VixenApp links RenderGraph PUBLIC). 2026-07-07
  - Cosmetic nit (non-blocking, not fixed): a comment in test_view_blob_equiv.cpp says activeLens=2 maps to "Grievance" but kLensNames[2]="Logistics" — the assert correctly uses "Logistics"; only the comment is stale.
  - PRE-EXISTING (orthogonal, for finish surface): 20x `VUID-vkCmdDispatch-None-08114` (voxel ray-march debug-capture descriptor `InstanceIterDebugBuffer` Set0/Binding14 never updated) during the capture run — run exit 0, all PNGs correct, anchor passes. Inc-2b touches zero voxel/dispatch code, so cannot be introduced here. Candidate for a Known-Issues entry by whoever owns the debug-capture path.

## PIPELINE COMPLETE — all 4 milestones + KI-015 fix DONE + validated. Ready for final whole-diff review + merge both mains.
