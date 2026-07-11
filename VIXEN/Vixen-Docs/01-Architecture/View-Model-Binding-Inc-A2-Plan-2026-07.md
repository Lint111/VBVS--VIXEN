# View↔Model Binding — Inc-A2 Plan (2026-07-10)

**Program design:** `View-Model-Binding-Framework-Design-2026-07.md` §10 item 1a (Inc-A2).
**Prior:** Inc-A (merged `224e393f`) made `ToggleLayer` write through `IViewDataProvider` and found the
editor layer view is **static RML with NO data-model** — so there is NO model→view path anywhere yet.
**Inc-A2 goal:** give the editor layer view a real RmlUi **data-model** so there is a `DirtyVariable`
target — the prerequisite for ALL model→view work (the same-frame echo and the Inc-B `.changed<T>()`
reconcile). This is the FIRST time the editor layer checkboxes reflect model state via a data binding.

## Scope boundary (what Inc-A2 IS and is NOT)
- **IS:** a `[View]` schema for the editor layer state (the per-layer checkbox booleans, derived from
  the `LayerController` mask); generated data-model registration (the SHIPPED `[View]`/`IView`/RmlUi
  data-model machinery — Inc-1/2/2b — applied to the editor layer view); a data-model-bound `editor.rml`
  (replace the static `checkbox checked` divs with data-model-bound markup driving `checked` off the
  bound booleans); `IView` registration for the editor layer view; and wiring so the bound model is
  populated from `LayerController` at load (initial model→view). OPTIONALLY the **same-frame echo** if it
  falls out cheaply once the data-model exists (handler dirties the bound checkbox var after the toggle
  write) — but that MAY defer to Inc-Ovr; decide by whether it's a 2-line add once the model exists.
- **IS NOT:** the Gaia-backed provider or the real per-frame `.changed<T>()` reconcile (Inc-B); the
  general projection/override *emitter syntax* (Inc-Ovr); multi-instance/selection (Inc-C); set mutation
  (Inc-D). Do NOT build ahead. The mask→checkbox-bools decomposition here is the SAME projection Inc-A
  used (`bit(mask,idx)`); reuse it, keep it a pure leaf.
- **Reuse, don't reinvent:** the `[View]` attribute + codegen + `IView` host + RmlUi data-model
  registration ALL already exist and ship (the View Contract Inc-1/2/2b program — see the HUD view
  `Hud.view.rml`/`HudView`/`BindHudModel` as the working exemplar). Inc-A2 is APPLYING that machinery to
  the editor layer view, not building new codegen. Study the HUD view path first as the template.

## Preflight (controller did): worktree `/mnt/c/cpp/VBVS--VIXEN/.claude/worktrees/view-binding-inc-a2`,
branch `feat/view-binding-inc-a2`, off main `224e393f`. Pre-blessed. CodeGraph indexing.

## Tasks — Milestone 1

### Task 1 — Study the HUD view as the template (READ)
Use codegraph_explore / read to map the SHIPPED data-model binding path so Inc-A2 mirrors it:
- The HUD `[View]` schema (`codegen/view-schemas/…` — the `EditorHud`/`Hud` schema), its generated
  `.g.h` registration, the `HudView : IView` host, `BindHudModel`, and how `UIRenderNode`/the IView host
  registers the model + how `Hud.view.rml`/`hud.rml` markup binds to it (`data-model=`, `data-*`
  attributes). This is the exact pattern to replicate for the editor layer view.
