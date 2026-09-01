# AppFlow Framework — Design

**Date:** 2026-07-05
**Status (reverified 2026-09-01):** ACTIVE — Inc-1, Inc-2, Inc-2b, and GraphRun shipped in the
engine history (`c5150e56`, `c20d507f`, `eea9e1ff`, `2ab4c534`); later View-Contract/AppFlow
convergence work remains in the reframe documents.
**Program:** App-flow / state-transition / systemic-action framework for VIXEN and its consumers
**Relates to:** Kernel-codegen framework (Yeroket single-source contract), Config-Struct-Codegen, Recipe authoring→render pipeline, Runtime-Kernel-Pipeline direction

---

## 1. Motivation

As VIXEN moves toward richer UI and player/editor interaction (undertow, the SDF editor, the
content editor), the current app-flow reality does not scale. `EditorApplication::Update()` is the
canonical symptom: every interaction is hand-wired — `glfwGetKey` polled inline for save,
`DrainClickedElementId()` + string-parsing (`ParseLayerToggleId`) for layer toggles, a manual
`ConsumeDirty()` flag for re-apply. Each new interaction adds another bespoke branch and another
implicit cross-dependency. Left unchecked this becomes exponential interconnected management.

There is today **no** command/action abstraction, **no** undo/redo, **no** app-flow state machine,
and **no** generic panel/layout system. The only durable-state-with-modifiers precedent is
`SelectionSet` (Replace/Add/Toggle/Range), and it is scoped strictly to selection.

**The one system that DOES exist lives in the wrong place.** undertow has a real, working UI-action
system — `Undertow.Sim/UiActions/` (`UiActionRegistry`, `ui_binding` UTDL, `UiBindingTable`): named,
typed-param UI actions bound to RML selectors, resolved + validated at content load, moddable. But it
is **undertow-specific** — an engine-level concern (UI actions binding to render elements) trapped
inside a game's sim library, and it stops at *resolution* (its handlers are placeholders; firing +
undo were deferred as "later"). It is an outlier: exactly the kind of consumer-owned mechanism the
project rule *"VIXEN owns the content format, not the consumer"* says belongs in the engine.

**Goal:** a framework for app-flow management — reversible actions with grouping, an app-flow state
machine, layer control, module control, and panel/layout composition — built by **generalizing
undertow's UI-action system into an engine-owned contract** and completing the half it left undone
(firing, undo, grouping, reversal). Consumers **latch into** it through VIXEN's already-proven
cross-consumer contract mechanism (the Yeroket single-source codegen used for SDF op-codes and
`[GpuStruct]` config), so the UI system interacts with app state through one seam instead of N bespoke
wirings. **undertow becomes consumer #1 of a general system — not the owner of a bespoke one** — the
same relationship it already has to the SDF recipe format and the config-struct codegen.

---

## 2. Approaches considered

**A. Consumer-owned framework in C# (rejected).** Put app-flow entirely in undertow/editor C#;
VIXEN stays flow-agnostic. Rejected: `EditorApplication` is C++ and needs the *same* machinery — a
C#-only framework leaves the C++ editor hand-wired forever and forces two parallel implementations.

**B. Bespoke C++ command/state library, hand-bound per consumer (rejected).** A classic
`ICommand`/undo-stack + FSM with a hand-written C-ABI per consumer. Rejected: it reinvents a
cross-consumer contract already solved once (Yeroket single-source codegen); every new consumer type
would need bespoke glue — the exact "exponential interconnection" we want to kill.

**C. Yeroket-contract app-flow framework (chosen).** Reuse the proven
`[Attribute] → source-gen → byte-identical C++ mirror → generic C++ runtime + loader` pipeline,
retargeted from geometry/config to app-flow. One contract mechanism, N consumers, zero bespoke glue.

---

## 3. Architecture — three-tier separation

The invariant, stated once: **Yeroket carries no app-flow logic; it only turns a consumer module's
flow declarations into an injectable state-machine artifact. All app-flow *meaning* lives in the
consumer module (Tier 2); all generic *execution* lives in VIXEN (Tier 0).** This mirrors SDF
exactly — `com.yeroket.utility.kernel-framework` (logic-free codegen) vs.
`com.utility.sdf` (`SdfCoreKernels.cs`, the consumer that authors the vocabulary).

