# View↔Model Binding Framework — Design Note (2026-07-10)

**Status:** DESIGN (Phase 0 — written before building, per user 2026-07-10). This is the coherent
whole against which increments are cut. It is the program-level design for goal **#2** ("view-management
actions source-generated + injectable") reframed by the user into a full **bidirectional MVVM-over-ECS
binding framework**. Goal **#3** (de-harden undertow's action set) consumes the result.

**Lineage:** extends [[view-contract-codegen-program]] (the `[View]` single-source codegen: Inc-1..3
data/renderer/upload faces + Inc-4 kernel-glue-transplant reframe D10–D16) and the just-shipped
`IViewDataProvider` accessor-seam (`View-Data-Provider-Seam-Design-2026-07.md`). Grounded in the
Gaia v0.9.2 observable investigation (verified against tag `v0.9.2` / `f2ea77a`).

---

## 1. The vision (user, 2026-07-10) — seven capabilities

> "what kind of ui and view interactions should be automated … for a base data presentor represented by
> the view V: entering data to the view flows back to the model, ideally through an event chain, to
> mutate the model state; when the model itself changes, the ui element should automatically update
> itself to represent the model. These setups need to be overrideable. There needs to be a clear
> instance identity the view is linked to, and in the case of multiple views, a query selection with the
> constraints node or the ecs pattern with an index for determining specific instance handling to
> resolve the action. We should also enable a set mutation (e.g. hiring 10 selected characters from an
> option list). By combining view, flow and kernel callables we should be able to assemble complete app
> state + ui — but it requires making state management manageable, with tooling and a ux api to make the
> increase in state non-exponential and with clear logic."

Restated as capabilities:
1. **Bidirectional binding** — view→model on input; model→view auto-update on change. Both automated.
2. **Overrideable** — every automated binding replaceable with custom logic.
3. **Instance identity** — a view is bound to a clear instance (single-instance = simple case).
4. **Multi-instance selection** — resolve *which* instance(s) an action targets via a query (ECS + index).
5. **Set mutation** — an action over a *selection set* (N instances at once).
6. **Assembly** — View + Flow + Kernel-callables compose into complete app state + UI from declarations.
7. **Non-exponential state** — tooling + a UX API keep total state growth linear and the logic clear.

---

## 2. The spine: the entity is identity + change + selection

The Gaia investigation makes the ECS entity the single spine of the whole framework:
- **Identity (cap 3)** = the Gaia entity a view is bound to.
- **Change (cap 1 model→view)** = Gaia's per-component change detection on that entity.
- **Selection (cap 4/5)** = a Gaia query yielding a set of entities; a runtime **tag component** makes
  "the selected set" itself data.

All three come from one primitive. This is why the framework is ECS-native rather than a bespoke
event system.

## 3. Gaia v0.9.2 mechanisms (verified) — the two the design uses

- **`func_set` hook** — real push callback, one **global slot per component type per World**, fires
  **synchronously inside `view_mut` mid-write**, **chunk-granular** (gives `World`, `ComponentRecord`,
  `Chunk` — NOT entity/row/value). Re-entrancy hazard: NEVER touch RmlUi inside it.
- **`.changed<T>()` version query** — pull/batched, **chunk-granular per-component**, needs a
  **persistent per-view Query object** (fresh query = version 0 = matches everything; the query stores
  its last-seen world version and updates it each run).
- **Both are fed automatically by the ordinary `set<T>` / mutable-ref write** (bumps version AND fires
  the hook — `chunk::set`→`view_mut_inter<T,true>`). The **silent** `sset`/`sview_mut` path skips both.

## 4. Model→view — DECISION: per-frame `.changed<T>()` reconcile default; hook = opt-in dirty-signal

(User decision 2026-07-10.) The default view-update driver is a **per-frame `.changed<T>()` reconcile**,
because it is batched, main-thread, side-effect-free, and cannot re-enter a write:

```
each frame, per bound view (holding a persistent Query):
    for chunk in view.query.changed<BoundComponents...>().each(Iter):
        for entity in chunk (re-verify per-entity — chunk-granular is coarse):
            pull changed noun value(s) from the component
            write into the view's backing store (ViewStore / IViewDataProvider)
            RmlUi: DirtyVariable(noun)     # forward "what changed" into RmlUi's own dirty tracking
```
- **The `func_set` hook is NOT the primary driver.** It is available as an **opt-in immediacy signal**:
  a view may register (via a shared per-component **dispatcher** hook, since the slot is global) to set
  a dirty flag the reconcile consumes the same frame. The hook NEVER does RmlUi work inline
  (re-entrancy). Most views won't need it; the per-frame reconcile is enough.