- The editor's current static `editor.rml` (`libraries/RenderGraph/assets/ui/editor.rml`) — the 3
  `checkbox checked` divs — and how the editor loads/hosts its document (`EditorApplication`
  LoadDocument, the UIRenderNode/IView host it uses). Determine where the editor's IView registration
  would live (mirror the main app's HudView registration).
- `LayerController` (Mask()/IsEnabled(i)/layer count) — the source of the checkbox booleans.
REPORT the template mapping before building.

### Task 2 — Author the editor layer `[View]` schema + generate the data-model
- A `[View]` schema (in `codegen/view-schemas/`) for the editor layer view: the per-layer rows
  (name/op already in the static markup) + a `checked` bool per layer, OR a bound array of layer rows
  with a `checked` field. Match the golden-mirror + drift-guard pattern the HUD schema uses (a
  `view_*_check` CMake guard + a golden gtest, mirroring `view_hud_*`/`test_view_*_golden`).
- Generate + commit the `.g.h` (run the codegen via the WSL Yeroket tool per the kernel-framework/
  codegen CMake path; the drift-guard proves it). Do NOT hand-edit the `.g.h`.

### Task 3 — Bind editor.rml to the model + register the IView
- Replace the static checkbox divs in `editor.rml` with data-model-bound markup (`data-model="…"`, the
  checkbox `checked` state driven by the bound per-layer bool — mirror how `hud.rml` binds).
- Add the editor's `IView` implementation (mirror `HudView`) + register it on the editor's UIRenderNode/
  IView host. Populate the model from `LayerController` at document load (bit(mask,idx) → per-layer
  `checked`), so the checkboxes reflect the ACTUAL mask on load (initial model→view).
- OPTIONAL same-frame echo: if, once the model exists, dirtying the bound checkbox var after the
  ToggleLayer write is a trivial add (§4a), do it (handler dirties its own var → checkbox flips same
  frame). If it needs non-trivial plumbing, DEFER to Inc-Ovr and document — REPORT which.

### Task 4 — Live gate (real GPU, WSL/Dozen)
- Build `vixen_editor` WSL-side (poll on interval, never blind-wait, never overlap builds one target,
  VK_ICD_FILENAMES unset).
- Run the editor script (toggle:2/undo/redo, captures 5/45/75/105) — the gate `test_editor_toggle_undo_capture`
  must STILL be 4/4 (byte-exact undo/redo round-trip capture_75≡5, capture_105≡45, mask delta 5≠45). The
  data-model binding must NOT regress the render (the 3D body render is the gate's signal; the checkbox
  UI is additive). If the same-frame echo was added, verify the checkbox visibly reflects the toggle
  (report a capture or the bound-model state), but the EXISTING gate's byte-exact assertions must hold.
- No-regression: AppFlow suite (42/42), the view golden/drift-guards (existing `view_hud_*` + the new
  editor-view guard) green. Byte-guards: if the editor `[View]` schema adds codegen, the NEW `.g.h` is
  committed + drift-guarded; the AppFlow byte-guards (AppFlow.g.h `b63b2b35…`, AppFlowCallables.g.hpp
  `989b65e4…`) must still HOLD (Inc-A2 doesn't touch AppFlow codegen).

## Gates / guardrails
- `vixen_editor` builds clean; `test_editor_toggle_undo_capture` 4/4 preserved live; AppFlow 42/42; view
  drift-guards green; AppFlow byte-guards hold; main-app HUD view UNAFFECTED (don't perturb the HUD).
- rtk masks git exit codes — use `/usr/bin/git`, `sha256sum`, `cmp` for evidence.
- Codegen via the WSL Yeroket tool (the `wsl.exe` bridge / KI-015 path); `git checkout --` the
  non-deterministic `SDFNodeGenerator.dll` after any codegen run so the repo stays clean.
- Commit in the worktree (pre-blessed). Do NOT push.

## Milestone Map
- [ ] **Milestone 1 (Tasks 1-4):** study HUD template → editor layer `[View]` schema + generated model +
  drift-guard → data-bind editor.rml + IView registration + initial model→view from LayerController
  (+ optional same-frame echo) → live gate 4/4 preserved + checkboxes reflect model. One Sonnet
  implementer + one Opus validator.

## Note
Inc-A2 is the FIRST model→view (even if only initial-population + optional echo) — it does NOT build the
Gaia `.changed<T>()` reconcile (Inc-B). Its win: the editor layer view is now a real data-model with a
`DirtyVariable` target, unblocking every model→view increment. Keep it to that.

## Progress Log
- Milestone 1 (Tasks 1-4): DONE · commit 61dc4eb0 · Opus validator APPROVED · 2026-07-11
  - Editor layer view is now a REAL RmlUi data-model: `EditorLayers` [View] schema → generated
    `EditorLayers.g.h` (BindEditorLayersModel) → `EditorLayersView : IView` (ModelName "editor_layers",
    DocumentPath "assets/ui/editor.rml") → data-bound `editor.rml` (data-model/data-for/data-attr-id/
    data-class-checked) → registered via `WireEditorLayersView`→`SetView` so `UIRenderNode::CompileImpl`
    runs `CreateDataModel`+`Register`. Mirrors the shipped HUD-view path (no new codegen face, only `--view`).
  - **Binding is PROVABLY LIVE** (validator, independent code-path evidence + clean RmlUi log, NOT the
    gate's byte-identity). The gate stays byte-identical because the capture reads `compute_render_target`
    (offscreen 3D body) while the RmlUi panel composites onto the SWAPCHAIN image — the panel is never in
    the captured PNG (Case A, definitive). Not a false green.
  - **Same-frame echo ADDED (not deferred):** one `RefreshLayersView()` at the end of the `Stack().Dispatch`
    ApplyFn echoes toggle+undo+redo uniformly, because Undo/Redo re-run the ApplyFn (apply(false)/apply(true))
    and `applyToggle` is self-inverse. So model→view (initial population + echo) is DONE for the editor's
    own input — Inc-B's remaining reconcile job narrows to EXTERNAL/sim-driven changes.
  - **robin_hood ODR landmine caught + fixed:** `EditorLayersView.h` in a gaia-touching TU triggered the
    gaia-v3.11.5 vs RmlUi-v3.9.0 collision; fixed with `EditorLayersViewBridge.h/.cpp` mirroring
    HudViewBridge (forward-decl + raw-ptr-owning + explicit `~EditorApplication()`). No TU includes both
    gaia + RmlUi data-model headers. DURABLE: any new IView consumer in a gaia-touching app needs this bridge.
  - Live gate 4/4 (62px baseline), new golden `test_view_editor_layers_golden` 2/2, AppFlow 42/42,
    drift-guards (view_editor_layers/view_hud/appflow/callables) green, EditorLayers.g.h regen byte-identical
    (not hand-edited), AppFlow byte-guards hold, HUD unaffected (hud_golden 3/3, hud_smoke 6/6).
  - CARRY to Inc-B (still open): ToggleLayer handler ignores ReadU32's bool return — handle the false case
    when the fallible Gaia provider is wired.