```
Tier 3 — THE APP  (undertow host / VIXEN EditorApplication)
         Instantiates the AppFlow runtime, loads a consumer's artifact, drives it.

Tier 2 — CONSUMER APP-FLOW MODULE  (C#, NEW package, e.g. com.<x>.app-flow)
         Declares ALL app-flow vocabulary + logic-as-data via marker attributes:
           [FlowState] [FlowTransition] [FlowAction] [FlowLayer] [FlowModule] [FlowPanel]
         Links Yeroket ONLY to generate its artifact.  ← mirrors com.utility.sdf
         Where "EnterSimulating" / "ToggleLayer" / an undo group MEAN something.

Tier 1 — YEROKET kernel-framework  (C#, EXISTING, logic-free)
         Pure codegen engine. Reads Tier-2 attributes, emits the injectable
         app-state-machine artifact: AppFlow.g.h (C++ mirror) + C# registration.
         Knows NOTHING about app flow.  ← like SDFNodeSourceGenerator emitting SdfOpCodes.g.h

Tier 0 — VIXEN AppFlow runtime  (C++, NEW library: VIXEN/libraries/AppFlow)
         Generic executor + loader. Ingests AppFlow.g.h, runs the five primitives
         over the declared types. Broadcasts AppFlowChangedEvent on the existing MessageBus.
```

### 3.1 The five runtime primitives

| Primitive | Owns | Built on |
|---|---|---|
| **ActionStack** | Declared reversible actions; undo/redo; transactional **groups** (one gesture = one undoable unit). Inverse-preferred, snapshot-fallback. | new; broadcasts via EventBus |
| **FlowStateMachine** | Named app modes (Editing↔Simulating↔Paused…) + guarded, declared transitions. | new; mirrors `SelectionSet` apply-shape |
| **LayerController** | Named layers: enable/disable/reorder **as actions** (undoable), replacing `EditorApplication::ToggleLayer`. | new; drives RenderGraph node enable |
| **ModuleController** | Register/activate/swap feature modules (SDF editor, content editor, sim view) with lifecycle. | new; latches modules to states |
| **PanelLayout** | RmlUi-native docking: drag/dock/resize panels; 3D panels = `RenderTargetNode` surfaces; layout persists + is undoable. | RmlUi + `RenderTargetNode` |

All five emit `AppFlowChangedEvent` on the existing `MessageBus`, so the RmlUi UI and undertow react
to state changes exactly as the HUD reacts to `SelectionChangedEvent` today — no new notification path.

### 3.2 Boundary model — start at data-only, architect for callbacks

- **V1 = data-only.** A consumer's action is a declared opcode + declared state-delta the generic
  VIXEN runtime executes; **no consumer code runs in-engine**. Purest contract; consumer stays data.
  Matches SDF-recipe / config-codegen exactly.
- **Future = callback escape hatch (modding).** A `[FlowAction(native: true)]` flag is **reserved in
  the schema now** (declared-but-unimplemented) so a later increment adds a C-ABI callback registry
  (function pointers for Apply/Invert/CanTransition) **append-only**, without reshaping the core. This
  realizes the previously-planned code-based registration for mods.

---

## 4. The contract — attributes and the generated artifact

### 4.1 Tier-2 declaration surface (consumer authors; Yeroket emits; neither VIXEN nor Yeroket knows the meaning)

```csharp
// A named app mode. Members become the FlowStateId enum.
[FlowState] Editing, Simulating, Paused, ...

// A guarded edge. from/to reference [FlowState] members; guard is a declared
// predicate OPCODE (a data condition), NOT arbitrary code in V1.
[FlowTransition(from: Editing, to: Simulating, guard: DocumentValid)]

// A reversible action. Declares its state FOOTPRINT (which schema regions it
// touches) + optionally an Invert opcode-sequence. No invert ⇒ engine snapshots
// the footprint (snapshot-fallback). group tags gesture grouping.
[FlowAction(id: ToggleLayer, footprint: LayerState, invert: ToggleLayer /*self-inverse*/)]
[FlowAction(id: SetParam,    footprint: DocState  /* no invert ⇒ snapshot */)]

// Named layer / module / panel — each emits an id + default metadata.
[FlowLayer]  base, bulge, cut
[FlowModule(entryState: Editing)] SdfEditor, ContentEditor, SimView
[FlowPanel(kind: RmlDocument | RenderTargetViewport)] LayerList, Viewport3D
```

Template of record: `com.utility.sdf/Runtime/Kernels/SdfCoreKernels.cs` (attributed static members
that Yeroket emits without understanding). The app-flow module is a **new sibling package** mirroring
that, NOT anything inside the kernel-framework.

