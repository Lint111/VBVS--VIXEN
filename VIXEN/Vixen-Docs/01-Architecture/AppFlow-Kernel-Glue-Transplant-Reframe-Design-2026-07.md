# AppFlow as Kernel-Transplanted Glue (Data + Logic) — Reframe Design

**Date:** 2026-07-07
**Status:** DESIGN — supersedes the framing of `View-Contract-Inc4-View-Action-AppFlow-Convergence-Design-2026-07.md` (D1–D9). The Inc-4 mechanism built + validated through M3 (the generic `InputProfile`/`BindingStore`/`FlowStateMachine.RequestReturn`/`DispatchByKey`) STANDS as hand-written primitives; this reframe changes *how the flow-control logic is authored and distributed*, not what the primitives do.
**Origin:** user reframe mid-execution of Inc-4, over a sequence of refinements (2026-07-07). See the Decision Log for the exact chain.

---

## 1. The one-paragraph thesis

VIXEN's render engine is **mechanical only**: it renders a view, exposes an element/button registry + a select-action (which element the user activated), carries typed data to/from the user (the View, Inc-1→3), and provides host primitives (buffers, an event bus). **Application state and control — the state machine, the action dispatch, undo/redo, navigation/return — is NOT the render engine's concern.** It is *app state + control* = state management, and state management is a separate layer the engine must not own.

That layer (call it **AppFlow**) follows the **same glue-ownership convention already used for View, opcodes, and content-config**: its declaration is a *glue source* declared against the **kernel** (the Yeroket codegen core) by a kernel consumer, and **the render engine and the consumers (editor, underset) depend on the glue's generated artifacts in parallel — not in a chain.** The kernel is a domain-blind transpiler whose one job is to emit **identical artifacts across domains**. Crucially the kernel transplants **both data AND logic** (it already turns C# SDF kernel bodies into byte-identical C++/HLSL/Burst via its AST visitors). So AppFlow's flow-control *logic* (dispatch, state-step, history/return, apply/invert) is **authored once as C# and transplanted identically to every consumer's domain** — exactly like its data tables. Nothing about AppFlow is invented, owned, or known by the render engine; the engine only calls the transplanted logic through host primitives.

```
                 KERNEL (Yeroket codegen core — domain-blind transpiler)
                   • one job: emit IDENTICAL artifacts across domains
                   • transplants DATA (structs/tables) AND LOGIC (C# body → C++/HLSL/Burst)
                   • knows NOTHING of AppFlow / render engine / underset
                          ▲  declared against (attributes + C# bodies)
              ┌───────────┴────────────────────────────────────────────┐
              │  FLOWSTATE GLUE SOURCE  (a kernel consumer authors it)  │
              │   DATA:  states, actions, triggers, key-defaults,       │
              │          view-noun targets, KeyChord …                  │
              │   LOGIC: dispatch(id), fsm-step, history/return,        │
              │          ActionStack apply/invert  — as C# kernel bodies│
              └───────────┬────────────────────────────────────────────┘
                          │  kernel transplants → identical DATA + LOGIC artifacts
             ┌────────────┴──────────────┐   (parallel, non-chain consumption)
             ▼                           ▼
      RENDER ENGINE (VIXEN)        CONSUMER (editor / underset)
      • consumes artifacts         • consumes the SAME artifacts
      • provides host primitives:  • declares its schema (attrs)
        View render, element/      • registers handler BEHAVIOR
        select seam, buffers,        (the only hand-written per-consumer piece)
        event bus
```

---

## 2. Decision log (D10–D16, supersedes/extends Inc-4 D1–D9)

