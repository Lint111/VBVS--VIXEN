# sn-editorlive1 Part B: declarations as runtime data, and foundation UI/AppFlow elements (R874)

Design note, no product code. Base `17526307` + lane-editorlive1 Part A. Grounded in the files named inline; MEASURED = read from the tree at that commit, INFERRED = my reading, marked.

## 1. What is baked today, what already loads from data

| Thing | Baked into C++ | Already runtime data | Evidence |
|---|---|---|---|
| Action ids, param names/types, footprints | `FlowActionId`, `kActionDecls` in `AppFlow.g.h` (46 actions) | `action`/`decl`/`param` rows of `AppFlow.appflow`, parsed into the runtime view | `AppFlowBlobFile::Parse` |
| Element triggers (selector pattern -> action + param) | `kElementTriggers` (42) | `trigger` rows; **reloaded live** (`PollAppFlowFile`, `ReloadAppFlowData`) | measured in Part A step 3 |
| Key defaults, return edges | `kKeyDefaults`, `kReturnEdges` | `keydefault`/`return` rows; **reloaded live** | Part A witness 3 |
| Handler bodies | `rt_.RegisterHandler(FlowActionId::X, lambda)`, 41 `RegisterHandler` calls in `EditorApplication.cpp` | none | `AppFlowRuntime::handlers_` (map by id) |
| Compatibility gate | `kAppFlowShapeHash` | `shape 0x...` line, compared by `Parse` | `AppFlowBlobFile.cpp:245` |
| View fields: names, kinds, row structs | `EditorLayers.g.h`: `EditorLayerRow...` structs, `EditorLayersBind`, `BindEditorLayersModel` (typed `RegisterStruct/RegisterMember`) | **only HUD**: `hud.viewblob` + `BlobView`/`ViewStore` (reflection: name, kind, element fields), reloaded by `PollHudHotReload` | `ViewBlob.h`, `BlobView.h`; no `EditorLayers.viewblob` exists |
| View row producers | `GaiaLayerViewDataProvider`, `EditorLayersViewBridge.cpp` fill typed rows | none | |
| Panel declarations (`PanelDecl`: id, document, region, size, closable, transient...) | `EditorApplication::DeclarePanels` (C++) | layout rows only (`PanelDock::RowsJson`, user settings) | R766's open question |
| Documents / stylesheets | none | `.rml`/`.rcss`: **reloaded live since Part A** | `UiDocumentReloader` |
| Labels, field order, visibility | in `.rml` (`{{param.name}}`, row order, `data-if`) | yes, live since Part A | |

Findings that matter for the design (0 first, found while building Part A): (0) `AppFlowRuntime::Load` rebuilt the flow state, the undo stack and cleared the handlers, so every `.appflow` reload silently wiped the document's undo history (MEASURED: undo depth 1 -> 0 after editing a key default, `run1/`); Part A adds `AppFlowRuntime::Reload`, which replaces only triggers, key defaults and data targets; (1) the .appflow shape line is only *compared* with the compiled constant, it is not recomputed from the file, so the real compatibility guarantee is "the emitter wrote that hash", and a hand edit to an `action` row passes the gate but changes nothing a handler serves; (2) an action in data with no handler returns `RejectedByState` (`AppFlowRuntime.cpp:163`): silent, no diagnostic (R796 gap); (3) row structs repeat the same shape per use (`upId/downId/valueId/label/value` exist in `EditorParameterRow`, `EditorTransformRow`, `EditorShapeFieldRow`, and as `blendUpId/blendDownId/blendValueId` inside `EditorLayerRow`).

## 2. Proposed split

**(i) Reloaded live, no build** (most of it already true after Part A): labels and display text, field order and visibility (`data-if`, row order in the document), element-trigger patterns, key defaults, return edges, documents and stylesheets, and, with step 1 below, panel declarations as data.

**(ii) Needs code, so a build:** a new handler body; a new view *source* (a producer filling a new row kind); a new compiled-in row struct while views are typed. Adding a *field* to an existing view is (ii) today only because the bind function is typed; moving the editor view to the `ViewBlob`/`BlobView` path makes the field list data and leaves only the producer in (ii).

**(iii) Declared in data, nothing behind it:**
- Action with no handler: the catalogue (`appflow.list`) already lists it from data; add `implemented: false` (handler map lookup, no schema change); `DispatchById`/`BySelector` returns a new `DispatchResult::NoHandler` and publishes one located diagnostic ("action X is declared in AppFlow.appflow but no handler is registered; add RegisterHandler or remove the declaration"), instead of the silent `RejectedByState`. Reload does not fail on it: the data is valid, the action is just inert until code lands (R772: swap first, verify after).
- View field in data with no producer: the bound variable keeps its type default and the editor's reconcile publishes one `view.unfed` diagnostic per field at load (field name, view, file). Never a blank panel, never a throw (R213, R796).