### 4.2 Generated `AppFlow.g.h` — the injectable state machine

Byte-identical mirror following `SdfOpCodes.g.h`'s shape (pinned explicit enum values, append-only):

- `enum class FlowStateId : uint16_t { Editing=0, ... }`
- `enum class FlowActionId : uint16_t { ToggleLayer=0, ... }`
- `struct FlowTransition { FlowStateId from, to; GuardOpCode guard; }` + a `constexpr` transition table
- **State-schema structs** — a consumer's declared state (`LayerState`, `DocState`) emitted as a
  `[GpuStruct]`-style serializable blob so the engine can **generically snapshot/diff a footprint**
  (this is what makes snapshot-fallback work with no hand-written diff code)
- `struct ActionDecl { FlowActionId id; FootprintMask footprint; InvertOpSeq invert; GroupTag group; }`
  + table
- A **reader** `AppFlowContainerView` (mirrors `RecipeContainerView`) that VIXEN parses the artifact through

The consumer also gets `RegisterGeneratedAppFlow()` (C#) — undertow-side symmetry, like
`RegisterGeneratedActions()`.

**Not in the V1 contract:** arbitrary action logic. An action is `(id, footprint, invert-opseq)` — pure
data. The `native: true` callback flag is reserved but unimplemented.

---

## 5. Runtime data flow

**Load (startup):** App (Tier 3) → `AppFlowLoader.Load(artifact)` → parses `AppFlow.g.h` tables via
the reader → populates the runtime (FSM states+transitions, ActionStack registry, LayerController
layers, ModuleController modules, PanelLayout panels). One ingest, mirroring `RecipePackLoader`.

**A user gesture, end to end** (replacing hand-wired `EditorApplication::Update`):

```
UI click (RmlUi #layer-0-toggle)
  → UISelectionProviderNode drains element selector                     [EXISTING]
  → BindingStore.TryGetForSelector("#layer-0-toggle") → bound action + params   [GENERALIZED from undertow UiBindingTable]
       (bindings were resolved at load: name→action key, param names validated, first-win — undertow's LoadUiBindingsInto algorithm)
  → ActionStack.Begin(group) ... Dispatch(action, params) ... Commit()   [THE half undertow deferred — firing]
       • has invert-opseq?  → record forward + inverse ops on the stack
       • no invert?         → snapshot footprint(LayerState) before, apply, snapshot after
       • apply = execute the action's opcode/state-delta on the declared state
  → state mutated → LayerController flips the layer → drives RenderGraph node enable
  → MessageBus.Publish(AppFlowChangedEvent{changed: LayerState, group})  [EXISTING bus]
  → RmlUi HUD + undertow react (same as SelectionChangedEvent today)      [EXISTING pattern]
```

The selector→binding→action resolution is undertow's, generalized: `BindingStore` is the engine-owned
`UiBindingTable`; the load-time resolution is `LoadUiBindingsInto`. `params` carry through the
declared param signature (undertow's `UiParamSchema[]` → the `[FlowAction]` param contract). VIXEN's
own editor authors a minimal built-in binding set; undertow authors via UTDL `ui_binding` (a
serialization of the same contract) — same store, same resolution, different authoring front-ends.

**Undo/redo:** `ActionStack.Undo()` pops the last **group** (one gesture = one unit), walks its ops
backward (run inverse-opseq, or restore pre-snapshot), republishes `AppFlowChangedEvent`. Redo
re-applies forward. Groups collapse "drag = 40 param sets" into one undo.

**Transitions:** `FSM.Request(to)` → look up `(from→to)` in the table → evaluate the `guard` opcode
against current state → on pass, mutate + fire `AppFlowChangedEvent{stateChanged}`; on fail, return a
typed rejection (no throw). Modules latch via `entryState` — activating a module is itself a transition.

**Panels:** PanelLayout is RmlUi-native — layout is RmlUi documents/RCSS (drag/dock/resize as RML
elements + a layout controller); 3D-content panels embed a `RenderTargetNode`'s image as a panel
surface. Layout persists as a serialized doc and layout changes are undoable actions.

---

## 6. Error handling

Follows VIXEN conventions — result-return, never throw across the host boundary (the
`VulkanGraphApplication::Prepare` rule):

- **Load-time (contract) errors** (unknown enum, transition→missing state, footprint→undeclared
  schema): caught by the generated artifact's own validity (codegen won't emit an invalid table)
  **plus** a runtime `AppFlowLoader` validation pass returning `LoadResult{ok, err}`, like
  `RecipeRegistry::Register`'s coded results and `ValidateProgramOpCodes`.
- **Dispatch-time errors** (action in a forbidding state, guard rejection, undo with empty stack):
  typed enums (`DispatchResult::RejectedByState` / `::GuardFailed` / `::NothingToUndo`), never
  exceptions. App logs + ignores, as `EditorApplication` logs `ApplyDocumentToScene` failures today.
- **Snapshot-fallback safety:** an unserializable footprint is a **codegen-time diagnostic** (like
  `SDFK040/041`), not a runtime surprise — the action fails to *register*, loudly, at load.
- **Boundary safety:** C-ABI loader entry points are `noexcept` and translate internal failure to a
  result code, per the C#↔C++ UB rule already in the codebase.

---

## 7. Testing

Mirrors the SDF-recipe program's proven gate structure (which repeatedly caught runtime bugs static
review missed — see the *live-run-is-authoritative* rule):

