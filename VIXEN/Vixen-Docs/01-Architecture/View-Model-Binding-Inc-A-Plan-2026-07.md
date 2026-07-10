# View↔Model Binding Framework — Inc-A Plan (2026-07-10)

**Program design:** `Vixen-Docs/01-Architecture/View-Model-Binding-Framework-Design-2026-07.md` (Rev 2, all decisions settled).
**Seam (shipped):** `View-Data-Provider-Seam-Design-2026-07.md` — `IViewDataProvider` synchronous by-value fallible typed-noun read/write.
**Increment goal:** the smallest END-TO-END proof of the framework's **view→model generated-handler path + same-frame echo + the first PROJECTION binding**, on the editor's existing layer view, single-instance, retiring the hand-written 5-handler block. Per design §10 Inc-A (RESCOPED: proves the write path + RmlUi DirtyVariable forward — NOT the Gaia `.changed<T>()` reconcile shape, which is Inc-B).

## Scope boundary (what Inc-A is and is NOT)
- **IS:** generated (or generator-shaped) handler registration for the editor's layer actions targeting `IViewDataProvider`; the `mask_`↔checkboxes **projection** as a pure hand-authored function (the first real projection); the **same-frame echo** (handler dirties its own RmlUi variable on write, design §4a); RmlUi `DirtyVariable` forwarding; retire the editor's hand-written 5 `RegisterHandler` block.
- **IS NOT:** the Gaia-backed provider (editor datum stays `LayerController` direct-field — Inc-B swaps to Gaia); the real per-frame `.changed<T>()` reconcile (Inc-B); multi-instance selection (Inc-C); set mutation (Inc-D); the general projection/override *mini-syntax* (Inc-Ovr — Inc-A uses the MINIMAL projection mechanism needed for `mask_` only, per §10). Do NOT build ahead into these.
- **Value-prop framing (user-decided):** the framework generates the WIRING; the consumer supplies a pure leaf projection function. `mask_`→checkboxes IS a projection (bit-decompose), not identity — Inc-A exercises exactly this.

## Preflight (controller did): worktree `/mnt/c/cpp/VBVS--VIXEN/.claude/worktrees/view-binding-inc-a`, branch `feat/view-binding-inc-a`, off main. Pre-blessed. CodeGraph indexed.

## Tasks — Milestone 1 (the whole increment; small)

### Task 1 — Ground the current editor handler/binding shape (READ, no change yet)
Use codegraph_explore / read to map EXACTLY what exists today, so the change is a precise retirement not a rewrite:
- `application/editor/source/EditorApplication.cpp` — the `LoadDocument` block registering the 5 self-contained handlers (ToggleLayer via `Stack().Dispatch` + live `applyToggle`, Undo/Redo via `Stack()`, Save, Return via `NavPop()`), the `ApplyDocumentToScene`, the `Update()`/`PreTick()` dispatch drain, `DispatchBySelector`.
- `rt_.Layers()` → `LayerController` (`libraries/AppFlow/include/LayerController.h`) — `Mask()`/`SetMask()`/`Snapshot()`/`Restore()`.
- How a UI id resolves: `DispatchBySelector` → `BindingStore` parametric `layer-{index}-toggle` → `ToggleLayer`.
- The RmlUi data-model the editor's HUD/layer view binds to (the `[View]` schema + generated model for the editor, if any — or is the layer view static RML today? `editor.rml` has static checkbox divs). Determine whether there IS a data-model-bound representation of the layer state or only static markup + click hit-testing. THIS DETERMINES how model→view echo lands (DirtyVariable on a bound var vs. re-render). Report findings before proceeding.

### Task 2 — Define the projection + the IViewDataProvider direct-field provider for LayerController
- Implement the direct-field `IViewDataProvider` provider over `LayerController` (noun `LayerMask` → `Mask()`/`SetMask()`; `ReadU32` always true; ignores `instance`). This is the ~3-line provider the seam design specifies.
- Author the FIRST **projection**: `mask_` (uint32 bitfield) ↔ per-layer checkbox bool. A pure function pair (decompose: `bit(mask, idx)`; compose: `applyToggle(mask, idx)` — the latter ALREADY EXISTS as the transplanted `[KernelCallable] applyToggle` from the Inc-4 reframe; reuse it, do not reinvent). Keep the projection a pure leaf function, no wiring inside it. Document it AS the exemplar projection.

