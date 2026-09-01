# View Contract Codegen — Program Design + Increment 1

**Status (reverified 2026-09-01):** ACTIVE — Inc1's data-side walking skeleton shipped in the
current engine history (`058924a3`, `0c9f6c11`); the later faces are tracked by the renderer-agnostic
program design. The rostered AppFlow "Inc-3" string-source param model is shelved (see §11). This doc
specifies **Increment 1** (the data-side walking skeleton).

**Goal (one sentence):** Make a UI view a first-class, single-source-generated artifact — a C# `[View]` schema source-generates the RmlUi data-model registration (and, in later increments, the RML markup + data→view projection + input→action binding) — so UI stops binding to stringly-typed selectors and gains typed, autocomplete-able, compile-checked data linkage.

---

## 1. Motivation & framing

The rostered AppFlow Inc-3 was going to resolve UI bindings via opaque param-source **strings** (`"dom:attr:data-layer"`) — unsafe, stringly-typed, and rot-prone (`BindingStore::ValidateParams` today literally `(void)source;`-ignores the source). The user's redirect: don't build that band-aid. Instead make **the view a first-class source-generated contract** — typed view-element structs + data→view population + input→action readback, auto-compiled into referenceable elements the UI links to (autocomplete, compile-time-checked data binding, automatic two-way data handling). This is the same single-source codegen philosophy VIXEN already applies to SDF recipes (`RecipeContainer.g.h`) and GPU config structs (`OctreeConfig.g.h`), now applied to UI/view state.

**Locked foundational decisions (brainstorming):**