- **RmlUi boundary:** Gaia supplies "what changed"; RmlUi's `DataModel::DirtyVariable`/`DirtyAllVariables`
  supplies "tell the UI." Keep them distinct — forward changes into `DirtyVariable`, never mutate RmlUi
  state hoping it notices.

## 5. View→model — the generated flow-action path (extends the shipped seam)

View→model is the source-generated handler path from the Inc-4 reframe, now targeting the
`IViewDataProvider` seam:
```
UI input (click/key) -> DispatchBySelector/ByKey -> handler_[id](params)
    handler (GENERATED): resolve instance (§6) -> provider.WriteU32(ViewNounKey{noun,instance}, v)
        -> Gaia setComponent<T>  (auto version-bump + hook -> §4 reconcile closes the loop, FREE)
    undo: handler wraps the write in ActionStack::Dispatch (OUTSIDE the provider), as today
```
- The write auto-feeds change detection, so **the loop closes with no explicit notify** — this is the
  "event chain" the user described, realized as the ECS write→version→reconcile chain.
- **Overrideable (cap 2):** the generated handler is the DEFAULT. A schema may mark a binding
  `Override` → codegen emits a *hook point* (a virtual/injected callable) instead of the direct
  provider write, and the consumer supplies the body. Same for a model→view field: an `Override`
  field gets a custom projection instead of the identity pull. Default is generated; override is a
  declared escape hatch, never a fork of the framework.

## 6. Instance identity + multi-instance selection + set mutation (caps 3/4/5)

