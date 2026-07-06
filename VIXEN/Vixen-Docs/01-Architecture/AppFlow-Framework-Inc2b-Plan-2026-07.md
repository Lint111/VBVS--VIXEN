# AppFlow Framework — Increment 2b Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Put the live windowed editor on `AppFlowRuntime` so a layer toggle is undoable in the running app (Ctrl+Z / Ctrl+Y), proven by an unattended, frame-scripted windowed run that captures PNGs from an offscreen render target and asserts toggle≠initial / undo==initial / redo==toggle.

**Architecture:** `EditorApplication` swaps its raw `LayerController layers_` for an owned `AppFlowRuntime rt_`; the click→toggle path routes through `rt_.ToggleLayer(index, [this]{dirty_=true;})` (through the ActionStack, gaining undo); Ctrl+Z/Y call `rt_.Undo()`/`rt_.Redo()` which reuse the same `dirty_`→re-flatten tail. A `VIXEN_EDITOR_SCRIPT` env harness injects toggle/undo/redo at set frames and a `VIXEN_EDITOR_CAPTURE_FRAMES`-gated readback of an offscreen `RenderTargetNode` dumps PNGs; a post-run gtest asserts the three pixel relations.

**Tech Stack:** C++23, CMake, GoogleTest, VIXEN AppFlow (Inc-2: `AppFlowRuntime`/`LayerController`/`ActionStack`) + editor app + RenderGraph (`RenderTargetNode`, `IRenderTarget`) + Vulkan + GLFW. Built + run **Windows-side** (`vixen-ninja` preset).

**Design doc:** `VIXEN/Vixen-Docs/01-Architecture/AppFlow-Framework-Inc2b-Design-2026-07.md`
**Inc-2 (shipped, built on):** local main `1a072c1c` — `AppFlowRuntime.ToggleLayer(index, onChanged)` / `Undo()` / `Redo()` / `Layers()`; `EditorDocumentModel::{FlattenToRecipeEntry,Save}(uint32_t mask, ...)`; the M4 headless gate `test_appflow_editor_toggle_render`.

## Global Constraints

- C++ standard: **C++23**.
- **No throw across the Update tick** — `VulkanGraphApplication::Update` already wraps its body in try/catch; all new toggle/undo/capture code lives inside that guard. `rt_.Undo()`/`Redo()` return typed `DispatchResult` (never throw); an empty stack → `NothingToUndo` (a no-op).
- **PREFER WINDOWS-SIDE build/test (strong default, esp. GPU work — this whole increment).** Use the `vixen-ninja` preset via a `.bat` through `cmd.exe /c` (env vars set INSIDE the `.bat`; bash env does not reach a Windows `.exe`). WSL (`vixen-wsl`) is the fallback only. Templates: `VIXEN/temp/win_configure.bat`, `win_build.bat`.
- **Watch long builds by POLLING, not blind-waiting.** A `cmake --build` here auto-backgrounds; run a foreground interval loop (~15–30s) that tails the build log, and NEVER launch overlapping builds of one target (they race+truncate the binary).
- **Tests run via direct gtest binaries**, NOT `ctest` (KI-014).
- **Do NOT regenerate `AppFlow.g.h`** and do NOT modify `AppFlowRuntime`/`LayerController`/`ActionStack` (Inc-2 — consumed as-is). Do NOT modify `RenderTargetNode` (consumed as a standard node instance).
- **Keep the interactive editor unchanged when the env knobs are unset** — `VIXEN_EDITOR_SCRIPT` / `VIXEN_EDITOR_CAPTURE_FRAMES` unset ⇒ zero behaviour change, zero capture-target overhead.
- Commit per task. In-tree git ops are pre-blessed in the pipeline worktree. Do NOT commit `build/` or captured PNGs.

## Milestone Map (context-manager pipeline)

