---
title: View Contract Inc-4 — View→Action Face (AppFlow Convergence) — Design
status: design (approved for planning)
created: 2026-07-07
parent: Renderer-Agnostic-View-Contract-Design-2026-07.md (§7 — the face-4 trajectory this realizes)
related:
  - view-contract-codegen-program
  - appflow-framework-direction
tags: [view-contract, appflow, codegen, input, state-machine, convergence, keychord, input-profile]
---

# View Contract Inc-4 — View→Action Face (the AppFlow Convergence)

**Goal (one sentence):** Make the app-flow **declared state/transition graph** the single-source authoring
surface — states + typed transitions (start→end, action, params, guards, optional effect-ref) generated from
one schema — and route *all* editor input (element clicks + typed key chords) through it, retiring
`ParseLayerToggleId` and the hand-wired `glfwGetKey` action literals, so **no stringly-typed or hand-parsed input
path remains**.

This is **face 4 of 4** of the renderer-agnostic View Contract program
(`Renderer-Agnostic-View-Contract-Design-2026-07.md` §7) — the convergence where the View Contract meets AppFlow:
**AppFlow provides the verbs** (reversible `FlowAction`s, `ActionStack`, `FlowStateMachine`, `DispatchBySelector` —
all shipped), **the `[View]`/AppFlow schema provides the typed triggers** that fire its edges. It resurrects the
shelved AppFlow Inc-3 work (retire `ParseLayerToggleId`, tie into `BindingStore`) — see
[[appflow-framework-direction]] — now expressed through the typed declared graph.

---

## 1. Locked decisions

