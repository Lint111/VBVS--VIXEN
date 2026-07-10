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

- **M1 — AppFlow emitter, retire the hand-mirror (Tasks 1–3): ✅ DONE** `AppFlowEmitter` generates the EXISTING `AppFlow.g.h` byte-equivalent from `AppFlowReference.cs`; `--appflow` CLI; NUnit byte-equivalence test; the committed header becomes generated; `test_appflow_golden` stays green. NO new features. Drift-guard pair. Testable: `--appflow --check` passes against the committed (now-generated) header; AppFlow offline suite green.
- **M2 — Schema extension + emitter tables (Tasks 4–6): ✅ DONE** new `[Flow*]` attributes; `KeyId`/`KeyMod`/`KeyChord`/`FlowScope` + new `FlowAction`s; `kElementTriggers`/`kKeyDefaults`/`kReturnEdges` + effect-ref column emitted; regenerate + commit the extended `AppFlow.g.h`. Testable: `AppFlowEmitterTests` asserts the new tables; drift-guard passes.
- **M3 — Runtime (Tasks 7–10): ✅ DONE** `InputProfile` (hierarchical), `BindingStore` pattern matching, `FlowStateMachine` entry-history + `RequestReturn`, `AppFlowRuntime::DispatchByKey` + `Load()` seeding. Testable: `test_input_profile`/`test_binding_pattern`/`test_flow_return`/`test_keychord` green; AppFlow suite green.
- **M3.5 — First-class `Return` action (Task 10.5; user refinement 2026-07-07):** Make `Return` a `FlowAction` (not a key-only concept) so a back **button** (element trigger) and a `KeyChord` (`Esc`) both dispatch the SAME action through the same spine. Add `FlowAction.Return=5` (schema + regen header + golden); route `Return` → `RequestReturn()` (bypassing the `ActionStack`) via a shared `RouteAction` helper used by BOTH `DispatchByKey` and `DispatchBySelector`; seed `kReturnEdges` into the `InputProfile` as `Return` bindings. Testable: a gtest proves both `DispatchByKey({Esc,None})` in Settings AND `DispatchBySelector("back-button")` resolve to `Return` → FSM pops; no `ActionStack` entry created by `Return`; AppFlow suite green. Sequenced BEFORE M4 so the editor binds a back-button + Esc to `Return` uniformly. (Design D6 refined.)
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
- Milestone 1 (Tasks 1–3): DONE · Yeroket commits c0e374bb..6b997a6a (AppFlowEmitter + LoadAppFlow, --appflow CLI + NUnit AppFlowEmitterTests) · VIXEN commit 60b30b62 (regenerated AppFlow.g.h — byte-identical to prior committed except the removed HAND-AUTHORED/TODO(appflow-codegen) banner block; appflow_check/appflow_regen CMake drift-guard pair) · Opus validator APPROVED all 7 criteria (diff empty; drift-guard verified BOTH directions exit 0/exit 1; NUnit non-vacuous; AppFlow offline suite 27/27 green with test_appflow_golden.cpp.obj force-recompiled against the regenerated header; pre-existing all-build failures [test_body_instance_raymarch_render setenv/MSVC, SdfRecipes/SdfCoreKernels macro collision] independently confirmed unrelated — those files don't reference AppFlow, untouched by 60b30b62; tree integrity clean, no DLL/bin/obj committed) · 2026-07-07
- Milestone 2 (Tasks 4–6): DONE · VIXEN commits 98e1cae4 (Task4: 6 new [Flow*] attrs + FlowScope in AppFlowAttributes.cs [NOT Yeroket GpuStructAttributes.cs — that's where the existing [Flow*] attrs live; plan's literal path corrected]; AppFlowReference.cs +KeyId/KeyMod/Undo=1/Redo=2/Save=3/UndoSettingChange=4/Settings=3/triggers/key-defaults/return-edge/effect/ToSettings-transition) + 88e995eb (Task6: regenerated AppFlow.g.h with key vocab + trigger tables, extended test_appflow_golden, + AppFlowLoader::IsValidState cap Paused→Settings root-cause fix) · Yeroket commit 3bc39904 (Task5: emit KeyId/KeyMod/KeyChord/FlowScope + AppFlowElementTrigger/KeyDefault/ReturnEdge structs + kElementTriggers/kKeyDefaults/kReturnEdges + AppFlowTransition.effect column, TDD red→green) · Opus validator APPROVED all 7 criteria (regen diff EMPTY; Ctrl+Z-twice tightest-wins proof point CONFIRMED [Global→Undo + State/Settings→UndoSettingChange]; drift-guard both directions; IsValidState fix independently confirmed correct+necessary+root-cause [cap tracks schema max, still rejects >Settings]; AppFlow suite 28/28, Yeroket 30/30; tree clean, no DLL/bin/obj) · NON-BLOCKING FINDING: the AppFlowTransition `const char* effect;` column is NOT gated behind hasKeyVocab (only the key structs/tables are), so a keyless schema no longer produces the LITERAL M1 header (2-line diff: the effect field + "none"). NOT a functional regression — the plan's Task 5 interface explicitly lists effect as intended M2 output, [FlowEdgeEffect] is transition-scoped (gating behind key vocab would be arbitrary), M1 NUnit still green, purely additive. Only the Task5 commit-message wording ("all new emission is gated") overstates it. · 2026-07-07

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

---

## Reframe Slice Plan (2026-07-10 — supersedes M3.5/M4)

> **For agentic workers:** this section supersedes **M3.5 (Task 10.5)** and **M4 (Tasks 11–13)** above. M1–M3 (Tasks 1–10) stand as-built. Execute the R-milestones below via superpowers:subagent-driven-development. Same house style: Files / Interfaces / checkbox Steps / exact commands + expected counts. Every "match the existing shape" instruction names its arbiter.

### What changed since the original M4

Two things changed the ground under M3.5/M4:

1. **The reframe** (`AppFlow-Kernel-Glue-Transplant-Reframe-Design-2026-07.md`, D10–D16). The action model becomes a **uniform categoryless registry** (D14): `Dispatch(id, params) → handlers_[id](params)`, `RegisterHandler(id, fn)`, and the public **named verbs are DELETED** (D15). The editor becomes a **pure consumer** — it registers handlers and dispatches by selector/key/id only, naming zero triggers/actions in code. `Return` is no longer a first-class M3.5 concern; it is **just a registered handler** that calls the nav-pop primitive (this DISSOLVES the old Task 10.5 / M3.5). A `Data` action names the **View noun** it mutates (D16), compile-checked against the `[View]` schema. And a **walking-skeleton logic-transplant** (D12) proves the kernel transplants flow *logic*, not only data, on the smallest real body.

2. **The kernel-unification program merged to Yeroket main 2026-07-10** (see the `kernel-framework` skill §11). This plan builds on the POST-unification world, not the pre-unification one M1–M3 were written against:
   - `[Flow*]` attribute **definitions are kernel-owned**: `$KF/Runtime/AppFlowAttributes.cs`, namespace `Yeroket.Util.KernelFramework`. NEW attributes (D16's data-target marker) go THERE, not in VIXEN's schema dir. (VIXEN's `codegen/appflow-schemas/AppFlowReference.cs` holds only the schema INSTANCE and already does `using Yeroket.Util.KernelFramework;`.)
   - `AppFlowEmitter` is **model-indirected**: `AppFlowModelBuilder.Build(Compilation)` → plain-data `AppFlowModel` → pure `AppFlowEmitter.Emit(AppFlowModel)`. A new table/column = extend the **MODEL** (`AppFlowModel.cs`) + the **BUILDER** (`AppFlowModelBuilder.Build`) + the **EMITTER** (`AppFlowEmitter.Emit`) — never inline symbol discovery in the emitter (arbiter: `AppFlowModel.cs` + `AppFlowEmitter.cs` in `$KF/SourceGenerator~/Transpiler/`).
   - Shared helpers are canonical: banner via `GeneratedBanner.Line(...)`, C#→C++ type map via `CppTypeMapping.MapToCppType`, nested collect via `NestedCollect.DepthFirst<T>`. Never hand-roll a second switch.
   - Codegen-internal namespace is `Yeroket.KernelFramework.Codegen`. Yeroket suites baseline: `SDFNodeGenerator.Tests 230/230`, `CodegenTool.Tests 38/38`. Yeroket main = `ca198ba6`.

**Worktrees for execution** (unchanged from the brief):
- **VIXEN** work continues on branch **`worktree-view-contract-inc4`** in the existing worktree `$W = /mnt/c/cpp/VBVS--VIXEN/.claude/worktrees/view-contract-inc4`; sources under `$W/VIXEN/`. Windows build dir `$W/build/ninja` (preset `vixen-ninja`, `binaryDir=${sourceDir}/../build/ninja`). Helper bats: `$W/build.bat` (configure+build), `$W/build/appflow_val_build.bat` (builds the 7 core AppFlow test exes via vcvars64), `$W/build/appflow_val_run.bat` (runs them). The live-gate driver is `$W/VIXEN/temp/run_editor_script.bat`.
- **Yeroket** work on NEW branch **`feat/appflow-reframe-slice`**, worktree `$KFW = /home/liory/Github/Yeroket-Fantasy/.worktrees/appflow-reframe`; kernel package at `$KFW/Packages/com.yeroket.utility.kernel-framework` (this is `$KF` for THIS stream). dotnet: `/home/liory/.dotnet/dotnet`.

**Byte-verification rule (carried into every task):** use `/usr/bin/diff`, `cmp`, or `sha256sum` for artifact evidence — the rtk proxy has reported false "identical". Poll long Windows builds on a foreground ~15–30s interval, never blind-wait; never overlap same-target builds. No pushes (gated).

### The two load-bearing decisions (grounded in the real code)

**D12 route — WHICH body, WHICH kernel mechanism, WHAT kernel-side change.**
The reframe recommends `applyToggle`'s self-inverse. Grounding it: the hand-written toggle logic today is inside `AppFlowRuntime::ToggleLayer` (`$W/VIXEN/libraries/AppFlow/src/AppFlowRuntime.cpp:90-101`) — its self-inverse core is `layers_.Toggle(index)`, and `LayerController::Toggle` flips one bit of `mask_`. The smallest **pure, branch-having-optional, side-effect-free** kernel of that is:
```csharp
// applyToggle: self-inverse — XOR-flips bit `index` of `mask`. mask ^ applyToggle(mask,i) restores.
[KernelCallable] public static uint applyToggle(uint mask, uint index) => mask ^ (1u << (int)index);
```
This is fully in-grammar for `CppAstVisitor.ToCpp` (binary `^`, shift, cast, invocation — all handled at `CppAstVisitor.cs:152/155/161/97`).

The mechanism: the KernelCallable→C++ route (`CppEmitter.BuildCppHeader` → `CppAstVisitor.EmitFunction`) is **source-gen-only today** (skill §5; no CLI branch — verified: no `--cpp`/`--callable` flag in `Program.cs`) AND `CppEmitter`'s preamble hard-codes `namespace Yeroket::Sdf::Generated` + `#include <glm/glm.hpp>` (`CppEmitter.cs:15-19`), which is SDF-flavoured and wrong for AppFlow. The domain-blind core is `CppAstVisitor.EmitFunction` (`CppAstVisitor.cs:18`) — it emits ONE `inline` function with NO namespace/preamble.

**Kernel-side change (exactly this, no more):** add a minimal CLI branch `--callable-cpp` to `$KF/CodegenTool~/Program.cs` that (a) sweeps a `--schema <dir>` for `[KernelCallable]` methods, (b) wraps `CppAstVisitor.EmitFunction`'s output in a neutral header (`#pragma once`, `GeneratedBanner.Line("--callable-cpp", …)`, `#include <cstdint>`, `namespace Vixen::AppFlow::Generated { … }`), and (c) supports `--check`. This REUSES the existing tested visitor (`CppEmitterTests.cs` already asserts `EmitFunction` output for `SdfCore_Sphere`/`SmoothUnion`) and adds no parallel transpiler. It needs a `CompilationLoader.LoadKernelCallables(files)` (a 3-line sibling of `LoadAppFlow`, returning the `Compilation`; the branch scans it for `[KernelCallable]` methods exactly as `EmitCppEmitter` does at `SDFNodeSourceGenerator.cs:2064` minus the `[SdfCoreKernel]` filter). Vendor the emitted `AppFlowCallables.g.hpp` into `$W/VIXEN/libraries/AppFlow/include/generated/` beside `AppFlow.g.h` (same convention as `SdfCoreKernels.g.hpp` under `libraries/SVO/include/Recipe/generated/`), drift-guarded. Then swap `AppFlowRuntime`'s hand-written toggle to call `Vixen::AppFlow::Generated::applyToggle(mask, index)` via the `LayerController` (see Task R5). This is a genuinely SMALL walking skeleton: one CLI branch + one loader helper + one vendored header + one call-site swap. Rejected alternative: rerouting `CppEmitter.BuildCppHeader` (drags in glm + `Yeroket::Sdf` namespace) or the full source-gen path (needs a live consuming-assembly compilation a schema-file CLI can't provide — skill §5/§6).

**D16 mechanism — HOW the emitter compile-checks the View-noun reference.**
Grounded: the View schema (`Hud`) lives in a SEPARATE dir `$W/VIXEN/codegen/view-schemas/Hud.cs` (NOT `appflow-schemas/`), with `[View] struct Hud` fields `tick/bodyCount/activeLensName/activeLensCount/factions/events` — these are the target nouns. The `--appflow` CLI branch (`Program.cs:123-136`) today loads only `--schema <appflow-dir>`. The CMake already exposes both dirs as vars: `_schema_appflow_run` and `_schema_view_run` (`codegen/CMakeLists.txt:114/106`).

**Smallest honest mechanism:** the `--appflow` branch gains an OPTIONAL second input `--view-schema <dir>`. When present, load the `[View]`-attributed struct's public field NAMES via the existing `CompilationLoader.LoadViews` (`CompilationLoader.cs:31`) into a `HashSet<string>`, and pass that set into `AppFlowModelBuilder.Build(comp, viewNouns)`. A new `[FlowDataTarget(nameof(Hud.tick))]`-style declaration (kernel-owned attribute) resolves to a bare noun name string; the builder VALIDATES it is in the set and THROWS `AppFlowTargetException("[FlowDataTarget] target 'Hud.foo' — no View noun 'foo'; known: tick, bodyCount, …")` on miss. `Program.cs` catches it, prints the message to stderr, returns 2. `--check` inherits the failure (a bad target fails generation, so the golden goes stale/errors). When `--view-schema` is ABSENT (e.g. a keyless/View-less schema), target validation is skipped and the emit is byte-identical to today — so this is append-only for the existing header. This adds one attribute (kernel `Runtime/AppFlowAttributes.cs`), one optional CLI flag + load, and one model column + validation; no new schema-load infrastructure.

### Reframe Milestone Map

Sequenced so the two BYTE-changing steps for `AppFlow.g.h` are isolated (quarantined with their regen + golden updates, the way original Task 6 was), and the C++-only registry refactor (which changes NO generated artifact) lands first as the behavioural spine.

- **R1 — Uniform registry + method retirement (C++ only; Tasks R1a–R1c).** Add `RegisterHandler`/`Dispatch(id,params)`/`DispatchById` to `AppFlowRuntime`; delete the public named verbs (`RequestState`/`DispatchAction`/`RequestReturn`/`Undo`/`Redo`/`ToggleLayer`); make FSM/ActionStack/Layers services handlers reach; update ALL AppFlow gtests + the RenderGraph headless gate that called the deleted verbs. NO generated-artifact change. Testable: full AppFlow offline suite green through the new registry; no named-verb call sites remain outside the handler bodies.
- **R2 — `Return` action + back-button + return-edge seeding (BYTE-CHANGING for `AppFlow.g.h`; Tasks R2a–R2c).** Schema gains `FlowAction.Return=5` (append-only pinned) + a `[FlowElementTrigger]` `back-button` + wire `kReturnEdges` seeding into the `InputProfile` at Load. Regenerate + commit the extended header + extend `test_appflow_golden`. Editor-agnostic proof: a gtest shows BOTH `DispatchByKey({Escape,None})` in Settings AND `DispatchBySelector("back-button")` reach the Return handler → FSM pops; no `ActionStack` entry from Return. Quarantined regen step.
- **R3 — D16 Data→View-noun target (BYTE-CHANGING for `AppFlow.g.h`; Tasks R3a–R3c).** Kernel `[FlowDataTarget]` attr + `AppFlowModel` target column + builder compile-check against the `[View]` schema + `--view-schema` CLI input + CMake wiring (second schema dir). Declare one `Data` action targeting a real `Hud` noun; regenerate + commit the header carrying the target; a mismatch fails generation (NUnit proves both the pass and the fail-with-clear-error). Quarantined regen step.
- **R4 — D12 logic-transplant walking skeleton (BYTE-NEW artifact; Tasks R4a–R4c).** Kernel `--callable-cpp` CLI branch + `LoadKernelCallables` loader (reusing `CppAstVisitor.EmitFunction`); author `applyToggle` `[KernelCallable]` in a VIXEN schema; vendor `AppFlowCallables.g.hpp`; drift-guard it. NUnit asserts the emitted C++ content. New artifact only — no existing header changes.
- **R5 — Editor pure consumer + swap the transplanted body (C++ only; Tasks R5a–R5c).** `KeyMap.h` (old Task 11, verified against current editor key usage); rewrite the editor input block + the PreTick script injector onto `RegisterHandler` + `DispatchBySelector`/`DispatchByKey`/`DispatchById` (registers handlers at init incl. Return); delete `ParseLayerToggleId`; swap `AppFlowRuntime`'s hand-written toggle to call the transplanted `Generated::applyToggle`; grep gate.
- **R6 — Live gate + close-out (Tasks R6a–R6c).** Extend the windowed real-GPU gate (`test_editor_toggle_undo_capture`) so the injector drives the real click/dispatch path (not deleted verbs) + a back-button→Return path; calibrate the toggle delta LIVE; full no-regression sweep (AppFlow suite, Yeroket `SDFNodeGenerator.Tests`/`CodegenTool.Tests`, all drift-guards incl. `appflow_check` + the new `callables_check`); progress-log the pipeline.

---

## Task R1: Uniform registry + retire the named verbs (VIXEN — `worktree-view-contract-inc4`)

**Commits to:** VIXEN worktree, branch `worktree-view-contract-inc4`. No Yeroket change, no generated-artifact change.

**Files:**
- Modify: `$W/VIXEN/libraries/AppFlow/include/AppFlowRuntime.h` + `src/AppFlowRuntime.cpp`.
- Modify (test callers of the deleted verbs — the FULL inventory, grep-verified): `$W/VIXEN/libraries/AppFlow/tests/test_appflow_loader.cpp` (`RequestState`×2 @23/80, `Undo`×2 @40/67, `RequestReturn` @82), `$W/VIXEN/libraries/AppFlow/tests/test_snapshot_undo.cpp` (`rt.ToggleLayer` @103, `rt.Undo` @106), `$W/VIXEN/libraries/RenderGraph/tests/Nodes/test_appflow_editor_toggle_render.cpp` (`rt.ToggleLayer` @562, `rt.Undo` @568). (`test_action_stack.cpp`/`test_flow_state_machine.cpp` call `st.`/`fsm.` directly — NOT `rt` — untouched.)

**Interfaces (arbiter for shape = the existing `AppFlowRuntime.h:20-87`):**
- Produces on `AppFlowRuntime`:
  - `using Params = std::vector<std::pair<std::string,std::string>>;` (mirror `BoundAction::params`, `BindingStore.h:21`).
  - `using Handler = std::function<void(const Params&)>;`
  - `void RegisterHandler(Generated::FlowActionId id, Handler fn);`
  - `DispatchResult Dispatch(Generated::FlowActionId id, const Params& params);` — the entire router (design §4.2): `auto it=handlers_.find(id); if(it==end) return RejectedByState; it->second(params); return Ok;`.
  - `DispatchResult DispatchById(Generated::FlowActionId id, const Params& params={});` — trigger-less programmatic path (tests). Thin: `return Dispatch(id, params);`.
  - `DispatchBySelector(sel)` / `DispatchByKey(chord)` REWIRED to resolve → `Dispatch(id, extractedParams)` (drop the `ActionStack::ApplyFn apply` parameter — behaviour now lives in the registered handler, not the call site; design §4.2/§4.3).
  - **Services handlers reach** (kept, but no longer public dispatch VERBS): `ActionStack& Stack() { return stack_; }`, `FlowStateMachine`-nav via a kept-but-renamed private-facing accessor, `LayerController& Layers()` (already public). Since a handler is a lambda registered by the consumer with `[this]` capture of the runtime, expose `ActionStack& Stack()`, and keep `RequestReturn`'s BODY as a private/service method the Return handler calls — but NOT as a public verb the editor calls directly. Simplest honest shape: keep `Stack()`, `Layers()`, `Input()`, `Current()`, `SetCurrent()`, `SetGuardResult()`, `Load()`, `AddBinding()`; add a **service** `DispatchResult NavPop();` (the old `RequestReturn` body, renamed so it reads as a service not a user verb) that the Return handler calls; DELETE public `RequestState`, `DispatchAction`, `RequestReturn`, `Undo`, `Redo`, `ToggleLayer`.
  - Keep `RequestState`'s BEHAVIOUR reachable as a service too — rename to `DispatchResult NavTo(Generated::FlowStateId to);` (same body, publishes StateChanged). The editor never calls it this increment, but the tests need forward-nav to set up Return; naming it `NavTo` (service) not `RequestState` (verb) satisfies D15's "no named ACTION methods" while keeping FSM driving possible. (Arbiter for the publish body: the current `RequestState`/`RequestReturn` at `AppFlowRuntime.cpp:33-39/66-72` — reuse verbatim, just rename.)

- [ ] **Step 1 (R1a): Add the registry to `AppFlowRuntime`**
  In `AppFlowRuntime.h` add `#include <vector>` (for `Params`), the `Params`/`Handler` aliases, `handlers_` member (`std::unordered_map<uint16_t, Handler>` keyed by `uint16_t(id)` — mirror `BindingStore`'s `registry_` key type, `BindingStore.h:71`), and the `RegisterHandler`/`Dispatch`/`DispatchById` declarations. In `.cpp` implement the three (Dispatch is the §4.2 body verbatim).

- [ ] **Step 2 (R1b): Rewire selector/key dispatch + rename verbs to services + delete the rest**
  - `DispatchBySelector`/`DispatchByKey`: resolve as today (`bindings_.TryGetForSelector` / `inputProfile_.Resolve`) but call `Dispatch(action, params)` where `params` = the resolved `BoundAction::params` for selectors, `{}` for keys. Remove the `ActionStack::ApplyFn apply` parameter from both signatures (arbiter: `AppFlowRuntime.h:41/46`).
  - Rename `RequestState`→`NavTo`, `RequestReturn`→`NavPop` (bodies unchanged, `AppFlowRuntime.cpp:33-39/66-72`).
  - DELETE public `DispatchAction`, `Undo`, `Redo`, `ToggleLayer` from the header AND their `.cpp` bodies. (Their behaviour moves into editor-registered handlers in R5 that call `Stack().Dispatch`/`Stack().Undo`/`Stack().Redo`/`Layers().Toggle` + `applyToggle`.)
  - Add `ActionStack& Stack() { return stack_; }` (handlers reach undo/redo/dispatch through it).

- [ ] **Step 3 (R1c): Update every deleted-verb test call site (the grep inventory above)**
  Convert each to the registry model. Pattern for the loader/snapshot tests: register a handler THEN dispatch by id, e.g. replace `rt.ToggleLayer(2, cb)` with:
  ```cpp
  rt.RegisterHandler(FlowActionId::ToggleLayer, [&](const AppFlowRuntime::Params&){
      rt.Stack().Dispatch(FlowActionId::ToggleLayer, [&](bool){ /* the test's toggle body */ }); });
  EXPECT_EQ(rt.DispatchById(FlowActionId::ToggleLayer), DispatchResult::Ok);
  ```
  `rt.Undo()` → register an Undo handler (`[&](const Params&){ rt.Stack().Undo(); }`) + `rt.DispatchById(FlowActionId::Undo)`, OR (simpler for undo-round-trip tests) call `rt.Stack().Undo()` directly — `Stack()` is the service. `rt.RequestState(x)` → `rt.NavTo(x)`. `rt.RequestReturn()` → `rt.NavPop()`. Keep each test's ASSERTED behaviour identical (same value deltas, same DispatchResult).
  > The RenderGraph headless gate `test_appflow_editor_toggle_render.cpp` @562/568 is the byte-exact render gate — its `rt.ToggleLayer`/`rt.Undo` must become the registry equivalents that produce the IDENTICAL mask mutation (register a ToggleLayer handler that does `rt.Stack().Dispatch(ToggleLayer, [&]{ rt.Layers().Toggle(kCutLayerIndex); ++changed; })`, dispatch by id; undo via `rt.Stack().Undo()`). Do NOT change the render assertions.

- [ ] **Step 4: Build + run the AppFlow offline suite (Windows-side, poll)**
  ```
  cmd.exe /c "C:\cpp\VBVS--VIXEN\.claude\worktrees\view-contract-inc4\build\appflow_val_build.bat"
  ```
  then `appflow_val_run.bat`. Expected: the 7 core exes + the 4 M3 exes (`test_input_profile test_keychord test_binding_pattern test_flow_return`) build; all green. (The `appflow_val_*` bats list the 7 core; also build+run the 4 M3 tests via `cmake --build ... --target test_input_profile test_keychord test_binding_pattern test_flow_return` in the same ninja dir.) Poll the build on a 15–30s foreground interval.

- [ ] **Step 5: Grep gate + commit**
  `grep -rnE "rt(_)?\.(Undo|Redo|RequestState|RequestReturn|ToggleLayer|DispatchAction)\(" $W/VIXEN/libraries $W/VIXEN/application` → EMPTY (all converted; the editor is R5). Then:
  ```bash
  cd $W && git add -- VIXEN/libraries/AppFlow/include/AppFlowRuntime.h VIXEN/libraries/AppFlow/src/AppFlowRuntime.cpp VIXEN/libraries/AppFlow/tests/test_appflow_loader.cpp VIXEN/libraries/AppFlow/tests/test_snapshot_undo.cpp VIXEN/libraries/RenderGraph/tests/Nodes/test_appflow_editor_toggle_render.cpp
  git commit -m "refactor(appflow): uniform categoryless registry — Dispatch(id,params)+RegisterHandler, retire named verbs (Inc-4 reframe R1)"
  ```

---

## Task R2: `Return` action + back-button + return-edge seeding (Yeroket + VIXEN)

**Commits to:** VIXEN worktree `worktree-view-contract-inc4` (schema + regen + tests). NO Yeroket change (the attributes/emitter already handle `FlowAction` members, `[FlowElementTrigger]`, `kReturnEdges`). **BYTE-CHANGING** for `AppFlow.g.h` — quarantine the regen.

**Files:**
- Modify: `$W/VIXEN/codegen/appflow-schemas/AppFlowReference.cs` (add `Return=5`; a `back-button` element trigger).
- Modify: `$W/VIXEN/libraries/AppFlow/include/generated/AppFlow.g.h` (regenerated).
- Modify: `$W/VIXEN/libraries/AppFlow/src/AppFlowLoader.cpp` (seed `kReturnEdges` into the `InputProfile` as `Return` bindings — currently it seeds only `elementTriggers` + `keyDefaults`, `AppFlowLoader.cpp:38-45`).
- Modify: `$W/VIXEN/libraries/AppFlow/tests/test_appflow_golden.cpp` (assert `Return=5`).
- Create: `$W/VIXEN/libraries/AppFlow/tests/test_return_dispatch.cpp` (both paths reach the Return handler).

**Interfaces:** `FlowActionId::Return=5`; `kElementTriggers` gains `{"back-button", FlowActionId::Return, "", "click"}`; the loader binds each `kReturnEdges` entry as a `Return` action in the `InputProfile` (so `Esc` in Settings resolves to `Return`).

- [ ] **Step 1 (R2a): Extend the schema (append-only)**
  In `AppFlowReference.cs`: `[FlowActionEnum] public enum FlowAction { ToggleLayer=0, Undo=1, Redo=2, Save=3, UndoSettingChange=4, Return=5 }`. Add a back-button element trigger (the design's "a BUTTON reaches Return identically to Esc", §4.3):
  ```csharp
  [FlowElementTrigger(nameof(FlowAction.Return))]
  public static class BackButtonTrigger { public const string Element = "back-button"; public const string ParamName = ""; public const string On = "click"; }
  ```
  (Arbiter for trigger shape: `ToggleLayerTrigger`, `AppFlowReference.cs:70-76`. Note the existing `SettingsReturn` `[FlowReturnEdge]` already declares `Escape`; leave it.)

- [ ] **Step 2 (R2b): Seed return-edges into the InputProfile at Load**
  In `AppFlowLoader::Load` (`AppFlowLoader.cpp`), after the `keyDefaults` loop, add:
  ```cpp
  // Seed return edges as Return-action key bindings (Esc in <from> -> Return). The FROM state
  // scopes the binding so Esc only pops where a return edge is declared (design §4.3 / D-Return).
  for (const auto& r : view.returnEdges())
      input.Bind(Generated::FlowScope::State, r.from, r.trigger, Generated::FlowActionId::Return);
  ```
  (Arbiter: the adjacent `keyDefaults` seed loop, `AppFlowLoader.cpp:43-45`; `InputProfile::Bind` signature at `InputProfile.h`.)

- [ ] **Step 3: Regenerate the header (WSL-side) + verify only the intended diff**
  ```bash
  /home/liory/.dotnet/dotnet run --project $KF/CodegenTool~ -c Release -- \
    --schema $W/VIXEN/codegen/appflow-schemas --appflow \
    --out-header /tmp/appflow_r2.g.h
  /usr/bin/diff /tmp/appflow_r2.g.h $W/VIXEN/libraries/AppFlow/include/generated/AppFlow.g.h
  ```
  Expected diff: `Return=5` added to `FlowActionId`; a `Return` entry in `kActionDecls`; a `back-button` entry in `kElementTriggers`. If anything else moved, fix the schema. Then regenerate IN PLACE (same command, `--out-header` at the committed path) and confirm `--check` exit 0 with `sha256sum` on both.

- [ ] **Step 4: Golden + the dual-path gtest**
  Extend `test_appflow_golden.cpp`: `EXPECT_NE(h.find("Return=5"), std::string::npos);`. Create `test_return_dispatch.cpp` proving BOTH paths reach a Return handler:
  ```cpp
  TEST(ReturnDispatch, EscAndBackButtonBothPop) {
      AppFlowRuntime rt(nullptr, 0);
      ASSERT_EQ(rt.Load(), LoadResult::Ok);
      rt.SetGuardResult(FlowGuardId::DocumentValid, true);
      int returns = 0;
      rt.RegisterHandler(FlowActionId::Return, [&](const AppFlowRuntime::Params&){ rt.NavPop(); ++returns; });
      // enter Settings so there is history + the Esc return-edge is in scope
      rt.SetCurrent(FlowStateId::Editing);
      ASSERT_EQ(rt.NavTo(FlowStateId::Settings), DispatchResult::Ok);
      EXPECT_EQ(rt.DispatchByKey({KeyId::Escape, KeyMod::None}), DispatchResult::Ok);   // Esc -> Return
      EXPECT_EQ(rt.Current(), FlowStateId::Editing);
      EXPECT_EQ(returns, 1);
      // back-button selector reaches the SAME handler
      ASSERT_EQ(rt.NavTo(FlowStateId::Settings), DispatchResult::Ok);
      EXPECT_EQ(rt.DispatchBySelector("back-button"), DispatchResult::Ok);
      EXPECT_EQ(rt.Current(), FlowStateId::Editing);
      EXPECT_EQ(returns, 2);
      // Return created NO ActionStack entry (nav, not data).
      EXPECT_EQ(rt.Stack().UndoDepth(), 0u);
  }
  ```
  Register `test_return_dispatch` in `$W/VIXEN/libraries/AppFlow/tests/CMakeLists.txt` (add to BOTH `foreach` lists, arbiter `tests/CMakeLists.txt:6/17`). Build + run (poll). Green.

- [ ] **Step 5: `--appflow --check` exit 0 + commit**
  ```bash
  cd $W && git add -- VIXEN/codegen/appflow-schemas/AppFlowReference.cs VIXEN/libraries/AppFlow/include/generated/AppFlow.g.h VIXEN/libraries/AppFlow/src/AppFlowLoader.cpp VIXEN/libraries/AppFlow/tests/test_appflow_golden.cpp VIXEN/libraries/AppFlow/tests/test_return_dispatch.cpp VIXEN/libraries/AppFlow/tests/CMakeLists.txt
  git commit -m "feat(appflow): Return action + back-button trigger + seed return-edges; Esc & back-button both pop (Inc-4 reframe R2)"
  ```

---

## Task R3: D16 — `Data` action names a `[View]` noun, compile-checked (Yeroket + VIXEN)

**Commits to:** Yeroket worktree `feat/appflow-reframe-slice` (attribute + model + builder + CLI), then VIXEN `worktree-view-contract-inc4` (schema + regen + CMake). **BYTE-CHANGING** for `AppFlow.g.h` — quarantine the regen.

**Files (Yeroket, `$KF`):**
- Modify: `$KF/Runtime/AppFlowAttributes.cs` — add `[FlowDataTarget]`.
- Modify: `$KF/SourceGenerator~/Transpiler/AppFlowModel.cs` — add a `DataTargets` table (action→noun) to `AppFlowModel`.
- Modify: `$KF/SourceGenerator~/Transpiler/AppFlowModel.cs`'s `AppFlowModelBuilder.Build` — accept `IReadOnlySet<string> viewNouns`, discover `[FlowDataTarget]` classes, VALIDATE, throw on miss.
- Modify: `$KF/SourceGenerator~/Transpiler/AppFlowEmitter.cs` — emit the target column/table.
- Modify: `$KF/CodegenTool~/CompilationLoader.cs` — no new loader needed (`LoadViews` exists); OR a `ViewNounNames(files)` helper returning the field-name set.
- Modify: `$KF/CodegenTool~/Program.cs` — `--appflow` branch reads optional `--view-schema <dir>`; builds the noun set via `LoadViews`; passes it to `Build`; catches the validation exception → stderr + return 2.
- Create/Modify: `$KF/CodegenTool~/Tests/AppFlowEmitterTests.cs` (or the existing AppFlow emitter test file) — NUnit for the PASS and the FAIL-with-clear-error.

**Files (VIXEN, `$W`):**
- Modify: `$W/VIXEN/codegen/appflow-schemas/AppFlowReference.cs` — one `Data` action + `[FlowDataTarget(nameof(...))]` naming a real `Hud` noun.
- Modify: `$W/VIXEN/libraries/AppFlow/include/generated/AppFlow.g.h` (regenerated with the target).
- Modify: `$W/VIXEN/codegen/CMakeLists.txt` — `appflow_check`/`appflow_regen` gain `--view-schema "${_schema_view_run}"`.

**Interfaces (arbiter = the existing model/builder/emitter triple, kernel-framework skill §3-A):**
- `[FlowDataTarget]` on a `[FlowActionParams]`-style class OR on a dedicated marker class: `public sealed class FlowDataTargetAttribute : Attribute { public string ActionName; public string ViewNoun; public FlowDataTargetAttribute(string action, string viewNoun){…} }` — `viewNoun` authored as `nameof(Hud.tick)` which the C# compiler lowers to `"tick"`. (Grounded: `nameof(Hud.tick)` yields the bare member name `"tick"` — the same lowering `nameof(FlowAction.ToggleLayer)`→`"ToggleLayer"` already relied on across the schema.)
- `AppFlowModel` gains `IReadOnlyList<FlowDataTargetEntry> DataTargets` (entry = `{ ActionName, ViewNoun }`).
- `AppFlowModelBuilder.Build(Compilation comp, IReadOnlySet<string> viewNouns = null)` — a NULL/empty `viewNouns` skips validation (keeps the View-less path byte-identical). For each `[FlowDataTarget]`, if `viewNouns != null && !viewNouns.Contains(entry.ViewNoun)` → `throw new AppFlowTargetException($"[FlowDataTarget] action '{a}' target 'View.{n}' — no View noun '{n}'; known: {string.Join(\", \", viewNouns)}")`.
- Emitter: emit an `AppFlowDataTarget { FlowActionId action; const char* viewNoun; }` struct + `kDataTargets[]` (only when non-empty — same `hasKeyVocab`-style guard so the existing header stays byte-identical when no Data action is declared). Add `dataTargets()` to `AppFlowContainerView`.

- [ ] **Step 1 (R3a, Yeroket): the attribute + model column + validating builder**
  Add `[FlowDataTargetAttribute]` to `$KF/Runtime/AppFlowAttributes.cs` (arbiter: `FlowElementTriggerAttribute`, same file). Add `FlowDataTargetEntry` + `DataTargets` to `AppFlowModel` (arbiter: `FlowElementTriggerEntry` + `ElementTriggers`, `AppFlowModel.cs:83-91/124`). In `AppFlowModelBuilder.Build`, add the `viewNouns` param + a `BuildDataTargets(FindClasses(comp,"FlowDataTargetAttribute"), viewNouns)` that discovers + validates + throws. Add `AppFlowTargetException : Exception`.
  > Attribute definitions are KERNEL-owned (skill §7). Do NOT put `[FlowDataTarget]` in VIXEN's schema dir.

- [ ] **Step 2 (R3a, Yeroket): the emitter + the CLI branch**
  In `AppFlowEmitter.Emit`, emit `AppFlowDataTarget` + `kDataTargets[]` + the accessor, guarded on `m.DataTargets.Count > 0` (arbiter: the `hasKeyVocab` guards, `AppFlowEmitter.cs:71-81/104-109`). In `Program.cs`'s `--appflow` branch (`:123`): read `string? viewSchema = Flag(args, "--view-schema")`; if present, `var viewNouns = CompilationLoader.ViewNounNames(Directory.GetFiles(viewSchema, "*.cs", AllDirectories));` (a helper that runs `LoadViews` and collects the `[View]` struct's public field names); wrap `AppFlowModelBuilder.Build(comp, viewNouns)` in try/catch(`AppFlowTargetException` e){ `Console.Error.WriteLine(e.Message); return 2;` }.

- [ ] **Step 3 (Yeroket): NUnit — pass + fail-with-clear-error, then build + test**
  Add two tests (arbiter shape: `CliTests.cs` round-trip, skill §9): (a) a schema with a valid `[FlowDataTarget("Data","tick")]` + a stub `[View] Hud { int tick; }` view-noun set → `Build` succeeds, emitted header contains `kDataTargets` + `"tick"`; (b) target `"nope"` → `Build` throws `AppFlowTargetException` whose message contains `no View noun 'nope'`. Run from the Tests dir (NOT the project dir — false-green trap, skill §9):
  ```bash
  cd $KF/SourceGenerator~ && /home/liory/.dotnet/dotnet build -c Release 2>&1 | tail -3
  /home/liory/.dotnet/dotnet test $KF/CodegenTool~/Tests/CodegenTool.Tests.csproj 2>&1 | tail -6   # verify NON-ZERO count, expect 40/40 (38 baseline + 2)
  /home/liory/.dotnet/dotnet test $KF/SourceGenerator~/Tests/SDFNodeGenerator.Tests.csproj 2>&1 | tail -6  # 230/230 unchanged
  ```
  > If `SDFNodeGenerator.dll` shows dirty after the build with no source change beyond your files, `git checkout --` it (skill §10). Commit Yeroket:
  ```bash
  cd $KF && git add -- Runtime/AppFlowAttributes.cs SourceGenerator~/Transpiler/AppFlowModel.cs SourceGenerator~/Transpiler/AppFlowEmitter.cs CodegenTool~/CompilationLoader.cs CodegenTool~/Program.cs CodegenTool~/Tests/AppFlowEmitterTests.cs
  git commit -m "feat(appflow-codegen): [FlowDataTarget] — Data action names a [View] noun, compile-checked via --view-schema (Inc-4 reframe R3)"
  ```

- [ ] **Step 4 (R3b, VIXEN): declare the Data action + wire CMake + regen**
  In `AppFlowReference.cs`: `FlowAction` gains `Data=6` (append-only); add `[FlowDataTarget(nameof(FlowAction.Data), nameof(Vixen.Views.Hud.tick))]` (or whichever real `Hud` noun — `tick`/`bodyCount`/`activeLensName`/…; use `bodyCount` to avoid confusion with the `tick` counter). Wire the CLI second input in `codegen/CMakeLists.txt` `_appflow_args` (`:219-222`): append `--view-schema "${_schema_view_run}"` (the var already exists at `:106/120`). Regenerate WSL-side WITH `--view-schema`, `/usr/bin/diff` against committed → expect `Data=6` + `kDataTargets` added; regenerate in place; `sha256sum` match; `--check` exit 0.
  > If `AppFlowReference.cs` can't `nameof(Hud.…)` because the appflow-schemas dir doesn't compile against `view-schemas/Hud.cs`, author the target as a bare string constant the attribute takes, OR add a `using`/type-forward — the CLEANEST is the attribute taking the bare noun name string and the SCHEMA using `nameof` only if the View type is visible; since the two dirs are compiled SEPARATELY by the tool, pass the noun as a plain string literal `"bodyCount"` in the VIXEN schema and let the KERNEL compile-check it against the loaded `--view-schema` noun set (this is exactly what R3a builds — the check is at GENERATION time across the two loaded dirs, not at C# compile time within one dir). State this in the task: **the target is a plain string in the VIXEN schema; the cross-schema compile-check happens in the emitter via `--view-schema`.**

- [ ] **Step 5: golden + suite + commit (VIXEN)**
  Extend `test_appflow_golden.cpp` (`EXPECT_NE(h.find("kDataTargets"), npos)`). Build AppFlow + golden (poll), green. `appflow_check` on a fresh configure exit 0 (the `--view-schema` now flows through). Commit:
  ```bash
  cd $W && git add -- VIXEN/codegen/appflow-schemas/AppFlowReference.cs VIXEN/libraries/AppFlow/include/generated/AppFlow.g.h VIXEN/codegen/CMakeLists.txt VIXEN/libraries/AppFlow/tests/test_appflow_golden.cpp
  git commit -m "feat(appflow): Data action targets a [View] noun (bodyCount), drift-guarded via --view-schema (Inc-4 reframe R3)"
  ```

---

## Task R4: D12 logic-transplant walking skeleton — `applyToggle` via the kernel (Yeroket + VIXEN)

**Commits to:** Yeroket `feat/appflow-reframe-slice` (CLI branch + loader), then VIXEN `worktree-view-contract-inc4` (schema + vendored header + drift-guard). NEW artifact only — no existing header changes.

**Files (Yeroket, `$KF`):**
- Modify: `$KF/CodegenTool~/CompilationLoader.cs` — add `LoadKernelCallables(files)` (sibling of `LoadAppFlow`, returns the `Compilation`).
- Modify: `$KF/CodegenTool~/Program.cs` — add the `--callable-cpp` branch.
- Create/Modify: `$KF/CodegenTool~/Tests/` — NUnit asserting the emitted `applyToggle` C++.

**Files (VIXEN, `$W`):**
- Create: `$W/VIXEN/codegen/appflow-schemas/AppFlowCallables.cs` — the `[KernelCallable] applyToggle` body (in a NEW file in the appflow-schemas dir, so `--appflow` still sweeps only the reference; `--callable-cpp` sweeps this one).
- Create: `$W/VIXEN/libraries/AppFlow/include/generated/AppFlowCallables.g.hpp` — vendored generated header.
- Modify: `$W/VIXEN/codegen/CMakeLists.txt` — `callables_check`/`callables_regen` pair (wsl-bridged).

**Interfaces:**
- `--callable-cpp --schema <dir> --out-header <path> [--check]`: sweep `[KernelCallable]` methods; for each, `CppAstVisitor.EmitFunction(retType, name, params, syntax)` (arbiter: `CppEmitter.BuildCppHeader`, `CppEmitter.cs:34-62`, minus the SDF preamble); wrap in `#pragma once` + `GeneratedBanner.Line("--callable-cpp", "Regenerate from the [KernelCallable] schema.")` + `#include <cstdint>` + `namespace Vixen::AppFlow::Generated { … }` + terminal `.Replace("\r\n","\n")`.
- `applyToggle` emits to `inline uint32_t applyToggle(uint32_t mask, uint32_t index) { return mask ^ (1u << (int)index); }` (the exact cast/shift/xor mapping is what `CppAstVisitor.ToCpp` produces; the worker asserts the actual emitted string in the NUnit test and vendors whatever it produces VERBATIM — skill §10 "vendored .g.* stay verbatim").

- [ ] **Step 1 (R4a, Yeroket): loader + CLI branch**
  Add `LoadKernelCallables` (3 lines: parse files → `CSharpCompilation.Create("callableschema", trees, BuildRefs())`, return the `Compilation` — identical to `LoadAppFlow`, `CompilationLoader.cs:54`). Add the `--callable-cpp` branch to `Program.cs` (mirror the `--appflow` branch structure `:123-136`): validate `--schema` + `--out-header`; sweep the compilation for methods carrying a `KernelCallable`/`KernelCallableAttribute` attribute (scan syntax like `EmitCppEmitter` does at `SDFNodeSourceGenerator.cs:2064`, but WITHOUT the `[SdfCoreKernel]` filter — this is the domain-blind subset); build the neutral header; `--check` = byte-compare via `Same()`.
  > `CppAstVisitor.EmitFunction` and the `Same()`/`Write()` helpers are already present; do NOT re-roll them.

- [ ] **Step 2 (R4a, Yeroket): NUnit + build + test**
  Add a test: an inline schema with `[KernelCallable] static uint applyToggle(uint mask, uint index) => mask ^ (1u<<(int)index);` (+ an inline `KernelCallableAttribute` stub, as `CppEmitterTests.cs`'s `FrameworkStubs` does — skill §9) → `Program.Main(--callable-cpp …)` writes a header CONTAINING `inline` + `applyToggle` + `namespace Vixen::AppFlow::Generated` + `mask ^`. Run from the Tests dir (verify non-zero count): `CodegenTool.Tests` now 41/41 (39 after R3 + 2). Commit Yeroket:
  ```bash
  cd $KF && git add -- CodegenTool~/CompilationLoader.cs CodegenTool~/Program.cs CodegenTool~/Tests/<file>.cs
  git commit -m "feat(codegen): --callable-cpp — transplant a [KernelCallable] body to neutral C++ via CppAstVisitor (Inc-4 reframe R4)"
  ```

- [ ] **Step 3 (R4b, VIXEN): author the callable + generate + vendor**
  Create `$W/VIXEN/codegen/appflow-schemas/AppFlowCallables.cs`:
  ```csharp
  using Yeroket.Util.KernelFramework;   // [KernelCallable] is kernel-owned
  namespace Vixen.AppFlow.Reference {
      public static class AppFlowCallables {
          // Self-inverse: mask ^ applyToggle(mask,i) restores mask. Transplanted C# -> C++ (D12).
          [KernelCallable] public static uint applyToggle(uint mask, uint index) => mask ^ (1u << (int)index);
      }
  }
  ```
  Generate WSL-side into the committed path:
  ```bash
  /home/liory/.dotnet/dotnet run --project $KF/CodegenTool~ -c Release -- \
    --schema $W/VIXEN/codegen/appflow-schemas --callable-cpp \
    --out-header $W/VIXEN/libraries/AppFlow/include/generated/AppFlowCallables.g.hpp
  ```
  Inspect the vendored header. Do NOT hand-edit it (skill §10).
  > `[KernelCallable]` is defined in `$KF/Runtime/` (kernel-owned) — confirm it's swept by the netstandard Attributes shim / resolvable by `BuildRefs()`. If the CLI can't bind `[KernelCallable]` (it's a kernel-side attribute not in the Attributes shim DLL), add it to the shim the same way GpuStruct is (skill §0) — flag this as the ONE possible extra kernel-side step and scope it exactly (add `KernelCallableAttribute` to the `Attributes~` recompile set). Verify with a build before assuming.

- [ ] **Step 4 (R4b, VIXEN): the `callables_check` drift-guard**
  In `codegen/CMakeLists.txt`, mirror the `appflow_check`/`appflow_regen` pair (`:219-230`) for `AppFlowCallables.g.hpp` (`--callable-cpp`, schema `_schema_appflow_run`, out `.../generated/AppFlowCallables.g.hpp`). Add the wsl path translation + native path in both bridge branches (mirror `_out_appflow_hdr_run`, `:115/129`). `--check` exit 0.

- [ ] **Step 5: commit (VIXEN)** (the runtime SWAP is R5 — this task only lands the vendored artifact + guard)
  ```bash
  cd $W && git add -- VIXEN/codegen/appflow-schemas/AppFlowCallables.cs VIXEN/libraries/AppFlow/include/generated/AppFlowCallables.g.hpp VIXEN/codegen/CMakeLists.txt
  git commit -m "feat(appflow): transplant applyToggle C# body -> vendored AppFlowCallables.g.hpp + drift-guard (Inc-4 reframe R4)"
  ```

---

## Task R5: Editor pure consumer + swap the transplanted body (VIXEN)

**Commits to:** VIXEN worktree `worktree-view-contract-inc4`. C++ only.

**Files:**
- Create: `$W/VIXEN/application/editor/include/KeyMap.h` (old Task 11 — verify against current editor key usage: `EditorApplication.cpp` uses `GLFW_KEY_S`/`Z`/`Y`/`LEFT_CONTROL`/`RIGHT_CONTROL` @368-384; ADD `GLFW_KEY_ESCAPE` for Return).
- Modify: `$W/VIXEN/application/editor/source/EditorApplication.cpp` — delete `ParseLayerToggleId` (`:26-40`); rewrite the Update input block (`:355-387`) + the PreTick script injector (`:328-335`) onto the registry; register handlers at init.
- Modify: `$W/VIXEN/application/editor/include/EditorApplication.h` — add `escWasDown_`; a one-time `handlersRegistered_` guard (or register in `LoadDocument` after `rt_.Load()`).
- Modify: `$W/VIXEN/libraries/AppFlow/src/AppFlowRuntime.cpp` — swap the (now-private/service) toggle path to call `Generated::applyToggle`.

**Interfaces (arbiter for the editor apply-lambda shape = old Task 12 §Step 2/3, corrected to the registry model since the named verbs are gone):**
- Editor registers, once (e.g. end of `LoadDocument` after `rt_.Load()`):
  ```cpp
  rt_.RegisterHandler(FlowActionId::ToggleLayer, [this](const auto& p){
      uint32_t idx = ParseParam(p, "layerIndex");           // typed param from the BoundAction
      rt_.Stack().Dispatch(FlowActionId::ToggleLayer, [this,idx](bool){ rt_.Layers().Toggle(idx); dirty_=true; }); });
  rt_.RegisterHandler(FlowActionId::Undo,   [this](const auto&){ rt_.Stack().Undo(); });
  rt_.RegisterHandler(FlowActionId::Redo,   [this](const auto&){ rt_.Stack().Redo(); });
  rt_.RegisterHandler(FlowActionId::Save,   [this](const auto&){ if(!SaveDocument()) logger_->Error("[EditorApplication] SaveDocument failed: "+lastEditorError_); });
  rt_.RegisterHandler(FlowActionId::Return, [this](const auto&){ rt_.NavPop(); });
  ```
  (`ParseParam` = a tiny local reading the `{name,value}` vector; the value is the extracted `{index}` string from `BindingStore`'s pattern match — arbiter: `BoundAction::params`, `BindingStore.h:21`.)
- Dispatch sites carry NO behaviour: clicks → `rt_.DispatchBySelector(clickedId)`; keys → `rt_.DispatchByKey({GlfwToKeyId(k), mods})`.

- [ ] **Step 1 (R5a): `KeyMap.h`** — old Task 11 verbatim (plain-int GLFW codes → `KeyId`, completeness-guarded), INCLUDING `case 256: return KeyId::Escape;`. (Arbiter: old Task 11 Step 3.)

- [ ] **Step 2 (R5b): register handlers + rewrite the input block**
  Register the 5 handlers once. Replace the Update click-drain (`:356-364`) with `if(!clickedId.empty()) rt_.DispatchBySelector(clickedId);` (the ToggleLayer handler carries the toggle; back-button reaches Return). Replace the glfw key block (`:367-387`) with edge-detected `rt_.DispatchByKey(...)` for S/Ctrl+Z/Ctrl+Y AND `Esc`→`rt_.DispatchByKey({KeyId::Escape,KeyMod::None})` (resolves to Return in Settings via R2's return-edge seeding). Delete `ParseLayerToggleId`. Add `escWasDown_` to the header.

- [ ] **Step 3 (R5b): rewrite the PreTick script injector**
  The injector at `:328-335` currently calls `ToggleLayer(idx)`/`rt_.Undo()`/`rt_.Redo()` DIRECTLY — the deleted verbs. Rewrite it to drive the SAME dispatch path a real click/key takes (this is the reframe's "the injector must exercise the real click/dispatch path"):
  ```cpp
  case ScriptedAction::Kind::Toggle: rt_.DispatchBySelector("layer-" + std::to_string(action.layerIndex) + "-toggle"); break;
  case ScriptedAction::Kind::Undo:   rt_.DispatchByKey({Generated::KeyId::Z, Generated::KeyMod::Ctrl}); break;
  case ScriptedAction::Kind::Redo:   rt_.DispatchByKey({Generated::KeyId::Y, Generated::KeyMod::Ctrl}); break;
  ```
  (This routes toggle through the element-trigger pattern match → ToggleLayer handler, and undo/redo through the InputProfile → Undo/Redo handlers — the real spine, no shortcut.)

- [ ] **Step 4 (R5c): swap the transplanted body into the runtime**
  In `AppFlowRuntime.cpp`, `#include "generated/AppFlowCallables.g.hpp"`. Replace `LayerController::Toggle`'s use inside the toggle path so the mask flip goes through the transplanted function — the cleanest honest swap that keeps behaviour identical: in the editor's ToggleLayer handler (Step 2) change `rt_.Layers().Toggle(idx)` to `rt_.Layers().SetMask(Vixen::AppFlow::Generated::applyToggle(rt_.Layers().Mask(), idx))`. (`Mask()`/`SetMask` exist, `LayerController.h:22-23`; `applyToggle(mask,i)=mask^(1<<i)` is byte-identical to `Toggle`'s single-bit flip.) This makes the transplanted C++ the LIVE toggle logic. Confirm `test_snapshot_undo`/`test_appflow_editor_toggle_render` still pass (byte-identical mask math).

- [ ] **Step 5: build editor + grep gate + commit**
  Build `vixen_editor` Windows-side (poll) → links clean. `grep -n "ParseLayerToggleId\|rt_\.\(Undo\|Redo\|ToggleLayer\)(" EditorApplication.cpp` → EMPTY. `grep -n "applyToggle" AppFlowRuntime.cpp EditorApplication.cpp` → present. Commit:
  ```bash
  cd $W && git add -- VIXEN/application/editor/include/KeyMap.h VIXEN/application/editor/source/EditorApplication.cpp VIXEN/application/editor/include/EditorApplication.h VIXEN/libraries/AppFlow/src/AppFlowRuntime.cpp
  git commit -m "refactor(editor): pure consumer — register handlers + dispatch by selector/key; live transplanted applyToggle (Inc-4 reframe R5)"
  ```

---

## Task R6: Live gate + close-out (VIXEN)

**Commits to:** VIXEN worktree `worktree-view-contract-inc4`.

**Files:**
- Modify: `$W/VIXEN/libraries/RenderGraph/tests/Nodes/test_editor_toggle_undo_capture.cpp` (delta LIVE-calibrated + a back-button→Return assertion).
- Possibly modify: `$W/VIXEN/temp/run_editor_script.bat` (only if the return-edge exercise needs a new scripted verb — keep minimal; the FSM `test_return_dispatch` already proves the logic, this proves the WIRED windowed path).

- [ ] **Step 1 (R6a): baseline the retire is behaviour-neutral**
  Run `run_editor_script.bat` (Windows-side, poll: `until ! kill -0 $PID; do echo "[watch +${t}s] $(tail -1 $LOG)"; sleep 15; t=$((t+15)); done`). The injector now drives `DispatchBySelector("layer-2-toggle")`/`DispatchByKey(Ctrl+Z/Y)` (R5 Step 3) — the toggle/undo/redo captures @5/45/75/105 must still round-trip (frame 75==frame 5 byte-exact, frame 105==frame 45 byte-exact). If they fail, R5 broke the wired path — fix before extending. This is the real proof the injector exercises the click/dispatch path.

- [ ] **Step 2 (R6a): calibrate the toggle delta LIVE**
  From the passing run read the actual `boreDiffPixels(png5,png45)` (the test prints it). Set `kMinBoreDiffPixels` below the observed delta but > 0. Do NOT assume the prior 4/6px — measure THIS run (design §5 / global constraint).

- [ ] **Step 3 (R6a): back-button→Return wired assertion**
  Add a scripted `return`/`back` verb OR a state-dump hook so the windowed run drives `DispatchBySelector("back-button")` from Settings and the harness asserts `rt_.Current()` popped. Keep minimal — if a windowed capture can't cheaply show the pop, assert via a `rt_.Current()` state-dump the script emits (arbiter: the existing scripted-action + capture mechanism, `EditorApplication.cpp` PreTick/Update). The point: prove Esc AND back-button reach Return in the RUNNING editor, not just the unit test.

- [ ] **Step 4 (R6b): full no-regression sweep**
  - AppFlow offline suite (all ~13 exes incl. `test_return_dispatch`) → green.
  - Yeroket `SDFNodeGenerator.Tests` 230/230, `CodegenTool.Tests` 41/41 (38 + R3's 2 + R4's ... state the exact final count from the run).
  - Drift-guards on a FRESH configure: `appflow_check` (now with `--view-schema`), `callables_check`, + the existing five (`octreeconfig_check`, `view_hud_*`) → all pass (or the documented KI-015 WARNING if the tool is unreachable — verify the guard actually RAN, not silently no-op'd; skill §10).
  - `test_editor_toggle_undo_capture` → PASS on real GPU with the calibrated delta + the Return assertion.
  - `grep -rn "ParseLayerToggleId" $W/VIXEN` → EMPTY.

- [ ] **Step 5 (R6c): progress-log + commit**
  Append per-milestone lines to the Progress Log (below) as each R-milestone lands (`Reframe Rn (Tasks Ra–Rc): DONE · VIXEN <sha>.. · Yeroket <sha> · Opus validator OK · <date>`). Commit:
  ```bash
  cd $W && git add -- VIXEN/libraries/RenderGraph/tests/Nodes/test_editor_toggle_undo_capture.cpp VIXEN/temp/run_editor_script.bat VIXEN/Vixen-Docs/01-Architecture/View-Contract-Inc4-View-Action-AppFlow-Convergence-Plan-2026-07.md
  git commit -m "test(editor): windowed gate proves registry dispatch + back-button/Esc Return; reframe close-out (Inc-4 reframe R6)"
  ```

---

### Reframe self-review (spec coverage)

- **D10/D11 (engine owns nothing of app-flow; glue consumed in parallel)** → no new engine→AppFlow dep introduced; verified by R6 no-regression sweep. ✓
- **D14 (uniform categoryless registry)** → R1 `Dispatch(id,params)`/`RegisterHandler`, no categories. ✓
- **D15 (no public named verbs)** → R1 deletes `RequestState/DispatchAction/RequestReturn/Undo/Redo/ToggleLayer`; only `DispatchBySelector/DispatchByKey/DispatchById` public; FSM/ActionStack/Layers are services (`NavTo`/`NavPop`/`Stack()`/`Layers()`). R1 grep gate. ✓
- **Return-as-handler** → R2 `FlowAction.Return`, back-button trigger, return-edge seeding; R5 editor Return handler; R2 gtest (Esc + back-button both pop, no ActionStack entry). ✓
- **D16 (Data → View-noun, compile-checked)** → R3 `[FlowDataTarget]` + `--view-schema` cross-schema check + fail-with-clear-error NUnit. ✓
- **D12 (logic-transplant walking skeleton)** → R4 `applyToggle` `[KernelCallable]` → `--callable-cpp` → vendored `AppFlowCallables.g.hpp`; R5 swaps it live; R4 NUnit content check + byte-verbatim vendor. Smallest honest kernel change = one CLI branch + one loader (+ possibly the Attributes-shim add for `[KernelCallable]`, flagged in R4 Step 3). ✓
- **Editor pure consumer** → R5 registers handlers + dispatches by selector/key/id; `ParseLayerToggleId` deleted; injector drives the real path. ✓
- **Live gate** → R6 delta LIVE-calibrated + injector exercises the real click/dispatch path + back-button→Return in the running editor. ✓
- **Close-out** → R6 full AppFlow + Yeroket suites + all drift-guards + no-regression + progress-log. ✓

**Sequencing rationale:** R1 (C++-only registry) lands the behavioural spine with zero artifact churn. R2 and R3 are the two BYTE-CHANGING `AppFlow.g.h` steps, each isolated with its own regen + golden update (Task-6 quarantine pattern). R4 adds a NEW artifact only. R5/R6 are C++-only consumer + gate. Each R-milestone is independently buildable + testable.

### Reframe Progress Log
- R1 (uniform registry + verb retirement): IMPLEMENTED, commit 8ff9f20d (5 files; offline suite 11/11 exes green Windows-side; AppFlow.g.h sha f7a1d870 unchanged; grep gate clean in libraries/, 5 expected editor call sites remain for R5). **Opus validator APPROVED 2026-07-10** (spend-limit failure was transient; validator completed): Pending validation items: (1) rule on the dropped ActionApplied/ActionUndone/ActionRedone Publish() calls (implementer verified no TEST asserts them; non-test consumers unswept); (2) run test_appflow_editor_toggle_render WSL-side (needs a software Vulkan device absent on native Windows; no WSL build dir exists in this worktree yet — first configure ~500s); (3) confirm the temporarily-broken editor target (5 deleted-verb calls, R5 scope) is contained to the editor executable. All three resolved: (1) dropped Publishes = design-correct, zero live consumers; (2) render gate PASSED WSL-side (vixen-wsl Dozen, boreDiffPixels=6400, byte-exact undo via DispatchById); (3) editor breakage contained to vixen_editor, exactly R5 scope. R1 COMPLETE.
- R2 (Return action + back-button + return-edge seeding): DONE, commit e349a4cd. Opus validator APPROVED 2026-07-10: header diff = exactly the 3 intended additions (sha f7a1d870 -> c7e082d2, --check exit 0); dual-path gtest strong; 12/12 exes green. REAL BUG found+fixed in scope: BindingStore::AddElementTrigger silently dropped placeholder-less patterns -> literal patterns now route to the exact map, first-win (validator-confirmed necessary + sound precedence). LATENT EMITTER DEFAULT noted: actions without declared params get {sizeof(LayerState), hasInvert=true} rows in kActionDecls (all 4 non-data siblings identical) — harmless (Return proven no-ActionStack), candidate cleanup for a later emitter pass.