- **Milestone 1 — Runtime swap + undo/redo (Tasks 1–2):** `EditorApplication` owns `AppFlowRuntime`; click→`rt_.ToggleLayer`; Ctrl+Z/Y keybindings; editor builds + links. Offline: the M4 headless gate still green (no-regression).
- **Milestone 2 — Capture RenderTarget + scripted harness (Tasks 3–4):** the `VIXEN_EDITOR_SCRIPT` frame-action injector + a `VIXEN_EDITOR_CAPTURE_FRAMES`-gated offscreen `RenderTargetNode` readback→PNG helper (shared with the render-test readback).
- **Milestone 3 — Windowed gate + close-out (Tasks 5–6):** the `run_editor_script.bat` + the post-run PNG-assertion gtest; the authoritative Windows-side scripted windowed run (toggle≠initial / undo==initial / redo==toggle); docs.

## Progress Log

- Milestone 1 (Tasks 1–2): DONE · commits 0e81dc3f (Task 1), 50b1e52e (Task 2) · Opus validator APPROVED (re-built editor Windows-side + re-ran M4 gate on WSL/Dozen) · 2026-07-05
  - `EditorApplication` owns `AppFlowRuntime rt_{nullptr,0}` (raw `LayerController layers_` removed); `LoadDocument` calls `rt_.Load()` THEN `rt_.Layers().SetLayerCount()` (Load-bearing — populates the action table `ToggleLayer` dispatches against; without it every toggle silently no-ops; ordering guaranteed by main.cpp Load→Initialize→Update). `ToggleLayer` → `rt_.ToggleLayer(i, [this]{dirty_=true;})` (through ActionStack → undoable). Ctrl+Z→`rt_.Undo()` / Ctrl+Y→`rt_.Redo()` edge-detected in Update; both reuse the UNCHANGED `dirty_`→ApplyDocumentToScene tail (no duplicate re-flatten). `ParseLayerToggleId`/DrainClickedElementId click path unchanged (BindingStore deferred to Inc-3).
  - Editor builds+links Windows-side (663/663 first build; incremental relink clean; AppFlow already on the editor link line from Inc-2). M4 headless gate `test_appflow_editor_toggle_render` PASSED (boreDiffPixels=6400) — validator re-ran on WSL/Dozen. AppFlow libs + AppFlow.g.h UNTOUCHED; tree clean.
  - **DURABLE (env): the GPU-shader render tests only build on the WSL side** — `test_critical_nodes.cmake` gates that test group on the WSL-provisioned Linux glslc, so a Windows/MSVC configure SKIPS them. So: editor/app builds go Windows-side (fast); the GPU render-gate tests run WSL-side (Dozen/RTX 3060). M3's windowed gate must account for this split. Also: the harness "task completed" notification can fire while ninja is STILL running — verify via `tasklist.exe|grep ninja` / `pgrep ninja` before trusting a build finished (both M1 workers hit this; already in the friction log).