| # | Decision | Rationale |
|---|----------|-----------|
| **D10** | **The render engine owns nothing of app state/control.** Its mechanical surface is: View render + typed data I/O, the element/button registry + select-action seam (`UISelectionProviderNode::DrainClickedElementId` today), buffers, and the event bus. AppFlow (FSM, dispatch, undo, nav) is a *separate layer* the engine neither owns nor references. | "app flow is app state and control … not owned by the render engine." Already true structurally (no engine→AppFlow production dep — verified); this decision makes it a rule, not an accident. |
| **D11** | **AppFlow's declaration is a kernel-declared GLUE SOURCE consumed in PARALLEL** by the engine and the consumers — the same non-chain shape as View/opcodes/config. No party owns the glue relative to its co-consumers. | "the source is a consumer of the kernel, and everyone is dependent on the source … kernel ← flowstate declaration ← render engine, consumer (underset)." Consistent glue-ownership convention across the whole program. |
| **D12** | **The kernel transplants DATA *and* LOGIC.** Flow-control logic (dispatch/registry, fsm-step, history/return, apply/invert) is authored ONCE as C# kernel bodies + attributes and transplanted identically into each domain via the kernel's AST visitors (`CppAstVisitor` et al.) — the same capability that emits byte-identical C++/HLSL/Burst from SDF kernel bodies. There is **no hand-written per-consumer flow logic** in the target state. | "read in more depth about kernel capabilities, it transplants data and logic." The kernel's `CppAstVisitor.EmitFunction` already transpiles `if`/return/assignment/invocation/ternary/element-access — a dispatcher body is in-grammar. |
| **D13** | **The kernel is domain-blind and has exactly one job: emit identical artifacts.** It does not know its consumers, does not know the logic's *meaning*, does not special-case AppFlow. The consumer declares all logic + implementation via attributes/bodies; the kernel faithfully transplants. | "kernel doesn't know about its users and doesn't care about them; it has one specific job … generate artifacts that are identical across different domains." |
| **D14** | **The action model is a UNIFORM registry — no framework action-categories, no action-name literals, no framework undo-knowledge.** The dispatch is `Dispatch(id, params){ handlers_[id](params); }` and nothing more. Whether an action is undoable / nav / side-effect is *what its handler does* (a handler uses `ActionStack`, `RequestReturn`, or a plain side-effect), not a category the framework enumerates. | "why would we need the arbitrary 5 categories … the ui actions setup was one registry following the same pattern." Mirrors undertow's single UI-action registry. Retires the earlier 5-`ActionKind` proposal. |
| **D15** | **No public named action methods.** `rt_.Undo()/Redo()/ToggleLayer()/RequestReturn()` are DELETED from the public surface. The only public dispatch path is `DispatchBySelector` / `DispatchByKey` / `DispatchById`. The consumer (editor) names ZERO triggers and ZERO actions in code — it registers handlers and dispatches by selector/key. | The named methods encode action knowledge in the framework — exactly what D10/D14 remove. |
| **D16** | **A `Data` action names the View noun it mutates** (`Target = nameof(View.…)`), generated + compile-checked from the same source. The View (Inc-1→3) is the single source for the data flowing engine↔consumer; a `Data` verb operates on a declared View noun, not an opaque blob. | "data should be elements that are view declared, as the view represents data that flows from and to the engine." Makes the "view→action face" literal: verb references noun, compile-checked. |
| **D-future-1** | **Instance-addressed views/actions** (a collection View — e.g. a character roster — where an action edits *one* instance and the edit flows back to the sim) is DESIGNED-not-built. The forward seam: an action's target resolves through an **instance selector** param (`Instance = param("characterId")`); the existing `layer-{index}` param IS the degenerate single-axis case of that selector. Nothing this increment forecloses it. | "we would need a view instance selector … rendering a list of characters, editing one, data goes back to the sim." |
| **D-future-2** | **Physical relocation of the flowstate glue to a kernel-declared source + parallel underset consumption** is the sequenced FOLLOW-UP increment. This increment proves the model on ONE consumer (the editor); underset consumption crosses into undertow (Inc-4 D1 deferred it). | Keeps this increment shippable; the big model is recorded + sequenced, not crammed. |

---

## 3. What stays, what changes, what's new

