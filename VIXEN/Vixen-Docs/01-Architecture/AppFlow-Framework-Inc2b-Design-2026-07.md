# AppFlow Framework — Increment 2b Design (windowed editor on AppFlowRuntime + live undo/redo)

**Date:** 2026-07-05
**Status (reverified 2026-09-01):** COMPLETE — windowed `EditorApplication` routing and live
undo/redo shipped and merged (`eea9e1ff`, merge `79786a66`).
**Program:** AppFlow app-flow/state/action framework — see `AppFlow-Framework-Design-2026-07.md`
**Builds on:** Inc-2 (SHIPPED to local main `1a072c1c`) — `LayerController`, `ActionStack` snapshot engine, `AppFlowRuntime.ToggleLayer/Undo/Redo`, the `EditorDocumentModel` mask seam, and the M4 headless GPU toggle/undo gate.

---

## 1. Goal

Put the **live windowed editor** on `AppFlowRuntime` so a layer toggle is **undoable in the running app**.
Inc-2 wired the toggle→re-flatten→render→undo loop and proved it *headless* (`test_appflow_editor_toggle_render`), but the actual windowed `EditorApplication` still toggles a **raw `LayerController` directly** (`EditorApplication.cpp:149-154`) — bypassing the ActionStack, so there is **no undo in the live editor**. Inc-2b closes that: the editor owns an `AppFlowRuntime`, routes clicks through `rt.ToggleLayer(...)`, and adds Ctrl+Z / Ctrl+Y keybindings — verified by an **unattended, frame-scripted windowed run** on the Windows side.

**This is the "Inc-2b — windowed EditorApplication rewire" the Inc-2 design §6 deferred.**

---

## 2. Scope (user-locked)

**IN (decided — "Runtime + undo/redo"):**
- `EditorApplication` **owns an `AppFlowRuntime`** in place of its raw `LayerController layers_` member. Layer state now lives inside the runtime (`rt_.Layers()`), consistent with the AppFlow spine.
- The click→toggle path goes **through the ActionStack**: `rt_.ToggleLayer(layerIndex, reflattenCallback)` (gaining live undo), replacing the direct `layers_.Toggle` + `dirty_` in `EditorApplication::ToggleLayer`.
- **Ctrl+Z → `rt_.Undo()`**, **Ctrl+Y → `rt_.Redo()`**, each triggering a re-flatten. Edge-detected (press-only), mirroring the existing `S`-key save pattern.
- The existing **`ParseLayerToggleId` hand-parse of `"layer-<N>-toggle"` is KEPT** — the click-id→layerIndex mapping stays hand-coded this increment.

**OUT (deferred, unchanged from the Inc-2 roadmap):**
- **BindingStore selector-resolution** (retiring `ParseLayerToggleId` for a data-declared selector→action map) — needs a param-resolution story (which layer index) that **Inc-3** owns. Not this increment.
- FlowStateMachine states in the editor (Editing/Simulating/Paused), ModuleController, PanelLayout, `graph.Run()` consolidation, undertow migration.
- Grouping multiple toggles into one undo unit (the ActionStack supports `BeginGroup`/`EndGroup`, but the editor has no multi-toggle gesture yet).

---

## 3. Architecture — the rewire

### 3.1 `EditorApplication` member change

Today (Inc-2): `LayerController layers_; bool dirty_ = false; bool sKeyWasDown_ = false;`
Inc-2b: replace `layers_` with an owned runtime.

```cpp
// EditorApplication.h
#include "AppFlowRuntime.h"
...
Vixen::AppFlow::AppFlowRuntime rt_{nullptr, /*sender*/0};  // bus optional; editor doesn't consume the events yet
bool dirty_ = false;
bool sKeyWasDown_ = false;
bool ctrlZWasDown_ = false;   // undo edge-detect
bool ctrlYWasDown_ = false;   // redo edge-detect
```

`LoadDocument` calls `rt_.Load()` (loads the reference vocab tables) then `rt_.Layers().SetLayerCount(doc_.LayerCount())` — replacing today's `layers_.SetLayerCount(...)`. The mask is read via `rt_.Layers().Mask()` everywhere `layers_.Mask()` is used today (`ApplyDocumentToScene:104`, `SaveDocument:161`).