`ViewNounKey { ViewNounId noun; uint64_t instance; }` (from the shipped seam) — `instance` is the
provider-interpreted handle. For the Gaia provider it resolves to an **entity**:
- **Single-instance (cap 3):** `instance` = the one bound entity (editor's document body today). The
  direct-field provider ignores it.
- **Multi-instance selection (cap 4):** a view/action declares a **selection query**. The idiomatic
  Gaia pattern (investigation §5) is a **runtime tag component** — "selection is data":
  `struct Selected {};` added/removed at runtime; the query is `all<Selected, BoundComponents...>()`.
  An action targeting "the selected" iterates that query. Indexing the Nth = iterate `.each(Iter&)`
  (no built-in Nth-match; iterate to the index). This supersedes the seam's opaque `instance` slot
  with a first-class, ECS-composable selection (the slot remains the mechanical carrier).
- **Set mutation (cap 5):** an action declared over a selection runs its write **for each entity the
  query yields** — "hire 10 selected characters" = `for e in query.all<Selected>(): action(e)`. One
  declared action, N instances. Undo captures the set (forward = apply to all; inverse = restore all).
- **The "constraints node" alternative** the user mentioned = a query expressed via VIXEN's own
  constraint/selection node rather than a raw Gaia query; both resolve to the same "set of instances"
  contract. The framework depends on the *set-of-instances* abstraction, not a specific query engine —
  so `ISelectionProvider { ids(); at(index); }` is the seam (direct-list impl today, Gaia-query impl
  later, mirroring `IViewDataProvider`).

## 7. Assembly (cap 6): View + Flow + Kernel-callables → app state + UI

The three single-source codegen faces already exist and compose:
- **`[View]`** → data-model + backing store + the render/upload faces (shipped Inc-1..3).
- **`[Flow*]`** → the categoryless action registry + FSM + dispatch (shipped Inc-4 reframe).
- **`[KernelCallable]`** → transplanted logic (e.g. `applyToggle`) usable in generated handlers (shipped).
This framework adds the **binding layer** that ties them to the ECS spine: a schema declares a view, its
bound component(s), its instance/selection, and its actions; codegen emits the bidirectional wiring
(handlers + reconcile registration) against the two seams. "Complete app state + UI from declarations"
= the sum of these faces + the binding.

## 8. THE hard constraint (cap 7): non-exponential state

The user's central worry: an MVVM-over-ECS binding graph explodes combinatorially without discipline.
The architecture is designed so it **can't** explode — state grows with the number of *declarations*,
not the product of views×instances×actions:
- **State is declared ONCE per noun.** A noun (component field) is declared in the `[View]`/component
  schema a single time; every view/action referencing it reuses the same declaration. No per-view or
  per-instance re-declaration.
- **Instances are DATA, not code.** N instances of a view = N entities, not N generated code paths. One
  generated binding drives all instances of a view type via the query. Adding an instance adds a row,
  not a handler.
- **Bindings are GENERATED per schema, not hand-multiplied.** The handler/reconcile wiring is emitted
  from the schema; adding a field regenerates, it doesn't fork.
- **Selection is data (tag component), not a bespoke code path per selection.**
- **One dispatcher, one reconcile pass** — not per-view hooks (the global-slot hook forces a shared
  dispatcher anyway; the reconcile is one pass over all bound views' persistent queries).
So the growth is: +1 field = +1 noun declaration; +1 view type = +1 binding block; +1 instance = +1
entity (zero code); +1 action = +1 handler. **Linear in declarations, constant in instances.** This is
the property that keeps it manageable.

**Tooling / UX API (cap 7, direction — a later increment):** once there is enough binding surface, add
an authoring/inspection API: declare state+view+action coherently in one place; inspect the binding
graph (which views bind which nouns/entities, which actions mutate what); dedup/lint guards that flag a
noun declared twice or a view bound to a nonexistent component. Design principle now (architecture that
can't explode); tooling built as its own increment when the surface justifies it.

## 9. Seams summary (what the framework depends on — all provider-swappable)
- **`IViewDataProvider`** (SHIPPED design) — read/write a noun. Direct-field today; Gaia later. Its Gaia
  provider also owns the **observe** side (the per-view `.changed<T>()` query + reconcile) — but the
  *interface* stays read/write; observe is provider-internal, not in generated handlers.
- **`ISelectionProvider`** (NEW, this design) — `ids()` / `at(index)` yielding the instance set for a
  view/action. Direct-list impl today; Gaia-query (tag-component) impl later. Set mutations iterate it.
- **RmlUi `DirtyVariable`** — the forward target for model→view; not a VIXEN seam, RmlUi's own API.

## 10. Increment cut (proposed — to review)
1. **Inc-A — bidirectional bind, single-instance, on the editor.** Editor layer view ↔ `LayerController`
   via `IViewDataProvider`; input→model (generated handler, shipped path) + **model→view auto-update**
   (per-frame reconcile — but note: the direct-field `LayerController` isn't Gaia yet, so Inc-A's
   reconcile is a direct-provider dirty-pull; it proves the *shape* the Gaia `.changed<T>()` reconcile
   will fill). Overrideable hook. Retire the hand-written 5-handler block. **Smallest end-to-end proof.**
2. **Inc-B — Gaia-backed provider + real `.changed<T>()` reconcile.** Swap the editor's datum to a Gaia
   component; the model→view driver becomes the real per-frame `.changed<T>()` reconcile + `DirtyVariable`
   forward; opt-in `func_set` dispatcher. Proves the ECS observe path for real.
3. **Inc-C — multi-instance selection + `ISelectionProvider`.** Tag-component selection, a view bound to
   one of many entities via query+index. (Prereq for sets.)
4. **Inc-D — set mutation.** An action over a selection set (the "hire 10 selected" capability), undo
   over the set.
5. **Inc-E — authoring tooling / UX API + non-exponential lint guards.** Built once the surface exists.
Each increment via [[post-brainstorm-context-manager]] (Opus-validated + final whole-diff review),
own worktree, live-gated where it touches render/UI.

## 11. Open decisions for review (before cutting increments)
- **The `.changed<T>()` reconcile owner:** a dedicated `ViewReconcileNode` in the render graph (runs the
  per-view queries each frame) vs. folding it into the existing UI render node. (Leaning: a dedicated
  node — keeps the reconcile explicit and orderable in the frame.)
- **Override declaration syntax:** how a `[View]` schema marks a field/action `Override` (attribute vs.
  a partial-class/hook convention) — needs a small syntax design against the kernel-framework emitter.
- **`ISelectionProvider` vs the "constraints node":** whether selection is expressed as a raw Gaia query
  or routed through a VIXEN constraint/selection node (the user named both) — resolves the query-engine
  boundary. Both satisfy the set-of-instances contract; pick per how selections are authored.
- **Inc-A on direct-field vs. jumping straight to Gaia (Inc-B first):** Inc-A proves the shape without
  Gaia risk; but if the editor datum should just become a Gaia component immediately, Inc-A and Inc-B
  merge. Depends on whether we want the editor's layer state to be ECS-backed now.