1. **Golden-mirror test** — canonical Tier-2 declarations → regenerate `AppFlow.g.h` → assert
   byte-identical to committed golden (a declaration change *must* fail it). Like `SdfOpCodes.g.h`'s
   real-enum-sourced golden.
2. **Runtime unit tests (offline, no GPU)** — ActionStack undo/redo/grouping; FSM transition + guard
   accept/reject; LayerController toggle-as-action reversibility; **snapshot-vs-inverse parity** (an
   action undone via snapshot lands in the same state as the same action given an explicit inverse).
   The `SelectionSet`-style pure-logic tier — the bulk of coverage.
3. **Loader test** — a malformed artifact yields the right `LoadResult` code, not a crash
   (`test_recipe_manifest`-style).
4. **Integration / live gate** — `EditorApplication` re-expressed on AppFlow: click-toggles-layer,
   S-saves, undo reverts the toggle, all through the framework; verified by **actually running**, with
   a PNG/behavioral check that a layer visibly toggles and un-toggles.
5. **Panel gate** — dock/drag/resize a panel, persist layout, reload, assert restored; a
   RenderTarget viewport panel shows 3D content.

References: undertow `[Action]` / `EmitViewContractHeader` codegen tests for tiers 1–3; SDF live gates
for 4–5.

---

## 7b. UI-action consolidation (boundary)

