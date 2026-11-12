# Panel Dock Framework (R766)

Every VIXEN panel — the editor's outliner, layers, inspector and status line, and the game's HUD and
building inspector — is **declared** by its app and **mounted by one dock**. No app hard-wires an RmlUi
document or a page layout any more. RmlUi `.rml`/`.rcss` files are per-panel content and style.

## Pieces

| Piece | Where | Role |
|---|---|---|
| `PanelDock` | `libraries/RenderGraph/include/Ui/PanelDock.h` | Pure model: declarations, layout rows, layout verbs, `Compute(width, height)` → `DockFrame`. No RmlUi. |
| `PanelDockHost` | `Ui/PanelDockHost.h` | Mounts the chrome and every declared panel through `IUiCompositionHost`, re-places them when the layout or window size changes, loads/saves the layout. RmlUi-free header. |
| `PanelLayout` [View] | `codegen/view-schemas/PanelLayout.cs` → `Ui/PanelLayout.g.h` | The layout state: stacks, panels (region, stack, visible, active, tab rect, button ids), viewport rect. Bound by the chrome; readable by agents. |
| `PanelLayoutView` | `Ui/PanelLayoutView.h` | The chrome's `IView` over the generated binder. |
| Dock chrome | `assets/ui/dock.rml` / `dock.rcss` | Stack frames and tab strips; buttons are AppFlow selectors. |
| `IUiCompositionHost` growth | `Ui/IView.h`, `UIRenderNode` | `Mount(view, documentPath)` (several panels share one view's model), `BindPrimary(view)`, `Place(handle, rect, visible, z)`, `Extent()`. |
| AppFlow vocabulary | `codegen/appflow-schemas/AppFlowReference.cs` | `OpenPanel`, `ClosePanel`, `FocusPanel`, `MovePanel`, `SplitPanel`, `FloatPanel`, `ResizeRegion`, `ResetLayout`; selectors `panel-{panelId}-tab`, `-close`, `-to-{region}`; Ctrl+R resets. |

## Declaring a panel

An app overrides `VulkanGraphApplication::DeclarePanels(PanelDock&)` and `PanelLayoutName()`. A `PanelDecl`
names: `id`, `title`, the bound `IView*`, its `document`, the default `region` (`Left`, `Right`, `Bottom`,
`Overlay`, `Floating`) and `stack` (split index), `size`/`minSize`, `closable`, `header` (a lone headerless
panel draws no tab strip) and `mount` (`Fragment`, or `Primary` for the node's primary document — the HUD,
which owns the HUD blob hot reload). The game declares `hud` (Primary, Overlay) and `building` (Overlay);
the editor declares `outliner`, `layers`, `inspector` and `status`. What an app does not declare is not
mounted.

## Layout rules

- Left and Right are full-height columns beside the viewport, Bottom spans the full width beneath them. A
  region is as wide as its stored size, never below its largest visible minimum, zero when empty, and
  shrinks so the viewport keeps at least `kMinViewport` (160 px). Docked panels never cover the viewport.
- A region holds stacks split along its long axis (equal shares); a stack holds tabs, one active.
- Overlay panels take the viewport rect with no chrome; Floating panels carry their own rect and draw above.
- Chrome z 500 (frames and tabs only), docked panels z 100, overlay z 0, floating 1000+ by focus order.

## Persistence

`PanelDock::RowsJson()` — `{"version":1,"regions":[{region,size}],"panels":[{panel,region,stack,order,
visible,active,focus,float:[x,y,w,h]}]}` — at `UserSettingsDirectory()/layouts/<app>.json`
(`VIXEN_PANEL_LAYOUT` overrides the file, `VIXEN_SETTINGS_DIR` the root). Rows for undeclared panels are
dropped; declared panels missing from the rows keep their defaults. The editor saves after every panel verb.

## Agents

`appflow.read` returns `panelLayout` (`viewport`, `stacks`, `panels` with content rects, the persisted
`rows`, `layoutPath`, `revision`); `appflow.read {noun: "PanelLayout_panels"}` (and the other
`PanelLayout_*` nouns) returns one field. Each verb records a `panel.<verb>` channel event.

## Keyboard, text and pointer input

Every app that hosts RmlUi forwards its `InputState` into the `Rml::Context` (`UiInputForwarder`, owned by `UIRenderNode`,
fed through the node's optional `INPUT_STATE` slot): pointer moves and button edges in arrival order, wheel, every raw key
transition and every typed code point (`InputNode` registers the GLFW char callback). With no focusable element a document
sees only `:hover`/pointer state, so the game HUD is unchanged.

- **Focus rule.** A focused text control (`<input type="text">`) owns the keyboard: `UIRenderNode::WantsKeyboard()` is true, the
  editor's chord poll advances its edge state but dispatches nothing (`KeyEdgeTracker::Poll(..., typing)`), the camera key
  axes are dropped, and `InputNode` no longer treats Escape as the shutdown request, the movement keys as axes or the digits as
  debug-mode switches. Enter commits the value and blurs; Escape restores the value the field had on focus and blurs; leaving
  the field after an edit commits it.
- **Pointer ownership.** `WantsPointer()` is true while the pointer is over a UI element, or a press that began on one is still
  held; a press that began outside the UI stays the world's (a camera orbit that crosses a panel keeps orbiting). The editor
  drops camera orbit and wheel zoom while the UI owns the pointer. Editor panel bodies take pointer events (the wheel scrolls
  a panel, a click on panel background is no longer a world pick).
- **Typed values reach actions through the existing element triggers.** A text control's `id` is an element-trigger pattern with
  its value slot left as the literal `{<param>}` (`parameter-0-set-{value}`, `layer-2-name-{name}`,
  `layer-3-field-halfExtents-x-{x}`, `layer-0-placement-px-{px}`, `layer-1-blend-{radius}`). On commit the forwarder fills the
  slot with the typed text and the host calls `DispatchBySelector(<filled selector>)` (`UIRenderNode::DrainCommittedInputs()`),
  so the last placeholder binds the text to the action parameter it names: one `ActionStack` entry, refusals through the
  handler's normal diagnostic path. The field's value is data-bound one way from the model (`data-attr-value`); after a commit
  the node re-evaluates every bound value, so a refused value snaps back to the model's.
- **Agents** use the same path: the channel verb `appflow.input` queues text, keys and pointer events into `InputNode`'s callback
  ring (see AppFlow-Agent-Channel.md), so typing in a field can be witnessed without a human.

## Flyout panels (sn-editorpicker1)

A flyout is an ordinary declared panel (the editor's `picker`: Floating, closable, header), opened by `OpenPanel` (selector
`panel-{panelId}-open`) and closed by `ClosePanel`; it adds no framework concept. What it relies on:

- **Focus on open.** `UIRenderNode::ApplyPlacements` calls `ElementDocument::Show()` when a placement makes a panel visible, and
  RmlUi's default focus flag focuses the document's first `autofocus` element: a text control marked `autofocus` takes the keyboard
  the moment its panel opens (and the focus rule above then applies).
- **Live text.** A text control with `data-value="<view field>"` writes the bound `Rml::String` on every change (RmlUi's own two-way
  binding over the generated `Bind(name, ptr)`); the host reads the field next frame. Typed values that must reach an *action* still use
  the committed `{slot}` id.
- **Start closed.** `PanelDecl` has no initial-visibility flag, so the editor closes the flyout after the dock is built and
  never trusts the saved layout for it.