### Task 3 — Route the editor's layer action through the seam + same-frame echo; retire the hand-written handler
- The `ToggleLayer` handler: instead of the hand-written `rt_.Layers().SetMask(applyToggle(rt_.Layers().Mask(), idx))`, route through the provider: read via provider → apply the projection (`applyToggle`) → write via provider. Undo stays OUTSIDE the provider (wrap in `Stack().Dispatch` exactly as today).
- **Same-frame echo (design §4a):** right after the provider write, the handler dirties the input-originating view's bound RmlUi variable (`DirtyVariable(noun)` on the layer view's data model) so the checkbox reflects the toggle THE SAME FRAME. If Task 1 finds the layer view is static RML (no data model), then either (a) introduce a minimal data-model binding for the layer checkboxes (so DirtyVariable has something to dirty), or (b) if that's too large for Inc-A, document that the echo is deferred to when the layer view becomes data-model-bound and keep Inc-A to the write-path retirement — REPORT which, don't silently pick the smaller one.
- Retire the hand-written handler bodies where the generated/seam-routed path replaces them. Keep the 5-action SET (ToggleLayer/Undo/Redo/Save/Return) working — this is a REROUTE of ToggleLayer through the seam + projection, not a deletion of functionality. Undo/Redo/Save/Return may stay as-is if they don't touch a view noun (they're not view-data bindings); ONLY ToggleLayer is a view→model binding. Be precise: Inc-A converts the ONE genuine view→model binding (ToggleLayer) to the seam+projection path; it does not force the nav/save actions through it.

### Task 4 — Live gate (real GPU, WSL/Dozen)
- Build `vixen_editor` WSL-side (poll on interval, never blind-wait, never overlap builds, VK_ICD_FILENAMES unset).
- Run the editor script (`temp/run_editor_script.bat` equivalent: toggle:2, undo, redo, captures 5/45/75/105) — the SAME gate the residency fix restored (`test_editor_toggle_undo_capture` is 4/4 on main now). Confirm it STILL passes: the reroute must preserve the byte-exact undo/redo round-trip (capture_75≡capture_5, capture_105≡capture_45) + the mask delta (capture_5≠capture_45). If the same-frame echo is added (Task 3a), the toggle should reflect identically — the captures must match the existing golden behavior (no regression).
- No-regression: the AppFlow suite + editor tests that exist. The byte-guards (AppFlowCallables.g.hpp `989b65e4…`, AppFlow.g.h `b63b2b35…`) must HOLD if any codegen is touched (Inc-A likely touches NONE — it's editor-side wiring + a provider + reusing the existing transplanted applyToggle; confirm the .g artifacts are byte-identical).

## Gates / guardrails
- `vixen_editor` builds clean; `test_editor_toggle_undo_capture` 4/4 preserved live; AppFlow suite green; byte-guards hold; main-app unaffected.
- rtk masks git exit codes — use `/usr/bin/git`, `sha256sum`, `cmp` for evidence.
- Commit in the worktree (pre-blessed). Do NOT push.

## Milestone Map
- [ ] **Milestone 1 (Tasks 1-4):** ground current shape → LayerController IViewDataProvider provider + mask projection → reroute ToggleLayer through seam+projection+same-frame-echo, retire hand-written body → live gate preserves 4/4. One Sonnet implementer + one Opus validator.

## Note on Inc-A's honest scope (from the design critic)
Inc-A proves the **view→model generated-handler-over-seam path + the projection mechanism + the DirtyVariable/same-frame echo** — it does NOT prove the Gaia `.changed<T>()` reconcile (LayerController is a direct field with no chunks/versions/query; the real reconcile is authored fresh in Inc-B). Do not claim Inc-A validates the reconcile shape. The concrete Inc-A win = the ONE genuine view→model binding runs through the framework's seam+projection+echo path, and the hand-written RMW is retired.
