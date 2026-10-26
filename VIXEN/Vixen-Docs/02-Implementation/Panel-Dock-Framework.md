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