> **Bus = nullptr is intentional this increment.** `AppFlowRuntime::Publish` no-ops when `bus_` is null (`AppFlowRuntime.cpp:13`), so the editor gets the ActionStack/undo behaviour without wiring event consumption. Passing the app's real `MessageBus` (so a future HUD reacts to `AppFlowChangedEvent`) is a clean follow-up, not required for undo.

### 3.2 The toggle path (through the ActionStack)

`EditorApplication::ToggleLayer(uint32_t)` today mutates `layers_` and sets `dirty_`. Inc-2b routes it through the runtime, with the re-flatten as the `onChanged` callback the runtime already fires on **both** apply and undo:

```cpp
void EditorApplication::ToggleLayer(uint32_t layerIndex) {
    rt_.ToggleLayer(layerIndex, [this]{ dirty_ = true; });
}
```

- `AppFlowRuntime::ToggleLayer` (Inc-2, `AppFlowRuntime.cpp:74`) records a self-inverse ActionStack entry that flips `Layers().Toggle(index)` and fires `onChanged` on forward apply AND on undo's `apply(false)`. So a click sets `dirty_`; a later `Undo()` also sets `dirty_`. The `dirty_`→`ApplyDocumentToScene` tail in `Update()` (`EditorApplication.cpp:198-203`) is **unchanged** — it re-flattens with `rt_.Layers().Mask()` regardless of what moved the mask.
- Out-of-range clicks: `LayerController::Toggle` no-ops (returns false); `ToggleLayer`'s `onChanged` won't fire on a no-op if we gate it — but the runtime's current `ToggleLayer` fires `onChanged` unconditionally inside the apply lambda even for an out-of-range index (it calls `Layers().Toggle` which no-ops but the lambda still runs `onChanged`). **This is fine** — a spurious `dirty_=true` re-flattens to the identical mask (idempotent, one wasted bake). `ParseLayerToggleId` already filters non-editor ids to -1 before we ever call `ToggleLayer`, so real out-of-range is only a malformed `"layer-99-toggle"`, not a hot path. (Noted; not worth a guard this increment.)

### 3.3 Undo/redo keybindings (in `Update`)

Add to `EditorApplication::Update()`, next to the existing `S`-save block, using the same `GetWindowHandle()` + `glfwGetKey` edge-detect. Ctrl is `GLFW_KEY_LEFT_CONTROL`/`RIGHT_CONTROL`:

```cpp
const bool ctrl = glfwGetKey(window, GLFW_KEY_LEFT_CONTROL)==GLFW_PRESS
               || glfwGetKey(window, GLFW_KEY_RIGHT_CONTROL)==GLFW_PRESS;
const bool zDown = ctrl && glfwGetKey(window, GLFW_KEY_Z)==GLFW_PRESS;
const bool yDown = ctrl && glfwGetKey(window, GLFW_KEY_Y)==GLFW_PRESS;
if (zDown && !ctrlZWasDown_) rt_.Undo();   // onChanged (set inside ToggleLayer's apply) fires → dirty_=true
if (yDown && !ctrlYWasDown_) rt_.Redo();
ctrlZWasDown_ = zDown; ctrlYWasDown_ = yDown;
```

`rt_.Undo()`/`rt_.Redo()` re-run the stored apply lambda, which sets `dirty_`; the `Update` tail re-flattens. **No new re-flatten call site** — undo/redo reuse the exact `dirty_` path a toggle uses.

### 3.4 What is removed / what stays

- REMOVED: the raw `LayerController layers_` member; the direct `layers_.Toggle` in `ToggleLayer`.
- KEPT unchanged: `ParseLayerToggleId`, `DrainClickedElementId` drain, the `S`-save block, `SaveDocument` (now reads `rt_.Layers().Mask()`), `ApplyDocumentToScene` (reads `rt_.Layers().Mask()`), the `dirty_` re-flatten tail.
- `EditorApplication` must now link `AppFlow` — already added in Inc-2's Task-5 CMake fix (`editor/CMakeLists.txt` links `AppFlow`), so **no new link edge** is needed. (Confirm at plan time.)

---

## 4. Verification (user-locked: env-driven scripted windowed run)

The authoritative proof is the **real windowed editor**, run unattended, Windows-side (per the new build rules — Windows-native + `vixen-ninja`; live-run-is-authoritative).

### 4.1 The frame-scripted harness — `VIXEN_EDITOR_SCRIPT`

