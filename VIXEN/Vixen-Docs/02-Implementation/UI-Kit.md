# UI Kit (R874): foundation elements declared once

Buttons, toggles, text and number fields, lists, toolbars, status line, panels and flyouts exist **once**. Every panel of
every app (editor, game HUD, loader UI) instances them; a capability added to an element reaches every instance on the
next build, and a change to the kit's markup or style reaches every instance on the **running app**, with no build.

## Where each part lives

| Part | File | Edit means |
|---|---|---|
| Behaviour (nudge / step / clamp, reset, range text, scrub, refusal snap-back, variants, hit-mask, attribute contract) | `libraries/RenderGraph/src/Ui/UiKit.cpp` (`KitElement`) + `include/Ui/UiKit.h` | one build; every instance has it |
| Structure (the parts inside an element) | `libraries/RenderGraph/assets/ui/kit/<element>.rml` (`button`, `toggle`, `number-field`, `choice-group`, `status-line`) | live reload of every document |
| Look | `assets/ui/kit/kit.rcss` | live reload of every document |
| Registration | `UIRenderNode` registers the kit once (like `<ut-text>`); nothing names the editor | |

The kit fragments are dependencies of **every** document (`UiDocumentReloader::SetLoadPrelude` opens them through RmlUi's
file interface inside each tracked load), so editing one reloads every panel; a fragment that does not parse keeps every
panel's old document and the error names the fragment `file:line` (status line `[UiReload] number-field.rml:11: ...`).

## Elements and the override contract

An instance may carry only these attributes (plus `id`, `class`, `style`, `title` and any `data-*` binding). Anything else
is refused with a located diagnostic at document load (`<doc>:<line>: <ut-x> attribute 'y' is not part of the kit override
contract`); `hit-mask` is the kit's (a button/toggle always stamps `none`, the `round` variant `ellipse`).

| Element | Attributes | Notes |
|---|---|---|
| `ut-button` | `label`, `variant` | id = the AppFlow selector it clicks; variants (space separated, each is a class): `row`, `name`, `tool`, `tab`, `round`, `selected`/`open`/`active` come from `data-class-*` |
| `ut-toggle` | `checked`, `variant` | id = selector |
| `ut-text-field` | `value`, `autofocus`, `variant` (`class="wide"`) | a real form control: `data-value` binds two-way; id = selector pattern with a value slot (`picker-filter-{filter}`) |
| `ut-number-field` | `label`, `value`, `value-id`, `reset-id`, `default`, `min`, `max`, `unit`, `step`, `variant` (`inline`) | `value-id` = the declared typed-value selector (`parameter-0-set-{value}`); `reset-id` = an action selector; `min`/`max` give the range (both finite = range text + nudge clamp) |
| `ut-choice-group` | `label`, `variant` | children are `ut-button` rows |
| `ut-list` | `variant` (`fill`) | scroll area |
| `ut-toolbar`, `ut-panel`, `ut-flyout` | `variant` | kit styling over the existing dock (the dock stays the panel framework, R766) |
| `ut-status-line` | `doc`, `state`, `undo`, `redo`, `message` | |

A `data-for` on a kit element needs content (`<ut-button data-for="..."> </ut-button>`): RmlUi stores the row template
from the element's inner RML, and a self-closing element has none.

## The one rules

* **Value changes** (typed + Enter/blur, nudge, scrub) all reach the host as one `ut-commit` event that `UiInputForwarder`
  queues exactly like a typed commit: one `DispatchBySelector`, one ActionStack entry, refusals through the handler's own
  diagnostic (the per-handler `refuse` texts stay the source of the reason; the status line is the one place that shows it).
* **Nudge**: `value + direction * step * multiplier`, clamped to `[min, max]` when both are finite. `step` attribute, else an
  eighth of a finite range, else 0.25. **Shift = x10** (capability added after the migration, one place: `UiKit.cpp`
  `kShiftStepMultiplier`). Pointer modifiers persist across frames in `UiInputForwarder`.
* **Scrub**: press the field's label and drag; one range per 200 px (40 steps when unbounded); the input previews, one commit
  on release (one undo entry).
* **Reset**: the reset part dispatches `reset-id`; it shows when `reset-id` is set and the value differs from `default`.
* **Range**: `range a–b` (en dash), one format.
* **Refusal**: the element mirrors typed text into its `value` attribute, so after a refused commit the data binding writes
  the model value back and the field snaps back.

## How to add a capability

Edit `KitElement` in `UiKit.cpp` (one place), add a headless case to `tests/test_ui_kit.cpp` (the pure rule in
`UiKit::Nudge`/`ScrubValue`/... or an Rml event on the element), build. No document changes. Example (done): Shift+nudge
x10 = `StepMultiplier(event)` in `Nudge`; shown working on a parameter, a shape field and a placement field in
`sn-uikit1-evidence/witness/final/08-*/09-*.png`.

## How to add an element

Add a row to `Kinds()` in `UiKit.cpp` (tag, fragment, contract attributes, required parts), a fragment file if it has parts,
rules in `kit.rcss`. The registry registers it and the load prelude makes its fragment a dependency of every document.

## Not here

Panel declarations as a data block (R874.2), the game HUD migration (round buttons, building inspector rows; see the
sn-uikit1 report `GAME HUD HANDOFF`) and the loader UI.