| # | Decision | Choice |
|---|----------|--------|
| D1 | **Scope** | **Full input→action retire.** Element clicks (`layer-{index}-toggle`) AND keys (`S`, `Ctrl+Z`, `Ctrl+Y`) route through the typed contract. The **editor** is the live in-tree consumer; the existing windowed real-GPU gate proves it. No undertow (Inc-5+). |
| D2 | **Authoring surface = a declared state/transition GRAPH** | The consumer declares states + typed transitions (start→end state, action, typed params, guards, optional effect-ref) + a first-class **`[FlowReturnEdge]`** kind, as autocompletable non-stringy symbols. Built by **extending AppFlow's existing `FlowStateMachine`/`AppFlowReference.cs`** — ONE authoritative graph. A binding is a **trigger on a graph edge**, not a string→action lookup. |
| D3 | **Two trigger kinds; keys are TYPED chords; scope is hierarchical** | (a) **element-click** — compile-time bound to an edge, the `{placeholder}` param **typed on the edge** (dissolves parametric-selector *parsing*). The element *identity* is a **string on the CURRENT dynamic path** (RmlUi keys elements by string id; the trigger reads it dynamically) — this is a deliberate present-state, NOT a permanent limitation: elements gain their own **bake/typed path** as a designed future evolution (§7), symmetric to how keys are baked/typed now. (b) **key** — a **typed `KeyChord{ KeyId key; KeyMod mods }`** (NEVER a string) resolved via a **runtime `InputProfile` registry** that is **hierarchical/scoped** (`global → flow-state → context`, **tightest-declaration-wins**), mutable/rebindable, seeded from schema defaults. `KeyId`/`KeyMod` are generated from the schema. |
| D4 | **Intents stay compile-time; dispatch reuses the shipped spine** | `FlowActionId` (exists). Dispatch flows through the shipped `DispatchBySelector` (elements) + a new `DispatchByKey(KeyChord)` (keys), both landing on the existing `ActionStack` (undoable) — Inc-4 adds **typed resolution in front of** an unchanged dispatch spine. |
| D5 | **Build the real AppFlow emitter** | A Yeroket sibling emitter (`AppFlowEmitter`, like `RecipeContainerEmitter`/`ViewBlobEmitter`) generates `AppFlow.g.h` from `AppFlowReference.cs` — enums + existing decl/transition tables + the NEW element-trigger/key-default/return-edge/`KeyId`/`KeyMod` tables + reader. **Retires the hand-authored mirror + the standing `TODO(appflow-codegen)`.** `--appflow` CLI + wsl-bridged drift-guard. |
| D6 | **Undo vs Return are distinct verbs — and Return is a FIRST-CLASS action, not a key-only concept** | **Undo** = revert a *data change* (the shipped `ActionStack` inverse). **Return** = pop *navigation state* via a **bounded entry-history stack** in `FlowStateMachine` (`RequestReturn`). A settings-menu `Ctrl+Z` undoes the *setting change*; `Esc`/back-button *returns* to the prior state. The graph knows the difference. **Refinement (2026-07-07, user):** `Return` is a first-class `FlowAction` (`FlowAction.Return`), so ANY input surface — a `KeyChord` (`Esc`), an element trigger (a back **button**), a future gamepad/Steam action — dispatches the SAME `Return` action through the SAME dispatch spine (`DispatchByKey`/`DispatchBySelector`). The mechanism (`RequestReturn`) lives ENGINE-side in `Vixen::AppFlow` (not consumer source-gen); the trigger is a binding, not a hardcoded key path. `Return` is dispatched but ROUTES TO `RequestReturn()` (nav-pop), BYPASSING the `ActionStack` — because a nav-pop is not a reversible data mutation (that IS the D6 distinction). `kReturnEdges` becomes "the `Return` action's default per-state key binding"; `Esc` is one default binding to `Return`, not the only way to reach it. |
| D7 | **Effects, rebind-UI, Steam/gamepad DESIGNED not built** | An edge may carry an **effect-ref** (emitted as a table column) but no animation runtime consumes it. `InputProfile` is mutable (rebind *capability*) but there is no settings-UI, no Steam Input adapter, no gamepad this increment. |
| D8 | **Triggers are compositional (forward-principle)** | A trigger is `{ primary input } + { a composable SET of typed qualifiers }`. Inc-4 ships exactly ONE qualifier kind — `KeyMod` — but the resolver treats qualifiers as a composable set, not a fixed field, so **timing (double-click min/max, long-press), sequence, and repeat qualifiers can be added later as new modifier kinds with no resolver rewrite** (mirrors Yeroket's `ConnectionModifier` composition). Non-foreclosed, not built. |
| D9 | **Proof** | Reuse the live windowed real-GPU gate (`test_editor_toggle_undo_capture`): element-click→pattern→toggle, scoped key-chord→undo (byte-exact), and a return-edge pop — plus offline C# emitter tests + C++ runtime unit tests. |

---

## 2. Architecture & the convergence seam

```
AppFlowReference.cs  (ONE authoritative schema)
   │  states / guards / actions / param-sigs / transitions (existing)
   │  + NEW: KeyId + KeyMod enums; element-triggers; key-defaults (scoped, typed KeyChord);
   │         return-edges; edge effect-ref column
   │ (NEW Yeroket AppFlowEmitter — sibling of RecipeContainerEmitter/ViewBlobEmitter)
   ▼
AppFlow.g.h  (GENERATED — retires the hand-authored mirror + TODO(appflow-codegen))
   │  enums (FlowState/Guard/Action/ParamType + KeyId/KeyMod) + struct KeyChord
   │  kActionDecls + kTransitions (existing, byte-equivalent to today)
   │  + kElementTriggers  (pattern → action + typed param name)
   │  + kKeyDefaults      ({action, KeyChord, scope, state})
   │  + kReturnEdges      (from-state + trigger KeyChord)
   │  + edge effect-ref column (emitted, unused — designed not run)
   ▼
┌──────────────────────────── VIXEN runtime (libraries/AppFlow) ────────────────────────────┐
│  BindingStore   (extended: parametric pattern match "layer-{index}" → typed param)          │
│  InputProfile   (NEW: KeyChord→action, HIERARCHICAL global→state→context, tightest-wins)    │
│  FlowStateMachine (extended: bounded entry-history stack; RequestReturn())                  │
│  AppFlowRuntime : DispatchBySelector (shipped) + DispatchByKey(KeyChord) (NEW)              │
└──────────────────────────────────────────────────────────────────────────────────────────┘
   ▲ element click                              ▲ key press
   │ (DrainClickedElementId → selector string)  │ (glfw keycode → KeyId via one guarded map → KeyChord)
   │                                            │
EditorApplication  (RETIRE: ParseLayerToggleId, glfwGetKey S/Ctrl+Z/Ctrl+Y action literals)
```

**New / reused / retired:**

- **New (Yeroket C#):** `AppFlowEmitter` (generates all of `AppFlow.g.h`); the new `[Flow*]` schema attributes
  (`[FlowKeyEnum]`, `[FlowModEnum]`, `[FlowElementTrigger]`, `[FlowKeyDefault]`, `[FlowReturnEdge]`,
  `[FlowEdgeEffect]`); a `--appflow` CLI branch.
- **New (VIXEN C++):** `KeyChord`/`KeyId`/`KeyMod` (generated into `AppFlow.g.h`); the `InputProfile` registry;
  `BindingStore` parametric-pattern matching; `FlowStateMachine` entry-history + `RequestReturn`;
  `AppFlowRuntime::DispatchByKey`; a wsl-bridged `appflow_check` drift-guard; the glfw→`KeyId` map (host-side, the
  one raw-hardware→typed translation, completeness-guarded).
- **Reused unchanged:** `ActionStack`, `DispatchBySelector`, `AppFlowRuntime::ToggleLayer/Undo/Redo`, the editor's
  `dirty_` re-flatten tail, `DrainClickedElementId` (selection subsystem — stays put), the windowed capture harness.
- **Retired (deleted):** `ParseLayerToggleId` (the bespoke `stoi` parse fn); the editor's inline `glfwGetKey`
  action literals (S-save, Ctrl+Z, Ctrl+Y); the hand-authored `AppFlow.g.h`; `TODO(appflow-codegen)`.

**The join / correctness argument:** the graph is declared once (`AppFlowReference.cs`), generated once
(`AppFlow.g.h`), and *both* trigger surfaces (element table compile-time; key defaults seeding the runtime profile)
derive from it — an element, a key chord, and the action they fire all reference the **same generated symbol**. No
string names an action twice; keys carry no strings at all.

**Boundary discipline:** cross-repo (VIXEN worktree + Yeroket in-place, per every prior increment). No undertow.
The editor is the live consumer; the existing windowed real-GPU gate proves click/key→transition→toggle/undo
unchanged after the retire.

---

## 3. The extended schema + AppFlow emitter (face 4 codegen)

### 3.1 Schema extension (`AppFlowReference.cs`)

Existing declarations (states/guards/actions/param-sig/transitions) stay verbatim. New declarations, same
data-as-C# idiom:

```csharp
// Typed key vocabulary — generated, referenceable, autocompletable (NOT strings).
[FlowKeyEnum] public enum KeyId : ushort { None = 0, A, /*…*/, Z, S, Escape, /*…as needed*/ }
[FlowModEnum] public enum KeyMod : byte  { None = 0, Ctrl = 1, Shift = 2, Alt = 4, Super = 8 }  // bitflags

// New FlowActions (append-only, pinned) — today the enum has only ToggleLayer.
// FlowAction { ToggleLayer = 0, Undo = 1, Redo = 2, Save = 3, UndoSettingChange = 4 }

// Element trigger — a clicked element pattern fires an action; {placeholder} → typed param.
[FlowElementTrigger(action: nameof(FlowAction.ToggleLayer))]
public static class ToggleLayerTrigger {
    public const string Element = "layer-{index}-toggle";  // {index} → the typed edge param
    public const string ParamName = "layerIndex";           // maps to the action's declared Int param
    public const string On = "click";
}

// Key default — SCOPED. Global unless a tighter state/context override is declared. KeyChord is typed.
[FlowKeyDefault(action: nameof(FlowAction.Undo), scope: FlowScope.Global)]
public static class UndoKey { public const KeyId Key = KeyId.Z; public const KeyMod Mods = KeyMod.Ctrl; }
// Tighter override — same chord, different action, only in Settings context:
[FlowKeyDefault(action: nameof(FlowAction.UndoSettingChange), scope: FlowScope.State, state: nameof(FlowState.Settings))]
public static class SettingsUndoOverride { public const KeyId Key = KeyId.Z; public const KeyMod Mods = KeyMod.Ctrl; }
// (Save → {S, None}, Redo → {Y, Ctrl} declared likewise, Global scope.)

// Return edge — from a state, pop to whatever state we entered it from. Trigger is a typed chord.
[FlowReturnEdge(from: nameof(FlowState.Settings))]
public static class SettingsReturn { public const KeyId Key = KeyId.Escape; public const KeyMod Mods = KeyMod.None; }

// Optional edge effect-ref (designed, NOT run) — emitted as a table column; no runtime consumes it.
[FlowEdgeEffect(transition: 0)] public static class T0Effect { public const string Effect = "none"; }
```

### 3.2 Generated tables (`AppFlow.g.h`)

Everything the hand-authored file has today (enums, `kActionDecls`, `kTransitions`, param schemas,
`AppFlowContainerView`), **byte-equivalent for the unchanged parts**, plus:

```cpp
enum class KeyId  : uint16_t { None=0, A, /*…*/, Z, S, Escape, /*…*/ };
enum class KeyMod : uint8_t  { None=0, Ctrl=1, Shift=2, Alt=4, Super=8 };
struct KeyChord { KeyId key; KeyMod mods; };   // typed, comparable, hashable — no string

enum class FlowScope : uint8_t { Global=0, State=1, Context=2 };  // widening order; Context tightest

struct AppFlowElementTrigger { const char* elementPattern; FlowActionId action; const char* paramName; const char* on; };
struct AppFlowKeyDefault     { FlowActionId action; KeyChord chord; FlowScope scope; FlowStateId state; };
struct AppFlowReturnEdge     { FlowStateId from; KeyChord trigger; };

inline constexpr AppFlowElementTrigger kElementTriggers[] = { … };
inline constexpr AppFlowKeyDefault     kKeyDefaults[]     = { … };
inline constexpr AppFlowReturnEdge     kReturnEdges[]     = { … };
// AppFlowTransition gains a `const char* effect;` column (designed, unused).
```

`AppFlowContainerView` gains `elementTriggers()` / `keyDefaults()` / `returnEdges()` accessors.

### 3.3 The emitter (`AppFlowEmitter`, Yeroket `Transpiler/`)

Sibling of `RecipeContainerEmitter` — reads the `[Flow*]`-attributed symbols of `AppFlowReference.cs` via Roslyn,
emits the full `AppFlow.g.h`. Follows the established pattern: enum→underlying-type mapping, values read from C#
consts (never hand-typed), `.Replace("\r\n","\n")` normalization. Delivered via a `--appflow` CLI branch (mirrors
`--view-blob`/`--recipe-container`) + a wsl-bridged `appflow_check`/`appflow_regen` drift-guard pair (the KI-015
`_CODEGEN_RUNNER` + `_codegen_to_wsl_path` machinery, like the existing pairs).

**Emitter-first retire discipline:** the emitter's FIRST deliverable is generating the **existing** `AppFlow.g.h`
(before the new tables) and proving it byte-equivalent to today's hand-authored file — so the hand-mirror is retired
against a known-good baseline BEFORE any new table rides on it. Then §3.1's extensions are added on the verified
baseline.

---

## 4. The runtime

### 4.1 `InputProfile` — hierarchical, tightest-wins (new, `libraries/AppFlow`)

```cpp
// A scoped KeyChord→action table. Resolution walks scopes tightest→widest and returns the first
// match. Seeded from AppFlow.g.h's kKeyDefaults at Load(). Mutable → the (deferred) rebind/Steam seam.
class InputProfile {
public:
    void Bind(FlowScope scope, FlowStateId state /*or any for Global*/, KeyChord chord, FlowActionId action);
    // Resolve under the active flow-state: Context override → State override → Global. false if unbound.
    bool Resolve(KeyChord chord, FlowStateId active, FlowActionId& out) const;
};
```

`Ctrl+Z` seeds once at `Global → Undo`; a `State(Settings) → UndoSettingChange` entry wins ONLY while
`active == Settings`. Everywhere else Global applies — **zero re-declaration**. `KeyChord` is comparable/hashable
(the map key). **Forward-principle (D8):** qualifiers are a composable set — `KeyMod` is the only kind Inc-4 ships,
but the resolution shape does not hard-code it as the *only* qualifier a chord can carry, so timing/sequence
qualifiers slot in later without a resolver rewrite.

### 4.2 `BindingStore` parametric pattern matching (extend shipped `BindingStore`)

```cpp
void AddElementTrigger(const AppFlowElementTrigger& trig);   // register "layer-{index}-toggle" from kElementTriggers
```

`TryGetForSelector("layer-2-toggle")` now ALSO matches patterns and extracts the `{placeholder}` into the resolved
`BoundAction`'s typed params (`layerIndex = 2`, carried as the action's declared `FlowParamSchema` Int). Exact
bindings still win over patterns (first-win preserved). The extraction lives HERE — generic + tested — replacing
`ParseLayerToggleId`'s bespoke `stoi`.

### 4.3 `FlowStateMachine` entry-history stack (extend shipped FSM)

```cpp
// Bounded stack (cap 16) of prior states. RequestState pushes the state being left; RequestReturn()
// pops and transitions back to it. Bounded → oldest dropped. Empty-history return = logged no-op (not a crash).
DispatchResult RequestReturn();
```

**Return** is a distinct verb from **Undo**: `RequestReturn` pops *navigation* history; `ActionStack::Undo` reverts
*data*. `Return` is a first-class `FlowAction` (D6): a `Return` binding (a `KeyChord` like `Esc`, OR a back **button**
element trigger) resolves to `FlowActionId::Return`, and the runtime ROUTES `Return` to `RequestReturn()` —
NOT to the `ActionStack`. This keeps the nav/data separation while unifying the trigger surface: every input
face names the same action.

### 4.4 `AppFlowRuntime` dispatch (extend shipped runtime)

- **Element click** → `DispatchBySelector(clickedId)` (shipped) — now resolves pattern triggers + typed params.
- **Key press** → `DispatchByKey(KeyChord)` (new) → `InputProfile::Resolve(chord, fsm_.Current(), action)`.
- **Action routing (shared by both surfaces):** the resolved `FlowActionId` routes by KIND — `Return` → `RequestReturn()`
  (nav-pop, no `ActionStack` entry); every other action → `DispatchAction` → `ActionStack` (undoable). A single
  private `RouteAction(FlowActionId, apply)` helper does this so a key and a button reaching `Return` behave identically.

Data actions flow through the existing `ActionStack` (undoable); `Return` bypasses it (nav-pop) — Inc-4 changes nothing
else downstream of resolution.

### 4.5 Seeding at `Load()`

`AppFlowRuntime::Load()` (already populates the action table) additionally: registers `kElementTriggers` into
`BindingStore`, seeds `kKeyDefaults` into the default `InputProfile` (by scope), and seeds `kReturnEdges` into the
`InputProfile` as `Return`-action key bindings (`{FlowActionId::Return, edge.trigger, FlowScope::State, edge.from}`) —
so `Esc` in a return-edge state resolves to `Return` through the same `InputProfile::Resolve` path as any other key.
Load-order invariant preserved (table populated before any dispatch).

---

## 5. Editor retire & error handling

### 5.1 What gets deleted (`EditorApplication`)

| Deleted | Replaced by |
|---------|-------------|
| `ParseLayerToggleId()` (bespoke `stoi` parse, `.cpp:28-40`) | pattern match in `BindingStore` (§4.2) |
| click drain → `ParseLayerToggleId` → `ToggleLayer` (`.cpp:356-364`) | `rt_.DispatchBySelector(clickedId)` — zero parsing |
| `glfwGetKey` S-save, edge-detected (`.cpp:367-374`) | `rt_.DispatchByKey({KeyId::S, KeyMod::None})` |
| `glfwGetKey` Ctrl+Z / Ctrl+Y, edge-detected (`.cpp:379-386`) | `rt_.DispatchByKey({KeyId::Z, KeyMod::Ctrl}` / `{KeyId::Y, KeyMod::Ctrl})` |

**Stays untouched:** the click *drain* (`DrainClickedElementId` — selection subsystem's job); the `dirty_`
re-flatten tail; the capture/script harness; the try/catch backstop. The editor still **edge-detects** presses
(press-not-held) — but feeds the edge-detected chord to `DispatchByKey` instead of hardcoding the action. The
handlers (`rt_.Undo()`/`rt_.Redo()`/`SaveDocument()`) are unchanged — the *binding* is retired, not the *handler*.

### 5.2 The one raw boundary — glfw→`KeyId` map

`DispatchByKey` takes a typed `KeyChord`. The editor builds it from its existing `glfwGetKey` edge-detection via a
single generic **glfw-keycode → `KeyId`** lookup (not per-action `if`s), and reads the modifier keys into a `KeyMod`
mask. This is the SOLE point a raw hardware keycode becomes typed — unavoidable (the OS gives an int), correct, and
guarded by a **completeness check** (every editor-used glfw key maps to a non-`None` `KeyId`; a gap is caught, not
silent). No key string exists anywhere. (A future gamepad/Steam adapter produces `KeyChord`s/`InputId`s the same
way — the deferred portability seam.)

**Element identity — current dynamic-string path, future bake path (deferred).** Unlike keys, the *element*
identity (`"layer-2-toggle"`) is a **string on the current dynamic path** — the trigger reads the RmlUi element id
dynamically at click time, and RmlUi keys elements by string. This is a deliberate present-state, not an inherent
limitation: elements are slated to get their own **bake/typed path** (symmetric to how the SDF recipe format and the
View Contract's own blob have a baked form) as a future evolution — at which point the element→edge binding becomes a
baked typed reference like the key chord. Inc-4 keeps the dynamic-string element path and does NOT build the element
bake path (§7); the `{placeholder}`→param *extraction* is already typed (§4.2), so the only remaining string is the
element identity itself, on its documented dynamic path.

### 5.3 Error handling (mirrors the program's "surface, don't swallow")

- **Unknown element / no binding** → `DispatchBySelector` → `RejectedByState` (shipped), logged; click is a no-op.
- **Key chord unbound in every scope** → `InputProfile::Resolve` false → `DispatchByKey` logged no-op.
- **Return with empty history** → `RequestReturn` logs + no-ops (§4.3) — never underflows.
- **Malformed schema trigger** (unknown action/param symbol) → C# compile error in the emitter; an unresolvable
  binding at Load is warn-skip-inert (shipped `AddBinding` discipline) — dropped+logged, never a wrong dispatch.
- **Drift** → `appflow_check` fails the build if `AppFlow.g.h` doesn't match the schema.

### 5.4 The `dirty_` invariant is preserved

Every retired path ended by setting `dirty_` (directly or via the toggle's onChanged) → the re-flatten tail
re-applies next tick. `DispatchBySelector`/`DispatchByKey` route to the SAME `ToggleLayer(onChanged=[]{dirty_=true})`
/ `Undo`/`Redo` methods, so the tail fires identically. The byte-exact windowed gate proves this.

---

## 6. Testing strategy

### 6.1 Offline C# (NUnit, Yeroket `CodegenTool~/Tests/`)

- **`AppFlowEmitterTests`** — the retire proof: generate `AppFlow.g.h` from `AppFlowReference.cs` and assert (a) for
  the *unchanged* declarations it is **byte-equivalent to a golden transcribed from today's hand-authored file**
  (faithful retire); (b) the new tables emit correctly — `kElementTriggers`, `kKeyDefaults`
  (`{action, KeyChord{KeyId,KeyMod}, scope, state}`), `kReturnEdges`, the `KeyId`/`KeyMod` enums + `KeyChord`/
  `FlowScope`. Same style as `ViewBlobEmitterTests`.

### 6.2 Offline C++ (gtest, `libraries/AppFlow/tests/`)

- **`test_input_profile`** — hierarchical resolution: seed `{Z,Ctrl}→Undo` Global + `{Z,Ctrl}→UndoSettingChange`
  State(Settings); assert `Resolve({Z,Ctrl}, Editing)→Undo` and `Resolve({Z,Ctrl}, Settings)→UndoSettingChange`
  (**tightest-wins, non-vacuous** — same chord resolves differently by state). Unbound chord → false.
- **`test_binding_pattern`** — `BindingStore` pattern match: register `"layer-{index}-toggle"`;
  `TryGetForSelector("layer-2-toggle")` → `ToggleLayer` with typed `layerIndex=2`; non-matching id → no binding;
  exact binding beats a pattern.
- **`test_flow_return`** — entry-history stack: `A→B→C`, `RequestReturn` pops to B then A; empty-history return →
  logged no-op (no underflow); bounded cap drops oldest.
- **`test_keychord`** — `KeyChord` equality/hash; `KeyMod` bitmask composition (`Ctrl|Shift` order-independent); the
  glfw→`KeyId` map **completeness guard** (every editor-used glfw key → non-`None` `KeyId`).

### 6.3 Authoritative live gate (real GPU, windowed) — reuse `test_editor_toggle_undo_capture`

Extend the existing windowed capture harness (`VIXEN_EDITOR_SCRIPT`, real D3D12/dzn) to prove the retired path
end-to-end:

- **Element click → toggle:** scripted `toggle:2` drives a synthesized click on `layer-2-toggle` →
  `DispatchBySelector` → pattern-match → `ToggleLayer(2)`; capture asserts a positive toggle delta vs baseline
  (Inc-2b byte-exact discipline: any positive pixel count is real signal, a broken/no-op toggle → EXACTLY 0px). The
  concrete threshold is **calibrated LIVE in the plan against the actual editor camera**, NOT copied — Inc-2b's own
  gotcha was that the off-axis editor camera yields a small silhouette-edge delta (~6px there) that must be measured,
  not assumed (see [[appflow-framework-direction]]).
- **Scoped key → undo:** `{KeyId::Z, KeyMod::Ctrl}` resolves through the Global `InputProfile` → `Undo`; the
  undo-capture == baseline BYTE-EXACT (the gate's core assertion, now proving the *typed* path).
- **Return edge:** requires a minimal editor sub-state to be a genuine home for the scoped override + return (the
  editor today has only Editing/Simulating/Paused — the plan adds a minimal reachable sub-state, or the harness
  drives the FSM directly and asserts the popped `FlowStateId`). Enter → return trigger → `RequestReturn` pops to
  the prior state (asserted via `FlowStateId`, plus a capture if it has a visible effect).

The byte-exact PNG assertions are the backstop: a broken retire (wrong param extraction, wrong scope resolution, a
dropped `dirty_`) shifts or blanks the captures. This is the program's "live-run gate is authoritative for GPU work"
rule (see [[live-verification-authoritative-for-gpu-work]]).

### 6.4 No-regression

Full AppFlow offline suite (Inc-1/2/2b, ~27 tests) + View Contract suites stay green. The `appflow_check`
drift-guard (new, wsl-bridged) joins the existing pairs. `test_editor_toggle_undo_capture`'s *original* assertions
(pre-retire behavior) must still hold — proving behavior-neutrality.

---

## 7. Out of scope (this increment / deferred within the program)

- **Inc-4 does NOT build:** the **effect/animation runtime** on edges (the effect-ref column emits but nothing
  consumes it); the **rebinding settings UI**; the **Steam Input action-set adapter**; **gamepad** input; additional
  **qualifier kinds** beyond `KeyMod` (timing/double-click/long-press/sequence — D8: non-foreclosed, not built); the
  **element bake/typed path** (element identity stays a dynamic-read string this increment — §5.2; the baked/typed
  element reference is a designed future evolution, symmetric to keys); the **undertow migration** (Inc-5+).
- **Committed architecture (enables the deferred payoff):** intents = compile-time `FlowActionId`; element→action =
  compile-time table; key→action = runtime hierarchical `InputProfile` of typed `KeyChord`s keyed by flow-state;
  triggers = primary input + a composable typed-qualifier set; return = navigation history distinct from data-undo;
  the whole graph generated from one schema. Each deferred item is a bolt-on onto this shape, not a rewrite.

---

## 8. Ground-truth references (read before planning)

**AppFlow (VIXEN) — the schema + runtime being extended:**
- `codegen/appflow-schemas/AppFlowReference.cs` — the canonical vocabulary (`[FlowStateEnum]`/`[FlowActionEnum]`/
  `[FlowTransition]`/`[FlowActionParams]`); extend with the new `[Flow*]` attributes. Carries the `TODO(appflow-codegen)`.
- `libraries/AppFlow/include/generated/AppFlow.g.h` — the HAND-AUTHORED mirror to RETIRE (enums + `kActionDecls` +
  `kTransitions` + `AppFlowContainerView`); the golden baseline the emitter must reproduce byte-equivalent first.
- `libraries/AppFlow/include/BindingStore.h` + `src/BindingStore.cpp` — extend with pattern matching + param
  extraction (`AddBinding`/`TryGetForSelector`/`ValidateParams`, exact-string map today).
- `libraries/AppFlow/include/AppFlowRuntime.h` — `DispatchBySelector` (reuse) + add `DispatchByKey`; `Load()` seeding;
  `ToggleLayer`/`Undo`/`Redo` (reuse).
- `libraries/AppFlow/include/FlowStateMachine.h` — extend with entry-history + `RequestReturn`.
- `libraries/AppFlow/tests/` — NUnit-style-in-gtest siblings (`test_binding_store`, `test_flow_state_machine`) to
  mirror for the new unit tests.

**Editor — the live consumer / retire target:**
- `application/editor/source/EditorApplication.cpp` — `ParseLayerToggleId` (`:28-40`) to DELETE; the input block
  (`:355-386`) to rewrite (click drain → DispatchBySelector; glfw keys → DispatchByKey(KeyChord)); the `dirty_`
  re-flatten tail (`:393-398`) + capture harness (`:406-413`) STAY.
- `libraries/RenderGraph/assets/ui/editor.rml` — the `layer-{index}-toggle` elements (`:14,19,24`) the pattern binds.
- `libraries/RenderGraph/tests/Nodes/test_editor_toggle_undo_capture.cpp` — the windowed real-GPU gate to extend.
- `libraries/RenderGraph/include/Nodes/UISelectionProviderNode.h` — `DrainClickedElementId` (stays put).

**Yeroket — the emitter neighborhood:**
- `$KF/SourceGenerator~/Transpiler/RecipeContainerEmitter.cs` — the sibling emitter pattern to follow (symbol
  reflection, `MapToCppType`, consts-not-hand-typed, provenance banner).
- `$KF/SourceGenerator~/Transpiler/ViewBlobEmitter.cs` — the other sibling (dedup collect, `.Replace("\r\n","\n")`).
- `$KF/CodegenTool~/Program.cs` — the CLI branch structure to extend with `--appflow`.
- `$KF/CodegenTool~/Tests/` — the NUnit test shape.
  (`$KF = /home/liory/Github/Yeroket-Fantasy/Packages/com.yeroket.utility.kernel-framework`)

**Program docs:**
- `Renderer-Agnostic-View-Contract-Design-2026-07.md` §7 (the face-4 trajectory this realizes), §3 (the four faces).
- `AppFlow-Framework-Design-2026-07.md` + the Inc-1/2/2b designs (the shipped runtime this extends).
- Build/drift-guard machinery: `codegen/CMakeLists.txt` (the wsl-bridged `*_check`/`*_regen` pairs to mirror for
  `appflow_check`).