- Milestone 2 (Tasks 3–4): DONE · commits d87aca38 (Task 3), 5867d645 (Task 4) · Opus validator APPROVED (verified KI-007 sync reasoning to source + ring-slot timing; re-built editor Windows-side) · 2026-07-05
  - Capture source = the EXISTING `compute_render_target` RenderTargetNode (the voxel-raymarch offscreen target, `STORAGE|TRANSFER_SRC`, R8G8B8A8, follows swapchain) — reused, not a new node. Readback = new shared header `RenderGraph/include/Debug/RenderTargetReadback.h` `CaptureRenderTargetToPng(...)` (device→host copy + stbi_write_png; new `StbImageWriteImpl.cpp` TU + editor links `stb`, no ODR collision — VixenApp doesn't reach Profiler's stb TU). `VIXEN_EDITOR_SCRIPT="toggle:2@30,undo@60,redo@90"` injector + `VIXEN_EDITOR_CAPTURE_FRAMES` dumps in Update; scripted frames call the WIRED methods (`ToggleLayer`/`rt_.Undo`/`rt_.Redo`); injection BEFORE the dirty_ re-flatten, capture AFTER. Zero overhead when envs unset. Editor links Windows-side (0 errors). AppFlow libs / RenderTargetNode / ComputeDispatchNode UNTOUCHED.
  - **KEY CORRECTNESS (validator-confirmed to source):** the readback does NO layout transition — `ComputeDispatchNode::BlitRenderTargetToSwapchain` leaves the target in `TRANSFER_SRC_OPTIMAL` and privately tracks it (`renderTargetImageLayouts_`, ComputeDispatchNode.cpp:708) as the next frame's compute-write `oldLayout` (`:402-404`); a transition in the readback would desync that → reintroduce KI-007. Ring-slot timing holds: `Update()` runs before `Render()` (which advances `currentIndex`), so the capture reads the SAME slot the prior blit left in TRANSFER_SRC_OPTIMAL. Same-layout memory barrier + `vkCmdCopyImageToBuffer` from a valid copy-source layout. **DURABLE: an offscreen-target readback that runs pre-Render must NOT transition the layout — it desyncs ComputeDispatchNode's private KI-007 layout tracking; read the same ring slot at its post-blit TRANSFER_SRC_OPTIMAL.**
  - **CARRY TO M3 (non-blocking note):** a code comment at EditorApplication.cpp:382-384 claims the capture runs "inside the base Update's try/catch" — INACCURATE (the base try/catch is scoped to the base method body, which returns before the editor-Update-override body runs; the editor Update override is UNGUARDED, pre-existing since Inc-2). Not a functional defect (CaptureFrameToPng + the parse helpers provably never throw). M3 should wrap the editor `Update()` override body in a try/catch (satisfying design §5's no-throw-across-the-tick) + correct the comment.

---

## File Structure (Inc 2b)

**Modified:**
- `VIXEN/application/editor/include/EditorApplication.h` — replace `LayerController layers_` with `Vixen::AppFlow::AppFlowRuntime rt_`; add `ctrlZWasDown_`/`ctrlYWasDown_`; the capture members are added in Task 3.
- `VIXEN/application/editor/source/EditorApplication.cpp` — `LoadDocument` (rt_.Load + Layers().SetLayerCount), `ApplyDocumentToScene`/`SaveDocument` (`rt_.Layers().Mask()`), `ToggleLayer` (→ `rt_.ToggleLayer`), `Update` (undo/redo keys + `VIXEN_EDITOR_SCRIPT` + capture), `BuildRenderGraph` (capture RenderTarget, Task 3).

**New:**
- An editor-local capture helper (`IRenderTarget*`→host RGBA8→`stbi_write_png`), shared with `test_editor_document_render.cpp`'s readback.
- `VIXEN/temp/run_editor_script.bat` — sets `VIXEN_EDITOR_SCRIPT`/`VIXEN_EDITOR_CAPTURE_FRAMES`/`VIXEN_EXIT_AFTER_FRAMES` inside the batch and runs the editor `.exe`.
- A post-run PNG-assertion gtest (reads the dumped PNGs, asserts the 3 relations).

**Unchanged:** `AppFlowRuntime`/`LayerController`/`ActionStack`; `AppFlow.g.h`; `RenderTargetNode`; `ParseLayerToggleId`; the flattener; the M4 headless gate.

---

### Task 1: `EditorApplication` owns `AppFlowRuntime` (runtime swap)

**Files:**
- Modify: `VIXEN/application/editor/include/EditorApplication.h`
- Modify: `VIXEN/application/editor/source/EditorApplication.cpp` (LoadDocument, ApplyDocumentToScene, SaveDocument, ToggleLayer)

**Interfaces:**
- Consumes: `Vixen::AppFlow::AppFlowRuntime` (Inc-2) — `Load() → LoadResult`, `Layers() → LayerController&`, `ToggleLayer(uint32_t, std::function<void()>) → DispatchResult`. `LayerController::{SetLayerCount,Mask}`.
- Produces: `EditorApplication` with an owned `rt_` and no raw `layers_`; `ToggleLayer` dispatches through the ActionStack.

- [x] **Step 1: Read the current seam.** Via `codegraph explore "EditorApplication.h EditorApplication.cpp layers_ ToggleLayer ApplyDocumentToScene SaveDocument LoadDocument"`. Note every `layers_` use: `EditorApplication.h` (the member), `LoadDocument` (`layers_.SetLayerCount`), `ApplyDocumentToScene` (`layers_.Mask()`), `SaveDocument` (`layers_.Mask()`), `ToggleLayer` (`layers_.Toggle` + `dirty_`).

- [x] **Step 2: Swap the member in the header.** In `EditorApplication.h`: `#include "AppFlowRuntime.h"`. Replace the `Vixen::AppFlow::LayerController layers_;` member with `Vixen::AppFlow::AppFlowRuntime rt_{nullptr, 0};` (bus=nullptr — Publish no-ops; sender=0). Keep `bool dirty_ = false;` and `bool sKeyWasDown_ = false;`. (If the header currently includes `LayerController.h` and nothing else uses it directly, that include can stay or go — AppFlowRuntime.h pulls it transitively; leave it to avoid churn.)

- [x] **Step 3: Rewire the `.cpp` call sites** (use `search-and-replace` for the mechanical `layers_.Mask()` → `rt_.Layers().Mask()` occurrences — there are 2 — then hand-edit the two structural ones):
  - `LoadDocument`: after `doc_.Load(...)` succeeds, replace `layers_.SetLayerCount(doc_.LayerCount());` with:
    ```cpp
    rt_.Load();  // load the AppFlow reference vocab (state/action tables)
    rt_.Layers().SetLayerCount(doc_.LayerCount());  // (re)sync the mask to the freshly loaded doc
    ```
  - `ApplyDocumentToScene`: `doc_.FlattenToRecipeEntry(layers_.Mask(), ...)` → `doc_.FlattenToRecipeEntry(rt_.Layers().Mask(), ...)`.
  - `SaveDocument`: `doc_.Save(layers_.Mask(), ...)` → `doc_.Save(rt_.Layers().Mask(), ...)`.
  - `ToggleLayer(uint32_t layerIndex)`: replace the body (`if (!layers_.Toggle(layerIndex)) return; dirty_ = true;`) with:
    ```cpp
    // Route through the ActionStack so the toggle is undoable; onChanged fires on BOTH the
    // forward apply AND on rt_.Undo()'s inverse, so a later Ctrl+Z re-flattens too.
    rt_.ToggleLayer(layerIndex, [this]{ dirty_ = true; });
    ```

- [x] **Step 4: Build the editor Windows-side + confirm it links.** Configure once if needed, then build the editor target:
  ```
  cmd.exe /c "C:\\cpp\\VBVS--VIXEN\\VIXEN\\temp\\win_configure.bat"   # if not configured
  ```
  Copy `win_build.bat` to a scratch `win_build_editor.bat` (or edit the target) so it builds `--target vixen_editor` (the editor exe target — confirm the exact name in `VIXEN/application/editor/CMakeLists.txt`), then:
  ```
  cmd.exe /c "C:\\cpp\\VBVS--VIXEN\\VIXEN\\temp\\win_build_editor.bat"
  ```
  Watch with a foreground poll loop (do NOT overlap builds). Expected: 0 errors, editor links. (`AppFlow` is already on the editor's link line from Inc-2 — confirm; if missing, that's the one CMake add.)

- [x] **Step 5: Commit.**
  ```bash
  git add VIXEN/application/editor/include/EditorApplication.h VIXEN/application/editor/source/EditorApplication.cpp
  git commit -m "refactor(editor): EditorApplication owns AppFlowRuntime; toggle routes through the ActionStack (Inc-2b)"
  ```

---

### Task 2: Ctrl+Z / Ctrl+Y undo-redo keybindings

**Files:**
- Modify: `VIXEN/application/editor/include/EditorApplication.h` (add `ctrlZWasDown_`/`ctrlYWasDown_`)
- Modify: `VIXEN/application/editor/source/EditorApplication.cpp` (`Update`)

**Interfaces:**
- Consumes: `rt_.Undo() → DispatchResult`, `rt_.Redo() → DispatchResult` (Inc-2); `GetWindowHandle()` + `glfwGetKey` (existing).
- Produces: live undo/redo in the windowed editor.

- [x] **Step 1: Add the edge-detect members.** In `EditorApplication.h` next to `sKeyWasDown_`: `bool ctrlZWasDown_ = false; bool ctrlYWasDown_ = false;`.

- [x] **Step 2: Add the keybinding block in `Update`.** Inside the existing `if (GLFWwindow* window = GetWindowHandle())` block (alongside the `S`-save edge-detect), add:
  ```cpp
  const bool ctrl = glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS
                 || glfwGetKey(window, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS;
  const bool zDown = ctrl && glfwGetKey(window, GLFW_KEY_Z) == GLFW_PRESS;
  const bool yDown = ctrl && glfwGetKey(window, GLFW_KEY_Y) == GLFW_PRESS;
  if (zDown && !ctrlZWasDown_) rt_.Undo();  // onChanged (set inside ToggleLayer's apply) fires → dirty_
  if (yDown && !ctrlYWasDown_) rt_.Redo();
  ctrlZWasDown_ = zDown;
  ctrlYWasDown_ = yDown;
  ```
  No new re-flatten call: `rt_.Undo()`/`Redo()` re-run the stored apply lambda which sets `dirty_`, and the existing `if (dirty_) { ... ApplyDocumentToScene(); }` tail re-flattens.

- [x] **Step 3: Build the editor Windows-side (as Task 1 Step 4).** Expected: 0 errors.

- [x] **Step 4: No-regression — the M4 headless gate.** The AppFlow libs are untouched, but confirm `test_appflow_editor_toggle_render` still builds + passes (it's the toggle/undo logic proof). Build + run it Windows-side (single target):
  ```
  cmd.exe /c "...\\win_build.bat"   # target test_appflow_editor_toggle_render
  # run the binary under the win build tree, --gtest_brief=0
  ```
  Expected: PASS. (If Windows-side GPU differs, WSL fallback is acceptable for this pre-existing headless gate.)

- [x] **Step 5: Commit.**
  ```bash
  git add VIXEN/application/editor/include/EditorApplication.h VIXEN/application/editor/source/EditorApplication.cpp
  git commit -m "feat(editor): Ctrl+Z/Ctrl+Y live undo-redo through AppFlowRuntime (Inc-2b)"
  ```

---

### Task 3: Offscreen capture RenderTarget + readback-to-PNG helper

**Files:**
- Modify: `VIXEN/application/editor/include/EditorApplication.h` (capture members)
- Modify: `VIXEN/application/editor/source/EditorApplication.cpp` (`BuildRenderGraph` capture target; a `CaptureFrameToPng` helper)
- Create (or reuse): a shared `IRenderTarget`→PNG readback util (extract from `test_editor_document_render.cpp`)

**Interfaces:**
- Consumes: `RenderTargetNode` / `RenderTargetNodeConfig` (`PARAM_WIDTH`/`PARAM_HEIGHT`/`PARAM_FORMAT`, output `RENDER_TARGET` = `IRenderTarget*`); `IRenderTarget::GetCurrentImage()`; the device→host `VkImage`→RGBA8 readback + `stbi_write_png` that `test_editor_document_render.cpp` already performs on an offscreen target.
- Produces: `bool EditorApplication::CaptureFrameToPng(const std::string& path, std::string& err)` reading the capture target's current image to a PNG; a capture `RenderTargetNode` in the editor graph (materialized only when capture is requested).

- [x] **Step 1: Read the render-test readback.** `codegraph explore "test_editor_document_render.cpp render target readback VkImage host buffer stbi_write_png RenderToRgba"`. Identify the exact device→host copy + PNG-write code to reuse. Prefer extracting it into a small shared helper (e.g. `VIXEN/libraries/RenderGraph/include/Debug/RenderTargetReadback.h` or an editor-local util) callable by BOTH the test and the editor — one implementation. If extraction is too invasive for this task, duplicate the minimal readback into the editor helper and note it (a follow-up can de-dup); do NOT block the gate on a refactor.

- [x] **Step 2: Add the capture target in `BuildRenderGraph`.** After the existing `VulkanGraphApplication::BuildRenderGraph()` call, when `std::getenv("VIXEN_EDITOR_CAPTURE_FRAMES")` is set, add a `RenderTargetNode` instance (fixed extent, e.g. 512×512, `VK_FORMAT_R8G8B8A8_UNORM`) that the scene renders into (option A). Wire it minimally: it must receive the same rendered scene content that reaches the window so a capture reflects the toggle. If wiring a parallel target into the existing graph is non-trivial, the acceptable minimum is to capture from the graph's EXISTING primary render target if one is reachable via `GetRenderGraph()->GetInstanceByName(...)` returning an `IRenderTarget*` — pick whichever reliably contains the rendered body; document the choice. Store the capture node/target name so `CaptureFrameToPng` can look it up live (never cache the pointer — mirror `GetWindowHandle`'s live-lookup rule).

- [x] **Step 3: Implement `CaptureFrameToPng`.** Look up the capture `IRenderTarget*` live, call the readback helper on its `GetCurrentImage()` at the target's extent, write the PNG to `path`. On failure set `err` + return false (must not crash the frame loop). Add capture members to the header as needed (the capture node's instance name string; no cached pointer).