Mirror the existing `VIXEN_RESIZE_AT_FRAME` one-shot pattern (`VulkanGraphApplication.cpp:341-351`: a `getenv` read in a `static` initializer + a per-Update frame counter). Add to `EditorApplication::Update()` an env-gated script that injects the same actions a user would, at set frames:

```
VIXEN_EDITOR_SCRIPT="toggle:2@30,undo@60,redo@90"   # action[:arg]@frame, comma-separated
VIXEN_EDITOR_CAPTURE_FRAMES="0,45,75,105"           # frames to dump a PNG (post-reflatten)
VIXEN_EXIT_AFTER_FRAMES=120                          # existing knob — clean exit
```

At each Update tick, if the tick matches a scripted action, call the SAME method the input path calls (`ToggleLayer(2)` / `rt_.Undo()` / `rt_.Redo()`) — so the harness exercises the real dispatch, not a shortcut. At a capture frame, grab the presented swapchain image to a PNG.

> The action injection deliberately calls the editor's own `ToggleLayer`/`rt_.Undo`/`rt_.Redo`, NOT `layers_` directly — the harness must prove the *wired* path (click-equivalent → ActionStack → re-flatten → undo).

### 4.2 Frame capture — a dedicated PNG-capture RenderTarget (design of record; user-directed)

