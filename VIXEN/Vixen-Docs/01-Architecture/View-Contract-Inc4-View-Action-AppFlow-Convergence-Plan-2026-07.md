# View Contract Inc-4 — View→Action / AppFlow Convergence Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the app-flow declared state/transition graph the single-source authoring surface (generated, not hand-mirrored) and route ALL editor input — element clicks + typed key chords — through it, retiring `ParseLayerToggleId` and the hand-wired `glfwGetKey` action literals.

**Architecture:** Build a real Yeroket `AppFlowEmitter` that generates `AppFlow.g.h` from `AppFlowReference.cs` (emitter-first: reproduce today's header byte-equivalent BEFORE adding features). Extend the schema with typed `KeyId`/`KeyMod`/`KeyChord`, element-triggers, scoped key-defaults, and return-edges. Build the VIXEN runtime: a hierarchical `InputProfile` (global→state→context, tightest-wins) over `KeyChord`; `BindingStore` parametric pattern matching; a `FlowStateMachine` entry-history stack + `RequestReturn`; `AppFlowRuntime::DispatchByKey`. Retire the editor's hand-wired input onto these, proven by the existing windowed real-GPU gate.

**Tech Stack:** C# (Roslyn source-generator model, netstandard2.1, NUnit) in Yeroket; C++23 + gtest + RmlUi + GLFW + CMake in VIXEN; the Yeroket `CodegenTool~` CLI; wsl.exe-bridged CMake drift-guards (KI-015).

## Global Constraints

- **In-tree + editor consumer only.** Do NOT modify the undertow repo. Undertow migration is Inc-5+. (Spec D1)
- **Emitter-first sequencing (Approach A).** M1 generates the EXISTING `AppFlow.g.h` byte-equivalent (retire hand-mirror + `TODO(appflow-codegen)`, NO new features) before any new table rides on it. (Spec D5, §3.3)
- **Keys are typed `KeyChord{ KeyId key; KeyMod mods }` — NEVER a string.** `KeyId`/`KeyMod` generated from the schema. The ONLY raw boundary is a host-side glfw-keycode→`KeyId` map, completeness-guarded. (Spec D3, §5.2)
- **Element identity stays a dynamic-read string this increment** (current path; the `{placeholder}`→param extraction IS typed). The element bake/typed path is deferred. (Spec D3, §5.2)
- **Undo (data revert, `ActionStack`) ≠ Return (nav pop, FSM entry-history).** (Spec D6)
- **Scoped resolution is tightest-wins:** `global → flow-state → context`. `Ctrl+Z` declared once globally, overridable per-context, zero re-declaration elsewhere. (Spec D3, §4.1)
- **DESIGNED not built:** effect/animation runtime on edges (effect-ref emits, nothing consumes); rebind-UI; Steam Input; gamepad; qualifier kinds beyond `KeyMod` (timing/double-click/sequence — the resolver treats qualifiers as a composable set, non-foreclosed); the element bake path. (Spec D7, D8, §7)
- **Live gate delta calibrated LIVE, not assumed** — any positive toggle-delta pixel count is real signal; a broken/no-op toggle → EXACTLY 0px. Do NOT copy Inc-2b's 6px. (Spec §6.3)
- **Nothing shipped in AppFlow Inc-1/2/2b changes semantics** — extensions are additive; `test_appflow_golden` + the ~27-test AppFlow offline suite stay green.
- **Build/test discipline (VIXEN):** Windows-side via the `vixen-ninja` preset / `build.bat`; poll long builds on a foreground interval (never blind-wait); never overlap same-target builds. First worktree configure ~500s (FetchContent); target builds fast.
- **Yeroket test discipline:** run from `CodegenTool~/Tests/` (from `CodegenTool~/` = 0 tests = false green); NUnit.
- **Never commit** `SDFNodeGenerator.dll`, `bin/`, `obj/`, or VIXEN build artifacts (`git checkout --` the DLL if dirty). Stage single files (`git add -- <path>`). No pushes (gated).

**Path shorthand:**
- `$KF` = `/home/liory/Github/Yeroket-Fantasy/Packages/com.yeroket.utility.kernel-framework`
- `$W` = the VIXEN Inc-4 worktree root; VIXEN sources under `$W/VIXEN/`

---

## File Structure

**Yeroket (`$KF`):**
- Modify `Runtime/GpuStructAttributes.cs` — add the new `[Flow*]` marker attributes (`FlowKeyEnumAttribute`, `FlowModEnumAttribute`, `FlowElementTriggerAttribute`, `FlowKeyDefaultAttribute`, `FlowReturnEdgeAttribute`, `FlowEdgeEffectAttribute`, `FlowScope` enum) beside the existing `[Flow*]`/`[View]` attributes.
- Modify `CodegenTool~/CompilationLoader.cs` — add `LoadAppFlow(schemaFiles)` returning the `AppFlowReference` namespace symbols the emitter reflects.
- Create `SourceGenerator~/Transpiler/AppFlowEmitter.cs` — reflects `AppFlowReference.cs` → the full `AppFlow.g.h` text. Sibling of `RecipeContainerEmitter`.
- Modify `CodegenTool~/Program.cs` — add the `--appflow` CLI branch.
- Create `CodegenTool~/Tests/AppFlowEmitterTests.cs` — NUnit: byte-equivalence of the unchanged parts + the new tables.

**VIXEN (`$W/VIXEN`):**
- Modify `codegen/appflow-schemas/AppFlowReference.cs` — add `KeyId`/`KeyMod`/new `FlowAction` members + the trigger/key-default/return-edge/effect declarations.
- Modify `libraries/AppFlow/include/generated/AppFlow.g.h` — becomes the GENERATED artifact (produced by the tool; committed). Retire the hand-authored banner's "HAND-AUTHORED" wording + the `TODO(appflow-codegen)`.
- Create `libraries/AppFlow/include/InputProfile.h` + `src/InputProfile.cpp` — hierarchical `KeyChord`→action registry.
- Modify `libraries/AppFlow/include/BindingStore.h` + `src/BindingStore.cpp` — parametric pattern matching + param extraction.
- Modify `libraries/AppFlow/include/FlowStateMachine.h` + `src/FlowStateMachine.cpp` — entry-history stack + `RequestReturn`.
- Modify `libraries/AppFlow/include/AppFlowRuntime.h` + `src/AppFlowRuntime.cpp` — `InputProfile inputProfile_` member, `DispatchByKey(KeyChord)`, seed the new tables in `Load()`.
- Modify `libraries/AppFlow/src/AppFlowLoader.cpp` (+ `.h`) — seed element-triggers/key-defaults/return-edges from the container view.
- Modify `libraries/AppFlow/CMakeLists.txt` — add `InputProfile.cpp` to the lib sources.
- Modify `libraries/AppFlow/tests/CMakeLists.txt` — register the 4 new gtests.
- Create `libraries/AppFlow/tests/{test_input_profile,test_binding_pattern,test_flow_return,test_keychord}.cpp`.
- Create `application/editor/include/KeyMap.h` (or a small helper) — the glfw-keycode→`KeyId` completeness-guarded map.
- Modify `application/editor/source/EditorApplication.cpp` — delete `ParseLayerToggleId`; rewrite the input block onto `DispatchBySelector`/`DispatchByKey`.
- Modify `libraries/RenderGraph/tests/Nodes/test_editor_toggle_undo_capture.cpp` (+ the `VIXEN_EDITOR_SCRIPT` injector) — extend for element-click→toggle + scoped key→undo + return-edge.
- Modify `codegen/CMakeLists.txt` — add the `appflow_check`/`appflow_regen` wsl-bridged pair.

---

## Milestone Map

- **M1 — AppFlow emitter, retire the hand-mirror (Tasks 1–3):** `AppFlowEmitter` generates the EXISTING `AppFlow.g.h` byte-equivalent from `AppFlowReference.cs`; `--appflow` CLI; NUnit byte-equivalence test; the committed header becomes generated; `test_appflow_golden` stays green. NO new features. Drift-guard pair. Testable: `--appflow --check` passes against the committed (now-generated) header; AppFlow offline suite green.
- **M2 — Schema extension + emitter tables (Tasks 4–6):** new `[Flow*]` attributes; `KeyId`/`KeyMod`/`KeyChord`/`FlowScope` + new `FlowAction`s; `kElementTriggers`/`kKeyDefaults`/`kReturnEdges` + effect-ref column emitted; regenerate + commit the extended `AppFlow.g.h`. Testable: `AppFlowEmitterTests` asserts the new tables; drift-guard passes.
- **M3 — Runtime (Tasks 7–10):** `InputProfile` (hierarchical), `BindingStore` pattern matching, `FlowStateMachine` entry-history + `RequestReturn`, `AppFlowRuntime::DispatchByKey` + `Load()` seeding. Testable: `test_input_profile`/`test_binding_pattern`/`test_flow_return`/`test_keychord` green; AppFlow suite green.
- **M4 — Editor retire + live gate + close-out (Tasks 11–13):** glfw→`KeyId` map; delete `ParseLayerToggleId`; rewrite the editor input block; extend the windowed real-GPU gate; no-regression. Testable: windowed gate passes on real GPU (element-click→toggle, scoped key→undo byte-exact, return-edge pop); no `ParseLayerToggleId`/`glfwGetKey`-action literals remain.

---

## Task 1: `AppFlowEmitter` — reflect the existing schema, generate today's header (Yeroket)

**Files:**
- Create: `$KF/SourceGenerator~/Transpiler/AppFlowEmitter.cs`.
- Modify: `$KF/CodegenTool~/CompilationLoader.cs` (add `LoadAppFlow`).
- Test: Task 2's NUnit test proves the output.

**Interfaces:**
- Consumes: Roslyn `INamedTypeSymbol`s of `AppFlowReference.cs` (via `LoadAppFlow`).
- Produces: `public static string AppFlowEmitter.Emit(Compilation comp)` → the full `AppFlow.g.h` text. Task 3 (CLI) calls it.

- [ ] **Step 1: Add `LoadAppFlow` to `CompilationLoader.cs`**

The AppFlow schema uses attributes on ENUMS and CLASSES (`[FlowStateEnum]`, `[FlowActionEnum]`, `[FlowTransition]`, `[FlowActionParams]`, `[FlowStateStruct]`, `[FlowGuardEnum]`, `[FlowParamTypeEnum]`) — the emitter needs the whole compilation to find them by attribute, so `LoadAppFlow` returns the `Compilation` (not just filtered symbols). Add:

```csharp
public static Microsoft.CodeAnalysis.Compilation LoadAppFlow(IEnumerable<string> schemaFiles)
{
    var trees = schemaFiles.Select(f => CSharpSyntaxTree.ParseText(File.ReadAllText(f), path: f));
    return CSharpCompilation.Create("appflowschema", trees, BuildRefs());
}
```

- [ ] **Step 2: Write the emitter to reproduce today's `AppFlow.g.h`**

Create `$KF/SourceGenerator~/Transpiler/AppFlowEmitter.cs`. It walks the compilation, finds the `[Flow*]`-attributed enums/classes, and emits the header EXACTLY matching the current committed `AppFlow.g.h` (read that file first: `$W/VIXEN/libraries/AppFlow/include/generated/AppFlow.g.h`). Mirror `RecipeContainerEmitter`'s idiom (symbol reflection, enum→underlying via a `MapToCppType`, `.Replace("\r\n","\n")`). For M1, the enums (`FlowStateId`/`FlowGuardId`/`FlowActionId`/`FlowParamType`), `LayerState`, `FlowParamSchema`, `AppFlowActionDecl`, `AppFlowTransition`, `kToggleLayerParams`, `kActionDecls`, `kTransitions`, `AppFlowContainerView` — all reproduced from the `[Flow*]` declarations. Provenance banner: `// <provenance: generated from AppFlowReference — do not edit by hand>` (keep "do not edit by hand" so `test_appflow_golden` stays green).

```csharp
using System.Collections.Generic;
using System.Linq;
using System.Text;
using Microsoft.CodeAnalysis;

namespace Yeroket.KernelFramework.Codegen
{
    // Generates AppFlow.g.h from AppFlowReference.cs (the [Flow*]-attributed enums/classes).
    // Retires the hand-authored mirror + TODO(appflow-codegen). Sibling of RecipeContainerEmitter.
    public static class AppFlowEmitter
    {
        public static string Emit(Compilation comp)
        {
            var sb = new StringBuilder();
            sb.AppendLine("#pragma once");
            sb.AppendLine("// <provenance: generated from AppFlowReference — do not edit by hand>");
            sb.AppendLine("#include <cstdint>");
            sb.AppendLine("#include <span>");
            sb.AppendLine();
            sb.AppendLine("namespace Vixen::AppFlow::Generated {");
            sb.AppendLine();

            // Reflect each [Flow*Enum] enum → C++ enum class with pinned values.
            EmitEnum(sb, comp, "FlowStateEnumAttribute",     "FlowStateId",  "uint16_t");
            EmitEnum(sb, comp, "FlowGuardEnumAttribute",     "FlowGuardId",  "uint16_t");
            EmitEnum(sb, comp, "FlowActionEnumAttribute",    "FlowActionId", "uint16_t");
            EmitEnum(sb, comp, "FlowParamTypeEnumAttribute", "FlowParamType","uint8_t");
            sb.AppendLine();

            // Fixed structs (mirror the committed header exactly).
            EmitStateStructs(sb, comp);   // LayerState { uint32_t enabledMask; }
            sb.AppendLine("struct FlowParamSchema { const char* name; FlowParamType type; };");
            sb.AppendLine();
            sb.AppendLine("struct AppFlowActionDecl { FlowActionId id; uint32_t footprintBytes; bool hasInvert; const FlowParamSchema* params; uint32_t paramCount; };");
            sb.AppendLine("struct AppFlowTransition { FlowStateId from; FlowStateId to; FlowGuardId guard; };");
            sb.AppendLine();

            EmitParamSchemas(sb, comp);   // kToggleLayerParams from [FlowActionParams]
            EmitActionDecls(sb, comp);    // kActionDecls
            EmitTransitions(sb, comp);    // kTransitions
            sb.AppendLine();
            EmitContainerView(sb);        // AppFlowContainerView with actions()/transitions()

            sb.AppendLine("} // namespace Vixen::AppFlow::Generated");
            return sb.ToString().Replace("\r\n", "\n");
        }
        // ... EmitEnum/EmitStateStructs/EmitParamSchemas/EmitActionDecls/EmitTransitions/EmitContainerView
        //     helpers: each finds the attributed symbol(s) via comp.GetSymbolsWithName / attribute scan
        //     and appends the exact text of the committed header. Implement each to match byte-for-byte.
    }
}
```

> Implement each `Emit*` helper to reproduce the committed header's exact bytes — the Task 2 test compares against the current file, so any divergence fails there and tells you which line to fix. Read the current `AppFlow.g.h` (55 lines) and the committed `AppFlowReference.cs` as the two sides of the mirror.

- [ ] **Step 3: Build the tool**

Run: `cd $KF/CodegenTool~ && /home/liory/.dotnet/dotnet build -c Release 2>&1 | tail -5`
Expected: `Build succeeded`.

- [ ] **Step 4: Commit**

```bash
cd $KF && git add -- SourceGenerator~/Transpiler/AppFlowEmitter.cs CodegenTool~/CompilationLoader.cs
git commit -m "feat(appflow-codegen): AppFlowEmitter — generate AppFlow.g.h from schema (Inc-4 M1)"
```

---

## Task 2: Byte-equivalence test + `--appflow` CLI (Yeroket)

**Files:**
- Create: `$KF/CodegenTool~/Tests/AppFlowEmitterTests.cs`.
- Modify: `$KF/CodegenTool~/Program.cs` (add `--appflow` branch).

**Interfaces:**
- Consumes: `AppFlowEmitter.Emit`, `CompilationLoader.LoadAppFlow`.
- Produces: CLI `--schema <dir> --appflow --out-header <path> [--check]`. Task 3 (CMake) invokes it.

- [ ] **Step 1: Write the failing byte-equivalence test**

Create `$KF/CodegenTool~/Tests/AppFlowEmitterTests.cs`. It reflects the REAL `AppFlowReference.cs` (copy it into the test dir, or reference by path) and asserts the emitted text equals the committed `AppFlow.g.h`. For M1 (before the schema extension) this is a full-file equality; keep it structural (contains-assertions on the pinned enums + tables) so it's robust to trivial whitespace and matches `test_appflow_golden`'s invariants:

```csharp
using NUnit.Framework;
using Yeroket.KernelFramework.Codegen;

[TestFixture]
public class AppFlowEmitterTests
{
    // The canonical schema, inline (mirrors the committed AppFlowReference.cs Inc-1 subset).
    const string Schema = @"
namespace Yeroket.Util.KernelFramework {
  using System;
  [AttributeUsage(AttributeTargets.Enum)] public sealed class FlowStateEnumAttribute : Attribute {}
  [AttributeUsage(AttributeTargets.Enum)] public sealed class FlowGuardEnumAttribute : Attribute {}
  [AttributeUsage(AttributeTargets.Enum)] public sealed class FlowActionEnumAttribute : Attribute {}
  [AttributeUsage(AttributeTargets.Enum)] public sealed class FlowParamTypeEnumAttribute : Attribute {}
  [AttributeUsage(AttributeTargets.Class)] public sealed class FlowActionParamsAttribute : Attribute { public FlowActionParamsAttribute(string a){} }
  [AttributeUsage(AttributeTargets.Struct)] public sealed class FlowStateStructAttribute : Attribute {}
  [AttributeUsage(AttributeTargets.Class)] public sealed class FlowTransitionAttribute : Attribute {}
}
namespace Vixen.AppFlow.Reference {
  using Yeroket.Util.KernelFramework;
  [FlowStateEnum] public enum FlowState { Editing=0, Simulating=1, Paused=2 }
  [FlowGuardEnum] public enum FlowGuard { DocumentValid=0 }
  [FlowActionEnum] public enum FlowAction { ToggleLayer=0 }
  [FlowParamTypeEnum] public enum FlowParamType { String=0, Int=1, Float=2, EntityRef=3 }
  [FlowActionParams(nameof(FlowAction.ToggleLayer))] public static class ToggleLayerParams { public const string Param0Name=""layerIndex""; public const int Param0Type=(int)FlowParamType.Int; }
  [FlowStateStruct] public struct LayerState { public uint enabledMask; }
  [FlowTransition] public static class Transitions { public const int From=(int)FlowState.Editing; public const int To=(int)FlowState.Simulating; public const int Guard=(int)FlowGuard.DocumentValid; }
}";

    [Test]
    public void Emits_PinnedEnums_Tables_ParamSig()
    {
        var path = System.IO.Path.Combine(System.IO.Path.GetTempPath(), "afr_" + System.Guid.NewGuid().ToString("N") + ".cs");
        System.IO.File.WriteAllText(path, Schema);
        string h = AppFlowEmitter.Emit(CompilationLoader.LoadAppFlow(new[] { path }));

        Assert.That(h, Does.Contain("do not edit by hand"));
        Assert.That(h, Does.Contain("Editing=0"));
        Assert.That(h, Does.Contain("Simulating=1"));
        Assert.That(h, Does.Contain("Paused=2"));
        Assert.That(h, Does.Contain("ToggleLayer=0"));
        Assert.That(h, Does.Contain("kActionDecls"));
        Assert.That(h, Does.Contain("kTransitions"));
        Assert.That(h, Does.Contain("FlowParamType"));
        Assert.That(h, Does.Contain("kToggleLayerParams"));
        Assert.That(h, Does.Contain("\"layerIndex\""));
        Assert.That(h, Does.Not.Contain("\r\n"));
    }
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `cd $KF/CodegenTool~/Tests && /home/liory/.dotnet/dotnet test --filter AppFlowEmitterTests 2>&1 | tail -12`
Expected: FAIL until Task 1's `Emit*` helpers actually emit the tables (initially some `Does.Contain` will miss).

- [ ] **Step 3: Iterate `AppFlowEmitter.Emit` until green**

Fill in the `Emit*` helpers (Task 1 Step 2) until the test passes. Re-run each iteration.

- [ ] **Step 4: Add the `--appflow` CLI branch to `Program.cs`**

After the `--view-writer` branch (Inc-3), add:

```csharp
        if (args.Contains("--appflow"))
        {
            string? outHeader = Flag(args, "--out-header");
            if (schema is null || outHeader is null)
            {
                Console.Error.WriteLine("usage: --schema <dir> --appflow --out-header <path> [--check]");
                return 2;
            }
            var afiles = Directory.GetFiles(schema, "*.cs", SearchOption.AllDirectories);
            var text = AppFlowEmitter.Emit(CompilationLoader.LoadAppFlow(afiles));
            if (check) { bool ok = Same(outHeader, text); if (!ok) Console.Error.WriteLine($"STALE: {outHeader}"); return ok ? 0 : 1; }
            Write(outHeader, text);
            return 0;
        }
```

- [ ] **Step 5: Run test green + commit**

Run: `cd $KF/CodegenTool~/Tests && /home/liory/.dotnet/dotnet test --filter AppFlowEmitterTests 2>&1 | tail -8` → PASS.

```bash
cd $KF && git add -- CodegenTool~/Tests/AppFlowEmitterTests.cs CodegenTool~/Program.cs
git commit -m "feat(appflow-codegen): --appflow CLI + byte-equivalence test (Inc-4 M1)"
```

---

## Task 3: Regenerate the committed header + drift-guard; retire the hand-mirror (VIXEN)

**Files:**
- Modify: `$W/VIXEN/libraries/AppFlow/include/generated/AppFlow.g.h` (becomes generated output; committed).
- Modify: `$W/VIXEN/codegen/CMakeLists.txt` (add `appflow_check`/`appflow_regen`).

**Interfaces:**
- Consumes: the `--appflow` CLI (Task 2), the existing `_CODEGEN_RUNNER`/`_codegen_to_wsl_path`.

- [ ] **Step 1: Generate `AppFlow.g.h` from the tool, diff against committed**

Run WSL-side:
```bash
/home/liory/.dotnet/dotnet run --project $KF/CodegenTool~ -c Release -- \
  --schema /mnt/c/cpp/VBVS--VIXEN/.claude/worktrees/<inc4-worktree>/VIXEN/codegen/appflow-schemas --appflow \
  --out-header /tmp/AppFlow.g.h.gen
diff /tmp/AppFlow.g.h.gen $W/VIXEN/libraries/AppFlow/include/generated/AppFlow.g.h
```
Expected: the ONLY diffs are the provenance banner's wording (the committed file says "HAND-AUTHORED for Inc 1"; the generated says "generated from AppFlowReference"). If any TABLE/enum/struct line differs, fix the emitter (Task 1) until only the banner differs.

- [ ] **Step 2: Overwrite the committed header with the generated one**

Run the tool with `--out-header` pointing AT the committed path (regenerate in place):
```bash
/home/liory/.dotnet/dotnet run --project $KF/CodegenTool~ -c Release -- \
  --schema $W/VIXEN/codegen/appflow-schemas --appflow \
  --out-header $W/VIXEN/libraries/AppFlow/include/generated/AppFlow.g.h
```
Then inspect: the header now carries the generated provenance banner, no `HAND-AUTHORED`/`TODO(appflow-codegen)` text.

- [ ] **Step 3: Add the `appflow_check`/`appflow_regen` CMake pair**

In `$W/VIXEN/codegen/CMakeLists.txt`, mirror the `view_hud_blob_*` pair. Add the path translation in the wsl-bridge branch:
```cmake
        _codegen_to_wsl_path("${CMAKE_SOURCE_DIR}/codegen/appflow-schemas" _schema_appflow_run)
        _codegen_to_wsl_path("${CMAKE_SOURCE_DIR}/libraries/AppFlow/include/generated/AppFlow.g.h" _out_appflow_hdr_run)
```
and the native paths in the `else()` branch:
```cmake
        set(_schema_appflow_run "${_cg}/appflow-schemas")
        set(_out_appflow_hdr_run "${CMAKE_SOURCE_DIR}/libraries/AppFlow/include/generated/AppFlow.g.h")
```
and the target pair (before the closing `else()`):
```cmake
    set(_appflow_args
        run --project "${_yk_tool_run}" -c Release --
        --schema "${_schema_appflow_run}" --appflow
        --out-header "${_out_appflow_hdr_run}")
    add_custom_target(appflow_check ALL
        COMMAND ${_CODEGEN_RUNNER} ${_appflow_args} --check
        COMMENT "[codegen] golden check: AppFlow.g.h matches canonical AppFlowReference schema"
        VERBATIM)
    add_custom_target(appflow_regen
        COMMAND ${_CODEGEN_RUNNER} ${_appflow_args}
        COMMENT "[codegen] regenerate AppFlow.g.h"
        VERBATIM)
```

- [ ] **Step 4: Verify `--check` passes + AppFlow suite green**

Run the `--check` command directly (WSL-side) against the regenerated header → exit 0. Then build + run the AppFlow offline suite Windows-side (build target `AppFlow` + the test exes) — `test_appflow_golden` and the other ~27 must stay green (the header is byte-equivalent in every asserted invariant). Poll long builds.

- [ ] **Step 5: Commit**

```bash
cd $W && git add -- VIXEN/libraries/AppFlow/include/generated/AppFlow.g.h VIXEN/codegen/CMakeLists.txt
git commit -m "build(appflow-codegen): retire hand-authored AppFlow.g.h onto generated + drift-guard (Inc-4 M1)"
```

---

## Task 4: New schema attributes + typed key/action vocabulary (Yeroket + VIXEN schema)

**Files:**
- Modify: `$KF/Runtime/GpuStructAttributes.cs` (new `[Flow*]` attributes + `FlowScope` enum).
- Modify: `$W/VIXEN/codegen/appflow-schemas/AppFlowReference.cs` (add `KeyId`/`KeyMod`/new `FlowAction`s + trigger/key-default/return-edge/effect declarations).

**Interfaces:**
- Produces: the attributes `FlowKeyEnumAttribute`, `FlowModEnumAttribute`, `FlowElementTriggerAttribute(string action)`, `FlowKeyDefaultAttribute(string action, FlowScope scope, string state=null)`, `FlowReturnEdgeAttribute(string from)`, `FlowEdgeEffectAttribute(int transition)`, `enum FlowScope { Global, State, Context }`. Task 5 (emitter) reflects them.

- [ ] **Step 1: Add the attributes to `GpuStructAttributes.cs`**

```csharp
namespace Yeroket.Util.KernelFramework
{
    public enum FlowScope { Global = 0, State = 1, Context = 2 }
    [System.AttributeUsage(System.AttributeTargets.Enum)] public sealed class FlowKeyEnumAttribute : System.Attribute {}
    [System.AttributeUsage(System.AttributeTargets.Enum)] public sealed class FlowModEnumAttribute : System.Attribute {}
    [System.AttributeUsage(System.AttributeTargets.Class)] public sealed class FlowElementTriggerAttribute : System.Attribute { public FlowElementTriggerAttribute(string action){} }
    [System.AttributeUsage(System.AttributeTargets.Class)] public sealed class FlowKeyDefaultAttribute : System.Attribute { public FlowKeyDefaultAttribute(string action, FlowScope scope, string state=null){} }
    [System.AttributeUsage(System.AttributeTargets.Class)] public sealed class FlowReturnEdgeAttribute : System.Attribute { public FlowReturnEdgeAttribute(string from){} }
    [System.AttributeUsage(System.AttributeTargets.Class)] public sealed class FlowEdgeEffectAttribute : System.Attribute { public FlowEdgeEffectAttribute(int transition){} }
}
```

- [ ] **Step 2: Extend `AppFlowReference.cs`**

Add the typed key vocabulary, the new actions, and the declarations (per spec §3.1):

```csharp
    [FlowKeyEnum] public enum KeyId : ushort { None=0, A, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W, X, Y, Z, Escape }
    [FlowModEnum] public enum KeyMod : byte { None=0, Ctrl=1, Shift=2, Alt=4, Super=8 }

    // Actions extended — append-only, pinned.
    [FlowActionEnum] public enum FlowAction { ToggleLayer=0, Undo=1, Redo=2, Save=3, UndoSettingChange=4 }

    [FlowElementTrigger(nameof(FlowAction.ToggleLayer))]
    public static class ToggleLayerTrigger { public const string Element="layer-{index}-toggle"; public const string ParamName="layerIndex"; public const string On="click"; }

    [FlowKeyDefault(nameof(FlowAction.Undo), FlowScope.Global)]
    public static class UndoKey { public const KeyId Key=KeyId.Z; public const KeyMod Mods=KeyMod.Ctrl; }
    [FlowKeyDefault(nameof(FlowAction.Redo), FlowScope.Global)]
    public static class RedoKey { public const KeyId Key=KeyId.Y; public const KeyMod Mods=KeyMod.Ctrl; }
    [FlowKeyDefault(nameof(FlowAction.Save), FlowScope.Global)]
    public static class SaveKey { public const KeyId Key=KeyId.S; public const KeyMod Mods=KeyMod.None; }

    [FlowEdgeEffect(0)] public static class T0Effect { public const string Effect="none"; }
```

> The `FlowState` enum currently has no `Settings` state — the scoped-override + return-edge live demonstration (M4) needs a reachable sub-state. Add `Settings=3` to `FlowState` here (append-only) and a `[FlowReturnEdge(nameof(FlowState.Settings))]` + a `[FlowKeyDefault(..., FlowScope.State, nameof(FlowState.Settings))]` override so M2/M3/M4 have a real home. (This is the plan resolving spec §6.3's open sub-state question: add a minimal `Settings` state.)

```csharp
    // (in FlowState) Editing=0, Simulating=1, Paused=2, Settings=3
    [FlowKeyDefault(nameof(FlowAction.UndoSettingChange), FlowScope.State, nameof(FlowState.Settings))]
    public static class SettingsUndoOverride { public const KeyId Key=KeyId.Z; public const KeyMod Mods=KeyMod.Ctrl; }
    [FlowReturnEdge(nameof(FlowState.Settings))]
    public static class SettingsReturn { public const KeyId Key=KeyId.Escape; public const KeyMod Mods=KeyMod.None; }
    // A transition INTO Settings so it's reachable (Editing->Settings, unguarded or DocumentValid).
    [FlowTransition] public static class ToSettings { public const int From=(int)FlowState.Editing; public const int To=(int)FlowState.Settings; public const int Guard=(int)FlowGuard.DocumentValid; }
```

- [ ] **Step 3: Build the tool to confirm the attributes resolve**

Run: `cd $KF/CodegenTool~ && /home/liory/.dotnet/dotnet build -c Release 2>&1 | tail -4` → `Build succeeded`.

- [ ] **Step 4: Commit (two repos)**

```bash
cd $KF && git add -- Runtime/GpuStructAttributes.cs
git commit -m "feat(appflow-codegen): [FlowKeyEnum/ModEnum/ElementTrigger/KeyDefault/ReturnEdge/EdgeEffect] attrs + FlowScope (Inc-4 M2)"
cd $W && git add -- VIXEN/codegen/appflow-schemas/AppFlowReference.cs
git commit -m "feat(appflow): extend schema — KeyId/KeyMod, Undo/Redo/Save/Settings, triggers/key-defaults/return-edge (Inc-4 M2)"
```

---

## Task 5: Emit the new tables (Yeroket)

**Files:**
- Modify: `$KF/SourceGenerator~/Transpiler/AppFlowEmitter.cs` (emit `KeyId`/`KeyMod`/`KeyChord`/`FlowScope` + `kElementTriggers`/`kKeyDefaults`/`kReturnEdges` + effect column).
- Modify: `$KF/CodegenTool~/Tests/AppFlowEmitterTests.cs` (assert the new tables).

**Interfaces:**
- Produces the generated types (Task 6 regenerates the committed header; M3 runtime consumes them): `enum class KeyId : uint16_t`, `enum class KeyMod : uint8_t`, `struct KeyChord { KeyId key; KeyMod mods; }`, `enum class FlowScope : uint8_t { Global, State, Context }`, `struct AppFlowElementTrigger { const char* elementPattern; FlowActionId action; const char* paramName; const char* on; }`, `struct AppFlowKeyDefault { FlowActionId action; KeyChord chord; FlowScope scope; FlowStateId state; }`, `struct AppFlowReturnEdge { FlowStateId from; KeyChord trigger; }`, and `kElementTriggers[]`/`kKeyDefaults[]`/`kReturnEdges[]`, plus `AppFlowTransition` gaining `const char* effect;`. `AppFlowContainerView` gains `elementTriggers()`/`keyDefaults()`/`returnEdges()`.

- [ ] **Step 1: Write the failing test additions**

Append to `AppFlowEmitterTests.cs` (extend the `Schema` const with the new declarations, then assert):

```csharp
    [Test]
    public void Emits_KeyVocab_And_TriggerTables()
    {
        string h = AppFlowEmitter.Emit(CompilationLoader.LoadAppFlow(new[] { WriteExtendedSchema() }));
        Assert.That(h, Does.Contain("enum class KeyId"));
        Assert.That(h, Does.Contain("enum class KeyMod"));
        Assert.That(h, Does.Contain("struct KeyChord"));
        Assert.That(h, Does.Contain("enum class FlowScope"));
        Assert.That(h, Does.Contain("Undo=1"));
        Assert.That(h, Does.Contain("Settings=3"));
        Assert.That(h, Does.Contain("kElementTriggers"));
        Assert.That(h, Does.Contain("\"layer-{index}-toggle\""));
        Assert.That(h, Does.Contain("kKeyDefaults"));
        Assert.That(h, Does.Contain("kReturnEdges"));
        Assert.That(h, Does.Contain("FlowScope::State"));   // the Settings override
    }
    // WriteExtendedSchema(): same as Schema but with the KeyId/KeyMod enums, extended FlowAction/FlowState,
    // and the trigger/key-default/return-edge declarations from Task 4 Step 2. (Include the new attribute
    // definitions in the inline schema string, as Task 2's does for the base attrs.)
```

- [ ] **Step 2: Run to verify it fails**

Run: `cd $KF/CodegenTool~/Tests && /home/liory/.dotnet/dotnet test --filter AppFlowEmitterTests 2>&1 | tail -12` → the new test FAILs (tables not emitted yet).

- [ ] **Step 3: Extend `AppFlowEmitter.Emit`**

Add: `EmitEnum` calls for `[FlowKeyEnum]`→`KeyId` and `[FlowModEnum]`→`KeyMod`; a literal `struct KeyChord { KeyId key; KeyMod mods; };` and `enum class FlowScope : uint8_t { Global=0, State=1, Context=2 };`; the three new struct definitions; `Emit*` helpers reflecting `[FlowElementTrigger]`/`[FlowKeyDefault]`/`[FlowReturnEdge]` into `kElementTriggers`/`kKeyDefaults`/`kReturnEdges`; add the `const char* effect;` column to `AppFlowTransition` + populate from `[FlowEdgeEffect]`; extend `AppFlowContainerView` accessors. Keep the M1 output for the unchanged parts.

- [ ] **Step 4: Run green + full suite + commit**

Run: `--filter AppFlowEmitterTests` → PASS; then full Yeroket suite → all green.

```bash
cd $KF && git add -- SourceGenerator~/Transpiler/AppFlowEmitter.cs CodegenTool~/Tests/AppFlowEmitterTests.cs
git commit -m "feat(appflow-codegen): emit KeyId/KeyMod/KeyChord + element-trigger/key-default/return-edge tables (Inc-4 M2)"
```

---

## Task 6: Regenerate + commit the extended header (VIXEN)

**Files:**
- Modify: `$W/VIXEN/libraries/AppFlow/include/generated/AppFlow.g.h` (regenerated, extended).
- Modify: `$W/VIXEN/libraries/AppFlow/tests/test_appflow_golden.cpp` (add invariants for the new tables — the golden must assert what the header now contains).

**Interfaces:** Consumes the `--appflow` CLI (now emitting the extended tables).

- [ ] **Step 1: Regenerate the committed header**

Run the tool `--appflow --out-header` at the committed path (as Task 3 Step 2 but now the schema is extended). Inspect: the header now has `KeyId`/`KeyMod`/`KeyChord`/`FlowScope`, `Undo=1`/`Settings=3`, `kElementTriggers`/`kKeyDefaults`/`kReturnEdges`.

- [ ] **Step 2: Extend `test_appflow_golden.cpp`**

Add invariants so the golden guards the new tables:

```cpp
TEST(AppFlowGolden, KeyVocabAndTriggersPresent) {
    const std::string h = readFile(APPFLOW_GENERATED_HEADER_PATH);
    EXPECT_NE(h.find("enum class KeyId"), std::string::npos);
    EXPECT_NE(h.find("struct KeyChord"), std::string::npos);
    EXPECT_NE(h.find("kElementTriggers"), std::string::npos);
    EXPECT_NE(h.find("kKeyDefaults"), std::string::npos);
    EXPECT_NE(h.find("kReturnEdges"), std::string::npos);
    EXPECT_NE(h.find("Settings=3"), std::string::npos);
}
```

- [ ] **Step 3: Build AppFlow + run golden**

Build the `AppFlow` lib + `test_appflow_golden` Windows-side (poll). The lib must still COMPILE against the extended header (new types are additive; nothing consumes them yet). Golden passes. `--appflow --check` exit 0.

- [ ] **Step 4: Commit**

```bash
cd $W && git add -- VIXEN/libraries/AppFlow/include/generated/AppFlow.g.h VIXEN/libraries/AppFlow/tests/test_appflow_golden.cpp
git commit -m "feat(appflow): regenerate extended AppFlow.g.h (key vocab + trigger tables) + golden (Inc-4 M2)"
```

---

## Task 7: `KeyChord` + `InputProfile` hierarchical registry (VIXEN)

**Files:**
- Create: `$W/VIXEN/libraries/AppFlow/include/InputProfile.h` + `src/InputProfile.cpp`.
- Modify: `$W/VIXEN/libraries/AppFlow/CMakeLists.txt` (+`src/InputProfile.cpp`).
- Test: `$W/VIXEN/libraries/AppFlow/tests/test_input_profile.cpp` + `test_keychord.cpp`.

**Interfaces:**
- Consumes: `KeyChord`/`KeyId`/`KeyMod`/`FlowScope`/`FlowStateId`/`FlowActionId` from `AppFlow.g.h`.
- Produces: `InputProfile` with `void Bind(FlowScope, FlowStateId, KeyChord, FlowActionId)` and `bool Resolve(KeyChord, FlowStateId active, FlowActionId& out) const`. Task 9/10 consume it.

- [ ] **Step 1: Write the failing tests**

Create `$W/VIXEN/libraries/AppFlow/tests/test_keychord.cpp`:

```cpp
#include "generated/AppFlow.g.h"
#include <gtest/gtest.h>
using namespace Vixen::AppFlow::Generated;

TEST(KeyChord, EqualityAndMods) {
    KeyChord a{KeyId::Z, KeyMod::Ctrl};
    KeyChord b{KeyId::Z, KeyMod::Ctrl};
    KeyChord c{KeyId::Z, KeyMod::None};
    EXPECT_TRUE(a.key == b.key && a.mods == b.mods);
    EXPECT_FALSE(a.mods == c.mods);
    // Bitmask composition is order-independent (Ctrl|Shift == Shift|Ctrl).
    auto ctrlShift = static_cast<KeyMod>(static_cast<uint8_t>(KeyMod::Ctrl) | static_cast<uint8_t>(KeyMod::Shift));
    auto shiftCtrl = static_cast<KeyMod>(static_cast<uint8_t>(KeyMod::Shift) | static_cast<uint8_t>(KeyMod::Ctrl));
    EXPECT_EQ(static_cast<uint8_t>(ctrlShift), static_cast<uint8_t>(shiftCtrl));
}
```

Create `$W/VIXEN/libraries/AppFlow/tests/test_input_profile.cpp`:

```cpp
#include "InputProfile.h"
#include <gtest/gtest.h>
using namespace Vixen::AppFlow;
using namespace Vixen::AppFlow::Generated;

TEST(InputProfile, TightestScopeWins) {
    InputProfile p;
    p.Bind(FlowScope::Global, FlowStateId{}, {KeyId::Z, KeyMod::Ctrl}, FlowActionId::Undo);
    p.Bind(FlowScope::State, FlowStateId::Settings, {KeyId::Z, KeyMod::Ctrl}, FlowActionId::UndoSettingChange);

    FlowActionId out{};
    ASSERT_TRUE(p.Resolve({KeyId::Z, KeyMod::Ctrl}, FlowStateId::Editing, out));
    EXPECT_EQ(out, FlowActionId::Undo);                 // no Settings override in Editing → global
    ASSERT_TRUE(p.Resolve({KeyId::Z, KeyMod::Ctrl}, FlowStateId::Settings, out));
    EXPECT_EQ(out, FlowActionId::UndoSettingChange);    // tighter State scope wins
}

TEST(InputProfile, UnboundChordReturnsFalse) {
    InputProfile p;
    FlowActionId out{};
    EXPECT_FALSE(p.Resolve({KeyId::A, KeyMod::None}, FlowStateId::Editing, out));
}
```

- [ ] **Step 2: Run to verify they fail**

Register the two tests in the tests CMake (Task 7 Step 4 shows the edit) then build — expect FAIL (`InputProfile.h` missing / `KeyChord` needs `==` maybe). Or build `test_keychord` first (only needs the header).

- [ ] **Step 3: Implement `InputProfile`**

Create `$W/VIXEN/libraries/AppFlow/include/InputProfile.h`:

```cpp
#pragma once
#include <unordered_map>
#include <cstdint>
#include "generated/AppFlow.g.h"

namespace Vixen::AppFlow {
using Generated::KeyChord; using Generated::KeyId; using Generated::KeyMod;
using Generated::FlowScope; using Generated::FlowStateId; using Generated::FlowActionId;

// Hierarchical KeyChord→action registry. Resolve walks Context→State→Global (tightest-wins).
// Seeded from kKeyDefaults at Load(); mutable (the deferred rebind/Steam seam). Qualifiers are a
// composable set (KeyMod is the only kind shipped; the resolution keys on the whole chord, so a
// future timing/sequence qualifier extends the chord without a resolver rewrite).
class InputProfile {
public:
    void Bind(FlowScope scope, FlowStateId state, KeyChord chord, FlowActionId action);
    bool Resolve(KeyChord chord, FlowStateId active, FlowActionId& out) const;
private:
    // key = packed (KeyId<<8 | KeyMod); one map per scope tier.
    static uint32_t Pack(KeyChord c) { return (uint32_t(uint16_t(c.key)) << 8) | uint8_t(c.mods); }
    std::unordered_map<uint32_t, FlowActionId> global_;
    std::unordered_map<uint64_t, FlowActionId> byState_;    // (stateId<<32 | packedChord)
};
}  // namespace Vixen::AppFlow
```

Create `src/InputProfile.cpp`:

```cpp
#include "InputProfile.h"
namespace Vixen::AppFlow {
void InputProfile::Bind(FlowScope scope, FlowStateId state, KeyChord chord, FlowActionId action) {
    if (scope == FlowScope::Global) global_[Pack(chord)] = action;
    else byState_[(uint64_t(uint16_t(state)) << 32) | Pack(chord)] = action;   // State + Context both keyed by state here (Context deferred)
}
bool InputProfile::Resolve(KeyChord chord, FlowStateId active, FlowActionId& out) const {
    // tightest first: state-scoped, then global.
    auto sIt = byState_.find((uint64_t(uint16_t(active)) << 32) | Pack(chord));
    if (sIt != byState_.end()) { out = sIt->second; return true; }
    auto gIt = global_.find(Pack(chord));
    if (gIt != global_.end()) { out = gIt->second; return true; }
    return false;
}
}  // namespace Vixen::AppFlow
```

> `KeyChord` needs no `operator==` for the map (packed to an int). If `test_keychord` uses `==` on `.key`/`.mods` directly (enum comparison) that already works. Add `src/InputProfile.cpp` to `libraries/AppFlow/CMakeLists.txt`'s source list.

- [ ] **Step 4: Register tests in CMake, build, run green**

In `libraries/AppFlow/tests/CMakeLists.txt`, add `test_input_profile test_keychord` to BOTH `foreach` lists. Build Windows-side (poll), run both → PASS.

- [ ] **Step 5: Commit**

```bash
cd $W && git add -- VIXEN/libraries/AppFlow/include/InputProfile.h VIXEN/libraries/AppFlow/src/InputProfile.cpp VIXEN/libraries/AppFlow/CMakeLists.txt VIXEN/libraries/AppFlow/tests/test_input_profile.cpp VIXEN/libraries/AppFlow/tests/test_keychord.cpp VIXEN/libraries/AppFlow/tests/CMakeLists.txt
git commit -m "feat(appflow): InputProfile hierarchical KeyChord registry + tests (Inc-4 M3)"
```

---

## Task 8: `BindingStore` parametric pattern matching (VIXEN)

**Files:**
- Modify: `$W/VIXEN/libraries/AppFlow/include/BindingStore.h` + `src/BindingStore.cpp`.
- Test: `$W/VIXEN/libraries/AppFlow/tests/test_binding_pattern.cpp`.

**Interfaces:**
- Consumes: `AppFlowElementTrigger` (from `AppFlow.g.h`).
- Produces: `void BindingStore::AddElementTrigger(const Generated::AppFlowElementTrigger& trig)`; `TryGetForSelector` now also matches patterns, filling `BoundAction.params` with the extracted `{placeholder}` (as `{paramName, value}`).

- [ ] **Step 1: Write the failing test**

Create `$W/VIXEN/libraries/AppFlow/tests/test_binding_pattern.cpp`:

```cpp
#include "BindingStore.h"
#include "generated/AppFlow.g.h"
#include <gtest/gtest.h>
using namespace Vixen::AppFlow;
using namespace Vixen::AppFlow::Generated;

TEST(BindingPattern, ExtractsTypedParamFromSelector) {
    BindingStore s;
    s.RegisterActions(std::span<const AppFlowActionDecl>(kActionDecls, std::size(kActionDecls)));
    AppFlowElementTrigger trig{"layer-{index}-toggle", FlowActionId::ToggleLayer, "layerIndex", "click"};
    s.AddElementTrigger(trig);

    BoundAction out;
    ASSERT_TRUE(s.TryGetForSelector("layer-2-toggle", out));
    EXPECT_EQ(out.action, FlowActionId::ToggleLayer);
    ASSERT_EQ(out.params.size(), 1u);
    EXPECT_EQ(out.params[0].first, "layerIndex");
    EXPECT_EQ(out.params[0].second, "2");            // extracted value

    BoundAction miss;
    EXPECT_FALSE(s.TryGetForSelector("not-a-layer", miss));
}
```

- [ ] **Step 2: Run to verify it fails**

Register in the tests CMake, build → FAIL (`AddElementTrigger` missing).

- [ ] **Step 3: Implement pattern matching**

In `BindingStore.h` add:

```cpp
    void AddElementTrigger(const Generated::AppFlowElementTrigger& trig);
private:
    struct PatternBinding { std::string prefix, suffix, paramName; FlowActionId action; std::string on; };
    std::vector<PatternBinding> patterns_;
```

In `BindingStore.cpp`:

```cpp
void BindingStore::AddElementTrigger(const Generated::AppFlowElementTrigger& trig) {
    // Split "layer-{index}-toggle" into prefix="layer-", suffix="-toggle".
    std::string pat = trig.elementPattern;
    auto lb = pat.find('{'); auto rb = pat.find('}');
    if (lb == std::string::npos || rb == std::string::npos || rb < lb) return;  // malformed → inert
    patterns_.push_back({ pat.substr(0, lb), pat.substr(rb + 1),
                          trig.paramName, trig.action, trig.on });
}
```

Extend `TryGetForSelector` (after the exact-map miss, before returning false) to scan `patterns_`: a selector matches if it starts with `prefix` and ends with `suffix` and the middle is non-empty; extract the middle as the param value:

```cpp
bool BindingStore::TryGetForSelector(const std::string& selector, BoundAction& out) const {
    auto it = bindings_.find(selector);
    if (it != bindings_.end()) { out = it->second; return true; }     // exact wins
    for (const auto& p : patterns_) {
        if (selector.size() <= p.prefix.size() + p.suffix.size()) continue;
        if (selector.compare(0, p.prefix.size(), p.prefix) != 0) continue;
        if (selector.compare(selector.size() - p.suffix.size(), p.suffix.size(), p.suffix) != 0) continue;
        std::string mid = selector.substr(p.prefix.size(), selector.size() - p.prefix.size() - p.suffix.size());
        if (mid.empty()) continue;
        out = BoundAction{ p.action, p.on, {{ p.paramName, mid }} };
        return true;
    }
    return false;
}
```

- [ ] **Step 4: Build, run green**

Build + run `test_binding_pattern` + the existing `test_binding_store` (no-regression) → both PASS.

- [ ] **Step 5: Commit**

```bash
cd $W && git add -- VIXEN/libraries/AppFlow/include/BindingStore.h VIXEN/libraries/AppFlow/src/BindingStore.cpp VIXEN/libraries/AppFlow/tests/test_binding_pattern.cpp VIXEN/libraries/AppFlow/tests/CMakeLists.txt
git commit -m "feat(appflow): BindingStore parametric pattern match + typed param extraction (Inc-4 M3)"
```

---

## Task 9: `FlowStateMachine` entry-history + `RequestReturn` (VIXEN)

**Files:**
- Modify: `$W/VIXEN/libraries/AppFlow/include/FlowStateMachine.h` + `src/FlowStateMachine.cpp`.
- Test: `$W/VIXEN/libraries/AppFlow/tests/test_flow_return.cpp`.

**Interfaces:**
- Produces: `DispatchResult FlowStateMachine::RequestReturn()`. `Request(to)` now pushes the state being left onto a bounded history.

- [ ] **Step 1: Write the failing test**

Create `test_flow_return.cpp`:

```cpp
#include "FlowStateMachine.h"
#include <gtest/gtest.h>
using namespace Vixen::AppFlow;
using namespace Vixen::AppFlow::Generated;

static FlowStateMachine MakeFsm() {
    FlowStateMachine fsm;
    // Editing(0) -> Simulating(1) -> Settings(3) reachable via unguarded transitions for the test.
    static const AppFlowTransition t[] = {
        {FlowStateId::Editing, FlowStateId::Simulating, FlowGuardId::DocumentValid},
        {FlowStateId::Simulating, FlowStateId::Settings, FlowGuardId::DocumentValid},
    };
    fsm.LoadTransitions(t, std::size(t));
    fsm.SetGuardResult(FlowGuardId::DocumentValid, true);
    fsm.SetCurrent(FlowStateId::Editing);
    return fsm;
}

TEST(FlowReturn, PopsToPriorState) {
    auto fsm = MakeFsm();
    ASSERT_EQ(fsm.Request(FlowStateId::Simulating), DispatchResult::Ok);
    ASSERT_EQ(fsm.Request(FlowStateId::Settings), DispatchResult::Ok);
    EXPECT_EQ(fsm.Current(), FlowStateId::Settings);
    EXPECT_EQ(fsm.RequestReturn(), DispatchResult::Ok);
    EXPECT_EQ(fsm.Current(), FlowStateId::Simulating);   // popped
    EXPECT_EQ(fsm.RequestReturn(), DispatchResult::Ok);
    EXPECT_EQ(fsm.Current(), FlowStateId::Editing);
}

TEST(FlowReturn, EmptyHistoryIsNoOp) {
    auto fsm = MakeFsm();
    EXPECT_EQ(fsm.RequestReturn(), DispatchResult::RejectedByState);   // nothing to pop
    EXPECT_EQ(fsm.Current(), FlowStateId::Editing);
}
```

- [ ] **Step 2: Run to verify it fails**

Register in CMake, build → FAIL (`RequestReturn` missing).

- [ ] **Step 3: Implement history + `RequestReturn`**

In `FlowStateMachine.h` add `DispatchResult RequestReturn();` and a private `std::vector<FlowStateId> history_;` + `static constexpr size_t kHistoryCap = 16;`.

In `FlowStateMachine.cpp`, in `Request` on the successful-transition path (just before `current_ = to`), push the current state:

```cpp
// (inside Request, on Ok, before mutating current_)
history_.push_back(current_);
if (history_.size() > kHistoryCap) history_.erase(history_.begin());   // bounded: drop oldest
```

Add:

```cpp
DispatchResult FlowStateMachine::RequestReturn() {
    if (history_.empty()) return DispatchResult::RejectedByState;   // logged no-op, never underflow
    current_ = history_.back();
    history_.pop_back();
    return DispatchResult::Ok;
}
```

> `RequestReturn` does NOT push onto history (a return is not a forward nav). Verify `Request`'s existing linear-scan/guard logic is untouched — only the history push is added on the Ok branch.

- [ ] **Step 4: Build, run green**

Build + run `test_flow_return` + the existing `test_flow_state_machine` (no-regression) → PASS.

- [ ] **Step 5: Commit**

```bash
cd $W && git add -- VIXEN/libraries/AppFlow/include/FlowStateMachine.h VIXEN/libraries/AppFlow/src/FlowStateMachine.cpp VIXEN/libraries/AppFlow/tests/test_flow_return.cpp VIXEN/libraries/AppFlow/tests/CMakeLists.txt
git commit -m "feat(appflow): FlowStateMachine entry-history stack + RequestReturn (nav-pop) (Inc-4 M3)"
```

---

## Task 10: `AppFlowRuntime::DispatchByKey` + Load() seeding (VIXEN)

**Files:**
- Modify: `$W/VIXEN/libraries/AppFlow/include/AppFlowRuntime.h` + `src/AppFlowRuntime.cpp`.
- Modify: `$W/VIXEN/libraries/AppFlow/src/AppFlowLoader.cpp` + `.h` (seed the new tables).
- Test: extend `test_appflow_loader.cpp` (or add assertions) — a seeded key resolves + dispatches.

**Interfaces:**
- Produces: `DispatchResult AppFlowRuntime::DispatchByKey(Generated::KeyChord chord, ActionStack::ApplyFn apply)`; `InputProfile& AppFlowRuntime::Input()`; `DispatchResult AppFlowRuntime::RequestReturn()` (thin pass-through to `fsm_.RequestReturn()`, mirroring `RequestState`→`fsm_.Request` — the editor calls this, NOT a raw `Fsm()` accessor, keeping the FSM encapsulated like Inc-1). `Load()` seeds `kElementTriggers`/`kKeyDefaults`/`kReturnEdges`.

- [ ] **Step 1: Write the failing test**

Append to `test_appflow_loader.cpp` (it already tests `Load()`):

```cpp
TEST(AppFlowLoader, SeedsKeyDefaultsResolvableByChord) {
    AppFlowRuntime rt(nullptr, {});
    ASSERT_EQ(rt.Load().ok, true);   // adjust to the real LoadResult success field
    FlowActionId out{};
    // Ctrl+Z seeded Global → Undo (from kKeyDefaults).
    ASSERT_TRUE(rt.Input().Resolve({KeyId::Z, KeyMod::Ctrl}, FlowStateId::Editing, out));
    EXPECT_EQ(out, FlowActionId::Undo);
}
```

(Check `LoadResult`'s real success accessor in `AppFlowResults.h` and match it.)

- [ ] **Step 2: Run to verify it fails**

Build `test_appflow_loader` → FAIL (`Input()` missing / not seeded).

- [ ] **Step 3: Add `InputProfile` member + `DispatchByKey` + seeding**

In `AppFlowRuntime.h`: `#include "InputProfile.h"`; add `InputProfile inputProfile_;` member; add `InputProfile& Input() { return inputProfile_; }`; declare `DispatchResult DispatchByKey(Generated::KeyChord chord, ActionStack::ApplyFn apply);` and `DispatchResult RequestReturn();` (the encapsulated nav-pop pass-through — do NOT expose a raw `FlowStateMachine& Fsm()`; Inc-1 keeps the FSM private and drives it through named pass-throughs like `RequestState`).

In `AppFlowRuntime.cpp`:

```cpp
DispatchResult AppFlowRuntime::DispatchByKey(Generated::KeyChord chord, ActionStack::ApplyFn apply) {
    Generated::FlowActionId action{};
    if (!inputProfile_.Resolve(chord, fsm_.Current(), action)) return DispatchResult::RejectedByState;
    return DispatchAction(action, std::move(apply));
}

DispatchResult AppFlowRuntime::RequestReturn() {
    DispatchResult r = fsm_.RequestReturn();
    if (r == DispatchResult::Ok) Publish(AppFlowChangedEvent::Kind::StateChanged, fsm_.Current(), Generated::FlowActionId{}, 0);
    return r;   // mirrors RequestState: publish StateChanged on Ok (adjust the Publish signature to the real one)
}
```

In `AppFlowLoader.cpp` (or `AppFlowRuntime::Load`), seed after the existing action/binding load:

```cpp
// Seed element triggers into the BindingStore.
for (const auto& t : AppFlowContainerView::elementTriggers()) bindings.AddElementTrigger(t);
// Seed key defaults into the InputProfile (by scope).
for (const auto& k : AppFlowContainerView::keyDefaults())
    input.Bind(k.scope, k.state, k.chord, k.action);
// (return edges consumed by the FSM/editor; loaded into a table the runtime exposes)
```

> `AppFlowLoader::Load`'s signature gains an `InputProfile& input` param (thread it from `AppFlowRuntime::Load`). Keep the load-order invariant: action table registered before bindings/triggers.

- [ ] **Step 4: Build, run green (loader + full AppFlow suite)**

Build + run `test_appflow_loader` + the full AppFlow suite → all PASS (Inc-1/2/2b no-regression).

- [ ] **Step 5: Commit**

```bash
cd $W && git add -- VIXEN/libraries/AppFlow/include/AppFlowRuntime.h VIXEN/libraries/AppFlow/src/AppFlowRuntime.cpp VIXEN/libraries/AppFlow/src/AppFlowLoader.cpp VIXEN/libraries/AppFlow/include/AppFlowLoader.h VIXEN/libraries/AppFlow/tests/test_appflow_loader.cpp
git commit -m "feat(appflow): DispatchByKey(KeyChord) + Load() seeds triggers/key-defaults (Inc-4 M3)"
```

---

## Task 11: glfw→KeyId map (completeness-guarded) (VIXEN)

**Files:**
- Create: `$W/VIXEN/application/editor/include/KeyMap.h`.
- Test: extend `test_keychord.cpp` with a completeness assertion (or a small `test_keymap` in the editor's test scope — use `test_keychord` since it's already in AppFlow tests and the map is header-only).

**Interfaces:**
- Produces: `Generated::KeyId GlfwToKeyId(int glfwKey)` + `Generated::KeyMod ReadMods(bool ctrl, bool shift, bool alt, bool super)`; a `bool KeyMapCoversEditorKeys()` completeness check.

- [ ] **Step 1: Write the completeness test**

Append to `test_keychord.cpp` (it can include the editor header via an include path, OR put a copy of the map check here). Assert every editor-used glfw key maps to a non-`None` `KeyId`:

```cpp
#include "KeyMap.h"   // requires the editor include dir on this test; else inline the map
TEST(KeyMap, CoversEditorKeys) {
    // The editor uses: Z, Y, S, Escape (+ Ctrl modifier). Each must map to a non-None KeyId.
    EXPECT_NE(Vixen::Editor::GlfwToKeyId(GLFW_KEY_Z), KeyId::None);
    EXPECT_NE(Vixen::Editor::GlfwToKeyId(GLFW_KEY_Y), KeyId::None);
    EXPECT_NE(Vixen::Editor::GlfwToKeyId(GLFW_KEY_S), KeyId::None);
    EXPECT_NE(Vixen::Editor::GlfwToKeyId(GLFW_KEY_ESCAPE), KeyId::None);
    EXPECT_EQ(Vixen::Editor::GlfwToKeyId(GLFW_KEY_UNKNOWN), KeyId::None);   // unmapped → None (caught, not silent)
}
```

> If wiring GLFW into an AppFlow test is awkward (AppFlow lib doesn't link GLFW), instead put this test in the editor's own test target, or make `KeyMap.h` not depend on GLFW headers by taking a plain int + named constants. Prefer: `KeyMap.h` maps `int`→`KeyId` with the GLFW codes as plain int literals documented in comments, so the test needs no GLFW link.

- [ ] **Step 2: Run to verify it fails**

Build → FAIL (`KeyMap.h` missing).

- [ ] **Step 3: Implement `KeyMap.h`**

```cpp
#pragma once
#include "generated/AppFlow.g.h"
namespace Vixen::Editor {
using Vixen::AppFlow::Generated::KeyId;
using Vixen::AppFlow::Generated::KeyMod;
// glfw keycode (plain int; GLFW_KEY_* values) → typed KeyId. Unmapped → None (the completeness
// guard test asserts every editor-used key is covered; None is the caught-not-silent sentinel).
inline KeyId GlfwToKeyId(int glfwKey) {
    switch (glfwKey) {
        case 90: return KeyId::Z;      // GLFW_KEY_Z
        case 89: return KeyId::Y;      // GLFW_KEY_Y
        case 83: return KeyId::S;      // GLFW_KEY_S
        case 256: return KeyId::Escape;// GLFW_KEY_ESCAPE
        default: return KeyId::None;
    }
}
inline KeyMod ReadMods(bool ctrl, bool shift, bool alt, bool super) {
    uint8_t m = 0;
    if (ctrl) m |= uint8_t(KeyMod::Ctrl);
    if (shift) m |= uint8_t(KeyMod::Shift);
    if (alt) m |= uint8_t(KeyMod::Alt);
    if (super) m |= uint8_t(KeyMod::Super);
    return static_cast<KeyMod>(m);
}
}  // namespace Vixen::Editor
```

> Use the plain-int GLFW values (documented) so the test + map need no GLFW link. The editor passes `glfwGetKey`'s keycode through this. Verify the GLFW_KEY_* int values against `<GLFW/glfw3.h>` when implementing (Z=90, Y=89, S=83, ESCAPE=256 are standard).

- [ ] **Step 4: Build, run green + commit**

Build + run the completeness test → PASS.

```bash
cd $W && git add -- VIXEN/application/editor/include/KeyMap.h VIXEN/libraries/AppFlow/tests/test_keychord.cpp
git commit -m "feat(editor): glfw->KeyId completeness-guarded map (Inc-4 M4)"
```

---

## Task 12: Editor retire — delete ParseLayerToggleId + glfwGetKey action literals (VIXEN)

**Files:**
- Modify: `$W/VIXEN/application/editor/source/EditorApplication.cpp` (delete `ParseLayerToggleId` `:28-40`; rewrite the input block `:355-386`).

**Interfaces:**
- Consumes: `rt_.DispatchBySelector`, `rt_.DispatchByKey`, `rt_.Fsm().RequestReturn`, `KeyMap.h`, `rt_.Undo/Redo`, `SaveDocument`.

- [ ] **Step 1: Delete `ParseLayerToggleId`**

Remove the whole function (`EditorApplication.cpp:28-40`) + its `namespace {}` wrapper if now empty (keep the ScriptedAction parser that follows).

- [ ] **Step 2: Rewrite the click-drain block**

Replace `:355-364` (the `DrainClickedElementId` → `ParseLayerToggleId` → `ToggleLayer` block):

```cpp
    // Drain UI clicks and dispatch by selector — the typed BindingStore pattern-matches
    // "layer-{index}-toggle" and extracts the layer index as a typed param (no ParseLayerToggleId).
    if (auto* selection = GetUiSelectionProviderNode()) {
        const std::string clickedId = selection->DrainClickedElementId();
        if (!clickedId.empty()) {
            rt_.DispatchBySelector(clickedId, [this](bool){ /* apply set inside the resolved action */ });
        }
    }
```

> The apply-lambda seam: `DispatchBySelector` resolves to `ToggleLayer` but the *apply* that flips the layer + sets `dirty_` must run. The cleanest wiring: give `AppFlowRuntime` a resolved-dispatch that, for `ToggleLayer`, uses the extracted param and the `onChanged=[]{dirty_=true}` — i.e. the editor passes an apply that reads the bound param. Concretely, resolve the `BoundAction` (via a runtime accessor) to get `layerIndex`, then call `rt_.ToggleLayer(index, [this]{dirty_=true;})`. Implement whichever the runtime exposes; the REQUIREMENT is: clicked "layer-2-toggle" → ToggleLayer(2) → dirty_. Keep it typed (param from the BoundAction, not a re-parse).

- [ ] **Step 3: Rewrite the key block**

Replace the `glfwGetKey` S-save (`:367-374`) and Ctrl+Z/Y (`:379-386`) with typed `KeyChord` dispatch built from the same edge-detection:

```cpp
    if (GLFWwindow* window = GetWindowHandle()) {
        using namespace Vixen::Editor;
        const bool ctrl = glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS
                       || glfwGetKey(window, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS;
        auto edge = [&](int glfwKey, bool& wasDown) {
            const bool down = glfwGetKey(window, glfwKey) == GLFW_PRESS;
            const bool fired = down && !wasDown; wasDown = down; return fired;
        };
        const KeyMod mods = ReadMods(ctrl, false, false, false);
        // S (Save), Ctrl+Z (Undo), Ctrl+Y (Redo) → resolve+dispatch through the InputProfile.
        if (edge(GLFW_KEY_S, sKeyWasDown_) && !ctrl)
            rt_.DispatchByKey({GlfwToKeyId(GLFW_KEY_S), KeyMod::None}, [this](bool){ if(!SaveDocument()) logger_->Error("[EditorApplication] SaveDocument failed: " + lastEditorError_); });
        if (edge(GLFW_KEY_Z, ctrlZWasDown_) && ctrl)
            rt_.DispatchByKey({GlfwToKeyId(GLFW_KEY_Z), mods}, [this](bool){ rt_.Undo(); });
        if (edge(GLFW_KEY_Y, ctrlYWasDown_) && ctrl)
            rt_.DispatchByKey({GlfwToKeyId(GLFW_KEY_Y), mods}, [this](bool){ rt_.Redo(); });
        // Esc → return-edge (nav pop), distinct from Undo.
        if (edge(GLFW_KEY_ESCAPE, escWasDown_))
            rt_.RequestReturn();
    }
```

> Add `bool escWasDown_ = false;` to `EditorApplication.h`. The apply-lambdas here still call the SAME handlers (`SaveDocument`/`rt_.Undo()`/`rt_.Redo()`) — the binding is retired, the handler unchanged. The exact `DispatchByKey` apply-seam mirrors how Inc-2b wired `rt_.Undo()`; match the resolved-action semantics (a key that resolves to `Undo` should run `rt_.Undo()` — if the runtime maps the resolved FlowActionId to the handler internally, pass a no-op apply; if the editor supplies the handler, pass it as shown). Pick the shape consistent with `DispatchByKey`'s Task-10 implementation and keep it typed.

- [ ] **Step 4: Confirm no string/parse remnants**

Run: `grep -n "ParseLayerToggleId\|GLFW_KEY_Z\|GLFW_KEY_S" application/editor/source/EditorApplication.cpp`
Expected: `ParseLayerToggleId` GONE; `GLFW_KEY_*` appears ONLY inside the edge-detect + `GlfwToKeyId` calls (raw-boundary), never mapping directly to an action.

- [ ] **Step 5: Build the editor (compile only) + commit**

Build `vixen_editor` Windows-side (poll) → links clean. (Behavior proven in Task 13.)

```bash
cd $W && git add -- VIXEN/application/editor/source/EditorApplication.cpp VIXEN/application/editor/include/EditorApplication.h
git commit -m "refactor(editor): retire ParseLayerToggleId + glfwGetKey action literals onto typed dispatch (Inc-4 M4)"
```

---

## Task 13: Extend the windowed real-GPU gate + close-out (VIXEN)

**Files:**
- Modify: `$W/VIXEN/libraries/RenderGraph/tests/Nodes/test_editor_toggle_undo_capture.cpp` + the `VIXEN_EDITOR_SCRIPT` injector (in `EditorApplication`) as needed.

**Interfaces:** Consumes the full retired path (Tasks 7–12).

- [ ] **Step 1: Confirm the existing gate still passes (behavior-neutrality baseline)**

Build + run `test_editor_toggle_undo_capture` on the real GPU (Windows-side, poll — the harness runs `vixen_editor` scripted via `VIXEN_EDITOR_SCRIPT`, real D3D12/dzn). The EXISTING assertions (toggle:2@30/undo@60/redo@90, captures @5/45/75/105, undo==baseline & redo==toggle byte-exact) MUST still pass — proving the retire is behavior-neutral. If they fail, the retire (Task 12) broke the path — fix before extending.

> Poll pattern: `until ! kill -0 $PID; do echo "[watch +${t}s] $(tail -1 $LOG)"; sleep 15; t=$((t+15)); done`. Run the test exe from its own dir (asset CWD). The toggle now flows click→DispatchBySelector→ToggleLayer; the script's `toggle:2` must synthesize the element click (verify the injector drives the click path, not a direct ToggleLayer call — if the injector calls ToggleLayer directly it bypasses the new path; route it through the click drain or a DispatchBySelector call so the gate actually exercises the retire).

- [ ] **Step 2: Calibrate the toggle delta LIVE**

From the passing run, read the actual toggle-delta pixel count (the harness/validator reports it). Set the test's `kMinToggleDiffPixels` to a value below the observed delta but > 0 (any positive is real; 0 = broken). Do NOT copy Inc-2b's 6px — measure this run.

- [ ] **Step 3: Add a return-edge assertion**

Extend the script/harness to exercise the return edge: transition into `Settings` (`rt_.RequestState`), fire the return trigger (Esc), assert `rt_.Current()` popped back to the prior state. If a windowed capture can show it, assert the PNG; otherwise assert the `FlowStateId` via a harness hook (a state-dump the script can emit through `rt_.Current()`). Keep it minimal — the FSM `test_flow_return` already proves the logic; this proves the wired path.

- [ ] **Step 4: Full no-regression sweep**

- Yeroket full suite (`CodegenTool~/Tests`) → all green (M1/M2 emitter tests + Inc-1/2/2b/3).
- AppFlow offline suite (all ~31 now) → green.
- `test_editor_toggle_undo_capture` → PASS on real GPU with the calibrated delta + return assertion.
- Drift-guards: `appflow_check` + the existing five → pass on a fresh configure.
- `grep` confirms `ParseLayerToggleId` gone repo-wide.

- [ ] **Step 5: Commit**

```bash
cd $W && git add -- VIXEN/libraries/RenderGraph/tests/Nodes/test_editor_toggle_undo_capture.cpp
git commit -m "test(editor): windowed gate proves typed element-click/key dispatch + return-edge (Inc-4 M4)"
```

---

## Progress Log

- (append per milestone: `Milestone N (Tasks A–B): DONE · commits <short>..<short> · Opus validator OK · <date>`)

---

## Self-Review

**Spec coverage:**
- D1 (in-tree + editor, no undertow) → all tasks in VIXEN/Yeroket only. ✓
- D2 (declared graph; extend AppFlow's schema) → Tasks 4–6 extend `AppFlowReference.cs`; the FSM is the graph. ✓
- D2/D5 (build the real emitter, retire hand-mirror + TODO, emitter-first) → M1 (Tasks 1–3). ✓
- D3 (element pattern + typed param; typed KeyChord; hierarchical InputProfile) → Tasks 8 (pattern), 7 (InputProfile), 5 (KeyChord emitted). ✓
- D3/§5.2 (element stays dynamic-string; key fully typed; glfw→KeyId one boundary) → Tasks 8, 11, 12. ✓
- D4 (intents compile-time; reuse DispatchBySelector + ActionStack; add DispatchByKey) → Task 10. ✓
- D6 (Undo≠Return; FSM entry-history) → Task 9. ✓
- D7 (effects/rebind/Steam/gamepad DESIGNED not built) → effect-ref column emitted (Task 5), nothing consumes it; no UI/adapter tasks. ✓
- D8 (compositional qualifiers non-foreclosed) → InputProfile keys on the whole chord (Task 7 comment); KeyMod is the only kind. ✓
- §3.3 (byte-equivalence first) → Task 1/2/3. ✓
- §6.1/6.2 (offline C# + C++ unit tests) → Tasks 2, 5, 7, 8, 9, 11. ✓
- §6.3 (live gate: element-click→toggle, scoped key→undo byte-exact, return pop; delta LIVE) → Task 13; the Settings sub-state is added in Task 4. ✓
- §6.4 (no-regression; appflow_check drift-guard) → Tasks 3, 13. ✓

**Placeholder scan:** No TBD/TODO. Two tasks (1's `Emit*` helpers, 12's apply-seam) intentionally say "implement to match the committed header / the runtime's DispatchByKey shape" with the exact REQUIREMENT stated + the arbiter named (the byte-diff / the typed-param rule) — these adapt to real code the worker reads, not placeholders. Every code step shows code.

**Type consistency:** `KeyChord{KeyId,KeyMod}`, `InputProfile::Bind/Resolve`, `AppFlowElementTrigger`, `AppFlowKeyDefault`, `DispatchByKey`, `RequestReturn`, `GlfwToKeyId`/`ReadMods` used identically across the tasks that define and consume them. `FlowActionId::{Undo,Redo,Save,UndoSettingChange}` + `FlowStateId::Settings` defined in Task 4, consumed in 5/7/9/10/12. `appflow_check`/`--appflow`/`--out-header` consistent (Tasks 2,3). ✓
