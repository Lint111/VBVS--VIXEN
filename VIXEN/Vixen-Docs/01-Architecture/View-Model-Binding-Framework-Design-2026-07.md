# View↔Model Binding Framework — Design Note (2026-07-10)

**Status:** DESIGN (Phase 0 — written before building, per user 2026-07-10). This is the coherent
whole against which increments are cut. **Rev 2** (2026-07-10) folds in the `design-critic` adversarial
review — see §12 for the resolved holes (override boundary, per-entity re-verify cost, undo-over-set
snapshot, transient-hover vs committed-selection, frame-ordering, Inc-A rescope). It is the program-level design for goal **#2** ("view-management
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
    for chunk in view.query.changed<BoundComponents...>().each(Iter):   # chunk-granular: whole dirtied chunk
        for entity in chunk that this view is bound to:
            pull noun value(s) from the component (VALUE-PUSH, not diff — see below)
            write into the view's backing store (ViewStore / IViewDataProvider)
            RmlUi: DirtyVariable(noun)     # forward "what changed" into RmlUi's own dirty tracking
```
- **Reconcile is VALUE-PUSH, not per-entity diff (resolves critic item 2).** `.changed<T>()` is
  chunk-granular (a 1-of-N change marks the whole chunk) and neither it nor `func_set` gives an old
  value or a per-entity flag. So there is nothing to "re-verify per-entity" against without a cache.
  The baseline is therefore: when a bound view's chunk is marked changed, **re-push the current value(s)
  of the entities that view is bound to** into the backing store + `DirtyVariable` — no diffing, no
  cache. RmlUi's own `DirtyVariable` coalescing absorbs the occasional redundant push. A per-entity
  **last-seen cache** (to suppress redundant pushes) is an *optional optimization with O(bound-instances)
  state cost*, added ONLY if a view binds a large N and re-push is measured too costly — it is NOT the
  baseline, so §8's "constant in instances" holds for the default design (the cache is opt-in, like the
  hook). For the single/few-instance views this framework targets first, value-push is cheap and correct.
- **The `func_set` hook is NOT the primary driver.** It is available as an **opt-in immediacy signal**:
  a view may register (via a shared per-component **dispatcher** hook — see §4a) to set a dirty flag the
  reconcile consumes the same frame. The hook NEVER does RmlUi work inline (re-entrancy). Most views
  won't need it; the per-frame reconcile is enough.
- **RmlUi boundary:** Gaia supplies "what changed"; RmlUi's `DataModel::DirtyVariable`/`DirtyAllVariables`
  supplies "tell the UI." Keep them distinct — forward changes into `DirtyVariable`, never mutate RmlUi
  state hoping it notices.

### 4a. Frame ordering + same-frame echo (resolves critic item 3 — the loop is NOT ordering-free)
The write→reconcile loop auto-feeds change detection, but it is **not free of frame ordering**. For the
user's OWN input (toggle a layer, expect it to reflect immediately), a separate per-frame reconcile pass
can show the change one frame late if the reconcile ran before input this frame. Decision:
- **The input-originating view echoes same-frame.** The generated handler, right after the provider
  write, dirties ITS OWN bound RmlUi variable directly (synchronous `DirtyVariable` on the view it
  belongs to). Direct manipulation reflects the same frame, no lag.
- **The per-frame `.changed<T>()` reconcile handles model→view for OTHER views** (cross-view / model-mutated-
  by-simulation propagation), where N+1-frame latency is invisible.
- **Reconcile-node placement** (§11) is thus a real ordering decision, not cosmetic: a dedicated
  `ViewReconcileNode` runs after input handling in the frame so cross-view propagation is same-frame
  where possible; the same-frame echo covers the self case regardless.

### 4b. Hook slot ownership (resolves critic's ODR/ownership note)
`func_set` is ONE global slot per component type per World. The framework's dispatcher must **own** that
slot exclusively and multiplex to registered view-dirty-flags; a second subsystem (e.g. a future Gaia
sync system) wanting the same component's hook must register THROUGH the dispatcher, not overwrite the
slot. Ownership is registered/asserted at startup (last-writer-wins on the raw slot is a bug). The
reconcile's `.changed<T>()` READ path must declare **immutable** access (`.all<T>()`, NOT `.all<T&>()`)
or it trips v0.9.2's hard query-constness `GAIA_ASSERT` (carried from the seam note) — an Inc-B gotcha.

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
### 5a. Identity binding vs. projection — the honest value prop (resolves critic items 1+4)
The critic's sharpest point: the framework's flagship example — LayerController's `mask_` shown as N
per-layer checkboxes — is a **bit-projection**, NOT an identity binding. That is not the exception; for
real UI it may be the COMMON case (units, formatting, clamping, derived/computed values, multi-field
composition, write-validation are all projections). So we define the boundary precisely and pitch the
value honestly:
- **Identity binding** = a scalar noun ↔ a scalar component field, 1:1, no transform either direction.
  These the framework generates end-to-end (read pull, write, reconcile, DirtyVariable) with zero
  hand-written code.
- **Projection binding** = anything with a transform: bit/enum decomposition (`mask_`→checkboxes),
  format/units, clamp/validate on write, derived/computed read, multi-field compose. The framework does
  NOT invent the transform — the schema declares the binding as a **projection** and the consumer
  supplies the transform body (a `[KernelCallable]`-style function, so it can itself be transplanted).
- **What the framework generates in BOTH cases (the honest, still-large value):** the *wiring* — handler
  registration + dispatch, the `IViewDataProvider`/`ISelectionProvider` seam calls, instance/selection
  resolution, the per-frame reconcile registration, the same-frame echo, `DirtyVariable` forwarding, the
  undo wrapping, and the RmlUi data-model registration. The projection is the ONLY hand-written piece,
  and it's a pure function of (component value ↔ view value) with no wiring in it. **So the pitch is:
  "declare views/actions/bindings once; the framework generates all the wiring and dispatch and
  reconcile; you supply only the pure projections where a transform is genuinely needed" — NOT "the
  framework writes your bindings." That is honest and still collapses the state-management burden**
  (the combinatorial part — which view drives which instance, when it reconciles, how input routes — is
  100% generated; only the leaf transforms are authored, and each is written once per binding, not per
  instance).
- **Non-exponential accounting, corrected (critic item 1):** state is **linear in declarations AND in
  projections** (each projection is one hand-written pure function, reused across all instances of that
  binding), **constant in instances**. The projection count is bounded by binding count, not by
  binding×instance — so it stays linear. §8 reflects this.

### 5b. Override (cap 2) — the escape hatch above projection
`Override` is a *stronger* form than a projection: a binding marked `Override` replaces the generated
handler/reconcile entirely with a consumer-supplied one (for a binding whose behavior isn't a pure
value transform — e.g. an action with side effects beyond the write, or a view that reconciles from
something other than its bound component). Projections are the common, cheap customization; overrides are
the rare, full-control escape hatch. Both are DECLARED in the schema (so codegen emits a hook point, not
a fork); neither forks the framework. **Override gets its own increment** (§10 Inc-Ovr) — its syntax +
emitter is real design work, flagged by the critic as under-designed, not a hand-wave in Inc-A.

## 6. Instance identity + multi-instance selection + set mutation (caps 3/4/5)

`ViewNounKey { ViewNounId noun; uint64_t instance; }` (from the shipped seam) — `instance` is the
provider-interpreted handle. For the Gaia provider it resolves to an **entity**:
- **Single-instance (cap 3):** `instance` = the one bound entity (editor's document body today). The
  direct-field provider ignores it.
- **Multi-instance selection (cap 4) — committed vs. transient (resolves critic item 5).** Selection
  splits into two mechanisms by frequency, because add/remove of a component in Gaia is a **structural
  change = archetype move = chunk relocation** (expensive, and it trips co-located components' reconciles):
  - **Committed selection** (low-frequency, user-intentional — the "10 selected to hire" set) → a
    **runtime tag component** `struct Selected {}`, "selection is data": `all<Selected, Bound...>()`.
    Changes are infrequent (a click that commits), so the archetype move is acceptable. Indexing the Nth
    = iterate `.each(Iter&)` to the index.
  - **Transient highlight** (high-frequency — hover/focus, changes on mouse-move) → **NOT a structural
    tag** (that would relocate archetypes on every hover enter/exit — per-frame archetype churn). Use
    either a **value field** (`uint8 highlightFlags` written via `set<T>` — no archetype move, just a
    version bump) or keep it **entirely UI-side** (RmlUi hover/focus state, never touches the ECS). The
    framework must never model transient highlight as a tag component.
  - The seam's opaque `instance` slot remains the mechanical carrier; `ISelectionProvider` (§9) yields
    the set. "Constraints node" vs. raw Gaia query is the query-engine choice (§11) — both satisfy the
    set-of-instances contract.
- **Set mutation (cap 5):** an action declared over a selection runs its write **for each entity the
  query yields** — "hire 10 selected characters" = `for e in query.all<Selected>(): action(e)`. One
  declared action, N instances.
  - **Undo-over-a-set (resolves critic item 8 — a real correctness hole):** undo must NOT re-run the
    live query at undo time (the `Selected` set, or entity liveness, may have changed since dispatch).
    At dispatch, undo captures a **materialized snapshot: the concrete `(entity, prior-value)` list**
    for exactly the entities the action touched. Inverse iterates that captured list, not the query.
    This needs an N-sized snapshot at dispatch (the existing `ActionStack::DispatchWithSnapshot` footprint
    blob sized to N (entity,value) pairs). **Dead-entity handling:** if a captured entity is no longer
    live at undo time (destroyed between do and undo), restore **skips it + logs** (a by-value Gaia write
    to a dead entity would no-op/assert). Redo similarly re-materializes from the forward snapshot, not
    the live query. This is Inc-D's core correctness contract, not a one-liner.
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
entity (zero code); +1 action = +1 handler; +1 non-identity binding = +1 pure **projection** function
(§5a, reused across all that binding's instances). **Linear in declarations AND projections, constant in
instances.** This is the property that keeps it manageable — corrected from Rev 1's "linear in
declarations" to include the projection term the critic surfaced (projections are bounded by binding
count, not binding×instance, so the sum stays linear). The two would-be super-linear terms are both
neutralized: the per-entity reconcile **cache** is opt-in (§4, not baseline), and the per-view persistent
**Query** is one-per-view-type (not per-instance). Reconcile *runtime cost* scales with changed chunks,
not total instances (bounded, coarse) — a cost note, not a state-count blowup.

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

## 10. Increment cut (proposed — to review; Rev 2 folds critic items 6+7)
1. **Inc-A — view→model generated-handler path + same-frame echo, single-instance, on the editor.**
   Editor layer view ↔ `LayerController` via `IViewDataProvider`; input→model (generated handler, shipped
   path) + the **same-frame echo** (handler dirties its own RmlUi variable, §4a) + `DirtyVariable`
   forwarding. Retire the hand-written 5-handler block. **RESCOPED (critic item 6):** Inc-A proves the
   write path + the RmlUi `DirtyVariable` forward — it does NOT prove the Gaia `.changed<T>()` reconcile
   shape (a direct-field singleton has no chunks/versions/query; the reconcile is authored fresh in
   Inc-B). Because `mask_`→checkboxes is a **projection** (§5a), Inc-A also exercises the FIRST projection
   binding — so a minimal projection mechanism is in-scope here (not the full override syntax).
2. **Inc-Ovr / projection mechanism — the schema syntax + emitter for projection bindings (§5a) and the
   override hook (§5b).** MAY fold into Inc-A if the `mask_` projection forces a minimal version anyway;
   but the *general* projection/override syntax + emitter is real design work (critic item 7) and gets
   explicit budget here rather than being hand-waved. Ordering: co-designed with Inc-A (Inc-A needs at
   least the projection leaf), generalized here.
3. **Inc-B — Gaia-backed provider + real `.changed<T>()` reconcile + `func_set` dispatcher.** Swap the
   editor's datum to a Gaia component; model→view driver becomes the real per-frame `.changed<T>()`
   reconcile (value-push baseline, §4) + `DirtyVariable`; the `ViewReconcileNode` placement (§4a) + the
   immutable-access `.all<T>()` const-assert gotcha (§4b) land here. Proves the ECS observe path for real.
4. **Inc-C — multi-instance selection + `ISelectionProvider`.** COMMITTED-selection tag component +
   value/UI-side transient highlight (§6, the split); a view bound to one of many entities via query+index.
   (Prereq for sets.)
5. **Inc-D — set mutation + undo-over-a-set.** An action over a selection set ("hire 10 selected"), with
   the **materialized (entity, prior-value) snapshot + dead-entity skip** undo contract (§6, critic item
   8). This increment carries real correctness weight — budget it as such (may need sub-milestones), do
   not treat undo as a one-liner.
6. **Inc-E — authoring tooling / UX API + non-exponential lint guards.** Built once the surface exists.
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
- **Inc-A on direct-field vs. jumping straight to Gaia (Inc-B first):** Inc-A proves the write path +
  DirtyVariable without Gaia risk and retires the 5 hand-written handlers (concrete win); Inc-B authors
  the real reconcile fresh (critic item 6 — the reconcile shape is NOT proven by Inc-A). Lean: keep
  separate (merging risks a too-big, hard-to-live-gate increment). Merge only if the editor datum should
  become a Gaia component immediately.

## 12. Critic resolution log (Rev 2, `design-critic` Opus adversarial review 2026-07-10)
The review found the skeleton sound (no hidden code-level combinatorial blowup) and four real holes,
all closed in-doc:
- **Override boundary / projection frequency (sev 1)** → §5a: defined identity-vs-projection precisely;
  the `mask_`→checkboxes flagship IS a projection; repositioned the honest value prop as "generates all
  wiring/dispatch/reconcile; you supply only pure projections" (still collapses state mgmt); §8 accounting
  corrected to "linear in declarations AND projections."
- **Per-entity re-verify cache (sev 2)** → §4: baseline is VALUE-PUSH (no diff, no cache); the last-seen
  cache is an opt-in optimization with O(bound-instances) state, not baseline — so "constant in instances"
  holds by default.
- **Undo-over-a-set (sev 3)** → §6: undo captures a materialized (entity, prior-value) snapshot at
  dispatch (not the live query) + dead-entity skip+log; Inc-D's core contract.
- **Transient-hover vs committed-selection (sev 4)** → §6: committed selection = tag component (infrequent,
  archetype move OK); transient highlight = value field or UI-side (NEVER a tag — avoids per-hover
  archetype thrash).
- **Frame ordering (sev 5)** → §4a: input-originating view echoes same-frame (handler dirties its own
  var); reconcile handles cross-view (N+1 OK); ties to the ViewReconcileNode placement decision.
- **Override needs its own increment (sev 6)** → §10 Inc-Ovr.
- **Inc-A rescope (sev 7)** → §10 Inc-A: proves write path + DirtyVariable, NOT the reconcile shape.
- Hook-slot ownership + `.all<T>()` const-assert → §4b.

**The ONE decision left for the user (it reframes the pitch, not just mechanics):** §5a's value-prop
framing. The critic's sharpest point is that if projections dominate (likely, for real UI), the framework
is a *wiring/dispatch/reconcile generator with hand-authored leaf projections*, NOT a "writes your
bindings" generator. That's still highly valuable (the combinatorial part is 100% generated) but it is a
different sell. Confirm this is the intended shape before cutting Inc-A — it decides how much the schema
should try to express declaratively vs. hand off to a projection function.
