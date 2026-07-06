# Renderer-Agnostic View Contract — Increment 2 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make `UIRenderNode` a generic RmlUi view-host that knows nothing about factions/events/tick, move the `[View]` schema (renamed `EditorHud`→`Hud`) + its generated binder + the data→view projection to VIXEN's **main app** as the first native consumer, generate the RML data-binding partial from the schema, and prove the live HUD renders byte-identically before/after via a real-GPU PNG-capture gate.

**Architecture:** The engine node gains a generic `IView` seam (`SetView(shared_ptr<IView>)`); a consumer implements `IView` by calling its generated `BindHudModel`. All faction/event/lens types + the `SetHudView` projection relocate from `libraries/RenderGraph` to `application/main`. A new Yeroket `RmlMarkupEmitter` generates the `{{}}`/`data-for` partial; `hud.rml` composes it. The proof is a byte-exact HUD PNG capture through the main app, mirroring the Inc-2b editor capture harness.

**Tech Stack:** C++23 + RmlUi (engine + consumer); C# / .NET 8 + Roslyn (the Yeroket markup emitter); CMake/dotnet (codegen wiring); gtest + `RenderTargetReadback` PNG capture (the live gate). Spec: `Renderer-Agnostic-View-Contract-Design-2026-07.md`.

## Global Constraints

- **CROSS-REPO.** The new `RmlMarkupEmitter` + its C# tests land in the **Yeroket repo at `/home/liory/Github/Yeroket-Fantasy`** (the WSL-home checkout CMake's `$ENV{HOME}` resolves to — NOT `/mnt/c/GitHub/Yeroket-Fantasy`). Everything else lands in VIXEN. Commit Yeroket changes on a Yeroket branch; VIXEN changes on the VIXEN worktree branch. **Pushing either repo stays GATED.**
- **Reuse the Yeroket core — do NOT build a parallel tool.** Extend `CodegenTool~` with a `--view-markup` mode + `RmlMarkupEmitter` (sibling of `RmlDataModelEmitter`). No new tool.
- **The renderer must end up view-agnostic.** After this increment `UIRenderNode.{h,cpp}` must contain ZERO references to `HudFaction`/`HudEvent`/`SetHudView`/`tick`/`bodyCount`/`factions`/`events`/lens/juice. Its only view surface is the generic `IView` seam. (Spec §4.2.)
- **`Hud` is the new name.** `EditorHud`→`Hud` everywhere: schema struct, generated symbols (`HudBind`, `BindHudModel`), CMake targets (`view_hud_check`/`view_hud_regen`), the golden test, and the data-model name unifies on `"hud"` (Inc-1's golden test used `"editorhud"`; the live node + `hud.rml` already use `"hud"`). Row structs `HudFaction`/`HudEvent` are already Hud-named — leave them.
- **`.g.h`/`.g.rml` are generated — DO NOT hand-edit;** regenerate via the `view_hud_regen`/`view_hud_markup_regen` targets.
- **Do NOT modify the live RmlUi render/composite/GPU-sync path** in `UIRenderNode` (the `CompileImpl` sync-object machinery `:188-222`, `RecordFrame`, composite semaphores, hot-reload). Only the view-DATA surface changes.
- **`editor.rml` / the editor layer panel is out of scope** — it uses no data model; leave it. The editor shares `UIRenderNode` though, so the editor windowed gate (`test_editor_toggle_undo_capture`) must stay green.
- **Build/test Windows-side first for the C++/GPU side** (`/mnt/c` WSL mount is slow); the C# emitter runs via `dotnet`. Never overlap builds of one target; poll long builds on a foreground interval. This env's GPU is REAL D3D12/dzn (not lavapipe) — render/capture tests genuinely run (~50s).
- **`SDFNodeGenerator.dll` / analyzer DLLs rebuild non-deterministically** — commit only on a *source* change; `git checkout --` a churned `bin/`/`obj/` DLL.
- **The authoritative proof for the live swap is the real-GPU byte-exact PNG capture** ([[live-verification-authoritative-for-gpu-work]]); static/golden equivalence is necessary but NOT sufficient.

---

## Reference: exact current shapes (read before starting)

- **`libraries/RenderGraph/include/Nodes/UIRenderNode.h`** — the surface to strip: `HudFactionIn`/`HudEventIn` (`:27-28`), `SetHudView`/`SetHudData` (`:58-63`), private `HudFaction`/`HudEvent` + `tick_`/`bodyCount_`/`activeLensName_`/`activeLensCount_`/`factions_`/`events_`/`hudModel_` (`:120-129`). Keep: `GetUiContext()` (`:71`), all the Vulkan/sync/composite members, `CompileImpl`/`ExecuteImpl`/`RecordFrame`.
- **`libraries/RenderGraph/src/Nodes/UIRenderNode.cpp`** — registration block `:116-138` (relocates); `SetHudView` projection `:357-393` (relocates: `kLensNames`, `kJuiceK`, per-field `DirtyVariable`); the model is built inside `CompileImpl`'s `if(context_)` block right after `CreateContext`; `hudModel_ = c.GetModelHandle()` at `:138`. `ResolveUiAsset` (`:29-50`) + hot-reload comment (`:174`) mention `data-model="hud"` and `SetHudView`.
- **`libraries/RenderGraph/include/Generated/EditorHud.g.h`** — Inc-1 output: `namespace Vixen::Views`, `struct HudFaction`/`HudEvent`, `struct EditorHudBind { int* tick; int* bodyCount; Rml::String* activeLensName; int* activeLensCount; std::vector<HudFaction>* factions; std::vector<HudEvent>* events; }`, `inline void BindEditorHudModel(Rml::DataModelConstructor& c, const EditorHudBind& b)`.
- **`codegen/view-schemas/EditorHud.cs`** — `[View] struct EditorHud { int tick; int bodyCount; string activeLensName; int activeLensCount; HudFaction[] factions; HudEvent[] events; }` (+ `HudFaction`/`HudEvent` row structs).
- **`libraries/RenderGraph/assets/ui/hud.rml`** — the live HUD doc. Data-bound region `:7-31` (inside `<div data-model="hud" id="hud">`): `{{tick}}`/`{{bodyCount}}` (`:8`), `{{activeLensName}}`/`{{activeLensCount}}` (`:11`), `data-for="f : factions"` row with `data-class-*` + `{{ f.known ? '✓' : '·' }}`/`{{f.name}}`/`{{f.grievance}}` (`:18-22`), `data-for="e : events"` row with `{{e.tick}}`/`{{e.kind}}` (`:26-29`). Interactive widgets `:36-45` are face-4 (out of scope). The shell classes (`.clock`/`.lens-status`/`.factions`/`.section-label`/`.col-*`) + the ternary + `data-class-*` are HAND-AUTHORED and must survive.
- **`libraries/RenderGraph/tests/test_ui_hud_smoke.cpp`** — couples to the engine types: `#include "Nodes/UIRenderNode.h"`, `HudFactionIn`/`HudEventIn`/`SetHudView`/`SetHudData`, `S1b_SetHudViewApiCompiles` (`:340-347`), internal mirror structs (`:173`). Must migrate (Task 6).
- **`libraries/RenderGraph/tests/test_view_editorhud_golden.cpp`** — Inc-1 golden. `kExpected` (`:42-61`) is anchored to `UIRenderNode.cpp:116-138`, which this increment DELETES — re-anchor (Task 7). Uses `"editorhud"` model name (`:111`) — unify to `"hud"`.
- **`application/editor/source/EditorApplication.cpp`** — the capture-harness template: `ParseEditorScript` + `VIXEN_EDITOR_SCRIPT`/`VIXEN_EDITOR_CAPTURE_FRAMES`/`VIXEN_EDITOR_CAPTURE_DIR` parsing (`:44-106`), `PreTick()` injector (`:299-341`), `CaptureRenderTargetToPng` against `compute_render_target` (`:250-276`), capture in the Update() tail (`:408`). `RenderTargetReadback.h` (`include/Debug/`) is the shared readback.
- **`libraries/RenderGraph/tests/Nodes/test_editor_toggle_undo_capture.cpp`** — the file-I/O PNG-gate template (loads PNGs a `.bat` run produced; `stb_image`; `BoreDiffPixels`; byte-`EXPECT_EQ`). Registered OUTSIDE the glslc gate so it builds Windows-side.
- **`application/main/source/main.cpp:81`** — `return app->Run({ .exitAfterFrames = exitAfterFrames, .enableFrameTimer = true });`. The main app uses `VulkanGraphApplication` DIRECTLY (no subclass like `EditorApplication`).
- **`application/main/source/graph/BuildRenderGraph.cpp`** — `:256` stores `uiRenderNode_` for `GetUiRenderNode()`; `:743` sets `RML_DOCUMENT_PATH = "assets/ui/hud.rml"`. Look for the `compute_render_target` RenderTargetNode instance name used in this graph (the capture target).
- **`application/main/include/VulkanGraphApplication.h`** — `GetUiRenderNode()` (`:232`), `uiRenderNode_` (`:205`). This is where a `HudView` member + `SetView` wiring + a capture/script hook attach (Tasks 4-5).
- **Yeroket** (`$KF = /home/liory/Github/Yeroket-Fantasy/Packages/com.yeroket.utility.kernel-framework`): `SourceGenerator~/Transpiler/RmlDataModelEmitter.cs` + `ViewModel.cs` (`ViewStruct`/`ViewField`/`ViewScalar{Int,Float,Bool,String}`/`ViewFieldKind{Scalar,Struct,StructArray}`), `CodegenTool~/Program.cs` (`--view` branch), `CodegenTool~/Tests/RmlDataModelEmitterTests.cs` (NUnit).

---

## Milestone Map

- **M1 — Rename EditorHud→Hud (Yeroket-neutral; VIXEN-side rename of Inc-1 artifacts, re-anchor golden).** Tasks 1-2. Deliverable: `Hud.cs`/`Hud.g.h`/`HudBind`/`BindHudModel`/`view_hud_*` targets; golden test re-anchored + green; model name `"hud"`. No behavior change, node still hand-written. **✅ DONE.**
- **M2 — Generic view-host + native Hud consumer (the decouple + live wire).** Tasks 3-5. Deliverable: `UIRenderNode` is view-agnostic (`IView` seam); a native `HudView` consumer in the main app calls `BindHudModel`; the live HUD renders through it. Includes the byte-exact real-GPU PNG gate.
- **M3 — RML markup partial emit + compose + close-out.** Tasks 6-8. Deliverable: Yeroket `RmlMarkupEmitter` generates the data-binding partial; `hud.rml` composes it; `test_ui_hud_smoke` migrated; full gate green; docs closed.

---

## Task 1: Rename the `[View]` schema + generated header + CMake targets (EditorHud→Hud)

**Repo:** VIXEN.

**Files:**
- Rename: `codegen/view-schemas/EditorHud.cs` → `codegen/view-schemas/Hud.cs`
- Rename: `libraries/RenderGraph/include/Generated/EditorHud.g.h` → `Generated/Hud.g.h`
- Modify: `codegen/CMakeLists.txt` (the `view_editorhud_*` block)

**Interfaces:**
- Produces: `Vixen::Views::{HudFaction, HudEvent, HudBind}` + `Vixen::Views::BindHudModel(Rml::DataModelConstructor&, const HudBind&)` in `Generated/Hud.g.h`; CMake targets `view_hud_check`/`view_hud_regen` generating from `--view Hud`.

- [ ] **Step 1: Rename the schema struct.** `git mv codegen/view-schemas/EditorHud.cs codegen/view-schemas/Hud.cs`, then edit the struct name:
```csharp
    [View]
    public struct Hud {
        public int    tick;
        public int    bodyCount;
        public string activeLensName;
        public int    activeLensCount;
        public HudFaction[] factions;
        public HudEvent[]   events;
    }
```
(The `HudFaction`/`HudEvent` row structs and `using Yeroket.Util.KernelFramework;` + `namespace Vixen.ViewSchemas` are unchanged.)

- [ ] **Step 2: Rewrite the CMake codegen block** in `codegen/CMakeLists.txt` — replace the `_view_args`/`view_editorhud_*` block (currently `--view EditorHud`, out-header `EditorHud.g.h`, targets `view_editorhud_check`/`_regen`) with:
```cmake
    # --- Hud [View] schema via the same Yeroket tool (View Contract Inc-2: renamed from EditorHud) ---
    set(_view_args
        run --project "${_yk_tool}" -c Release --
        --schema "${_cg}/view-schemas" --view Hud
        --out-header "${CMAKE_SOURCE_DIR}/libraries/RenderGraph/include/Generated/Hud.g.h")
    add_custom_target(view_hud_check ALL
        COMMAND ${VIXEN_DOTNET} ${_view_args} --check
        COMMENT "[codegen] golden check: Hud.g.h matches canonical [View] schema (Yeroket tool)"
        VERBATIM)
    add_custom_target(view_hud_regen
        COMMAND ${VIXEN_DOTNET} ${_view_args}
        COMMENT "[codegen] regenerate Hud.g.h (Yeroket tool)"
        VERBATIM)
```
Also update the KI-015 not-found WARNING text: change `OctreeConfig/EditorHud` → `OctreeConfig/Hud` and `EditorHud.g.h` → `Hud.g.h` (2 occurrences around lines 94-95).

- [ ] **Step 3: Regenerate the header under its new name (WSL-side, authoritative).** Delete the stale header and regen:
```bash
git rm libraries/RenderGraph/include/Generated/EditorHud.g.h
/home/liory/.dotnet/dotnet run --project "/home/liory/Github/Yeroket-Fantasy/Packages/com.yeroket.utility.kernel-framework/CodegenTool~" -c Release -- \
  --schema "$(git rev-parse --show-toplevel)/VIXEN/codegen/view-schemas" --view Hud \
  --out-header "$(git rev-parse --show-toplevel)/VIXEN/libraries/RenderGraph/include/Generated/Hud.g.h"
```
Expected: writes `Hud.g.h` with `struct HudBind` + `inline void BindHudModel(...)` (the tool derives symbol names from the `--view Hud` name → `HudBind`/`BindHudModel`; `HudFaction`/`HudEvent` row structs unchanged). If the Release run churns Yeroket's `SDFNodeGenerator.dll`, `git -C /home/liory/Github/Yeroket-Fantasy checkout -- ` it (don't commit it).