### Stays (M1–M3, built + Opus-validated — the mechanism)
- **Kernel DATA emission** (`AppFlowEmitter`, M1/M2): the flowstate declaration → identical C++ data tables/structs (`AppFlowActionDecl[]`, `kElementTriggers`, `kKeyDefaults`, `KeyChord`, …). This is *already* correctly kernel-shaped (a transpiler emitting identical data) and needs no rework.
- **Generic primitives** (`FlowStateMachine`, `ActionStack`): pure mechanism with no action-name knowledge — `Request`/`RequestReturn`/entry-history, `Dispatch`/`Undo`/`Redo`/apply-invert. These are the host-side primitives the transplanted logic *calls*. They stay hand-written this increment (their transplant is a follow-up).
- **Generic resolution** (`InputProfile` tightest-wins, `BindingStore` parametric pattern match): the trigger→action resolvers. Unchanged.

### Changes (this increment)
- **Uniform categoryless registry (D14):** `AppFlowRuntime::Dispatch(id, params) → handlers_[id](params)`. Remove any notion of action categories.
- **Full method retirement (D15):** delete `rt_.Undo()/Redo()/ToggleLayer()/RequestReturn()` from the public API; the sole public dispatch path is `DispatchBySelector`/`DispatchByKey`/`DispatchById`. `FlowStateMachine`/`ActionStack` become services the handlers reach (via a passed context or `rt_` accessors), not public verbs.
- **Editor = pure consumer (D10/D15):** registers one self-contained handler per action (each using the primitive it needs — `stack_.Dispatch`, `stack_.Undo`, `fsm_.RequestReturn`, or a side-effect); names zero triggers/actions in code.
- **`Return` via the registry:** no special-case — a registered handler that calls `RequestReturn()`. (Dissolves the earlier "first-class Return action" M3.5 as a *separate* concern — it's just a handler now.)
- **`Data`-action `Target = nameof(View.…)` (D16):** the emitter resolves + compile-checks the view-noun reference.

### New (this increment — the walking skeleton for D12)
- **Prove logic-transplant on the SMALLEST real flow-logic body:** author ONE flow-logic body (e.g. the uniform `dispatch(id)→handler`, or the `apply/invert` toggle) as a **C# kernel body**, have the kernel transplant it to C++ via the existing AST visitor, and **replace that hand-written M3 piece with the transplanted output** — proven byte/behaviour-identical. This validates "the kernel transplants flow LOGIC" concretely without rewriting all of M3.
- Feasibility confirmed: `CppAstVisitor.EmitFunction` already transpiles `if`/`return`/assignment/invocation/ternary/member-access/element-access — a dispatcher/apply body is within grammar today.

### Sequenced follow-up increments (recorded, NOT built here)
1. **Transplant the remaining flow logic** (full FSM step, history/return, the whole registry) through the kernel.
2. **Relocate the flowstate glue to a kernel-declared source** (physical), parallel to View's home.
3. **Parallel underset consumption** of the artifacts (crosses into undertow).
4. **Instance-addressed views/actions** (D-future-1).

---

## 4. Architecture (this increment's target)

### 4.1 Layers & dependency direction
- **Engine (mechanical):** `View` render + data I/O; `UISelectionProviderNode` element/select seam; `EventBus`; buffers. Never references AppFlow.
- **Kernel (transpiler):** emits identical DATA (done) + the transplanted LOGIC body (new, smallest slice). Domain-blind.
- **AppFlow (glue artifacts + generic primitives):** the kernel-emitted tables + the transplanted logic body + the hand-written `FlowStateMachine`/`ActionStack`/resolvers. Consumes the engine seam (via `EventBus`/select), never the reverse.
- **App (editor):** declares schema (attributes), registers handlers, dispatches by selector/key only.

### 4.2 The registry (D14) — the entire router
```cpp
// hand-written generic THIS increment (its transplant is a follow-up):
DispatchResult AppFlowRuntime::Dispatch(FlowActionId id, const Params& params) {
    auto it = handlers_.find(id);
    if (it == handlers_.end()) return DispatchResult::RejectedByState;  // declared-but-unwired = caught
    it->second(params);
    return DispatchResult::Ok;
}
// DispatchBySelector(sel)  -> resolve -> Dispatch(id, extractedParams)
// DispatchByKey(chord)     -> resolve -> Dispatch(id, {})
// DispatchById(id, params) -> Dispatch(id, params)   // trigger-less (tests/programmatic)
```
No categories, no action-name literals, no undo knowledge. `RegisterHandler(id, fn)` at startup.

### 4.3 Handlers are self-contained (D14)
```cpp
// editor init — each handler uses whatever primitive it needs; the framework knows none of this:
rt_.RegisterHandler(Toggle, [&](const Params& p){ stack_.Dispatch(Toggle, applyToggle(p)); dirty_=true; }); // undoable
rt_.RegisterHandler(Undo,   [&](const Params&){ stack_.Undo(); });
rt_.RegisterHandler(Redo,   [&](const Params&){ stack_.Redo(); });
rt_.RegisterHandler(Save,   [&](const Params&){ SaveDocument(); });        // side-effect, no undo entry
rt_.RegisterHandler(Return, [&](const Params&){ fsm_.RequestReturn(); });  // nav — no special framework case
// dispatch sites carry NO behavior:
//   DispatchBySelector("layer-2-toggle"); DispatchByKey({Z,Ctrl}); DispatchByKey({Escape,None});
//   DispatchBySelector("back-button");   // a BUTTON reaches Return identically to Esc
```

### 4.4 The logic-transplant slice (D12 walking skeleton)
Pick ONE body (recommend the smallest with real branching — e.g. `applyToggle`'s self-inverse, or the `Dispatch` lookup). Author it in C# under a kernel-callable attribute, run the kernel to emit C++, and swap the hand-written M3 implementation for the transplanted output. Prove identical behaviour by the existing AppFlow gtests (the swapped piece keeps them green) + a byte/AST equivalence check on the emitted C++.

---

## 5. Proof (this increment)
- **Offline:** the full AppFlow C++ suite stays green with (a) the uniform registry, (b) the retired named methods, (c) the transplanted logic body swapped in. The transplant slice adds a check that the kernel-emitted C++ matches the intended body.
- **Live gate:** the windowed real-GPU editor gate (`test_editor_toggle_undo_capture`) proves the pure-consumer editor end-to-end — element-click→toggle, key→undo byte-exact, `Esc` AND a `back-button` selector → return-pop — delta calibrated LIVE (not copied).
- **No-regression:** engine has zero new AppFlow dependency; drift-guards green.

---

## 6. Open scoping note (honest)
This reframe is materially larger than the original Inc-4 remainder. Per the user's explicit choices (2026-07-07): the full model is recorded here (D10–D16 + D-future); **this worktree finishes a coherent slice** = uniform registry + method retirement + pure-consumer editor + `Return`-as-handler + `Data`-target + the **smallest logic-transplant walking skeleton** + the live gate. Everything else (transplant the rest of the logic, relocate the glue source, parallel underset, instance selectors) is sequenced as named follow-up increments. M1–M3 stand.

---

## Decision Log (chronological, this session)
1. Return should be triggerable by a button, not only Ctrl/Esc → first-class `Return` (later dissolved into "just a handler" by D14).
2. No hardwired UI/flow actions in the framework → both layers, generic dispatch (D14/D15).
3. Data = view-declared elements (data flows to/from the engine) → D16 (`Target = nameof(View.…)`).
4. Need a view instance-selector for per-instance handling (character roster → edit → sim) → D-future-1.
5. Glue ownership: the source is a kernel consumer; engine + underset depend on its artifacts in parallel (like View/opcodes/config) → D11.
6. AppFlow is app state+control, not owned by the render engine → D10.
7. The kernel transplants data AND logic; it's domain-blind with one job (identical artifacts) → D12/D13.
8. Scope: spec the full model, finish a coherent editor slice now, sequence the rest → §6, D-future-2.
9. Prove logic-transplant on the smallest real body this increment → §4.4.