**Capture from an offscreen `RenderTargetNode`, NOT a swapchain readback.** VIXEN already has the right primitive: `RenderTargetNode` (`RenderTargetNode.h`, type 115) allocates an offscreen color target (`RenderTargetData : IRenderTarget`, default `VK_FORMAT_R8G8B8A8_UNORM`, `COLOR_ATTACHMENT | SAMPLED` usage, persistent across recompile), and the whole recording pipeline already draws into an `IRenderTarget*` rather than the swapchain directly (AR#28 — recording nodes depend on `IRenderTarget`, both swapchain and offscreen targets implement it). Reading back a stable, fixed-size offscreen target is **deterministic** and **present-timing-race-free**, unlike a swapchain-image copy (variable extent, acquire/present sync, format-usage stripping — see `IRenderTarget.h`'s note on the swapchain silently dropping storage usage).

**Why this is the better approach (vs. swapchain readback):**
- Fixed, known extent → the toggle/undo PNG diff is stable frame-to-frame (no resize/DPI variance).
- No present-sync race — the offscreen target is fully written before the capture copy; no need to hook acquire/present.
- Reuses the render-graph node model instead of bolting a raw `vkCmdCopyImage` onto the swapchain path.
- The exact device→host `VkImage`→CPU-buffer→`stbi_write_png` readback `test_editor_document_render.cpp` already performs on an offscreen target applies verbatim — the windowed capture reads a `RenderTargetData` image, identical to what that test reads.

**Two build options for the capture target (plan picks; A recommended):**
- **(A, recommended) A capture `RenderTargetNode` in the editor graph.** `EditorApplication::BuildRenderGraph` adds (or reuses) a `RenderTargetNode` sized to a fixed capture extent that the scene/composite also renders into (or that mirrors the main render target). At a scripted capture frame, read that node's `RENDER_TARGET` output (`IRenderTarget* → GetCurrentImage()`) back to host and `stbi_write_png`. Composes cleanly; the capture target persists across recompile (FR-7).
- **(B, fallback) Reuse an existing render target** already in the interactive graph if one is present at a stable extent, reading its `GetCurrentImage()` at the capture frame. Less new wiring, but depends on graph shape; A is the clean, explicit choice.

The readback helper (device→host copy + `stbi_write_png`) is **shared with the render tests** — extract/reuse `test_editor_document_render.cpp`'s existing `RenderTarget`-image→RGBA8 readback so there is one implementation. Gate the whole capture behind `VIXEN_EDITOR_CAPTURE_FRAMES` being set (zero cost — and no capture-target overhead — in normal interactive runs).

PNGs go to a scripted out-dir (e.g. `temp/editor_capture_<frame>.png`), NOT the repo tree.

### 4.3 The assertion (a checker script, run after the windowed exit)

The windowed `.exe` produces the PNGs; a small **post-run checker** (a gtest OR a shell/py step in the win `.bat`) asserts:
- `png[45]` (after `toggle:2@30`) **differs** from `png[0]` (initial) at the bore column — the toggle rendered. (Reuse the M4 bore-diff threshold logic.)
- `png[75]` (after `undo@60`) **== `png[0]`** byte-for-byte — undo restored the live render exactly.
- `png[105]` (after `redo@90`) **== `png[45]`** — redo re-applied.

This is the windowed analogue of M4's headless assertion, but through the **actual windowed input/dispatch path**. Byte-for-byte on the live swapchain is the strong proof (a broken undo leaves the toggled mask → `png[75] != png[0]` → fail).

### 4.4 Offline safety net (kept cheap)

The M4 headless gate (`test_appflow_editor_toggle_render`) already proves the toggle/undo LOGIC through `AppFlowRuntime` and stays green (no-regression). Inc-2b adds no new offline unit beyond confirming the editor still builds + that gate still passes; the windowed scripted run is the new authoritative gate.

---

## 5. Error handling + constraints

- **No throw across the Update tick** — `VulkanGraphApplication::Update` already wraps the body in try/catch (`:321`); the new toggle/undo/capture code lives inside it. `rt_.Undo()`/`Redo()` return typed `DispatchResult` (never throw); an empty stack returns `NothingToUndo` (a no-op, no re-flatten needed — but firing `dirty_` on a no-op undo is harmless/idempotent, so no special-casing).
- **Capture readback failure** propagates to `lastEditorError_` + a logged error, exactly as `ApplyDocumentToScene` failures do; it must not crash the frame loop (a failed screenshot ≠ a failed render).
- **`VIXEN_EDITOR_SCRIPT` malformed** → log a warning and ignore the bad token (never abort the app); an unset env = normal interactive editor (zero behaviour change).
- **≤32 layers** — inherited from `LayerController` (Inc-2), unchanged.
- **Windows-side build/test** — per the new repo rule (`commands.md` §Build Environment), the scripted windowed run is done Windows-side (`vixen-ninja`, real GPU); WSL is the fallback only.

---

## 6. Files (anticipated — the plan finalizes)

**Modified:**
- `VIXEN/application/editor/include/EditorApplication.h` — replace `LayerController layers_` with `AppFlowRuntime rt_`; add `ctrlZWasDown_`/`ctrlYWasDown_`.
- `VIXEN/application/editor/source/EditorApplication.cpp` — `LoadDocument` (rt_.Load + SetLayerCount), `ToggleLayer` (→ rt_.ToggleLayer), `ApplyDocumentToScene`/`SaveDocument` (rt_.Layers().Mask()), `Update` (undo/redo keys + `VIXEN_EDITOR_SCRIPT`/capture harness).
- `VIXEN/application/editor/source/EditorApplication.cpp` `BuildRenderGraph` — add a capture `RenderTargetNode` (option A) sized to a fixed capture extent, wired so the scene/composite renders into it (or mirrors the main target). Only materialized/read when `VIXEN_EDITOR_CAPTURE_FRAMES` is set.
- An **editor-local capture helper** — `IRenderTarget* → GetCurrentImage()` device→host readback + `stbi_write_png`, EXTRACTED/SHARED with `test_editor_document_render.cpp`'s existing offscreen-target readback (one implementation). Editor-local (or a small shared util), not a burden added to the base `VulkanGraphApplication`.
- `VIXEN/temp/win_*.bat` — a `run_editor_script.bat` template that sets the `VIXEN_EDITOR_SCRIPT`/capture/exit env inside the batch and runs the editor `.exe` (env-in-batch, per the WSL-env-vars rule).

**New:**
- The post-run PNG checker (a gtest `test_editor_toggle_undo_capture` that reads the dumped PNGs and asserts the 3 relations, OR a scripted check — plan picks; a gtest is more durable/CI-able).

**Unchanged:** `AppFlowRuntime`/`LayerController`/`ActionStack` (Inc-2 — consumed, not modified); `AppFlow.g.h`; the M4 headless gate; `ParseLayerToggleId`; the flattener; `RenderTargetNode` (consumed as-is — the capture target is a standard instance of it, no node change).

---

## 7. Deferred → the increments after this

- **Inc-3:** BindingStore selector-resolution (retire `ParseLayerToggleId`) + binding param-source resolution (layer index as a typed param) + Save/param-set + ModuleController.
- FlowStateMachine wired to editor modes; wire the real `MessageBus` into `rt_` so a HUD reacts to `AppFlowChangedEvent`.
- `graph.Run()` consolidation (design §7d); PanelLayout; undertow migration.