- [ ] **Step 4: Verify the generated symbols.** `grep -E "HudBind|BindHudModel|EditorHud" libraries/RenderGraph/include/Generated/Hud.g.h` — expect `struct HudBind`, `inline void BindHudModel`, and ZERO `EditorHud` occurrences. Then run the `--check` to confirm it's stable (regen twice = identical):
```bash
/home/liory/.dotnet/dotnet run --project "/home/liory/Github/Yeroket-Fantasy/Packages/com.yeroket.utility.kernel-framework/CodegenTool~" -c Release -- \
  --schema "$(git rev-parse --show-toplevel)/VIXEN/codegen/view-schemas" --view Hud \
  --out-header "$(git rev-parse --show-toplevel)/VIXEN/libraries/RenderGraph/include/Generated/Hud.g.h" --check; echo "CHECK=$?"
```
Expected: `CHECK=0`.

- [ ] **Step 5: Commit.**
```bash
git add codegen/view-schemas/Hud.cs codegen/CMakeLists.txt libraries/RenderGraph/include/Generated/Hud.g.h
git rm --cached codegen/view-schemas/EditorHud.cs libraries/RenderGraph/include/Generated/EditorHud.g.h 2>/dev/null || true
git commit -m "refactor(view-contract): rename [View] EditorHud -> Hud (schema + generated header + CMake)"
```
(The `git mv`/`git rm` already staged the deletions; the belt-and-suspenders `git rm --cached` is a no-op if so.)

---

## Task 2: Re-anchor the golden test to the schema (EditorHud→Hud + human-truth move)

**Repo:** VIXEN.

**Files:**
- Rename: `libraries/RenderGraph/tests/test_view_editorhud_golden.cpp` → `tests/test_view_hud_golden.cpp`
- Modify: `libraries/RenderGraph/tests/CMakeLists.txt` (the `test_view_editorhud_golden` block)

**Interfaces:**
- Consumes: `Generated/Hud.g.h` (`Vixen::Views::{HudBind, BindHudModel}`) from Task 1.

