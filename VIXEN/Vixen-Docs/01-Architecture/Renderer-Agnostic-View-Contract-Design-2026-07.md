---
title: Renderer-Agnostic View Contract — Program Design
status: ACTIVE — Inc1 through Inc5b implementation work is in the current engine history; Inc5b
closed with known gaps, while the undertow consumer migration remains blocked at Inc5 Milestone 4
(`5ed4f65f`, `1f651aa3`, `View-Contract-Inc5-Undertow-Migration-Plan-2026-07.md`).
created: 2026-07-06
supersedes: the Inc-2-only framing in View-Contract-Codegen-Design-2026-07.md §2 (Inc-2 line)
tags: [view-contract, codegen, rmlui, ui, renderer-agnostic, appflow-convergence]
---

# Renderer-Agnostic View Contract — Program Design

**Goal (one sentence):** Make a UI *view* a first-class, single-source-generated contract authored by the **consumer** (in C# + RML/RCSS content), so the **renderer stays agnostic** to any specific view — one `[View]` schema generates every face the system needs (renderer binding, UI-authoring references, C# data-upload, and eventually view→action bindings), and many different consumers can hand different views to the same generic renderer.

> **Relationship to prior docs.** This design is the corrected, program-level framing of what `View-Contract-Codegen-Design-2026-07.md` called "Inc-2 (RML markup + live wiring)". During Inc-2 brainstorming the user redirected the architecture: the `[View]` schema, its generated binder, and the data→view projection must live on the **consumer** side, not inside the engine renderer (`UIRenderNode`). This doc captures that inversion and the full end-state; the older doc's Inc-1 (shipped) remains accurate, and its Inc-3/Inc-4 roadmap items are re-expressed here as later faces/slices. See also [[vixen-owns-content-format-not-consumer]] (the standing rule this realizes) and the AppFlow convergence in §7.

---

## 1. The inversion (why this exists)

**Today the coupling runs the wrong way.** `UIRenderNode` (engine, `libraries/RenderGraph`) *is* the HUD. It hardcodes consumer/game logic:

- Consumer types: `HudFactionIn`, `HudEventIn`, and the private `HudFaction`/`HudEvent` row structs (`UIRenderNode.h:27-28,120-121`).
- A view-specific API: `SetHudView(tick, bodyCount, activeLens, activeLensCount, factions, events)` (`UIRenderNode.h:58-60`).
- The registration block (`UIRenderNode.cpp:116-138`) naming every field.
- The projection logic (`UIRenderNode.cpp:357-390`): the `kLensNames[]` enum→display-name table, the `kJuiceK` "recently changed" window (its own comment says "matches the C# LensKind enum" / "the C# RecentEventK in UndertowSim"), and the per-field `DirtyVariable` list.

That is the renderer knowing what a "faction" is — engine code carrying a specific consumer's game vocabulary. It means only ONE view can ever exist, and it can only be changed by editing the engine.

**The end state:** the renderer is a **generic RmlUi view-host** that knows nothing about factions/events/tick/lenses. A consumer authors a `[View]` schema (the single source of truth) plus its RML/RCSS content; codegen produces everything the engine needs to host that view generically. Many consumers (VIXEN's own app, undertow, a mod) can each hand the renderer a different view, and it handles each correctly.

**Grounded, not hypothetical.** Two facts from a code-map done during brainstorming make this buildable today:

1. **No production caller feeds real HUD data.** `SetHudView`/`SetHudData` are invoked only by `test_ui_hud_smoke` (and *referenced* in main-app comments); no shipping app pushes real faction/event data. So relocating the projection + storage to the consumer is low-risk — nothing live depends on it.
2. **RmlUi exposes a runtime (dynamically-typed) data-variable API beneath the ergonomic compile-time sugar.** `DataVariable.h` declares `VariableDefinition` (abstract), `ScalarDefinition`, `StructDefinition` with runtime `AddMember(const String&, UniquePtr<VariableDefinition>)`, `FuncDefinition`, and array definitions; `DataTypeRegister.h` declares `RegisterDefinition(FamilyId, UniquePtr<VariableDefinition>)`; and `DataVariable(VariableDefinition*, void* ptr)` pairs a definition with a raw pointer. This is the `RMLUICORE_API`-exposed layer under `RegisterStruct<T>()`. It means the engine can build a data model from a **runtime description** (names/types/offsets) bound to **raw consumer memory** — no compile-time `T` required. The generic host is real.

### 1.1 The concrete pain this eliminates (the automated boundary layer)

Today the consumer↔renderer **view boundary is maintained by hand**. In undertow, the view schema is duplicated across the seam — the C# sim side and the C++/renderer side each carry their own copy of the field layout — and they are kept in sync **manually, with a hand-bumped view version number** as the only guard against drift. Every field add/rename/reorder is a two-sided edit plus a version bump that someone must remember; a missed bump or a mismatched edit is a silent runtime desync (wrong bytes land in the wrong field, or the HUD reads garbage).

This program makes that boundary layer **generated, not hand-maintained**: the single `[View]` schema is the source, and codegen emits every side of the seam coherently (renderer face + C# upload face). The generator *is* the sync — a schema change either regenerates both sides together or fails the build-time drift-guard (`--check`), so the two sides cannot silently diverge. **The manually-tracked view version is subsumed by a generated schema version/hash** the marshaler validates at load (see §5.4): mismatched consumer data vs. renderer blob is caught at the boundary, automatically, instead of relying on a human to bump and check a number. Eliminating this manual, error-prone, version-tracked boundary layer is a primary motivation for the program — not just a nicety.

---

## 2. Locked decisions

| # | Decision | Choice |
|---|----------|--------|
| D1 | Inc-2 scope | Both live-wiring the registration **and** generating the RML data-binding partial — but reframed so the view is consumer-owned (D3). |
| D2 | Markup emit model | Generate a **data-binding partial** (the `{{scalar}}` lines + `data-for` row skeletons); the hand-authored shell keeps presentation/CSS/ternaries (`{{ f.known ? '✓' : '·' }}`). The schema owns *which* fields bind + row structure; the human owns presentation. |
| D3 | **View ownership** | **Consumer-owned, renderer-agnostic.** The `[View]` schema, its generated binder, and the data→view projection live on the **consumer**, not in `libraries/RenderGraph`. The engine node is a generic view-host. |
| D4 | Runtime view→renderer contract | **Generated reflection blob + generic marshaler.** Codegen emits a runtime-loadable description of the view (model name, field names/types, array element layouts, offsets); the engine has ONE generic "register a described model + marshal by-field data into it" path built on RmlUi's `VariableDefinition` API. Plus an optional generated compile-time `BindXModel` as the **native fast-path** for C++ consumers (VIXEN's own app). |
| D5 | Sequencing | **Spec the full contract, implement in slices.** This doc designs the whole thing; implementation leads with the renderer face, provable in-tree with VIXEN's own app, and schedules the C#/undertow faces for when reachable. |
| D6 | C# faces in spec | **Design all four faces** so they're coherent from one schema; implement renderer-first (code follows evidence). |
| D7 | Proof gate | **Byte-exact HUD PNG capture on the real GPU**, before/after the swap, driven through the **main app** (which actually loads `hud.rml` + the `hud` data model). Inc-1's golden test stays green. |
| D8 | Action face | A **fourth face** (view→action) is named + shaped here as trajectory — it absorbs AppFlow's stringly-typed action surface (`BindingStore` selectors, `ParseLayerToggleId`, `hit-mask`/`data-event` widgets) into the typed view contract — but is a later slice, not Inc-2. |

---

## 3. The four faces of `[View]`

One consumer-authored `[View]` schema → four generated faces, one source of truth. The **join** is that the *same* field/element declarations generate all four, so a field's data binding, its authoring reference, its C# setter, and any action it triggers are all checked against one contract — no string names a field twice, anywhere.

| # | Face | Generated artifact | Consumed by | Slice |
|---|------|--------------------|-------------|-------|
| 1 | **Renderer** | reflection blob (model name + field names/types + array element layouts + offsets) **and** native `BindXModel(constructor, boundPtrs…)` fast-path (Inc-1's emitter output) | the generic C++ RmlUi host (`UIRenderNode`) | Inc-2 (native binder), Inc-2b (blob path) |
| 2 | **Authoring reference** | typed references for the UI author so `.rml` bindings (`{{factions.grievance}}`, `data-for="f : factions"`) are checked against the schema — autocomplete instead of stringly-typed selectors | the UI author (authoring tools / review) | with markup gen (Inc-2, native side) |
| 3 | **C# data-upload** | a typed C# field-setter API (`view.tick = 42; view.factions[i].grievance = …`) that marshals field data to the renderer by field | the C# consumer feeding sim state (undertow) | when undertow reachable |
| 4 | **Action** (view→action) | typed element→`FlowAction` bindings with param signatures, derived from `[View]`, replacing `BindingStore` selector strings + `ParseLayerToggleId` | AppFlow runtime (`DispatchBySelector`) | later slice — the **AppFlow convergence** (§7) |

**Why the join matters.** In the current world, the *same* field is named as a string in three-plus disconnected places: the C++ `RegisterMember("name", …)`, the RML `{{f.name}}`, the C# projection that fills it, and (for actions) an opaque selector like `"dom:attr:data-layer"`. A typo in any one silently breaks a binding at runtime. With one `[View]` schema as the join, all faces derive from the same declaration — the compiler/generator catches a mismatch instead of the user catching a blank HUD.

---

## 4. Engine boundary — what shrinks, what becomes generic

### 4.1 What leaves `UIRenderNode` (engine → consumer)

All of the following **relocate to the consumer** (Inc-2: VIXEN's `application/main/`):

- `HudFactionIn`, `HudEventIn`, `HudFaction`, `HudEvent` (all faction/event types).
- `SetHudView(...)` and `SetHudData(...)` (the view-specific push API).
- The registration block (`UIRenderNode.cpp:116-138`).
- The projection logic (`UIRenderNode.cpp:357-390`): `kLensNames[]`, `kJuiceK`, the per-field `DirtyVariable` list, the `factions_`/`events_`/`tick_`/`activeLensName_`/… storage.
- The `[View] EditorHud` schema + generated `EditorHud.g.h` (move from `VIXEN/codegen/view-schemas/` + `libraries/RenderGraph/include/Generated/` to the consumer/app tree).

### 4.2 What the engine keeps — the generic view-host seam

`UIRenderNode` reduces to a generic RmlUi document+context host with zero consumer knowledge. The seam (exact shape finalized in the plan; two viable forms shown):

```cpp
// Engine side (RenderGraph) — no consumer types anywhere in this interface.

// A view the renderer can host. The consumer supplies the concrete implementation
// (native: calls its generated BindXModel; blob: driven by a reflection description).
class IView {
public:
    virtual ~IView() = default;
    virtual const char* ModelName() const = 0;                 // e.g. "hud"
    virtual void Register(Rml::DataModelConstructor& c) = 0;    // register scalars/structs/arrays + Bind to consumer storage
    virtual const char* DocumentPath() const = 0;              // the RML doc to load
};

void UIRenderNode::SetView(std::shared_ptr<IView> view);       // consumer hands its view in at setup
```

The node's `CompileImpl` becomes: `context_->CreateDataModel(view_->ModelName())` → `view_->Register(constructor)` → `LoadDocument(view_->DocumentPath())`. It no longer references any field name, struct, or projection. Dirtying is the consumer's job (the consumer holds the `DataModelHandle` its `Register` produced, or the node exposes a generic `MarkModelDirty(fieldName)` that forwards to `DataModelHandle::DirtyVariable`).

> The `GetUiContext()` selection seam and the GPU-sync/composite/hot-reload machinery stay exactly as-is — they are genuinely engine concerns (they never touch field semantics). Only the *view-data* surface is decoupled.

### 4.3 The consumer side (Inc-2: VIXEN's own app as the first consumer)

VIXEN's main app owns an `EditorHudView : IView` (name TBD in plan — the schema is misleadingly named `EditorHud` but models the **main-app HUD**; the plan may rename to `Hud`/`MainHud` for honesty). It:

- Holds its storage (`std::vector<Vixen::Views::HudFaction> factions_`, `tick_`, …) — now using the **generated** `Vixen::Views::HudFaction`/`HudEvent` row types (Inc-1 already emits these), reconciling the duplicate types.
- Implements `Register()` by calling the generated native binder `Vixen::Views::BindEditorHudModel(c, { &tick_, &bodyCount_, … })`.
- Owns the relocated projection (the old `SetHudView` body: lens names, juice-K, per-field dirty).

Different consumers implement `IView` differently — VIXEN natively (generated `BindFn`), undertow via the blob path (§5). The renderer never changes.

---

## 5. Runtime marshaling contract (the crux)

The renderer face has two paths against the **same** generated schema:

### 5.1 Native fast-path (Inc-2, C++ consumers)

The generated compile-time `BindXModel` + `XBind` pointer bundle (Inc-1's output). The consumer's `IView::Register` calls it. Fully typed, zero reflection cost. This is VIXEN's own app in Inc-2.

### 5.2 Blob path (Inc-2b, C#/modding consumers that ship no C++)

Codegen emits a **reflection blob** describing the view: model name; for each field its name, kind (scalar `int`/`float`/`bool`/`string`, or struct, or array-of-struct), and byte offset within the consumer's storage layout; for array element structs, the element's own field list + offsets + stride. The engine has ONE generic loader that, from the blob:

1. Builds a `ScalarDefinition` per scalar field (typed by the blob's kind), a `StructDefinition` (via runtime `AddMember(name, def)`) per struct, and array definitions per array field.
2. `RegisterDefinition`s them on the model's `DataTypeRegister`.
3. Binds each top-level field as a `DataVariable(def, base + offset)` pointing into the consumer's raw storage buffer.

The consumer pushes data **by field** — for C# over the existing SoA/bridge (the `Undertow.View` SoA contract, `ut_view`/`ut_feedback`, per [[undertow-vixen-integration-map]]); the marshaler writes the incoming bytes into the bound storage at the field offset and dirties the var. **No consumer C++, no compile-time types in the engine.**

Both paths produce an identical live data model; the byte-exact HUD gate (D7) can validate the native path in Inc-2 and the blob path in Inc-2b against the *same* expected pixels.

### 5.3 Why the blob is honest (RmlUi ground truth)

RmlUi's `RegisterStruct<T>()` is sugar over `StructDefinition` + `AddMember`; `Bind(name, &value)` is sugar over `DataVariable(definition, ptr)`. Building those directly from a runtime description is a supported, `RMLUICORE_API`-exposed use of the library — not a hack. (Confirmed: `DataVariable.h:73-150`, `DataTypeRegister.h:55-144`.)

### 5.4 Generated schema version — subsuming the manual view version (§1.1)

The reflection blob carries a **generated schema version** — a stable hash over the view's field names + types + order (and array element layouts), emitted by codegen. This replaces undertow's hand-bumped, hand-tracked view version with an automatic one that cannot go stale: any schema change changes the hash deterministically. At the boundary, the consumer's data stream carries the version the consumer was generated against, and the engine's blob carries the version the renderer was generated against; the marshaler compares them at model-load. A mismatch is a hard, logged boundary error (skip-register, visible empty view — never silent wrong-byte marshaling), exactly as §8 specifies for a blob/storage mismatch. Because both the C# upload face and the renderer blob derive from the same schema, a coherent regeneration produces matching versions on both sides automatically — the human never bumps or checks a number. (Native fast-path consumers, §5.1, get the equivalent guarantee at compile time: a schema change that isn't reflected in the consumer's storage types simply won't compile, and the `--check` golden guards the generated header itself.)

---

## 6. Implementation slices

- **Inc-2 (in-tree, provable now) — renderer-agnostic host + native consumer + markup partial.**
  - Engine: `UIRenderNode` → generic `IView` host; strip all consumer types/projection.
  - Consumer (VIXEN main app): relocate the `[View]` schema + generated binder + projection; implement `HudView : IView` using the native `BindEditorHudModel` + generated `Vixen::Views::HudFaction`/`HudEvent` row types.
  - Markup: a new **`RmlMarkupEmitter`** (Yeroket, sibling of `RmlDataModelEmitter`) generates the RML data-binding partial from the schema; `hud.rml` composes it (marked region or RmlUi include) keeping its hand-authored shell (face 2, native side).
  - Gate: **byte-exact HUD PNG capture on the real GPU** through the main app (needs a small scripted HUD-data injection + capture hook in `application/main`, mirroring Inc-2b's editor harness + reusing `RenderTargetReadback.h`). Inc-1 golden stays green; `test_ui_hud_smoke` updated for the relocated types.

- **Inc-2b — reflection blob + generic dynamic marshaler (face 1 blob path).** Emit the blob; build the engine's generic described-model host + by-field marshaler; prove it renders byte-identically to the native path.

- **Inc-3 — C# data-upload API (face 3).** Generated typed C# setters over the SoA bridge. (Absorbs the old doc's "data→view provenance" Inc-3.) This face is designed to **subsume undertow's existing hand-versioned view codegen** — see §6.5.

- **Inc-4 — view→action (face 4): the AppFlow convergence.** See §7.

- **Inc-5+ — editor-HUD migration; undertow adoption (subsume, not integrate).** Undertow's `ViewSchema.cs` + `Undertow.Authoring.Codegen` are **retired** onto this engine-owned contract (§6.5), so undertow authors views as `[View]` schemas + RML content and the whole boundary layer (C++ accessors, C# writer, version) is generated from one source instead of hand-maintained. This program is the SUCCESSOR to undertow's view system, not a peer to it.

---

## 6.5 The current undertow view boundary — the system this program SUBSUMES

Undertow already ships a view-boundary codegen system (`master`, examined 2026-07-06). This program is its **more robust, engine-owned successor** — the goal is to subsume it, not integrate two parallel systems ([[vixen-owns-content-format-not-consumer]]: a consumer-specific format absorbed by the engine contract). Documented here so the C#-face + migration slices have a concrete target.

**What exists today (undertow `core/src/`):**
- **`Undertow.View/ViewSchema.cs`** — a hand-authored static schema: 5 `SectionDef`s (`Bodies`, `Hud`, `HudFactions`, `HudEvents`, `HudInspect`), each with `ColumnDef[]` carrying a **stable integer id** (the wire contract — never reorder/reuse), a `ColumnType` (`F32/I32/U32/U8/Vec3f/ListVec3f/Str`), an **`IntroVersion`**, and a `Source` (a C# projection expression string like `"el.IsFocused ? (byte)1 : (byte)0"`). The `Bodies` section already carries the **per-body render recipe** (v10: `RecipeProvider`/`RecipeId`/`RecipeParams`) — the sim→renderer content contract.
- **The manual version** (the exact pain §1.1 names): a hand-bumped `FormatVersion = 10` plus a ladder of intro-version constants (`VersionHud=2`, `VersionFocus=3`, … `VersionRecipe=10`); every field add bumps `FormatVersion` and pins the new column's `IntroVersion` by hand; the reader version-gates on these (a column newer than the buffer reads empty).
- **`Undertow.Authoring.Codegen/`** — already generates multiple faces from that schema: `EmitViewContractHeader.cs` → the **C++ `view_contract.h`** (VIXEN-side consumer: id enum + per-section accessor classes with cached column pointers, null-guarded typed getters, `Str`/`ListVec3f` offset math), `EmitViewWriter.cs`/`ViewWriterGenerator.cs` → `ViewWriter.g.cs` (the C# serializer), `EmitHudView.cs`/`EmitHudDataModel.cs` → HUD faces; a golden test (`ViewContractHeader_MatchesSchema`) guards the C++ header against the schema.
- **The wire:** `ViewReader`/`ViewBufferReader` (SoA, magic `UTVW`) + `FeedbackReader` (`UTFB`, VIXEN→sim: UI click id + time mode) — forward-skip-unknown-section tolerant decoding.

**How this program improves on it (why the successor is "more robust"):**
1. **Engine-owned, not undertow-specific** — the contract + codegen live in VIXEN/Yeroket ([[vixen-owns-content-format-not-consumer]]); any consumer uses it, not just undertow.
2. **Generated version replaces the hand-bumped `FormatVersion` + intro-version ladder** (§5.4) — the schema hash is the version, validated automatically at the marshaling boundary; no human bumps or pins a number.
3. **Adds the RmlUi authoring face** (faces 2 + 4) undertow's codegen never had — typed, autocomplete-able `.rml` bindings + view-derived actions, so UI authoring itself is checked against the schema, not just the C++ accessors.
4. **One schema, four coherent faces** vs. undertow's schema → {C++ header, C# writer, HUD emitters} with the RmlUi + action + version-automation gaps filled.

**Migration shape (Inc-5+):** undertow's `ViewSchema.cs` sections/columns/ids/`Source` expressions map onto `[View]` schemas (the ids + intro-versions become the generated version's concern); `view_contract.h` + `ViewWriter.g.cs` are regenerated by the engine-owned pipeline; the wire (`UTVW`/`UTFB`) is preserved or regenerated from the same source. The per-body **render recipe** columns are a first-class case (they already cross the sim→renderer seam). Detailed migration design is deferred to that slice — recorded here so it isn't lost.

---

## 7. The AppFlow convergence (face 4 trajectory)

The capstone: the `[View]` schema eventually also owns **view→action**, absorbing AppFlow's stringly-typed action surface into the typed view contract.

**What it absorbs.** Today the input→action path is stringly-typed and scattered: AppFlow's `BindingStore` resolves RML-selector→action via opaque strings (its `ValidateParams` literally `(void)source;`-ignores the param source); the editor drains clicks via `DrainClickedElementId` + `ParseLayerToggleId` (string-parsing an element id like `layer-2-toggle`); `hud.rml`'s interactive widgets (`hit-mask`/`data-event`, lines 36-45) name actions in comments only. The original AppFlow Inc-3/Inc-4 were going to formalize this with param-source **strings** — the band-aid the user rejected that started this whole program.

**The view-derived form.** A `[View]` declares not just data fields but **interactions**: which elements fire which `FlowAction`s, with typed parameter signatures — so the element↔action binding is generated + compile-checked from the same schema as the data binding. At runtime it routes through `AppFlowRuntime::DispatchBySelector` (already built — see [[appflow-framework-direction]]), but the *selector→action* mapping is generated from the typed view, not resolved from hand-written strings. `ParseLayerToggleId` and the `BindingStore` string surface are **deleted**, replaced by generated bindings.

**Shape (designed as trajectory, implemented later).** A field-/element-level attribute on the `[View]` schema (e.g. `[ViewAction(element:"layer-{index}-toggle", action:ToggleLayer, param:index)]` — exact syntax in that slice's design) declares the binding; codegen emits (a) the C++/blob element→action table the generic host installs as RmlUi event listeners, and (b) the authoring-reference + C# faces for it. This resolves the `pointer-events:none`/selection-provider arbitration noted in the Inc-1 design (RmlUi `BindEventCallback` vs. the existing hit-mask drain path) as part of that slice.

**Why it converges here and not in AppFlow.** AppFlow owns the *action mechanism* (reversible `FlowAction`s, `ActionStack`, `DispatchBySelector`). The View Contract owns the *view* — and a UI action IS a property of a view element. So the binding surface belongs to the view contract (typed, single-source, autocomplete), while the actions it fires remain AppFlow's. The two programs meet exactly at face 4: AppFlow provides the verbs, `[View]` provides the typed nouns+bindings that trigger them. This is the pure form of AppFlow's original "interaction→action consolidation" headline.

---

## 8. Error handling

- **Blob/schema vs. storage mismatch** (blob path): the generic loader validates field count/kinds/offsets against the consumer's declared storage size at load; on mismatch it logs a loud error and **skips registering that model** (the document loads with unbound vars — a visibly empty HUD, never a crash or memory corruption). Mirrors KI-015's "surface, don't swallow" principle.
- **Missing/failed document**: unchanged — the existing RmlUi `LoadDocument`→null path already tolerates it (`document_` guarded before `Show()`).
- **Native path type drift**: caught at compile time (the generated `BindXModel` + `XBind` bundle won't compile against wrong storage types) — the Inc-1 golden test already guards schema↔generated drift.
- **No consumer set**: `SetView` never called → the node renders no data model (document with unbound vars or none). Logged at compile.

---

## 9. Testing strategy

- **Structural (fast, offline):** Inc-1's golden test stays green (schema↔generated-registration equivalence). Add C# codegen unit tests per new emitter/face as each lands (markup partial, blob, C# API) — same NUnit style as Inc-1's `RmlDataModelEmitterTests`.
- **Authoritative (real GPU, live):** **byte-exact HUD PNG capture** through the main app — render `hud.rml` with known injected faction/event data before and after the decouple/wire swap, assert byte-identical pixels on the real D3D12/dzn GPU. This is the program's established "live-run gate is authoritative for GPU work" rule (see [[live-verification-authoritative-for-gpu-work]]); static equivalence has repeatedly passed runtime bugs on GPU work in sibling programs. The blob path (Inc-2b) reuses the same gate against the same expected pixels.
- **No-regression:** `test_ui_hud_smoke` updated for the relocated types (it currently `#include`s the engine's `HudFactionIn`); it must still pass, proving the data model still drives the document. The editor windowed gate (`test_editor_toggle_undo_capture`) must stay green (the editor uses `editor.rml`, unaffected, but shares `UIRenderNode`).

---

## 10. Out of scope (this program / deferred within it)

- **Inc-2 specifically does NOT:** build the reflection blob or generic marshaler (Inc-2b); build any C# face (Inc-3+); implement view→action (Inc-4); touch `editor.rml`/the editor layer panel (unrelated to the `hud` model); change RmlUi's render/composite/GPU-sync path.
- **The program does NOT:** replace RmlUi's data-model engine (it drives it, generically); merge `[FlowAction]` (AppFlow) into `[View]` — they compose (face 4 binds view elements to existing FlowActions); reach into undertow before it is reachable in-tree.

---

## 11. Ground-truth references (read before planning)

- `libraries/RenderGraph/include/Nodes/UIRenderNode.h` — the consumer types + `SetHudView` API to strip (`:27-28, 58-63, 120-129`).
- `libraries/RenderGraph/src/Nodes/UIRenderNode.cpp` — the registration block (`:116-138`) and projection (`:357-393`) to relocate; the generic machinery to keep (`:99-223, 234-…`).
- `libraries/RenderGraph/assets/ui/hud.rml` — the main-app HUD doc (the `hud` data model consumer); its data-bound region (`:8-30`) is the markup-partial target; the interactive widgets (`:36-45`) are face-4 (later).
- `application/main/source/graph/BuildRenderGraph.cpp:743` — main app loads `hud.rml`; `:256` stores the UI node for `GetUiRenderNode()`.
- `application/main/source/main.cpp:81` — `app->Run({.exitAfterFrames=…})` (the graph.Run() entry; the capture hook rides here). `VIXEN_EXIT_AFTER_FRAMES` exists; no capture path yet.
- `libraries/RenderGraph/include/Debug/RenderTargetReadback.h` + `application/editor` capture harness (Inc-2b) — the byte-exact PNG-capture template to mirror for the main app.
- `libraries/RenderGraph/tests/test_ui_hud_smoke.cpp` — must be updated for relocated types; the S1b tests are the behavior contract.
- Inc-1 outputs: `Vixen::Views::{HudFaction,HudEvent,EditorHudBind}` + `BindEditorHudModel` in the generated header; `RmlDataModelEmitter`/`ViewModel` in Yeroket (`$KF/SourceGenerator~/Transpiler/`).
- RmlUi runtime API: `_deps/rmlui-src/Include/RmlUi/Core/DataVariable.h` (`VariableDefinition`/`ScalarDefinition`/`StructDefinition::AddMember`), `DataTypeRegister.h` (`RegisterDefinition`), `DataModelHandle.h`.
- Prior design: `View-Contract-Codegen-Design-2026-07.md` (Inc-1, shipped) + `View-Contract-Codegen-Inc1-Plan-2026-07.md`.
