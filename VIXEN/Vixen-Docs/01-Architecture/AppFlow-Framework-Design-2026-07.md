# AppFlow Framework — Design

**Date:** 2026-07-05
**Status:** Design (approved via brainstorm; awaiting spec review → writing-plans)
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

**Goal:** a framework for app-flow management — reversible actions with grouping, an app-flow state
machine, layer control, module control, and panel/layout composition — that **external consumers
latch into** through VIXEN's already-proven cross-consumer contract mechanism (the Yeroket
single-source codegen used for SDF op-codes and `[GpuStruct]` config), so the UI system interacts
with app state through one seam instead of N bespoke wirings.

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
  → UISelectionProviderNode drains element id                          [EXISTING]
  → AppFlowInput maps "#layer-0-toggle" → FlowActionId::ToggleLayer     [NEW: table lookup, no string parse]
  → ActionStack.Begin(group) ... Dispatch(ToggleLayer, args) ... Commit()
       • has invert-opseq?  → record forward + inverse ops on the stack
       • no invert?         → snapshot footprint(LayerState) before, apply, snapshot after
       • apply = execute the action's opcode/state-delta on the declared state
  → state mutated → LayerController flips the layer → drives RenderGraph node enable
  → MessageBus.Publish(AppFlowChangedEvent{changed: LayerState, group})  [EXISTING bus]
  → RmlUi HUD + undertow react (same as SelectionChangedEvent today)      [EXISTING pattern]
```

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
`glfwGetKey(S)` save poll, the `ConsumeDirty` re-apply) into declared `FlowAction`s. The seam is
`AppFlowInput`: a **declared mapping table** (`"#layer-0-toggle" → FlowActionId::ToggleLayer`,
`KEY_S → FlowActionId::Save`) replacing the bespoke per-interaction branches.

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

## 7c. Prior art — undertow's existing UI-action layer (alignment)

undertow already ships a working UI-action layer (`Undertow.Sim/UiActions/`). AppFlow must **compose
with it, not duplicate it**:

- **`UiActionRegistry`** — source of truth for "what UI actions exist", keyed by **namespaced name**
  (`core:move-haul`) with a **typed param signature** (`UiParamSchema{name, UiParamType}`) + a handler
  delegate. Hand-registered, not attribute-declared.
- **`ui_binding`** (authored UTDL doc, parsed by `UiBindingParser`) — wires an **RML selector** → an
  action **name** + `on` event (default `click`) + an ordered `{name, source}` param list read from the
  DOM at click time.
- **`UiBindingTable`** — resolved/validated bindings keyed by selector; unknown-action/bad-param
  bindings are warn-skipped (inert). First-win, idempotent, never throws.
- **`[Action]`** (separate seam) — the sim/faction AI-scored action system. NOT a UI/editor command.

**Three consequences for AppFlow:**

1. **AppFlow is the reversible *execution* layer beneath `ui_binding`, not a replacement for it.**
   undertow already solved "RML selector → named action + typed params" as authored data (better than a
   compiled mapping table — it's moddable UTDL). What that layer LACKS is undo/grouping/reversal — its
   handlers are fire-and-forget delegates (the code notes handlers are placeholders, firing is "later").
   The seam: **`ui_binding` resolves selector→action+params → dispatches into AppFlow's `ActionStack`**,
   which supplies reversibility + grouping. AppFlow owns undo/state; undertow's layer owns
   authoring/binding/params. For undertow, `AppFlowInput` is *undertow's `UiBindingTable`* — AppFlow
   consumes its resolved bindings rather than shipping a rival table. (VIXEN's own editor, lacking UTDL,
   uses a minimal built-in mapping — but the contract is the same: selector/event → action + params.)

2. **The action contract must carry a typed param signature (like `UiParamSchema[]`), not bare enum ids.**
   Inc-1's param-less `ToggleLayer` enum is a skeleton; the real `[FlowAction]` grows a declared param
   signature so a `ui_binding`'s `{name, source}` params flow through generically. Deferred to the
   increment that wires undertow (not Inc 1), but the contract is shaped for it now.

3. **Three distinct "action" concepts stay distinct and compose:** `[Action]` (AI/sim), `UiAction`
   (UI→command binding), `FlowAction` (app-flow/editor command **with reversal**). A `UiAction` handler
   MAY dispatch a `FlowAction` (to gain undo); a `FlowAction` MAY emit a sim command. They compose; they
   never merge.

## 8. Scope — V1 vs. deferred

**V1 (this program's first spec + plan):**
- The contract: `[FlowState]`/`[FlowTransition]`/`[FlowAction]`/`[FlowLayer]`/`[FlowModule]`/`[FlowPanel]`
  attributes (Tier 2), Yeroket emitter → `AppFlow.g.h` (Tier 1), the reader.
- VIXEN `AppFlow` runtime: ActionStack (inverse + snapshot-fallback + grouping), FlowStateMachine,
  LayerController, `AppFlowLoader`, `AppFlowChangedEvent` wiring, `AppFlowInput` mapping.
- ModuleController **register/activate** (swap may slip — see below).
- PanelLayout: RmlUi-native dock/drag/resize + RenderTarget viewport panel + persist/restore.
- `EditorApplication` re-expressed on the framework as the live proof.

**Deferred (architect now, build later — append-only extension points reserved in V1):**
- **Callback / native actions** (`[FlowAction(native: true)]`) — the modding escape hatch.
- **ModuleController hot-swap** of large modules (register/activate ships in V1; swap may be Inc 2).
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