A primary motivation is consolidating the editor's hand-wired UI interactions
(`EditorApplication::Update`'s `DrainClickedElementId`→`ParseLayerToggleId`→`ToggleLayer`, the raw
`glfwGetKey(S)` save poll, the `ConsumeDirty` re-apply) into declared `FlowAction`s dispatched through
the generalized binding store (§7c). The seam is `BindingStore` (the engine-owned generalization of
undertow's `UiBindingTable`): a resolved `selector/event → action + params` mapping replacing the
bespoke per-interaction branches — VIXEN's editor authors a minimal built-in binding set; undertow
authors via UTDL `ui_binding`.

**Boundary — what consolidates vs. what does not:**
- **Consolidates:** the *interaction→action* wiring. UI actions become declared `FlowAction`s dispatched
  through `ActionStack` (so they are undoable/groupable), selected by the `AppFlowInput` table instead
  of hand-written `if`-branches.
- **Does NOT move:** the *input/selection mechanism* — `UISelectionProviderNode`, the RmlUi hit-test,
  `SelectionCoordinator`/`SelectionSet`. AppFlow **consumes** their events; it does not replace them.
  Selection (voxel/UI picking) stays a separate concern; conflating it with app-flow actions would
  recreate the tangle in a new place.

Rule: **input/selection produces events → `AppFlowInput` maps events to declared actions → `ActionStack`
executes them undoably.** Consequence for Inc 1: the editor's layer-toggle is the walking-skeleton's
proof action, so UI-action consolidation is validated by construction in Inc 1 (not a later increment)
and completed (Save, param-set, …) as the vocabulary grows.

## 7c. Generalizing undertow's UI-action system into AppFlow

AppFlow's UI-action layer **is** the generalization of undertow's `Undertow.Sim/UiActions/`. This is
not "compose beside undertow" — it is "lift undertow's proven mechanism up into the engine, complete
the half it deferred, and make undertow consumer #1." Everything undertow got right is preserved
field-for-field; the framework adds firing + reversal.

**Field-by-field generalization** (undertow's outlier → AppFlow's engine-owned equivalent):

| undertow (`Undertow.Sim/UiActions/`) | AppFlow (engine-owned) | Change |
|---|---|---|
| `UiActionRegistry` — actions by namespaced name + `UiParamSchema[]` signature + handler delegate; hand-registered | AppFlow **action registry** — actions declared via the codegen contract, name + **typed param signature** carried in `AppFlow.g.h` | hand-registered → **declared** (any consumer, not just sim) |
| `UiParamType { String, Int, Float, EntityRef }` + `UiParamSchema{name,type}` | AppFlow **param signature** on `[FlowAction]` | promoted to core contract |
| `ui_binding` UTDL (`element`=RML selector, `on`=event, `action`=name, ordered `{name,source}` params) | AppFlow **binding format** — engine-owned, one serialization for all consumers; undertow's `ui_binding` becomes a serialization *of* it | format moves VIXEN-side; UTDL authoring preserved |
| `UiBindingTable` (selector → `BoundUiAction`; first-win; warn-skip inert; never throws) | AppFlow **resolved-binding store** — **same semantics kept verbatim** | none — adopt as-is |
| `ContentLoader.LoadUiBindingsInto` (resolve name→key, `ValidateParams`, first-win `Add`, warn-skip) | AppFlow **loader resolution pipeline** — same algorithm | generalized off `sim`, onto the registry |
| Handlers = fire-and-forget placeholders; "firing is later" | **`ActionStack`** — firing + undo + grouping + reversal | **THE half AppFlow adds** |
| `[Action]` (AI/faction, IAUS-scored) | *unchanged, separate* | NOT a UI/editor command — stays in the sim |

**The resolution algorithm AppFlow adopts wholesale** (undertow's `LoadUiBindingsInto`, proven
deterministic / never-throws / mod-friendly):

```
for each authored binding:
  resolve action name → key against the registry     → warn "unknown action, inert" + skip on miss
  validate param names against the action's signature → warn "unknown param, inert"  + skip on miss
  add (element-selector → bound action + params) to the table, FIRST-WIN (idempotent re-registration)
```

**What AppFlow completes (undertow's deferred half):** the click-time consume seam does not exist in
undertow — its pipeline stops at a resolved `UiBindingTable`; nothing fires. AppFlow supplies:
selector-hit → `TryGetForSelector` → dispatch the bound action **through `ActionStack`** (so it is
undoable + groupable) → publish `AppFlowChangedEvent`.

**Consequences:**

1. **The typed param signature is core to the `[FlowAction]` contract from the start** (not a deferred
   refinement) — because a binding's `{name, source}` params must validate + flow through generically,
   exactly as `UiActionRegistry.ValidateParams` does today.
2. **undertow migrates from owning to consuming:** `Undertow.Sim/UiActions/` is retired into an
   authoring/serialization front-end over the AppFlow contract; the registry + table + resolution move
   engine-side. (Migration is a later increment — see the roadmap — but the contract is shaped for it
   now so undertow is a clean consumer, not a special case.)
3. **`[Action]` (AI/sim) stays distinct and composes:** an AppFlow `FlowAction` MAY emit a sim command;
   they never merge. Two concepts remain — AI-scored sim actions vs. reversible UI/app-flow actions.

## 7d. Render-loop lifecycle consolidation — canonical `graph.Run()` (folded increment)

A sibling concern folded into this program (decision 2026-07-05): consolidate the render-loop
lifecycle so a dispatch entry point does not hand-roll it. This was previously scoped in
`Architecture-Review-Game-Renderer-2026-06-12.md` — "stable engine facade with **host-owned frame
loop**" (§ readiness), "**inverted frame-loop control**" (target-state), "multi-graph/multi-view
lifecycle under one device; clean teardown/re-init", and the named hard liability "**loop ticking
dead in the `RenderFrame()` path**".

**The problem today:** `RenderGraph::RenderFrame()` is the per-frame tick, but the *loop* around it
(build → compile → `glfwPollEvents` → `RenderFrame` → `VK_ERROR_DEVICE_LOST` recovery → shutdown-flag
check) is hand-rolled in `VulkanGraphApplication::Render` and **duplicated across three dispatch entry
points**: `application/main/source/main.cpp`, `application/editor/source/main.cpp`, and the undertow
host. Every entry point re-implements the same lifecycle plumbing and can get it subtly wrong. There
is no canonical lifecycle owner.

**The target:** a canonical run surface on the graph/engine so a dispatch entry point is just a thin
wrapper. Two shapes, both provided:
- `RenderGraph::Run()` (or an `EngineContext::Run()`) — **engine-owned loop**: builds/compiles once,
  then runs the poll→tick→recover→shutdown loop internally until a stop signal, owning its own
  lifecycle. Standalone `main()` becomes `engine.Run();`.
- `RenderGraph::Tick()` — **host-owned loop** (inverted control, for a host like undertow that owns its
  own outer loop / interleaves sim): one iteration of poll→tick→recover, returning a status the host
  loops on. `Run()` is implemented as `while (running) Tick();`.

This subsumes the scattered lifecycle logic (`Prepare`/`Render`/device-loss recovery/shutdown-flag)
into one place, retires the dead loop-ticking path, and makes the three entry points collapse to a
handful of lines each. It composes with AppFlow: the AppFlow runtime's per-frame work (draining UI
selections → dispatching bound actions, consuming `ConsumeDirty`) becomes a registered step inside the
canonical tick, not another hand-wired branch in each app's `Update()`.

**Why it belongs in this program:** AppFlow's whole thesis is "one seam owns app-flow lifecycle so
entry points don't each reinvent it." `graph.Run()` is the *render-loop* half of that same thesis —
the engine-lifecycle sibling of the app-flow-state consolidation. Kept as its own increment (it is a
RenderGraph refactor touching the entry points, orthogonal to the state/action contract), delivered
after the Inc-1 walking skeleton proved the AppFlow spine (`2ab4c534`).

## 8. Scope — V1 vs. deferred

**V1 (this program's first spec + plan):**
- The contract: `[FlowState]`/`[FlowTransition]`/`[FlowAction]` **(with a typed param signature —
  core, per §7c)** /`[FlowLayer]`/`[FlowModule]`/`[FlowPanel]` attributes (Tier 2), emitter →
  `AppFlow.g.h` (Tier 1), the reader.
- VIXEN `AppFlow` runtime: ActionStack (inverse + snapshot-fallback + grouping), FlowStateMachine,
  LayerController, `AppFlowLoader`, `AppFlowChangedEvent` wiring.
- **UI-action generalization (§7c):** the engine-owned action registry (name + param signature),
  `BindingStore` (generalized `UiBindingTable` — first-win, warn-skip-inert), and the load-time
  resolution pipeline (generalized `LoadUiBindingsInto`).
- ModuleController **register/activate** (swap may slip — see below).
- PanelLayout: RmlUi-native dock/drag/resize + RenderTarget viewport panel + persist/restore.
- `EditorApplication` re-expressed on the framework as the live proof.

**Deferred (architect now, build later — append-only extension points reserved in V1):**
- **Render-loop lifecycle consolidation — canonical `graph.Run()`/`Tick()`** (§7d): its own increment
  (a RenderGraph refactor + entry-point dedup, orthogonal to the state/action contract), delivered
  as GraphRun M1–M3 (`2ab4c534`). Previously scoped in Architecture-Review-Game-Renderer-2026-06-12.
- **undertow migration** — retire `Undertow.Sim/UiActions/` into an authoring/serialization front-end
  over the AppFlow contract (undertow's `ui_binding` UTDL → an AppFlow-binding serialization). undertow
  becomes consumer #2; its cross-repo pin + C-ABI wiring make this its own increment (see roadmap Inc 3+).
- **Callback / native actions** (`[FlowAction(native: true)]`) — the modding escape hatch.
- **ModuleController hot-swap** of large modules (register/activate ships in V1; swap may be a later inc).
- Anything requiring the callback boundary (arbitrary consumer logic in-engine).

---

## 9. Resolved decision — Tier-2 home

**Where does Tier 2 (the app-flow vocabulary module) first live? → DECIDED: (a).**

VIXEN ships a **minimal generic reference app-flow module** (states: Editing/Simulating/Paused;
actions: ToggleLayer/SetParam; the editor's 3 layers: base/bulge/cut) so the framework has a live
in-repo consumer and the `EditorApplication` live gate exists **without depending on undertow**.
undertow later extends it as a second consumer, proving the multi-consumer claim by construction.

Rejected alternative (b): undertow authors the only consumer initially, VIXEN ships just runtime +
emitter. Rejected because it defers the live gate onto a cross-repo dependency and leaves the C++
editor with no framework consumer to validate against.