## 3. Compatibility rule on reload (R333-style)

Keep the existing gate (shape hash must equal the compiled hash, else reject, keep last good) and make it honest: the emitter derives the shape hash from the interface part only (action ids and param schemas, states, transitions, guards, view field *types* when views move to blobs) and **excludes** presentation data (triggers' patterns, key defaults, labels, order). Then "data change that needs code" and "data change that does not" are distinguished by the hash itself, exactly as the existing message says ("alters the interface/graph, rebuild required"). Today a hand edit of a `keydefault` row reloads without touching the shape line (MEASURED in Part A step 3); whether the emitter excludes triggers from the hash is INFERRED, not verified in the kernel. The rule for new view data: a blob whose field names/kinds differ from the registered consumer version keeps the last good view (`BlobView::SetConsumerVersion` does this for HUD today).

## 4. Kernel emitter contracts that would change

- **First useful slice: none.** A "view presentation table" is not needed: labels/visibility/order already live in `.rml`. The no-handler diagnostic (2.iii) and `implemented:false` are VIXEN runtime only. **Slice 1 = no emitter change.**
- Slice 2 (editor view on the blob path): the VIEW emitter already writes `.viewblob` + `.blob.g.h` for HUD (`--view` blob output). Contract change: emit the editor views (`EditorLayers`, `PanelLayout`) as blobs too, including nested row structs, and let several arrays share one declared row type (a shared `[ViewRow]` struct instead of per-array structs). Needed for section 6 as well.
- Slice 3 (panel declarations as data): a declared-panel block in the AppFlow/view data file (id, document, region, stack, size, closable, header, transient, dismissible, view name). **This is a new declaration form: needs an owner ruling** (R766 left it open; STOP rule).

## 5. Lane plan (after review)

| Lane | Size | Content |
|---|---|---|
| sn-liveunhandled1 | S | `implemented` in the catalogue, `NoHandler` result + located diagnostic, `view.unfed` for unfed fields; tests; no emitter change |
| sn-uikit1 | L | the base set of section 8 (custom elements + one `kit.rcss` + kit fragments), migrate editor panels in the order of section 9 |
| sn-liveview1 | M | emit editor views as blobs; shared row type; editor loads them like HUD |
| sn-paneldata1 | M | panel declarations as data (after ruling) |
| sn-loaderui1 | S | loader (R862) consumes the same kit from its AppFlow app |

---

# R874 addendum: foundation elements, reused and overridden, effective everywhere

Owner: "baseline buttons, panels, input fields etc that can be reused and overridden for specific functionality, but if a functionality needs to be added it is added in one place and effect all existing instances of the pattern."

Searched for an existing component layer (none found): `grep -rli "custom element|ElementInstancer|template|component layer|widget|base element"` over `Vixen-Docs`, `libraries/RenderGraph`, `application/editor`; `Vixen-Docs/01-Architecture` file list for ui/panel/widget; `Panel-Dock-Framework.md` read in full; the undertow register for R766 (`### R766`, line 37882, "grow the existing View/HUD mount and AppFlow frameworks, not a parallel editor-only UI layer"). The only precedent for a registered Rml element is `<ut-text>` (`RmlTextElementRegistry`, registered once in `UIRenderNode`).

## 6. Inventory (MEASURED from `assets/ui/*` and the host, at this commit)

| Pattern | Where implemented | Copies | What differs |
|---|---|---|---|
| Button (`.button`, `hit-mask="none"`, id = AppFlow selector) | `.rml` markup + `.button` in `editor.rcss`; click -> `DrainClickedElementId` -> `DispatchBySelector` (host, one copy) | 21 `class="button"` in 4 panels (layers 10, inspector 9, outliner 1, picker 1); 29 `hit-mask="none"` incl. dock chrome (5); `.tool` and `.tab` in `dock.rml` are separate lookalikes | id source (`row.xId` vs literal), label, `data-class-selected/open` per panel |
| Toggle/checkbox | `.checkbox` + `data-class-checked` in `editor_layers.rml` | 1 | n/a (single) |
| Text field commit/revert (Enter commit, blur commit if edited, Escape revert then cancel, focus ownership of the keyboard) | **host, one copy**: `UiInputForwarder` | 1 | n/a |
| Value slot -> action param | `HasValueSlot` / `SelectorFromCommittedId` (one), id patterns in `AppFlow.appflow` triggers | 1 host; **16 typed-value trigger rows** (`set`, `blend`, `name`, field x/y/z/w, placement 9) each declared separately | the pattern text |
| Number field markup (input + - + reset + range) | hand-written in `.rml` | 4: parameters, shape fields, placement (`transform`), layer blend (layers panel + inspector); 6 `type="text"` inputs total | row struct differs (`EditorParameterRow`, `EditorShapeFieldRow`, `EditorTransformRow`, `EditorLayerRow.blend*`) with the same `upId/downId/valueId/label/value` set; input CSS `value-field` vs `name-field` vs `filter-field` (focus + selection styles written three times, `editor.rcss` ~171-262) |
| Nudge | handlers: `AdjustParameterUp/Down`, `EditProgramFieldUp/Down`, `NudgeLayerField` (blend, transform, primitive components via three lambdas) | 3 handler families, each with its own step and clamp | step size, clamp source |
| Reset to default | param: `ResetParameter` (always shown); shape: the set-selector with the default filled in (shown when `!isDefault`, label includes the default) | 2, different mechanisms | visibility, label, action |
| Range display | `.parameter-range` "range a-b" (inspector) and `.range` "a..b" (shape row) | 2, different text formats | format |
| Refusal message | each handler's `refuse(...)`/`CommitParameter` text | ~12 sites | wording |
| Drag-scrub | `StepParameterScrub` in `EditorApplication.cpp`, 200 px per range | **1, editor-only**, only on parameters (shape/placement fields cannot scrub) | n/a |
| Transient/dismissible, anchored | `PanelDecl.transient/dismissible`, `PanelDock::OpenAnchored`, `UiInputForwarder::DrainCancel` | 1 (generic) | n/a |
| Status/diagnostic line | `ReportEditorDiagnostic` -> `RefreshStatusView` -> `editor_status.rml` | 1 (+ the new `UiReload` code) | n/a |
| Panel chrome | `dock.rml` + `dock.rcss` | 1 | n/a |
| List/row with selection | layers rows, picker rows, outliner rows, op choices | 4 (`data-for` + button + `data-class-selected/open`) | row struct, class |
| Game HUD | `hud.rml`: 4 ellipse `hit-mask` buttons, each its own ~20-line block in `hud.rcss`; faction/event `data-for` rows; `building_inspector.rml`: 10 hand-written `.kv` rows and a private inline `<style>` repeating font/colour | 4 + 2 + 10 | colours, glyphs, anchors |

So the host side is mostly single-copy already (good: R764 worked); the duplication is in documents, row structs, trigger declarations and the per-field handlers/behaviours (nudge, reset, range, refusal, scrub).

## 7. Base elements (declared once)

`button`, `toggle`, `text-field`, `number-field` (value + range + reset + nudge + scrub + unit + refusal message), `choice-group`, `list` (rows with selection), `panel`, `flyout`, `toolbar`, `status-line`. Each has ONE definition with its behaviour on the base: keyboard ownership, commit/revert, refusal path, reset, drag, transient/dismiss.

Override/variant contract (what an instance may and may not change):
- **May override:** label/text, the bound view field, the action and the parameter slot it fills (a declared selector pattern), range source (view fields `minimum/maximum` or a literal), unit, step, a style variant (`variant="compact|danger|toggle"`, a class on the base's own style), visibility condition (`data-if`).
- **May not:** the behaviour contract (an instance cannot change what Enter/Escape/blur do, bypass the refusal path, or skip `hit-mask`/`pointer-events` rules), or restyle the base's structure (only its variants).

"Add a capability in one place": a capability is added to the base's behaviour class or fragment; every instance has it by construction because instances carry no copy. Part A makes the *markup/style* part of a base live: edit `kit.rcss` or a kit fragment and every instance updates on the running app (the watcher tracks files the element pulled in through RmlUi's file interface, so a fragment opened while instancing a document is a dependency of every document that uses it). A new *behaviour* needs code, once.

## 8. Mechanism options (R764: no parallel UI layer)

**(a) Rml custom elements + data-model conventions**, registered once in `UIRenderNode` like `<ut-text>`: `<ut-number field="param" action="SetParameter" step="0.1"/>`. Behaviour in C++ (one class per base element, uses the existing `UiInputForwarder` events and `DispatchBySelector`). Schema: nothing new in the kernel view/AppFlow schemas except (optional) one shared row type; ids derive by convention from the instance's selector prefix. Emitter change: none for the first slice. Cost: M. Weakness: the element's internal structure is C++ unless it reads a fragment.

**(b) Generated documents:** panels composed as runtime DATA (rows naming base elements + bindings) that a generic builder instantiates (`CreateElement` tree). Fits "declarations as runtime data" best and gives agents a data-only way to compose a panel. Schema: a new declaration form (a panel/row composition block) in the view/AppFlow schema, an emitter change, a builder in VIXEN. Cost: L. Weakness: it is a second authoring surface beside `.rml` (R764 risk) and loses RML's `data-for/data-if` expressiveness unless it re-implements it.

**(c) Mix (recommended):** base elements are Rml custom elements (behaviour once, in C++), each **instantiating its structure from a kit fragment file** (`assets/ui/kit/number-field.rml`, styles in `assets/ui/kit/kit.rcss`) so structure and style are data and live-reload with Part A; composition stays ordinary `.rml` documents bound to views (the existing path), with panel declarations-as-data (slice 3) added later if the owner rules for it. No kernel emitter change for the kit itself; the shared row type (section 4, slice 2) removes the four per-row id copies. The HUD uses the same kit (`<ut-button variant="round" hit-mask="ellipse">` replaces four copy-pasted rcss blocks) and the loader (R862: an AppFlow app on the same KFR/VIXEN UI path, generated loader kinds) uses the same elements and kit because the elements live in `UIRenderNode`, which every app already hosts: nothing in them names the editor.

Recommendation: **(c)**, built by `sn-uikit1`.

## 9. Migration order (each step: before/after capture, guard tests, what is deleted)

1. **status line** (1 doc, no inputs): `status-line` element. Guard: `test_panel_dock_documents` status checks, `UiCompositionHostTest`. Deletes: `.panel.status`, `.status-*` rules.
2. **inspector fields** (4 number-field copies, 6 inputs): `number-field` + the shared row. Guard: `test_ui_input_forwarder` (focus/commit/refuse suite), `ResetButtonsStayOnTheirFieldsRowAtDefaultAndMinimumInspectorWidth`. Deletes: `value-field/name-field` CSS, `parameter-row/controls/range` markup, `EditorParameterRow/TransformRow/ShapeFieldRow` and their `*Id` strings, the second range format and the second reset mechanism.
3. **layers rows and toolbar**: `list`, `toggle`, `toolbar`, `button`. Guard: `ViewEditorLayersGolden/Reconcile`, `AppFlowEditorToggleRenderTest`. Deletes: `.row/.checkbox/.col-*`, 10 hand-written buttons.
4. **picker flyout**: `flyout` + `text-field` (the third copy of the focus/selection CSS) + `list`. Guard: `PanelDockAnchored/Dismissible/Drag`, `UiInputForwarderTest.EscapeIsAGeneric...`. Deletes: `filter-field` CSS.
5. **dock chrome**: `tab`/`tool` buttons become `button` variants. Guard: `PanelDockDocumentsTest`, `PanelDockHost*`. Deletes: `.tab .tool` copies.
6. **game HUD panels**: HUD round buttons, `building_inspector` rows. Guard: `HudSmokeTest`, `HudViewTest`, `ViewHudGolden` (base red #13 unchanged), the HUD byte-identity capture harness (NOT run in this lane's evidence; needs a GPU capture job). Deletes: four rcss button blocks, the inline `<style>` in `building_inspector.rml`.

Every step ships a before/after full-frame readback through the Part A loop (edit, `ui.reload`, readback) so the migration itself is the proof that the reload path works for the kit.

## 10. Owner questions (3), each with a recommendation

1. **Panel declarations as data** (R766's open question): should the editor's `DeclarePanels` become a data block (id, document, region, size, closable, transient, dismissible) in the AppFlow/view data file, reloaded live? This is a new declaration form. *Recommend yes, but after the kit* (slice 3): it lets an agent add/move/rename a panel with no build, and the loader and game declare theirs the same way.
2. **Kit mechanism:** custom elements with kit fragments (c) versus data-composed panels (b). *Recommend (c)*: one authoring surface (RML + the kit), no second composition language; (b) can be layered on later without redoing the base elements.
3. **Unhandled declarations:** an action declared in data with no handler, or a view field with no producer: *recommend* visible-but-inert with one located diagnostic at load and on dispatch (never a silent reject, never a reload failure), and the shape hash covering interface only so "needs a build" is decided by the hash, not by a rule agents must remember.