| # | Decision | Choice |
|---|----------|--------|
| D1 | **Program vs increment** | A new **program**, brainstormed fresh; it subsumes the old Inc-3 (A) param-source work and reshapes (B) binding authoring. The string-source param model is dropped. |
| D2 | **Binding foundation** | **Generate RmlUi data models** from the view schema. RmlUi's proven data-model engine does the DOM↔data sync + change detection + list rendering; codegen provides the single source of truth, the typed C++ side, autocomplete, and compile-checked linkage on top. The generated RML references model variables (not opaque id strings) — which is what cleanly retires `ParseLayerToggleId` (a later increment). |
| D3 | **Schema authoring + toolchain** | Author in **C#** with a `[View]` attribute (sibling to `[GpuStruct]`/`[Flow*]`), and **extend the real Yeroket `CodegenTool~`** with a new emitter — **reuse the kernel core, do NOT build a parallel tool.** (The config-codegen program already made — then corrected — the parallel-tool mistake; the user's explicit steer here is "new visitor/emitter over the existing kernel setup.") |
| D4 | **Inc-1 scope** | **Data-side walking skeleton only** — the RmlUi-data-model **registration** emitter over the existing `StructModel`. No new field attributes, no data→view projection, no input→action, no selection-provider collision. |
| D5 | **Inc-1 proof** | **Golden-mirror only** — assert the generated registration block equals the hand-written `UIRenderNode` block; do **not** wire the generated header into the live HUD this increment (zero render-path risk). |
| D6 | **Emit shape (Inc-1)** | Emit a **free function** `BindEditorHudModel(constructor, boundPtrs…)` that compiles + is callable standalone (not a block that only compiles inside `UIRenderNode`'s member context). Inc-2 wiring adapts. |

**Ground-truth findings that shaped this (from a code-map done during brainstorming):**

1. **VIXEN's HUD is ALREADY a real RmlUi data model — not string interpolation, not greenfield.** `UIRenderNode` runs `context_->CreateDataModel("hud")` + `RegisterStruct<HudFaction>()`/`RegisterMember`/`RegisterArray`/`Bind` + `DirtyVariable` (`UIRenderNode.cpp:116-138`), and `SetHudView` (`UIRenderNode.cpp:357-390`) IS the data→view push. So the emit target is not hypothetical — Inc-1 regenerates *exactly what a human already wrote*.
2. **The Yeroket codegen is extensible additively (§7 of the map).** `CodegenTool~` (`CodegenTool~/Program.cs`) is the `dotnet` CLI VIXEN's CMake invokes; it already `ProjectReference`s the full emitter library (`GpuStructCppEmitter`, `RecipeContainerEmitter`, `StructLayout`, Roslyn). Adding a `--view` mode + a new `RmlDataModelEmitter` that walks the existing `StructModel` is purely additive — no new tool, no Roslyn-generator change.
3. **The `StructModel` is general enough for view *data*.** `StructLayout.Build(symbol)` → `StructModel{ Name, Fields[] }` with each `FieldLayout` carrying `Name/Class(Scalar|Float3|Mat4|Nested|Array)/…`. The HUD's `factions`/`events` (arrays-of-struct) + scalars map cleanly onto it; `GpuStructCppEmitter.cs` (which switches on `f.Class` and emits per-field) is the template to copy.
4. **The honest gap is the two view-specific concepts — deferred past Inc-1.** *data→view field provenance* (which sim column populates each field; the `recentEventAge → recentChanged = age<K` projection in `SetHudView`) and *input→action binding* (element id ↔ `FlowActionId`) have no existing attribute to hang on and need a minimal new declaration surface. Inc-1 needs **neither** (the host keeps writing the projection; no input generation yet).
5. **RmlUi constraints for later increments (recorded now):** `data-model` must be on an inner `<div>`, not `<body>` (parse-order; `test_ui_hud_smoke.cpp:247-253`); and the HUD is `pointer-events:none` (`hud.rcss:21`) so clicks fall through to voxel pick — meaning input→action via RmlUi `BindEventCallback`/`data-event-click` *collides* with the existing selection-provider arbitration. Both are **Inc-2 concerns**; Inc-1's C++-only registration emit sidesteps them.
6. **`undertow` is NOT reachable** (`/home/liory/Github/undertow` does not exist; only design-comment references in-tree). Its `ui_binding` UTDL prior art can't be quoted — the concrete in-repo data→view contract to build on is `UIRenderNode::SetHudView` itself.

---

## 2. Program roadmap (increments)

This doc specifies **Inc-1** in full; the rest are named so the arc is legible (each its own future spec):

- **Inc-1 — Data-side emitter (walking skeleton).** `[View]` marker + `--view` CLI mode + `RmlDataModelEmitter` over `StructModel` → the RmlUi registration block; golden-mirror against the hand-written `UIRenderNode` block; **not** wired live. **(THIS SPEC.)**
- **Inc-2 — RML markup emit + live wiring.** Emit the `{{}}`/`data-for` RML from the schema; swap `UIRenderNode` to `#include` the generated registration block (gated by the live HUD rendering identically). Honors the inner-`<div>` constraint.
- **Inc-3 — data→view provenance.** A `[ViewField(source:…)]` concept so the sim→struct projection (today hand-written in `SetHudView`) is declared, not hand-copied.
- **Inc-4 — input→action binding + retire `ParseLayerToggleId`.** The element↔`FlowAction` declaration surface; resolve the `pointer-events:none`/selection-provider collision (RmlUi `BindEventCallback` vs the existing hit-mask drain path); route the editor's live click through the generated binding → `AppFlowRuntime::DispatchBySelector`; delete `ParseLayerToggleId`. This is the original AppFlow "interaction→action consolidation" headline win, now done purely.
- **Inc-5+ — editor-HUD migration; undertow adoption (when reachable).**

---

## 3. Increment 1 architecture

```
VIXEN/codegen/view-schemas/EditorHud.cs   ([View] schema — authored)
   │
   ▼  (CMake custom command, dotnet-gated, mirrors octreeconfig_check)
CodegenTool~  --view EditorHud --schema <dir> --out-header EditorHud.g.h [--check]
   │   CompilationLoader.LoadViews(files)        ← NEW (mirror of LoadGpuStructs)
   │   → StructLayout.Build(symbol) → StructModel ← REUSED
   │   → RmlDataModelEmitter.Emit(model)         ← NEW emitter
   ▼
VIXEN/libraries/RenderGraph/include/Generated/EditorHud.g.h   (committed artifact)
   │
   ├─▶ view_editorhud_check (ALL target)  → build fails if committed ≠ regenerated  [drift guard]
   └─▶ golden-mirror gtest                → generated registration sequence ≡ hand-written
                                             UIRenderNode.cpp:116-138  [semantic parity]
```

The live `UIRenderNode` is **untouched** — `EditorHud.g.h` is validated, not consumed, this increment.

### 3.1 The `[View]` schema (authored)

`VIXEN/codegen/view-schemas/EditorHud.cs` — mirrors the HUD's bound structs using the existing struct-layout vocabulary; the only NEW attribute is `[View]` (a marker, sibling to `[GpuStruct]`):

```csharp
using Yeroket.KernelFramework.Attributes;

namespace Vixen.ViewSchemas
{
    // A view-element row struct (nested/array element).
    public struct HudFaction {
        public string name;
        public float  grievance;
        public bool   focused;
        public bool   known;
        public bool   inLens;
        public bool   recentChanged;
    }
    public struct HudEvent {
        public string kind;
        public int    tick;
    }

    // The view: scalars + arrays-of-row. [View] marks it for the --view emitter.
    [View]
    public struct EditorHud {
        public int    tick;
        public int    bodyCount;
        public string activeLensName;
        public int    activeLensCount;
        public HudFaction[] factions;
        public HudEvent[]   events;
    }
}
```

### 3.2 `[View]` attribute (new)

Add to Yeroket `Attributes~` (`Yeroket.KernelFramework.Attributes`), sibling to `GpuStructAttribute`:

```csharp
[AttributeUsage(AttributeTargets.Struct | AttributeTargets.Class)]
public sealed class ViewAttribute : Attribute { }
```

Rationale for putting it in Yeroket (not a VIXEN-local attribute): the tool loads/filters symbols by attribute, and `[GpuStruct]` lives there — consistency + the loader already references that assembly. (The row structs `HudFaction`/`HudEvent` need **no** attribute — they are discovered transitively as field types of the `[View]` struct, exactly as `ChannelDesc` is discovered under `OctreeConfig`.)

### 3.3 `CodegenTool~` CLI extension (additive)

`CodegenTool~/Program.cs` — add a `--view <Name>` branch beside the existing `--struct` branch:

```
Existing: --schema <dir> --struct <Name> --out-cpp <p> --out-glsl <p> [--check]
NEW:      --schema <dir> --view   <Name> --out-header <p>            [--check]
```

`CompilationLoader.cs` — add `LoadViews(files)` mirroring `LoadGpuStructs` but filtering on `ViewAttribute`. The `--view` branch: `LoadViews` → find the named `[View]` symbol → `StructLayout.Build(sym)` → `RmlDataModelEmitter.Emit(model)` → write/`--check` `--out-header`. In `--check` mode it regenerates in-memory and diffs against the on-disk header (exit non-zero on drift), identical to the `[GpuStruct]` `--check`.

### 3.4 `RmlDataModelEmitter` (new emitter)

New class in the shared emitter library (`SourceGenerator~/Transpiler/RmlDataModelEmitter.cs`), ProjectReferenced into the CLI. Walks `StructModel` and emits a **free function** (D6) that produces the registration block. The emit is deterministic and ordered (nested-struct types first via `RegisterStruct`, then arrays via `RegisterArray`, then scalars/arrays via `Bind`) so it round-trips through `--check`:

```cpp
// GENERATED by CodegenTool~ --view EditorHud. DO NOT EDIT.
#pragma once
#include <RmlUi/Core/DataModelHandle.h>
#include <vector>
#include <string>

namespace Vixen::Views {

// Row structs the view binds (mirrors the [View] schema's field-type structs).
struct HudFaction { Rml::String name; float grievance = 0.f; bool focused = false; bool known = false; bool inLens = false; bool recentChanged = false; };
struct HudEvent   { Rml::String kind; int tick = 0; };

// Bound-pointer bundle the host owns; passed in so the generated function is standalone-compilable (D6).
struct EditorHudBind {
    int* tick; int* bodyCount; Rml::String* activeLensName; int* activeLensCount;
    std::vector<HudFaction>* factions; std::vector<HudEvent>* events;
};

// Emits the exact registration sequence UIRenderNode.cpp:116-138 hand-writes.
inline void BindEditorHudModel(Rml::DataModelConstructor& c, const EditorHudBind& b) {
    if (auto fh = c.RegisterStruct<HudFaction>()) {
        fh.RegisterMember("name",          &HudFaction::name);
        fh.RegisterMember("grievance",     &HudFaction::grievance);
        fh.RegisterMember("focused",       &HudFaction::focused);
        fh.RegisterMember("known",         &HudFaction::known);
        fh.RegisterMember("inLens",        &HudFaction::inLens);
        fh.RegisterMember("recentChanged", &HudFaction::recentChanged);
    }
    if (auto eh = c.RegisterStruct<HudEvent>()) {
        eh.RegisterMember("kind", &HudEvent::kind);
        eh.RegisterMember("tick", &HudEvent::tick);
    }
    c.RegisterArray<std::vector<HudFaction>>();
    c.RegisterArray<std::vector<HudEvent>>();
    c.Bind("tick",            b.tick);
    c.Bind("bodyCount",       b.bodyCount);
    c.Bind("activeLensName",  b.activeLensName);
    c.Bind("activeLensCount", b.activeLensCount);
    c.Bind("factions",        b.factions);
    c.Bind("events",          b.events);
}

}  // namespace Vixen::Views
```

**Field-class → emit mapping** (the emitter's rules, from `StructModel`):
- `Scalar` (`int`/`float`/`bool`/`string`) on the top-level `[View]` → `c.Bind("name", b.name)`.
- `Nested` struct type → `c.RegisterStruct<T>()` + one `RegisterMember("field", &T::field)` per field (types discovered transitively).
- `Array` of a nested struct → `c.RegisterArray<std::vector<T>>()` + `c.Bind("name", b.name)`.

> **Type mapping note:** the emitter maps C# `string`→`Rml::String`, `int`→`int`, `float`→`float`, `bool`→`bool` (RmlUi builtin scalars; no `RegisterScalar` needed). Enum-name mappings (like the HUD's `activeLens`→`activeLensName`) are NOT modeled in Inc-1 — the schema already declares the resolved `activeLensName : string` field, matching the hand-written struct's `activeLensName_`. The enum→name projection is a data→view concern (Inc-3).

---

## 4. Components & file structure

**Create:**
- `VIXEN/codegen/view-schemas/EditorHud.cs` — the `[View]` schema (§3.1).
- Yeroket `Attributes~/ViewAttribute.cs` — the `[View]` marker (§3.2). *(In the Yeroket repo — see Global Constraints on the repo boundary.)*
- Yeroket `SourceGenerator~/Transpiler/RmlDataModelEmitter.cs` — the emitter (§3.4). *(Yeroket repo.)*
- Yeroket `CodegenTool~/` — `--view` branch in `Program.cs` + `LoadViews` in `CompilationLoader.cs`. *(Yeroket repo.)*
- `VIXEN/libraries/RenderGraph/include/Generated/EditorHud.g.h` — the committed generated artifact.
- `VIXEN/libraries/RenderGraph/tests/test_view_editorhud_golden.cpp` — the golden-mirror gtest (§6.1) + the standalone-compile check (§6.4).
- (Yeroket) a C# unit test for `RmlDataModelEmitter` over a small `StructModel` (§6.3).

**Modify:**
- `VIXEN/codegen/CMakeLists.txt` — add `view_editorhud_check` (ALL) + `view_editorhud_regen` custom targets, mirroring `octreeconfig_check`/`_regen` (dotnet-gated).
- `VIXEN/libraries/RenderGraph/tests/test_critical_nodes.cmake` (or the RenderGraph tests CMake) — register `test_view_editorhud_golden`. **The CMake registration must land in the same task that creates the test** (Inc-2/AppFlow durable lesson).

**Untouched (explicitly):** `UIRenderNode.cpp/.h` (the live HUD block stays hand-written this increment); `hud.rml`/`hud.rcss`; the RenderGraph render path; `BindingStore`/`AppFlowRuntime`; `ParseLayerToggleId`.

---

## 5. Error handling

- **`dotnet`-gated:** the `--view` targets sit behind the same `VIXEN_DOTNET`-present guard as `octreeconfig_check`. No dotnet → targets skipped, committed `EditorHud.g.h` still consumed by the test/build; the authoring/CI machine runs the check.
- **Generator failure is loud:** an unparseable `[View]` schema, an unsupported field type, or a `StructModel` the emitter can't express → the generator exits non-zero with a diagnostic and the `--check` target fails the build. Never emit a partial/invalid header.
- **`SDFNodeGenerator.dll` non-determinism (durable gotcha):** the Yeroket analyzer DLL rebuilds non-deterministically (same size, different bytes). Commit only on a *source* change; if only the DLL churned, `git checkout --` it. (Recorded in memory `runtime-kernel-pipeline-direction`.)
- **No throw across any boundary** — this is a build-time tool + a header; no runtime/host-boundary surface is added.

---

## 6. Testing

1. **Golden-mirror parity (authoritative).** `test_view_editorhud_golden` asserts the generated `BindEditorHudModel` registration sequence is **equivalent to the hand-written `UIRenderNode.cpp:116-138` block** — compared as the **normalized ordered call sequence** (the list of `RegisterStruct<T>` / `RegisterMember("name", &T::field)` / `RegisterArray<...>` / `Bind("name", …)`), NOT raw bytes/whitespace (D5 + user-confirmed: normalization is what makes the gate robust, not brittle). A field added/removed/reordered in *either* the schema or `UIRenderNode` fails it. Concretely: the test holds ONE checked-in **expected registration sequence** — a literal list transcribed by hand from `UIRenderNode.cpp:116-138` (the human truth), with a code comment pinning that source line range. The test asserts the **generated** `BindEditorHudModel` (parsed/extracted from `EditorHud.g.h`, or equivalently a compiled call-recording of it — see §6.4) produces that exact expected sequence. Do NOT build a runtime C++ parser of `UIRenderNode.cpp` — the expected sequence is a maintained literal; the pinning comment is what tells a future editor to update both sides together. So the gate catches: (a) the generator drifting from the schema (via `--check`, §6.2), and (b) the generated sequence diverging from the hand-written HUD truth (via this literal). If `UIRenderNode.cpp:116-138` itself is later changed, the pinning comment directs the author to re-transcribe the literal — the divergence surfaces as a failing/edited test, not a silent skew.
2. **CMake `--check` drift guard.** `view_editorhud_check` (ALL target) fails the build if committed `EditorHud.g.h` ≠ regenerated-from-schema. Mirrors `octreeconfig_check`.
3. **Emitter unit test (C#/offline, Yeroket repo).** Given a small `StructModel` (a scalar, a nested struct, an array-of-struct), `RmlDataModelEmitter.Emit` produces the expected registration call sequence in the expected order. Pure-logic, no C++/RmlUi.
4. **Standalone-compile check.** The generated `BindEditorHudModel` free function **compiles and is callable** against a local `EditorHud`-shaped bind bundle + a real `Rml::DataModelConstructor` inside `test_view_editorhud_golden`'s TU — proving the emitted code is valid RmlUi C++, not just a matching string. (RmlUi is already linked by the RenderGraph tests — `test_ui_hud_smoke` uses it.)
5. **No-regression.** Existing `test_ui_hud_smoke` + the live HUD render stay green. Since Inc-1 does not `#include` the generated header into `UIRenderNode`, this is a "we changed nothing that renders" check — but it must be run to confirm the codegen additions didn't perturb the build.

---

## 7. Global constraints

- **Two repos.** The emitter/tool/attribute changes land in the **Yeroket repo** (`$HOME/Github/Yeroket-Fantasy` / `/mnt/c/GitHub/Yeroket-Fantasy`); the schema, generated header, CMake wiring, and tests land in **VIXEN**. This is a **cross-repo increment** — the Yeroket changes are a real commit in that repo (like the config-codegen program's Yeroket-side work). The pipeline must treat the Yeroket working tree as a second in-tree surface; **pushing either repo stays gated** (user decides push timing).
- **Reuse the Yeroket core — do NOT build a parallel tool.** Extend `CodegenTool~`; add a new emitter; reuse `StructLayout`/`StructModel`. (Explicit user steer; the config-codegen program's corrected mistake.)
- **Do NOT modify the live render path.** `UIRenderNode`, `hud.rml`, `hud.rcss`, and the RenderGraph render remain untouched; `EditorHud.g.h` is validated, not consumed, this increment.
- **CMake/test registration lands with the file it needs** (durable lesson — an offline syntax gate can't catch a missing link/registration).
- **Build/test Windows-side first** (the `/mnt/c` WSL mount is slow); GPU/render tests run on the real D3D12/dzn GPU. The C# generator runs via `dotnet` (the same `VIXEN_DOTNET` the config codegen uses). Never overlap builds of one target; poll long builds on a foreground interval.
- **`.g.h` header is generated — DO NOT hand-edit;** it carries a "GENERATED — DO NOT EDIT" banner and is regenerated by `view_editorhud_regen`.
- C++23, RmlUi (fetched dep), C#/.NET for the generator.

---

## 8. Increment boundary & what Inc-1 deliberately does NOT do

- No RML markup generation (`{{}}`/`data-for`) — Inc-2.
- No data→view field-provenance attribute/projection — Inc-3.
- No input→action binding, no `BindEventCallback`, no `pointer-events:none`/selection-provider resolution, no `ParseLayerToggleId` retirement — Inc-4.
- No `UIRenderNode` wiring to consume the generated header — Inc-2.
- No new field-level attributes at all — only the `[View]` marker.

Inc-1's single job: **prove the single-source view-codegen spine** (C# `[View]` → Yeroket emitter → RmlUi registration block) is correct, by regenerating what a human already wrote and asserting equivalence, with zero render-path risk.

---

## 9. Relationship to the shelved AppFlow "Inc-3"

The rostered AppFlow Inc-3 = (A) param-source resolution + retire `ParseLayerToggleId`, (B) binding declaration/authoring, (C) ModuleController. This program **replaces (A) and (B)** with the pure view-contract approach: the editor's click retirement now happens in **View-Contract Inc-4** (input→action, done via generated typed bindings, not string sources), and binding "authoring" is the `[View]` schema itself. **ModuleController (C)** is orthogonal (FSM-latched feature modules) and remains a separate future AppFlow increment — untouched by this program. The `BindingStore` string-source model is not extended; `DispatchBySelector` remains the runtime dispatch entry the generated input→action glue will call (Inc-4).