**Why re-anchor:** Inc-1's golden asserted the generated block matches the HAND-WRITTEN block at `UIRenderNode.cpp:116-138`. Task 3 DELETES that block (the node goes generic). The golden's job becomes: the generated `BindHudModel` (a) still emits the canonical ordered registration sequence for the Hud schema, and (b) compiles + binds. The `kExpected` sequence is now anchored to **the canonical Hud schema field order** (tick, bodyCount, activeLensName, activeLensCount, factions[HudFaction], events[HudEvent]) — the same tokens, but the pinning comment points at the schema + the consumer `HudView::Register` (Task 4), not the deleted node block.

- [ ] **Step 1: Rename the test file + fix its identifiers.** `git mv libraries/RenderGraph/tests/test_view_editorhud_golden.cpp libraries/RenderGraph/tests/test_view_hud_golden.cpp`. Then edit:
  - Header comment: replace the `@file`/`@brief` to describe the Hud schema + that the human truth is the canonical schema (Task 4's consumer `HudView` mirrors it), not the (now-removed) node block.
  - `#include "Generated/EditorHud.g.h"` → `#include "Generated/Hud.g.h"`.
  - The compile-def macro `VIEW_EDITORHUD_G_H` → `VIEW_HUD_G_H` (both `ReadGeneratedHeader()` and the two ASSERT messages).
  - `Vixen::Views::EditorHudBind bind{...}` → `Vixen::Views::HudBind bind{...}`.
  - `Rml::DataModelConstructor c = ctx->CreateDataModel("editorhud");` → `CreateDataModel("hud");` (unify the model name).
  - `Vixen::Views::BindEditorHudModel(c, bind);` → `Vixen::Views::BindHudModel(c, bind);`.
  - The `TEST(ViewEditorHudGolden, GeneratedSequenceMatchesHandWrittenUIRenderNodeBlock)` → `TEST(ViewHudGolden, GeneratedSequenceMatchesCanonicalSchema)`; update its `EXPECT_EQ` failure message to reference the schema + `HudView::Register`, not `UIRenderNode.cpp:116-138`.
  - `TEST(ViewEditorHudGolden, GeneratedBindFunctionCompilesAndBinds)` → `TEST(ViewHudGolden, GeneratedBindFunctionCompilesAndBinds)`.
  - `kExpected` sequence is UNCHANGED (the field order is identical; only the anchoring comment above it changes to "canonical Hud schema field order").

- [ ] **Step 2: Update the tests CMake.** In `libraries/RenderGraph/tests/CMakeLists.txt`, in the `test_view_editorhud_golden` block (~`:629-654`): rename the target `test_view_editorhud_golden` → `test_view_hud_golden`, the source `test_view_editorhud_golden.cpp` → `test_view_hud_golden.cpp`, and the compile-def `VIEW_EDITORHUD_G_H="${...}/Generated/EditorHud.g.h"` → `VIEW_HUD_G_H="${...}/Generated/Hud.g.h"`. Update the two `message(STATUS ...)` strings.

- [ ] **Step 3: Build + run the golden — expect 2/2 PASS.** (Windows-side or WSL; no GPU needed — it's headless.)
```
cmd.exe /c "cd /d C:\cpp\VBVS--VIXEN\.claude\worktrees\<wt>\VIXEN && cmake --build build --config Debug --target test_view_hud_golden"
cmd.exe /c "cd /d C:\cpp\VBVS--VIXEN\.claude\worktrees\<wt>\VIXEN && build\libraries\RenderGraph\tests\Debug\test_view_hud_golden.exe --gtest_brief=1"
```
Expected: `[  PASSED  ] 2 tests.` — `GeneratedSequenceMatchesCanonicalSchema` + `GeneratedBindFunctionCompilesAndBinds`. Poll the build on a foreground interval.

- [ ] **Step 4: Commit.**
```bash
git add libraries/RenderGraph/tests/test_view_hud_golden.cpp libraries/RenderGraph/tests/CMakeLists.txt
git rm --cached libraries/RenderGraph/tests/test_view_editorhud_golden.cpp 2>/dev/null || true
git commit -m "test(view-contract): re-anchor golden to Hud schema (rename + human-truth = schema/HudView, not deleted node block)"
```

---

## Task 3: Give `UIRenderNode` a generic `IView` seam + strip all consumer types

**Repo:** VIXEN.

**Files:**
- Create: `libraries/RenderGraph/include/Ui/IView.h`
- Modify: `libraries/RenderGraph/include/Nodes/UIRenderNode.h`
- Modify: `libraries/RenderGraph/src/Nodes/UIRenderNode.cpp`
- Modify: `libraries/RenderGraph/CMakeLists.txt` (add `IView.h` to `RENDERGRAPH_UI_HEADERS`)

**Interfaces:**
- Produces: `Vixen::RenderGraph::IView` (pure interface) + `UIRenderNode::SetView(std::shared_ptr<IView>)` + `UIRenderNode::MarkViewDirty(const char* field)`. The node no longer declares any Hud type. `GetUiContext()` unchanged.

**Design (spec §4.2):** The node hosts a consumer-supplied `IView`: it creates the data model named `view_->ModelName()`, calls `view_->Register(constructor)` (the consumer registers its structs/arrays + binds to ITS OWN storage), stores the resulting `DataModelHandle`, and exposes `MarkViewDirty` so the consumer can dirty a variable after mutating its storage. The node knows no field names.

- [ ] **Step 1: Write the failing test** (extends the golden test file's spirit into a node-level unit — but the node needs Vulkan, so this is a *compile+interface* test, headless). Create it inside `test_view_hud_golden.cpp` as a new `TEST(ViewHudGolden, IViewSeamIsViewAgnostic)` that verifies the interface exists and a trivial `IView` impl registers a model with no engine knowledge:
```cpp
// at top: #include "Ui/IView.h"
namespace {
struct TrivialView : Vixen::RenderGraph::IView {
    int x = 5;
    const char* ModelName() const override { return "triv"; }
    const char* DocumentPath() const override { return "assets/ui/hud.rml"; }
    void Register(Rml::DataModelConstructor& c) override { c.Bind("x", &x); }
};
}
TEST(ViewHudGolden, IViewSeamIsViewAgnostic) {
    Vixen::Ui::VixenRmlSystemInterface sysIface; NullRenderInterface renderIface;
    Rml::SetSystemInterface(&sysIface); Rml::SetRenderInterface(&renderIface);
    ASSERT_TRUE(Rml::Initialise());
    Rml::Context* ctx = Rml::CreateContext("iview", Rml::Vector2i(64,64));
    ASSERT_NE(ctx, nullptr);
    TrivialView v;
    Rml::DataModelConstructor c = ctx->CreateDataModel(v.ModelName());
    ASSERT_TRUE(static_cast<bool>(c));
    v.Register(c);
    EXPECT_TRUE(static_cast<bool>(c.GetModelHandle()));
    Rml::RemoveContext("iview"); Rml::Shutdown();
}
```

- [ ] **Step 2: Run it — expect FAIL** (`Ui/IView.h` not found).
```
cmd.exe /c "cd /d C:\cpp\VBVS--VIXEN\.claude\worktrees\<wt>\VIXEN && cmake --build build --config Debug --target test_view_hud_golden"
```
Expected: compile error, `Ui/IView.h: No such file`.

- [ ] **Step 3: Create `libraries/RenderGraph/include/Ui/IView.h`:**
```cpp
#pragma once
// The generic view contract: a consumer supplies one of these to UIRenderNode. The renderer
// hosts it without knowing any field/struct name — the consumer registers its own data model
// (typically by calling its schema-generated Bind<Name>Model) and owns its storage. Renderer-
// agnostic view contract, Inc-2 (see Renderer-Agnostic-View-Contract-Design-2026-07.md).
namespace Rml { class DataModelConstructor; }

namespace Vixen::RenderGraph {

class IView {
public:
    virtual ~IView() = default;
    /// The RmlUi data-model name (must match the document's data-model="…"). e.g. "hud".
    virtual const char* ModelName() const = 0;
    /// Register scalars/structs/arrays on the constructor and Bind() them to the consumer's own
    /// storage. Called once, after CreateDataModel, before LoadDocument.
    virtual void Register(Rml::DataModelConstructor& c) = 0;
    /// The RML document to load (relative "assets/ui/…" path, resolved by the node).
    virtual const char* DocumentPath() const = 0;
};

}  // namespace Vixen::RenderGraph
```

- [ ] **Step 4: Strip the consumer surface from `UIRenderNode.h`.** Remove: `HudFactionIn`/`HudEventIn` (`:23-28`), `SetHudView`/`SetHudData` declarations (`:55-63`), the private `HudFaction`/`HudEvent` structs + `tick_`/`bodyCount_`/`activeLensName_`/`activeLensCount_`/`factions_`/`events_` members (`:115-128`). Change the `RmlUi` includes if needed (`Rml::String` may no longer be used here — keep `DataModelHandle.h`). Add:
```cpp
#include "Ui/IView.h"
// ... in the public section, replacing SetHudView/SetHudData:
    /// Renderer-agnostic view seam: the consumer hands in its view; the node hosts its data model
    /// (CreateDataModel(view->ModelName()) -> view->Register(c) -> LoadDocument(view->DocumentPath()))
    /// without knowing any field. Call before the first compile.
    void SetView(std::shared_ptr<IView> view);
    /// Dirty a bound variable after the consumer mutated its storage (forwards to DataModelHandle).
    void MarkViewDirty(const char* field);
// ... in the private section, replacing the hud members:
    std::shared_ptr<IView> view_;
    Rml::DataModelHandle   viewModel_;   // was hudModel_
```
Keep `#include <memory>`. Remove now-unused `<span>` if nothing else uses it (check).

- [ ] **Step 5: Rewrite the model-build in `UIRenderNode.cpp::CompileImpl`.** Replace the `if (Rml::DataModelConstructor c = context_->CreateDataModel("hud")) { … RegisterStruct… Bind… hudModel_ = c.GetModelHandle(); }` block (`:116-138`) with:
```cpp
            if (view_) {
                if (Rml::DataModelConstructor c = context_->CreateDataModel(view_->ModelName())) {
                    view_->Register(c);
                    viewModel_ = c.GetModelHandle();
                }
            }
```
And change the document load to honor the view's path when a view is set (fall back to the configured param otherwise):
```cpp
            const std::string docPath = ResolveUiAsset(
                view_ ? std::string(view_->DocumentPath())
                      : GetParameterValue<std::string>(UIRenderNodeConfig::RML_DOCUMENT_PATH, "assets/ui/demo.rml"));
```
Delete the entire `SetHudView` + `SetHudData` definitions (`:357-393`). Add the two new methods:
```cpp
void UIRenderNode::SetView(std::shared_ptr<IView> view) { view_ = std::move(view); }
void UIRenderNode::MarkViewDirty(const char* field) { if (viewModel_ && field) viewModel_.DirtyVariable(field); }
```
Update the hot-reload comment (`:174`) that references `SetHudView`/`data-model="hud"` to say "the consumer's view re-binds via data-model=view_->ModelName(), and the consumer keeps feeding it."

- [ ] **Step 6: Add `IView.h` to the UI headers in `libraries/RenderGraph/CMakeLists.txt`** — insert `include/Ui/IView.h` into `RENDERGRAPH_UI_HEADERS` (`:322-329`). **(Same-task CMake registration — durable lesson.)**

- [ ] **Step 7: Build the node + the interface test — expect the `IViewSeamIsViewAgnostic` test PASS and NO `HudFaction`/`SetHudView` symbols remain.** Build `RenderGraphNodes` then `test_view_hud_golden`:
```
cmd.exe /c "cd /d C:\cpp\VBVS--VIXEN\.claude\worktrees\<wt>\VIXEN && cmake --build build --config Debug --target test_view_hud_golden"
cmd.exe /c "cd /d C:\cpp\VBVS--VIXEN\.claude\worktrees\<wt>\VIXEN && build\libraries\RenderGraph\tests\Debug\test_view_hud_golden.exe --gtest_brief=1"
```
Expected: 3/3 PASS (golden sequence + compiles-and-binds + IView seam). Then assert agnosticism:
```bash
grep -nE "HudFaction|HudEvent|SetHudView|SetHudData|factions_|activeLensName_" libraries/RenderGraph/include/Nodes/UIRenderNode.h libraries/RenderGraph/src/Nodes/UIRenderNode.cpp || echo "CLEAN: node is view-agnostic"
```
Expected: `CLEAN: node is view-agnostic` (zero matches).

- [ ] **Step 8: Commit.**
```bash
git add libraries/RenderGraph/include/Ui/IView.h libraries/RenderGraph/include/Nodes/UIRenderNode.h \
        libraries/RenderGraph/src/Nodes/UIRenderNode.cpp libraries/RenderGraph/CMakeLists.txt \
        libraries/RenderGraph/tests/test_view_hud_golden.cpp
git commit -m "feat(view-contract): UIRenderNode is a generic IView host — strip all Hud/faction/SetHudView surface"
```

---

## Task 4: The native `HudView` consumer in the main app (calls BindHudModel + owns the projection)

**Repo:** VIXEN.

**Files:**
- Create: `application/main/include/graph/HudView.h`
- Create: `application/main/source/graph/HudView.cpp`
- Move: `libraries/RenderGraph/include/Generated/Hud.g.h` → `application/main/include/Generated/Hud.g.h` (the generated header becomes a CONSUMER artifact — spec §4.1) **and** update `codegen/CMakeLists.txt`'s `--out-header` + `view_hud_*` to the new path, **and** the golden test's `VIEW_HUD_G_H` path.
- Modify: `application/main/CMakeLists.txt` (add HudView sources + the generated include dir)

**Interfaces:**
- Consumes: `Vixen::Views::{HudFaction, HudEvent, HudBind, BindHudModel}` (Task 1, relocated header); `Vixen::RenderGraph::IView` (Task 3).
- Produces: `Vixen::App::HudView : IView` with storage (`int tick_`, `bodyCount_`, `Rml::String activeLensName_`, `int activeLensCount_`, `std::vector<Vixen::Views::HudFaction> factions_`, `std::vector<Vixen::Views::HudEvent> events_`), `Register()` calling `BindHudModel`, and `SetHudView(int tick, int bodyCount, int activeLens, int activeLensCount, span<const HudFactionIn>, span<const HudEventIn>)` — the RELOCATED projection (with `HudFactionIn`/`HudEventIn` moved here too). `MarkAllDirty(UIRenderNode&)` after a push.

**Decision — where the generated header lives:** it moves OUT of `libraries/RenderGraph/include/Generated/` (engine) to `application/main/include/Generated/` (consumer), because the engine must not carry a specific view's generated code (spec §4.1). The Yeroket `--out-header` + golden `VIEW_HUD_G_H` update to the new path in this task.

- [ ] **Step 1: Move the generated header + repoint codegen & golden.**
```bash
mkdir -p application/main/include/Generated
git mv libraries/RenderGraph/include/Generated/Hud.g.h application/main/include/Generated/Hud.g.h
```
In `codegen/CMakeLists.txt` set the `--out-header` to `"${CMAKE_SOURCE_DIR}/application/main/include/Generated/Hud.g.h"` in the `_view_args` (used by BOTH `view_hud_check` and `view_hud_regen`). Note `CMAKE_SOURCE_DIR` is the VIXEN root, so the app path is `${CMAKE_SOURCE_DIR}/application/main/...` (no `../`). In `libraries/RenderGraph/tests/CMakeLists.txt` change `VIEW_HUD_G_H` to `"${CMAKE_SOURCE_DIR}/application/main/include/Generated/Hud.g.h"` and ensure the golden test target adds `${CMAKE_SOURCE_DIR}/application/main/include` to its include dirs (so `#include "Generated/Hud.g.h"` resolves) — add `target_include_directories(test_view_hud_golden PRIVATE ${CMAKE_SOURCE_DIR}/application/main/include)`.

- [ ] **Step 2: Write the failing consumer test.** Create `application/main/tests/test_hud_view.cpp` (new app-level test dir; register in `application/main/CMakeLists.txt` in this same task):
```cpp
#include <gtest/gtest.h>
#include <RmlUi/Core.h>
#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/RenderInterface.h>
#include "graph/HudView.h"
#include "Ui/VixenRmlSystemInterface.h"
namespace { class NullRI final : public Rml::RenderInterface {
  Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex>, Rml::Span<const int>) override { return 0; }
  void RenderGeometry(Rml::CompiledGeometryHandle, Rml::Vector2f, Rml::TextureHandle) override {}
  void ReleaseGeometry(Rml::CompiledGeometryHandle) override {}
  void EnableScissorRegion(bool) override {} void SetScissorRegion(Rml::Rectanglei) override {}
  Rml::TextureHandle LoadTexture(Rml::Vector2i&, const Rml::String&) override { return 0; }
  Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte>, Rml::Vector2i) override { return 0; }
  void ReleaseTexture(Rml::TextureHandle) override {}
}; }
TEST(HudViewTest, RegistersAndProjectsFactionData) {
    Vixen::Ui::VixenRmlSystemInterface sys; NullRI ri;
    Rml::SetSystemInterface(&sys); Rml::SetRenderInterface(&ri);
    ASSERT_TRUE(Rml::Initialise());
    Rml::Context* ctx = Rml::CreateContext("hv", Rml::Vector2i(64,64));
    ASSERT_NE(ctx, nullptr);
    Vixen::App::HudView view;
    Rml::DataModelConstructor c = ctx->CreateDataModel(view.ModelName());
    ASSERT_TRUE(static_cast<bool>(c));
    view.Register(c);
    auto handle = c.GetModelHandle();
    EXPECT_TRUE(static_cast<bool>(handle));
    EXPECT_STREQ(view.ModelName(), "hud");
    // projection: lens enum 2 -> "Logistics", juice window
    Vixen::App::HudFactionIn f{ "acme", 3.5f, true, false, true, 0 };
    view.SetHudView(42, 7, 2, 4, {&f, 1}, {});
    EXPECT_EQ(view.DebugTick(), 42);
    EXPECT_STREQ(view.DebugLensName(), "Logistics");
    EXPECT_TRUE(view.DebugFactionRecentChanged(0));  // recentEventAge 0 < kJuiceK
    Rml::RemoveContext("hv"); Rml::Shutdown();
}
```
(`DebugTick`/`DebugLensName`/`DebugFactionRecentChanged` are small const accessors added to `HudView` for the test.)

- [ ] **Step 3: Run it — expect FAIL** (`graph/HudView.h` not found).

- [ ] **Step 4: Create `application/main/include/graph/HudView.h`:**
```cpp
#pragma once
#include "Ui/IView.h"
#include "Generated/Hud.g.h"   // Vixen::Views::{HudFaction,HudEvent,HudBind,BindHudModel}
#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/Types.h>  // Rml::String
#include <span>
#include <vector>
#include <cstdint>

namespace Vixen::App {

// Host-facing input types for the HUD projection (relocated from UIRenderNode — these are the
// CONSUMER's vocabulary, not the engine's). recentEventAge: ticks since this faction's most recent
// world event (255 = none within K); < kJuiceK -> recentChanged (drives the .changed CSS pulse).
struct HudFactionIn { const char* name; float grievance; bool focused; bool known; bool inLens; uint8_t recentEventAge; };
struct HudEventIn   { const char* kind; int tick; };

// The main app's HUD view. Owns its storage, registers the "hud" data model via the generated
// BindHudModel, and projects sim/host data into it. This is VIXEN's own native consumer of the
// renderer-agnostic view contract (Inc-2).
class HudView final : public Vixen::RenderGraph::IView {
public:
    const char* ModelName() const override { return "hud"; }
    const char* DocumentPath() const override { return "assets/ui/hud.rml"; }
    void Register(Rml::DataModelConstructor& c) override {
        Vixen::Views::BindHudModel(c, Vixen::Views::HudBind{
            &tick_, &bodyCount_, &activeLensName_, &activeLensCount_, &factions_, &events_ });
        model_ = c.GetModelHandle();
    }
    // The relocated SetHudView projection (was UIRenderNode::SetHudView). Copies inputs into the
    // bound storage, applies the lens-name + juice projection, and dirties the vars.
    void SetHudView(int tick, int bodyCount, int activeLens, int activeLensCount,
                    std::span<const HudFactionIn> factions, std::span<const HudEventIn> events);

    // Debug accessors for the unit test.
    int DebugTick() const { return tick_; }
    const char* DebugLensName() const { return activeLensName_.c_str(); }
    bool DebugFactionRecentChanged(size_t i) const { return factions_.at(i).recentChanged; }

private:
    int tick_ = 0;
    int bodyCount_ = 0;
    Rml::String activeLensName_ = "None";
    int activeLensCount_ = 0;
    std::vector<Vixen::Views::HudFaction> factions_;
    std::vector<Vixen::Views::HudEvent>   events_;
    Rml::DataModelHandle model_;
};

}  // namespace Vixen::App
```

- [ ] **Step 5: Create `application/main/source/graph/HudView.cpp`** (the relocated projection, verbatim logic from `UIRenderNode.cpp:357-390`, now writing into `Vixen::Views::HudFaction`/`HudEvent`):
```cpp
#include "graph/HudView.h"

namespace Vixen::App {

void HudView::SetHudView(int tick, int bodyCount, int activeLens, int activeLensCount,
                         std::span<const HudFactionIn> factions, std::span<const HudEventIn> events) {
    tick_      = tick;
    bodyCount_ = bodyCount;
    static const char* const kLensNames[] = { "None", "Intel", "Logistics", "Threat" };
    activeLensName_  = (activeLens >= 0 && activeLens < 4) ? kLensNames[activeLens] : "None";
    activeLensCount_ = activeLensCount;

    static constexpr uint8_t kJuiceK = 20;
    factions_.clear();
    factions_.reserve(factions.size());
    for (const HudFactionIn& f : factions)
        factions_.push_back({ f.name ? Rml::String(f.name) : Rml::String{},
                              f.grievance, f.focused, f.known, f.inLens,
                              f.recentEventAge < kJuiceK });

    events_.clear();
    events_.reserve(events.size());
    for (const HudEventIn& e : events)
        events_.push_back({ e.kind ? Rml::String(e.kind) : Rml::String{}, e.tick });

    if (model_) {
        model_.DirtyVariable("tick");
        model_.DirtyVariable("bodyCount");
        model_.DirtyVariable("activeLensName");
        model_.DirtyVariable("activeLensCount");
        model_.DirtyVariable("factions");
        model_.DirtyVariable("events");
    }
}

}  // namespace Vixen::App
```

- [ ] **Step 6: Wire `application/main/CMakeLists.txt`** — add `source/graph/HudView.cpp` to the main-app target sources; ensure `application/main/include` is on its include path (for `graph/HudView.h` + `Generated/Hud.g.h`); the app already links `RenderGraphNodes` (for `IView.h` + RmlUi). Register `tests/test_hud_view.cpp` as a gtest target mirroring how the app's existing tests register (link `GTest::gtest_main` + the app's view/RmlUi deps; if the main app has no test block yet, add one gated on `if(TARGET GTest::gtest_main)`), with the `application/main/include` include dir. **(Same-task registration.)**

- [ ] **Step 7: Build + run the consumer test — expect PASS.**
```
cmd.exe /c "cd /d C:\cpp\VBVS--VIXEN\.claude\worktrees\<wt>\VIXEN && cmake --build build --config Debug --target test_hud_view"
cmd.exe /c "cd /d C:\cpp\VBVS--VIXEN\.claude\worktrees\<wt>\VIXEN && build\application\main\Debug\test_hud_view.exe --gtest_brief=1"
```
Expected: `[  PASSED  ] 1 test.` Then rebuild the golden test (its `VIEW_HUD_G_H` path moved) — expect still 3/3.

- [ ] **Step 8: Commit.**
```bash
git add application/main/include/graph/HudView.h application/main/source/graph/HudView.cpp \
        application/main/include/Generated/Hud.g.h application/main/tests/test_hud_view.cpp \
        application/main/CMakeLists.txt codegen/CMakeLists.txt libraries/RenderGraph/tests/CMakeLists.txt
git commit -m "feat(view-contract): native HudView consumer in main app (BindHudModel + relocated projection); move generated header to consumer"
```

---

## Task 5: Wire HudView into the main app + byte-exact HUD PNG capture gate (real GPU)

**Repo:** VIXEN.

**Files:**
- Modify: `application/main/source/graph/BuildRenderGraph.cpp` (set the view on the UI node)
- Modify: `application/main/include/VulkanGraphApplication.h` + `application/main/source/VulkanGraphApplication.cpp` (own the `HudView`; add a scripted HUD-inject + capture path)
- Create: `application/main/source/main_hud_capture.bat` (the unattended real-GPU capture runner) — force-tracked past any temp gitignore if needed
- Create: `libraries/RenderGraph/tests/Nodes/test_hud_render_capture.cpp` (file-I/O PNG gate) + register it in the tests CMake

**Interfaces:**
- Consumes: `Vixen::App::HudView` (Task 4); `UIRenderNode::SetView`/`MarkViewDirty` (Task 3); `GetUiRenderNode()`; `RenderTargetReadback.h`'s `CaptureRenderTargetToPng`.

**Design:** The main app uses `VulkanGraphApplication` directly (no `EditorApplication`-style subclass). Add the HUD-inject-and-capture path onto `VulkanGraphApplication` itself, gated by env vars mirroring the editor's (`VIXEN_HUD_SCRIPT` = a frame-keyed set of known faction/event payloads to push via `HudView::SetHudView` + `MarkViewDirty`; `VIXEN_HUD_CAPTURE_FRAMES`; `VIXEN_HUD_CAPTURE_DIR`). The gate: run the app twice-in-one (baseline = empty HUD, populated = known factions) OR run before/after the code change — but since the code change is the decouple itself, the gate is **A/B across a KNOWN-DATA render**: capture the HUD with a fixed faction set, assert it is non-empty AND deterministic (byte-stable across two runs), and that a DIFFERENT faction set yields DIFFERENT pixels (proving the generated binding actually drives pixels). This proves the generic-host + generated-binder path renders the HUD, on the real GPU.

- [ ] **Step 1: Own the HudView + set it on the node.** In `VulkanGraphApplication.h` add `#include "graph/HudView.h"` and a member `Vixen::App::HudView hudView_;`. In `BuildRenderGraph.cpp`, right after `uiRenderNode_` is stored (`:256`) and the `RML_DOCUMENT_PATH` is set (`:743`), set the view:
```cpp
    if (auto* ui = GetUiRenderNode()) {
        ui->SetView(std::shared_ptr<Vixen::RenderGraph::IView>(&hudView_, [](Vixen::RenderGraph::IView*){}));  // non-owning: hudView_ outlives the node
    }
```
(Non-owning `shared_ptr` aliasing — `hudView_` is a member of the app, which outlives the graph. Document the WHY.)

- [ ] **Step 2: Add the scripted HUD-inject + capture path.** Mirror `EditorApplication`'s harness (`ParseEditorScript`/`PreTick`/capture-in-Update-tail/`CaptureRenderTargetToPng` against `compute_render_target`). Concretely, add to `VulkanGraphApplication` (or a small file-local helper in its .cpp): parse `VIXEN_HUD_SCRIPT` once (a token list like `factionsA@30,factionsB@60` selecting one of a couple of hard-coded known payloads — kept in the .cpp), on the named frame call `hudView_.SetHudView(...knownPayload...)`; parse `VIXEN_HUD_CAPTURE_FRAMES`/`VIXEN_HUD_CAPTURE_DIR`; on a capture frame call `CaptureRenderTargetToPng(GetCompositeRenderTarget("compute_render_target"), captureDir + "/hud_capture_" + frame + ".png", ...)`. Use the main app's actual capture-target instance name (grep `BuildRenderGraph.cpp` for the `RenderTargetNode`/`compute_render_target` name — confirm it exists in this graph; if the main graph's composite target has a different name, use that). Reuse the editor's frame-0-is-black lesson: capture at frame ≥5, not 0.

- [ ] **Step 3: Write the file-I/O PNG gate** `libraries/RenderGraph/tests/Nodes/test_hud_render_capture.cpp`, modeled on `test_editor_toggle_undo_capture.cpp` (stb_image load, byte compare). It asserts, over PNGs produced by the `.bat` run:
  - `hud_capture_A.png` (faction set A) and `hud_capture_A2.png` (same set A, later frame) are **byte-identical** (determinism).
  - `hud_capture_A.png` and `hud_capture_B.png` (different faction set) **differ** in the HUD region (`> kMinHudDiffPixels`, a small threshold measured live — the HUD is a small overlay; calibrate like the editor gate's `kMinBoreDiffPixels`).
  - Both are non-empty. This proves the generated binding drives real pixels deterministically, on the real GPU.
  Register it in the RenderGraph tests CMake OUTSIDE the glslc gate (pure file-I/O, needs `stb_image`), mirroring `test_editor_toggle_undo_capture`'s registration.

- [ ] **Step 4: Create the unattended runner `application/main/main_hud_capture.bat`** — mirrors `run_editor_script.bat`: `cd /d %~dp0..` (worktree-portable — NEVER a hardcoded worktree path, per the graph.Run() gotcha), set `VIXEN_HUD_SCRIPT`/`VIXEN_HUD_CAPTURE_FRAMES`/`VIXEN_HUD_CAPTURE_DIR`/`VIXEN_EXIT_AFTER_FRAMES`, run the built `VIXEN.exe`. Document the exact frame schedule it uses so the gate's expected filenames match.

- [ ] **Step 5: Run the unattended real-GPU capture, then the gate.** Build `VIXEN` (the main app) + the gate; run the `.bat` (real GPU, ~50s, POLL it on a foreground interval — never blind-wait); then run the gate:
```
cmd.exe /c "cd /d C:\cpp\VBVS--VIXEN\.claude\worktrees\<wt>\VIXEN && cmake --build build --config Debug --target VIXEN"
cmd.exe /c "cd /d C:\cpp\VBVS--VIXEN\.claude\worktrees\<wt>\VIXEN && application\main\main_hud_capture.bat"
cmd.exe /c "cd /d C:\cpp\VBVS--VIXEN\.claude\worktrees\<wt>\VIXEN && cmake --build build --config Debug --target test_hud_render_capture"
cmd.exe /c "cd /d C:\cpp\VBVS--VIXEN\.claude\worktrees\<wt>\VIXEN && build\libraries\RenderGraph\tests\Debug\test_hud_render_capture.exe --gtest_brief=1"
```
Expected: gate PASS (determinism byte-equal + A≠B in HUD region + non-empty). If the HUD region diff is 0 for A vs B, the binding is NOT driving pixels — STOP and diagnose (the whole increment's point failed); do not weaken the threshold to pass.

- [ ] **Step 6: Commit.**
```bash
git add application/main/source/graph/BuildRenderGraph.cpp application/main/include/VulkanGraphApplication.h \
        application/main/source/VulkanGraphApplication.cpp application/main/main_hud_capture.bat \
        libraries/RenderGraph/tests/Nodes/test_hud_render_capture.cpp libraries/RenderGraph/tests/CMakeLists.txt
git commit -m "feat(view-contract): wire HudView into main app + byte-exact HUD PNG capture gate (real GPU)"
```

---

## Task 6: Migrate `test_ui_hud_smoke` off the deleted engine surface

**Repo:** VIXEN.

**Files:**
- Modify: `libraries/RenderGraph/tests/test_ui_hud_smoke.cpp`
- Modify: `libraries/RenderGraph/tests/CMakeLists.txt` if the test's deps/includes change (needs `HudView.h` → the main-app include dir).

**Why:** `test_ui_hud_smoke` `#include "Nodes/UIRenderNode.h"` and uses `HudFactionIn`/`HudEventIn`/`SetHudView`/`SetHudData` — all deleted from the node in Task 3. The smoke test's VALUE (hud.rml loads; the data model drives `{{tick}}`/`{{factions}}` after a mutation+DirtyVariable+Update) is still worth keeping — it just needs to drive the model via the relocated `HudView` (or an inline equivalent), not the node.

- [ ] **Step 1: Repoint the includes + types.** Replace `#include "Nodes/UIRenderNode.h"` with `#include "graph/HudView.h"`; add the `application/main/include` dir to the test target's includes in CMake (same-task). Change `HudFactionIn`/`HudEventIn` → `Vixen::App::HudFactionIn`/`HudEventIn`. The `S1b_SetHudViewListsResolveInRml` test (which builds the model, mutates, Update()s, and greps the inner RML) now builds the model via a `Vixen::App::HudView` + `view.Register(constructor)` + `view.SetHudView(...)` instead of the node's private members. The `S1b_SetHudViewApiCompiles` test now type-checks `Vixen::App::HudView::SetHudView`.

- [ ] **Step 2: Run the smoke test — expect PASS** (all existing sub-tests green through the relocated path):
```
cmd.exe /c "cd /d C:\cpp\VBVS--VIXEN\.claude\worktrees\<wt>\VIXEN && cmake --build build --config Debug --target test_ui_hud_smoke"
cmd.exe /c "cd /d C:\cpp\VBVS--VIXEN\.claude\worktrees\<wt>\VIXEN && build\libraries\RenderGraph\tests\Debug\test_ui_hud_smoke.exe --gtest_brief=1"
```
Expected: PASS (same sub-test count as before, now via `HudView`). This proves the relocated consumer still drives `hud.rml` correctly.

- [ ] **Step 3: Commit.**
```bash
git add libraries/RenderGraph/tests/test_ui_hud_smoke.cpp libraries/RenderGraph/tests/CMakeLists.txt
git commit -m "test(view-contract): migrate test_ui_hud_smoke onto the relocated HudView consumer"
```

---

## Task 7: `RmlMarkupEmitter` — generate the RML data-binding partial (Yeroket)

**Repo:** Yeroket.

**Files:**
- Create: `$KF/SourceGenerator~/Transpiler/RmlMarkupEmitter.cs`
- Modify: `$KF/CodegenTool~/Program.cs` (add a `--view-markup <Name> --out-rml <path>` branch, mirroring `--view`)
- Test: `$KF/CodegenTool~/Tests/RmlMarkupEmitterTests.cs` (NUnit)

**Interfaces:**
- Consumes: `ViewStruct`/`ViewField`/`ViewScalar`/`ViewFieldKind` (the real M1 `ViewModel.cs` — READ it; do NOT trust assumed shapes) + `CompilationLoader.LoadViews`.
- Produces: `RmlMarkupEmitter.Emit(ViewStruct) → string` — a generated RML **fragment** (NOT a full document): the scalar `{{field}}` lines + a `data-for` row skeleton per array field. The fragment is a marked region the hand-authored shell composes (see Task 8's compose model). Emission: for each scalar field, a `<div data-gen-field="name">{{name}}</div>`; for each StructArray field `xs` of row `R`, `<div data-for="x : xs"> <span data-gen-field="R.f">{{x.f}}</span>… </div>` over R's fields. C# string/int/float/bool all render as `{{…}}` (RmlUi stringifies). The emitter is DELIBERATELY minimal — it emits the binding skeleton; presentation (classes, ternaries, data-class-*) stays in the hand shell (spec D2).

**Note on faithfulness:** the generated fragment does NOT reproduce hud.rml's hand-authored `.clock`/ternary/`data-class-*` — those are shell. The fragment's contract is: every schema field appears exactly once as a `{{}}`/`data-for` binding, in field order. Task 8 composes it inside the shell.

- [ ] **Step 1: Write the failing NUnit test** `$KF/CodegenTool~/Tests/RmlMarkupEmitterTests.cs` (NUnit — `[TestFixture]`/`[Test]`/`Assert.That`; build fixtures via `CompilationLoader.LoadViews` + `ViewModelBuilder.Build` like `RmlDataModelEmitterTests.cs`). Assert for a `Hud`-like view (`int tick; Row[] rows` where `Row{string name; float grief}`): the output contains `{{tick}}`, `data-for="r : rows"` (or the emitter's chosen loop var), `{{r.name}}`, `{{r.grief}}`, and that `tick` appears before the `rows` loop (field order).

- [ ] **Step 2: Run it — expect FAIL** (no `RmlMarkupEmitter`). Run from `$KF/CodegenTool~/Tests` (NOT `CodegenTool~/` — the durable 0-tests gotcha): `cd $KF/CodegenTool~/Tests && dotnet test --filter RmlMarkupEmitterTests 2>&1 | tail -20`.

- [ ] **Step 3: Implement `RmlMarkupEmitter.cs`** (adapt member/enum names to the REAL `ViewModel.cs`; keep the emitter's `namespace` matching the sibling `RmlDataModelEmitter`):
```csharp
using System.Text;
using System.Linq;

namespace Yeroket.KernelFramework.Codegen  // MATCH RmlDataModelEmitter's real namespace
{
    public static class RmlMarkupEmitter
    {
        // Emit a generated RML data-binding FRAGMENT for a [View]: {{scalar}} lines + a data-for row
        // skeleton per array field, in field order. Presentation (classes/ternaries/data-class-*) is
        // the hand-authored shell's job — this only wires each schema field to a binding exactly once.
        public static string Emit(ViewStruct v)
        {
            var sb = new StringBuilder();
            sb.AppendLine("<!-- GENERATED by Yeroket kernel-codegen (--view-markup) — DO NOT EDIT. -->");
            sb.AppendLine($"<!-- data-binding partial for [View] {v.Name}; compose inside a data-model=\"{v.Name.ToLowerInvariant()}\" element. -->");
            foreach (var f in v.Fields)
            {
                if (f.Kind == ViewFieldKind.Scalar)
                    sb.AppendLine($"<div data-gen-field=\"{f.Name}\">{{{{{f.Name}}}}}</div>");
                else if (f.Kind == ViewFieldKind.StructArray)
                {
                    string loop = f.Name.TrimEnd('s');
                    if (loop == f.Name) loop = f.Name + "_i";
                    sb.AppendLine($"<div class=\"gen-list\" data-for=\"{loop} : {f.Name}\">");
                    foreach (var rf in f.Struct.Fields)
                        sb.AppendLine($"    <span data-gen-field=\"{f.Struct.Name}.{rf.Name}\">{{{{{loop}.{rf.Name}}}}}</span>");
                    sb.AppendLine("</div>");
                }
                // Struct (non-array) fields: none in Hud; a data-gen scalar group would go here (deferred).
            }
            return sb.ToString().Replace("\r\n", "\n");
        }
    }
}
```

- [ ] **Step 4: Add the `--view-markup` CLI branch to `Program.cs`** (mirror the `--view` branch: read `--view-markup <Name>` + `--out-rml <path>` + `--check`; `LoadViews` → find named → `ViewModelBuilder.Build` → `RmlMarkupEmitter.Emit` → Write/`--check`). Confirm the exact flag-reading helpers (`Flag`, `Same`, `Write`) against the real `Program.cs`.

- [ ] **Step 5: Run the test — expect PASS**, then the whole tool suite: `cd $KF/CodegenTool~/Tests && dotnet test 2>&1 | tail -20` (all green, no regression).

- [ ] **Step 6: Commit (Yeroket).**
```bash
cd /home/liory/Github/Yeroket-Fantasy
git add Packages/com.yeroket.utility.kernel-framework/SourceGenerator~/Transpiler/RmlMarkupEmitter.cs \
        Packages/com.yeroket.utility.kernel-framework/CodegenTool~/Program.cs \
        Packages/com.yeroket.utility.kernel-framework/CodegenTool~/Tests/RmlMarkupEmitterTests.cs
git commit -m "feat(codegen): RmlMarkupEmitter — [View] schema -> RmlUi data-binding partial (--view-markup)"
```
(Do NOT stage bin/obj/*.dll. Do NOT push.)

---

## Task 8: Compose the generated partial into `hud.rml` + CMake gate + close-out

**Repo:** VIXEN.

**Files:**
- Create (generated, committed): `application/main/assets/ui/Hud.view.rml` (or `libraries/RenderGraph/assets/ui/` if the shell stays there — see decision below)
- Modify: `libraries/RenderGraph/assets/ui/hud.rml` (compose the partial into a marked region)
- Modify: `codegen/CMakeLists.txt` (add `view_hud_markup_check`/`view_hud_markup_regen`)
- Modify: `Vixen-Docs/01-Architecture/Renderer-Agnostic-View-Contract-Inc2-Plan-2026-07.md` (Progress Log)

**Compose model (spec D2):** RmlUi doesn't require a literal file-include; the cleanest composition that keeps the hand shell is **a marked region in `hud.rml`** that the generated partial's content matches, guarded by a golden check that the marked region's bindings equal the generated partial (so a schema change that isn't reflected in `hud.rml` fails the build). The hand-authored `.clock`/ternary/`data-class-*` presentation lives OUTSIDE the marked region wrapping the generated bindings. (If RmlUi's `data-include`/template is confirmed working in this version, an actual include is preferable — verify in Step 1; otherwise the marked-region + golden-check model is the fallback and is sufficient for Inc-2.)

- [ ] **Step 1: Generate the partial + decide compose mechanism.** Regen the partial from the Hud schema:
```bash
/home/liory/.dotnet/dotnet run --project "/home/liory/Github/Yeroket-Fantasy/Packages/com.yeroket.utility.kernel-framework/CodegenTool~" -c Release -- \
  --schema "$(git rev-parse --show-toplevel)/VIXEN/codegen/view-schemas" --view-markup Hud \
  --out-rml "$(git rev-parse --show-toplevel)/VIXEN/libraries/RenderGraph/assets/ui/Hud.view.rml"
```
Read the emitted `Hud.view.rml`. Check whether this RmlUi version supports `data-include`/`<template>` (grep `_deps/rmlui-src` for `data-include`/`RegisterTemplate`); if yes, `hud.rml` includes it; if no, use the marked-region model (Step 2).

- [ ] **Step 2: Compose into `hud.rml`.** Wrap the data-bound region (`hud.rml:8-30`) so the generated bindings live in a clearly-marked block (`<!-- GEN:hud-bindings BEGIN -->` … `<!-- GEN:hud-bindings END -->`), preserving the hand-authored shell (`.clock`, `.lens-status`, `.factions`/`.events` wrappers, `.section-label`, `data-class-*`, the `✓`/`·` ternary). The generated partial's fields must all be present inside that region. Keep the model name `data-model="hud"`.

- [ ] **Step 3: Add the CMake gate** in `codegen/CMakeLists.txt` (inside the same Yeroket-tool guard), mirroring `view_hud_check`:
```cmake
    set(_view_markup_args
        run --project "${_yk_tool}" -c Release --
        --schema "${_cg}/view-schemas" --view-markup Hud
        --out-rml "${CMAKE_SOURCE_DIR}/libraries/RenderGraph/assets/ui/Hud.view.rml")
    add_custom_target(view_hud_markup_check ALL
        COMMAND ${VIXEN_DOTNET} ${_view_markup_args} --check
        COMMENT "[codegen] golden check: Hud.view.rml matches canonical [View] schema (Yeroket tool)"
        VERBATIM)
    add_custom_target(view_hud_markup_regen
        COMMAND ${VIXEN_DOTNET} ${_view_markup_args}
        COMMENT "[codegen] regenerate Hud.view.rml (Yeroket tool)"
        VERBATIM)
```

- [ ] **Step 4: Re-run the live HUD capture gate (Task 5) — expect PASS** (the composed markup + the generated binding still render the HUD deterministically, A≠B). This is the authoritative proof the compose didn't break the live HUD:
```
cmd.exe /c "cd /d C:\cpp\VBVS--VIXEN\.claude\worktrees\<wt>\VIXEN && cmake --build build --config Debug --target VIXEN"
cmd.exe /c "cd /d C:\cpp\VBVS--VIXEN\.claude\worktrees\<wt>\VIXEN && application\main\main_hud_capture.bat"
cmd.exe /c "cd /d C:\cpp\VBVS--VIXEN\.claude\worktrees\<wt>\VIXEN && build\libraries\RenderGraph\tests\Debug\test_hud_render_capture.exe --gtest_brief=1"
```
Expected: PASS. Poll the `.bat` run on a foreground interval.

- [ ] **Step 5: Full no-regression sweep + close-out.** Build+run the whole affected test set and confirm green: `test_view_hud_golden` (3/3), `test_hud_view` (1/1), `test_ui_hud_smoke` (all), `test_hud_render_capture` (PASS), `test_editor_toggle_undo_capture` (unchanged — editor still works), and `view_hud_check` + `view_hud_markup_check` (both exit 0, WSL-side). Then update the Progress Log below and commit:
```bash
git add libraries/RenderGraph/assets/ui/Hud.view.rml libraries/RenderGraph/assets/ui/hud.rml \
        codegen/CMakeLists.txt Vixen-Docs/01-Architecture/Renderer-Agnostic-View-Contract-Inc2-Plan-2026-07.md
git commit -m "feat(view-contract): generate + compose Hud RML data-binding partial; Inc-2 complete"
```

---

## Progress Log

_(Appended per milestone during execution.)_

- **M1 (Tasks 1–2, VIXEN rename + golden re-anchor): DONE** · commits `21e41247` (Task 1 — rename schema/header/CMake) + `1a108ceb` (Task 2 — golden re-anchor) on `worktree-view-contract-inc2` · clean `git mv` renames (`EditorHud.cs=>Hud.cs`, `EditorHud.g.h=>Hud.g.h`, `test_view_editorhud_golden.cpp=>test_view_hud_golden.cpp`); symbols `HudBind`/`BindHudModel`; targets `view_hud_check`/`view_hud_regen`/`test_view_hud_golden`; macro `VIEW_HUD_G_H`; model name unified `"hud"`; `HudFaction`/`HudEvent` UNCHANGED (correct); node UNTOUCHED (no behavior change) · `test_view_hud_golden` **2/2 PASS** (fresh Windows-side ninja rebuild) · `view_hud_check` `--check` exit 0 (WSL-side, header == regen) · golden `kExpected` = same 18-token Inc-1 sequence, re-anchored to the canonical schema + future `HudView::Register` (not the soon-deleted node block) · Opus validator APPROVED (independent fresh rebuild + re-run) · Yeroket `SDFNodeGenerator.dll` churn reverted, both repos clean · 2026-07-06. Only rename residue = a provenance comment (`codegen/CMakeLists.txt:79`).
- _M2 (Tasks 3–5, generic host + HudView consumer + live gate): pending_
- _M3 (Tasks 6–8, smoke migrate + markup emit + compose + close-out): pending_

---

## Self-review notes (author)

- **Spec coverage:** §1 inversion → Task 3 (node agnostic) + Task 4 (consumer owns view). §3 face 1 native → Task 4 (`BindHudModel` via `HudView`); face 2 authoring/markup → Tasks 7-8 (partial emit + compose). §4.1 relocation → Tasks 3+4 (strip node, move header + projection to app). §4.2 `IView` seam → Task 3. §5.1 native fast-path → Task 4. §7 face 4 (view→action) NOT in this plan (later slice — correct). §9 testing: golden (Task 2), byte-exact live PNG (Task 5), smoke migrate (Task 6) all present. Blob path (§5.2) is Inc-2b — correctly out of this plan.
- **Rename completeness:** Task 1 (schema/header/CMake), Task 2 (golden test + its CMake), Task 4 (header relocation + repoint), Task 6 (smoke test types). All `EditorHud`/`editorhud`/`VIEW_EDITORHUD_G_H`/`view_editorhud_*` occurrences from the pre-plan grep are covered. Model name unified to `"hud"` in Task 2.
- **Type consistency:** `IView` (ModelName/Register/DocumentPath) identical in Tasks 3-5. `HudView` (Register/SetHudView/HudFactionIn/HudEventIn) identical in Tasks 4-6. `HudBind`/`BindHudModel` identical in Tasks 1,4. `SetView`/`MarkViewDirty` identical in Tasks 3,5.
- **Known-risk callouts for the implementer:** (a) the main app has NO app-subclass — Task 5 adds the inject/capture path onto `VulkanGraphApplication` directly; confirm the composite capture-target instance name in the MAIN graph (may not be `compute_render_target`). (b) The golden's human-truth anchor MOVED (node block deleted) — Task 2 re-anchors to the schema; don't leave a dangling "UIRenderNode.cpp:116-138" reference. (c) The live gate is A/B-on-known-data (determinism + A≠B), not before/after-a-swap, because the swap IS the code change — a 0-pixel A-vs-B diff means the binding isn't driving pixels: STOP, don't weaken the threshold. (d) `data-model` must stay on the inner `<div>`, never `<body>` (RmlUi parse-order constraint). (e) worktree `.bat` paths must be `%~dp0`-relative, never hardcoded (graph.Run() gotcha).
- **No placeholders:** every code step carries full code; every command has expected output. The one genuinely deferred detail (RmlUi include-vs-marked-region compose) is a Step-1 *decision with both branches specified*, not a placeholder.
- **Worker-path note:** worker prompts must fill `<wt>` (the VIXEN worktree name) and know Yeroket is at `/home/liory/Github/Yeroket-Fantasy`.