- [x] **Step 4: Build the editor Windows-side.** Expected: 0 errors, links. (Do not run the scripted path yet — that's Task 4/5.)

- [x] **Step 5: Commit.**
  ```bash
  git add VIXEN/application/editor/include/EditorApplication.h VIXEN/application/editor/source/EditorApplication.cpp <shared readback helper if created>
  git commit -m "feat(editor): offscreen capture RenderTarget + readback-to-PNG helper, gated on VIXEN_EDITOR_CAPTURE_FRAMES (Inc-2b)"
  ```

---

### Task 4: `VIXEN_EDITOR_SCRIPT` frame-action injector + capture-frame dumps

**Files:**
- Modify: `VIXEN/application/editor/source/EditorApplication.cpp` (`Update`)
- Modify: `VIXEN/application/editor/include/EditorApplication.h` (a frame counter if not already present)

**Interfaces:**
- Consumes: the `VIXEN_RESIZE_AT_FRAME` env-in-static-initializer + per-Update tick-counter pattern (`VulkanGraphApplication.cpp:341-351`); `ToggleLayer`/`rt_.Undo()`/`rt_.Redo()` (Tasks 1–2); `CaptureFrameToPng` (Task 3).
- Produces: env-scripted, unattended toggle/undo/redo + PNG capture in the windowed editor.

- [x] **Step 1: Parse the script env once.** In `EditorApplication::Update` (or a member init), read `VIXEN_EDITOR_SCRIPT` (e.g. `"toggle:2@30,undo@60,redo@90"`) and `VIXEN_EDITOR_CAPTURE_FRAMES` (e.g. `"0,45,75,105"`) once into parsed structures (a `static` local like the resize pattern, or a member parsed in the ctor). Malformed tokens → log a warning and skip that token (never abort the app). Unset ⇒ empty structures ⇒ no behaviour.

- [x] **Step 2: Maintain an editor Update tick counter.** Add `long updateTick_ = 0;` (or reuse an existing frame count). Increment once per `EditorApplication::Update` AFTER the base `VulkanGraphApplication::Update()` call. (The base already has its own counters, but a local one keyed to editor Update is simplest + independent.)

- [x] **Step 3: Inject the scripted actions.** In `Update`, for any script entry whose frame == `updateTick_`, call the SAME method the input path calls:
  - `toggle:N` → `ToggleLayer(N)` (the wired ActionStack path — NOT `rt_.Layers().Toggle` directly).
  - `undo` → `rt_.Undo()`.
  - `redo` → `rt_.Redo()`.
  Place this BEFORE the `if (dirty_) { ... ApplyDocumentToScene(); }` tail so the re-flatten happens the same tick.

- [x] **Step 4: Dump capture frames.** After the `dirty_` re-flatten tail (so the capture reflects the post-toggle scene), if `updateTick_` is in the capture-frames set, call `CaptureFrameToPng("temp/editor_capture_" + std::to_string(updateTick_) + ".png", err)`; log on failure. (The path root should be overridable via an env like `VIXEN_EDITOR_CAPTURE_DIR`, defaulting to `temp/`; keep it simple.)

- [x] **Step 5: Build the editor Windows-side.** Expected: 0 errors. (The live scripted run is Task 5.)

- [x] **Step 6: Commit.**
  ```bash
  git add VIXEN/application/editor/include/EditorApplication.h VIXEN/application/editor/source/EditorApplication.cpp
  git commit -m "feat(editor): VIXEN_EDITOR_SCRIPT frame-action injector + capture-frame PNG dumps (Inc-2b)"
  ```

---

### Task 5: The scripted windowed gate (authoritative live proof)

**Files:**
- Create: `VIXEN/temp/run_editor_script.bat`
- Create: a post-run PNG-assertion gtest (e.g. `VIXEN/libraries/RenderGraph/tests/Nodes/test_editor_toggle_undo_capture.cpp`) + its CMake registration
- (Run) the windowed editor Windows-side via the `.bat`

**Interfaces:**
- Consumes: the editor `.exe` (Tasks 1–4) driven by `VIXEN_EDITOR_SCRIPT`/`VIXEN_EDITOR_CAPTURE_FRAMES`/`VIXEN_EXIT_AFTER_FRAMES`; the dumped PNGs; the bore-column pixel-diff logic from `test_editor_document_render.cpp` / `test_appflow_editor_toggle_render.cpp`.
- Produces: a green windowed gate proving live toggle/undo/redo.

- [ ] **Step 1: Write `run_editor_script.bat`.** Model on `VIXEN/temp/run_debug_1440.bat` (sets `VIXEN_*` env INSIDE the batch, runs `binaries\VIXEN.exe`, captures output). Set:
  ```bat
  set VIXEN_EDITOR_SCRIPT=toggle:2@30,undo@60,redo@90
  set VIXEN_EDITOR_CAPTURE_FRAMES=0,45,75,105
  set VIXEN_EDITOR_CAPTURE_DIR=temp
  set VIXEN_EXIT_AFTER_FRAMES=120
  ```
  and run the editor exe (confirm its binary name/path under `binaries\`). Redirect output to a log. Use cut layer index 2 (the bore-producing layer, matching the M4 gate).

- [ ] **Step 2: Run the scripted windowed editor Windows-side.** `cmd.exe /c "C:\\cpp\\VBVS--VIXEN\\VIXEN\\temp\\run_editor_script.bat"`. Watch with a foreground poll loop. Expected: clean exit at frame 120; PNGs `temp/editor_capture_{0,45,75,105}.png` written. If the editor can't run windowed in this env, report it (the gate strategy would then need a headless-window fallback — but the editor is a real windowed app and should run Windows-side).

- [ ] **Step 3: Write the post-run assertion gtest.** `test_editor_toggle_undo_capture.cpp` reads the 4 PNGs (via `stb_image`) and asserts:
  ```cpp
  // png[45] (after toggle:2@30) differs from png[0] at the bore column (toggle rendered)
  EXPECT_GT(BoreDiffPixels(png0, png45), 3000);
  // png[75] (after undo@60) == png[0] byte-for-byte (undo restored the live render exactly)
  EXPECT_EQ(png75, png0);   // exact buffer compare
  // png[105] (after redo@90) == png[45] (redo re-applied)
  EXPECT_EQ(png105, png45);
  ```
  Reuse the `BoreDiffPixels` / region logic from `test_appflow_editor_toggle_render.cpp`. The test reads pre-dumped PNGs from `VIXEN_EDITOR_CAPTURE_DIR` (or a fixed `temp/`), so it does NOT itself need a GPU — it's a pure file-diff gate that runs after Step 2. Register it in the same CMake as the other Nodes tests (link `stb`/GTest; no Vulkan needed, but co-locating is fine).

- [ ] **Step 4: Build + run the assertion gtest Windows-side.** Build the test target (single), run its binary against the PNGs Step 2 produced. Expected: 3/3 assertions PASS. If `png[75] != png[0]`, undo is broken — investigate (should not happen given the M4 logic proof; a windowed-only failure would point at capture timing, not undo).

- [ ] **Step 5: Commit.**
  ```bash
  git add VIXEN/temp/run_editor_script.bat VIXEN/libraries/RenderGraph/tests/Nodes/test_editor_toggle_undo_capture.cpp VIXEN/libraries/RenderGraph/tests/<cmake>
  git commit -m "test(editor): scripted windowed gate — live toggle/undo/redo capture asserts (Inc-2b)"
  ```

---

### Task 6: Inc-2b close-out — verify + docs

**Files:**
- Modify: `VIXEN/Vixen-Docs/01-Architecture/AppFlow-Framework-Inc2b-Plan-2026-07.md` (Progress Log + DONE)

- [ ] **Step 1: Full verify from fresh output.** Confirm, Windows-side: (a) the editor builds + links; (b) the M4 headless gate `test_appflow_editor_toggle_render` still PASSES (no-regression); (c) the scripted windowed run produces the 4 PNGs and `test_editor_toggle_undo_capture` passes 3/3. Paste the real results.

- [ ] **Step 2: Sanity — interactive editor unchanged with knobs unset.** Run the editor Windows-side with NO `VIXEN_EDITOR_*` env (or confirm by code inspection that unset ⇒ empty script/capture ⇒ no capture target built, no scripted actions). Confirm it launches + renders as before.

- [ ] **Step 3: Commit the close-out** (mark Progress Log + DONE).
  ```bash
  git add VIXEN/Vixen-Docs/01-Architecture/AppFlow-Framework-Inc2b-Plan-2026-07.md
  git commit -m "docs(appflow): Inc-2b COMPLETE — windowed editor on AppFlowRuntime, live undo/redo gated"
  ```

---

## Self-Review

**Spec coverage (design §2–§6 vs plan):**
- §2 scope (runtime swap + undo/redo, ParseLayerToggleId kept, BindingStore deferred) → Tasks 1–2. ✓
- §3.1 member swap → Task 1. §3.2 toggle-through-ActionStack → Task 1 Step 3. §3.3 Ctrl+Z/Y → Task 2. §3.4 removed/kept → Task 1. ✓
- §4.1 `VIXEN_EDITOR_SCRIPT` harness → Task 4. §4.2 render-target capture (option A) → Task 3. §4.3 post-run assertion → Task 5. §4.4 offline no-regression → Task 2 Step 4 + Task 6. ✓
- §5 error handling (no-throw Update, capture-failure non-fatal, malformed-env warn) → Tasks 3/4 + Global Constraints. ✓
- §6 files → all mapped. ✓

**Placeholder scan:** Task 3 Step 2 leaves the exact capture-target wiring bounded-but-open (option A preferred, a documented fallback allowed) — this is a directed decision within the task, not a placeholder; the worker picks the reliable path for the graph shape and documents it. No TBD/TODO elsewhere.

**Type consistency:** `rt_` = `AppFlowRuntime` used identically (Tasks 1/2/4); `rt_.Layers().Mask()` (Task 1); `rt_.ToggleLayer(uint32_t, std::function<void()>)` / `rt_.Undo()` / `rt_.Redo()` (Tasks 1/2/4); `CaptureFrameToPng(path, err)` (Tasks 3/4); `BoreDiffPixels` reused from the M4 test (Task 5). Cut layer index **2** consistent across Tasks 4/5 and the M4 gate. ✓

---

## Execution Handoff

Same shape as Inc-1/Inc-2 — the **post-brainstorm-context-manager** pipeline. Milestone 3's validator must actually run the scripted windowed editor Windows-side (live-run-is-authoritative). Windows-side build/test throughout (GPU work).
