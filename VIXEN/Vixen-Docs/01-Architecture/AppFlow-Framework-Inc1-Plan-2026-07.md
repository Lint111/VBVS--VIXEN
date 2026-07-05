# AppFlow Framework — Increment 1 (Walking Skeleton) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the thinnest end-to-end slice of the AppFlow framework — a declared app-state + one reversible UI action, resolved from an element SELECTOR through the (generalized-from-undertow) binding store, flowing through a generated C++ mirror into a generic VIXEN runtime — proving the whole contract→binding→runtime→undo spine with offline tests.

**Architecture:** AppFlow is the **generalization of undertow's UI-action system** (`Undertow.Sim/UiActions/`) into an engine-owned contract (design §7c). A consumer C# schema declares one `[FlowState]` set + one `[FlowAction]` (ToggleLayer, with a **typed param signature**) + its state footprint struct. The codegen tool emits a byte-identical C++ mirror (`AppFlow.g.h`: state enum, action enum, action-decl table **incl. param signature**, footprint struct, a reader). A new VIXEN C++ library `AppFlow` ingests that mirror through a loader, resolves an element-selector→action binding through a `BindingStore` (the engine-owned generalization of undertow's `UiBindingTable` — first-win, warn-skip-inert), and runs a minimal `ActionStack` (dispatch + undo/redo + one group) and `FlowStateMachine` (guarded transition) over it, broadcasting `AppFlowChangedEvent` on the existing `MessageBus`. **undertow becomes consumer #1 of this system, not its owner.**

**Tech Stack:** C# incremental source-gen / dotnet codegen tool (Yeroket kernel-core reuse), C++23, CMake, GoogleTest, VIXEN EventBus (`MessageBus`).

**Design doc:** `VIXEN/Vixen-Docs/01-Architecture/AppFlow-Framework-Design-2026-07.md`

## Global Constraints

- C++ standard: **C++23** (`target_compile_features(... cxx_std_23)`), matching all VIXEN libs.
- Generated headers are **hand-edit-forbidden**: every `.g.h` carries `// <provenance: generated from <Source> — do not edit by hand>` (verbatim convention from `SdfOpCodes.g.h`).
- Enum values are **pinned + explicit + append-only** (never renumber; new members get the next free value). Convention from `SdfOpCodes.g.h`.
- Codegen is **dotnet-only, no Unity**: regen via `~/.dotnet/dotnet` (build the generator, run the tool / `UPDATE_GOLDENS=1 ~/.dotnet/dotnet test`). Never invoke Unity or Unity MCP.
- The generated analyzer/generator DLL **rebuilds non-deterministically** (same size, different bytes): commit it only on a real source change, else `git checkout --` it.
- Never throw across a host boundary: loader/dispatch entry points return typed result enums (the `VulkanGraphApplication::Prepare` rule).
- New VIXEN library registers via `add_subdirectory` under `VIXEN/libraries/` (the top build already does `add_subdirectory(libraries)` at `VIXEN/CMakeLists.txt:393`); tests behind `if(BUILD_TESTS)`.
- Nodes/consumers use the base `NodeInstance::device` accessors — N/A here (AppFlow is CPU-only), noted so no device member is introduced.

## Milestone Map (context-manager pipeline — grouping confirmed 2026-07-05)

Execution via post-brainstorm-context-manager in worktree `.claude/worktrees/appflow-inc1` (branch `worktree-appflow-inc1`). Implementers = Sonnet 5 / medium; validators + final review = Opus / high.

- **Milestone 1 — Contract (Tasks 1–2): ✅ DONE** — declare reference vocabulary + generate `AppFlow.g.h` + golden test. **Codegen decision (RESOLVED):** Yeroket `CodegenTool~` emits ONLY `[GpuStruct]` structs (no enum/table/reader emitter) → took plan Outcome 2: `AppFlowReference.cs` canonical + documented, `AppFlow.g.h` HAND-AUTHORED to match (both carry `TODO(appflow-codegen)` for a future real emitter). No Yeroket edit. No `codegen/CMakeLists.txt` regen target (nothing to regen yet).
- **Milestone 2 — Core primitives (Tasks 3, 4, 5, 5b): ✅ DONE** — `AppFlowEvents.h`/`AppFlowResults.h` + `FlowStateMachine` + `ActionStack` + `BindingStore`. Four pure-logic C++ units, offline gtests. All 3 test units compiled+linked+ran GREEN against vendored gtest (3/3 + 4/4 + 3/3) a milestone early. Validator wrote 6 extra adversarial probes for the untested auto-group/empty-selector paths — all correct.
- **Milestone 3 — Integration + build (Tasks 6–7): ✅ DONE** — `AppFlowLoader` + `AppFlowRuntime` (`DispatchBySelector` spine) + CMake wiring + full green suite. **17/17 PASS via the real VIXEN CMake build** (validator independently reproduced from a clean rebuild). Walking skeleton functional.
- **Milestone 4 — Close-out (Task 8): folds into the Finish step** — verify from fresh output + record the codegen-tool decision + Known-Issues (ctest gap).

## Progress Log

- (pipeline started 2026-07-05; entries appended per milestone)
- Milestone 1 (Tasks 1–2): DONE · commits 5bb5c465..b70d9114 · Opus validator APPROVED (7/7 checks, symbol-by-symbol; std::span-constexpr trap verified avoided) · codegen Outcome-2 (hand-authored header) · nits handled (Yeroket DLL reverted to HEAD ca4eb7ad; redundant `using` left cosmetic) · 2026-07-05
- Milestone 2 (Tasks 3, 4, 5, 5b): DONE · commits a7e13d5e..3adf90c5 (11 files, all added) · Opus validator APPROVED (7/7 via full compile+link+run: FSM 3/3, ActionStack 4/4, BindingStore 3/3; +6 adversarial probes for untested paths all pass) · error model clean (zero throw); C++23 verified · Yeroket clean · 2026-07-05
- Milestone 3 (Tasks 6–7): DONE · commits 2cd8ab70..30bcc4b7 (8 files, 283 insertions) · Opus validator APPROVED — independently reproduced 17/17 PASS from a clean rebuild via the real VIXEN CMake build (golden 4 + FSM 3 + ActionStack 4 + BindingStore 3 + loader/runtime 3), incl. the `DispatchBySelectorRunsBoundActionUndoably` walking-skeleton spine · only shared CMake change = 1 `add_subdirectory(AppFlow)` insert · Yeroket clean · 2026-07-05
- **Known-Issue found (pre-existing, project-wide):** `VIXEN/CMakeLists.txt` calls `add_subdirectory(libraries)` (:393) BEFORE `enable_testing()` (:445), so no library's tests are `ctest`-discoverable (`ctest -N` → 0 tests project-wide). Run gtest binaries directly (CLAUDE.md's documented method). Not AppFlow-specific → Known-Issues entry.
- **Inc-2 note:** `AppFlowChangedEvent` carries filler `state`/`action` for kinds where they aren't meaningful (e.g. ActionUndone/Redone carry FlowActionId{}=ToggleLayer id 0, StateChanged carries a filler action). Inc-1 tests don't assert them; before Inc-2 wires real consumers, either populate the affected id or document that consumers must key off `kind`. [RESOLVED for Inc-1: documented at the `AppFlowRuntime::Publish` call sites, commit c5150e56.]
- **Finish (Milestone 4 folded): ✅ DONE** — final Opus whole-diff review APPROVED (17/17 independently re-run; contract-coherent; generalization thesis holds; error model uniform; no blockers). Filler-field nit taken as a code comment (c5150e56, verified rebuild+test still green). KI-014 (ctest gap) recorded. Ready for branch integration.

## ★ INC-1 COMPLETE ★ — walking skeleton SHIPPED (branch `worktree-appflow-inc1`, 22 code files / 964 LoC, 17/17 tests green via real VIXEN CMake build). Contract → BindingStore (undertow UI-action generalization) → ActionStack/FSM → AppFlowRuntime.DispatchBySelector spine, all offline-verified. Awaiting merge to main.

## Increment roadmap (context; only Inc 1 is planned in detail below)

- **Inc 1 (this plan):** Walking skeleton — 1 state set + 1 action (ToggleLayer, typed param sig) + `BindingStore` (selector→action, first-win/warn-skip) + FSM + loader, offline tests. Proves the generalized UI-action spine end-to-end. No editor wire-up, no GPU.
- **Inc 2:** Full ActionStack (snapshot-fallback path, multi-op groups, redo edge cases) + LayerController driving RenderGraph node enable; `EditorApplication` re-expressed on AppFlow with a built-in binding set (live gate: click toggles + undo reverts a layer, PNG-verified).
- **Inc 3:** Full binding param-source resolution (DOM/event `{name, source}` read at click time), Save/param-set actions, ModuleController register/activate.
- **Inc 4:** PanelLayout — RmlUi-native dock/drag/resize + RenderTarget viewport panel + persist/restore.
- **Inc 5+ (deferred, extension points reserved):** **undertow migration** — retire `Undertow.Sim/UiActions/` into an authoring/serialization front-end over the AppFlow contract (undertow = consumer #2); callback/native actions (modding); ModuleController hot-swap.

---

## File Structure (Inc 1)

**C# consumer schema + codegen (Tier 2 + Tier 1 emit):**
- Create: `VIXEN/codegen/appflow-schemas/AppFlowReference.cs` — the reference vocabulary declaration (states, one action, footprint struct) using AppFlow marker attributes.
- Create: `VIXEN/codegen/appflow-schemas/AppFlowAttributes.cs` — the `[FlowState]`/`[FlowAction]`/`[FlowStateStruct]` marker attributes (if the reused codegen tool needs them defined consumer-side; Task 1 confirms whether existing `[GpuStruct]`-style attributes suffice).
- Modify: `VIXEN/codegen/CMakeLists.txt` — add the appflow-schemas regen target alongside config-schemas.

**Generated mirror (Tier 1 output):**
- Create (generated): `VIXEN/libraries/AppFlow/include/generated/AppFlow.g.h` — state enum, action enum, `AppFlowActionDecl` table, `LayerState` footprint struct, `AppFlowContainerView` reader.

**C++ runtime (Tier 0):**
- Create: `VIXEN/libraries/AppFlow/CMakeLists.txt`
- Create: `VIXEN/libraries/AppFlow/include/AppFlowEvents.h` — `AppFlowChangedEvent : BaseEventMessage`.
- Create: `VIXEN/libraries/AppFlow/include/AppFlowResults.h` — `LoadResult`, `DispatchResult` enums.
- Create: `VIXEN/libraries/AppFlow/include/AppFlowLoader.h` + `src/AppFlowLoader.cpp` — ingests `AppFlow.g.h` tables.
- Create: `VIXEN/libraries/AppFlow/include/ActionStack.h` + `src/ActionStack.cpp` — dispatch, undo, redo, one group.
- Create: `VIXEN/libraries/AppFlow/include/FlowStateMachine.h` + `src/FlowStateMachine.cpp` — guarded transition.
- Create: `VIXEN/libraries/AppFlow/include/BindingStore.h` + `src/BindingStore.cpp` — engine-owned generalization of undertow's `UiBindingTable` + `UiActionRegistry` resolution: register actions (name+param sig), resolve a binding (selector→action, validate params), `TryGetForSelector`. First-win, warn-skip-inert, never throws.
- Create: `VIXEN/libraries/AppFlow/include/AppFlowRuntime.h` + `src/AppFlowRuntime.cpp` — owns loader+stack+fsm+bindings, holds the `MessageBus*`, the single façade.
- Modify: `VIXEN/libraries/CMakeLists.txt` — `add_subdirectory(AppFlow)`.

**Tests (offline, no GPU):**
- Create: `VIXEN/libraries/AppFlow/tests/CMakeLists.txt`
- Create: `VIXEN/libraries/AppFlow/tests/test_appflow_golden.cpp` — mirror-golden guard.
- Create: `VIXEN/libraries/AppFlow/tests/test_action_stack.cpp` — dispatch/undo/redo/group.
- Create: `VIXEN/libraries/AppFlow/tests/test_flow_state_machine.cpp` — transition accept/reject.
- Create: `VIXEN/libraries/AppFlow/tests/test_binding_store.cpp` — resolve selector→action, unknown-action/bad-param warn-skip, first-win.
- Create: `VIXEN/libraries/AppFlow/tests/test_appflow_loader.cpp` — good + malformed ingest.

---

### Task 1: Confirm codegen tool + declare the reference vocabulary

**Files:**
- Read: `VIXEN/codegen/config-schemas/OctreeConfig.cs`, `VIXEN/codegen/CMakeLists.txt` (existing `[GpuStruct]`→`.g.h` tool — the closest precedent)
- Create: `VIXEN/codegen/appflow-schemas/AppFlowReference.cs`
- Create (only if needed): `VIXEN/codegen/appflow-schemas/AppFlowAttributes.cs`

**Interfaces:**
- Produces: a C# declaration set that the codegen tool consumes. Canonical names later tasks depend on: states `Editing`, `Simulating`, `Paused`; action `ToggleLayer` (id 0); footprint struct `LayerState { uint32_t enabledMask; }`; guard `DocumentValid` (id 0); transition `Editing→Simulating` guarded by `DocumentValid`.

- [ ] **Step 1: Determine the tool.** Read `OctreeConfig.cs` + `VIXEN/codegen/CMakeLists.txt`. Decide: does the existing VIXEN `codegen/` `[GpuStruct]`-style tool already emit standalone enums + structs + a reader into `libraries/`, or must the Yeroket `SDFNodeSourceGenerator` path be used? Record the decision in a one-line comment at the top of `AppFlowReference.cs`. **Expected:** the VIXEN `codegen/` tool (dotnet, `~/.dotnet/dotnet run --project ...`) is the closer fit for structs+enums; use it. If it cannot emit an opcode-style enum, fall back to the Yeroket generator and note it.

- [ ] **Step 2: Write the reference declaration.** In `AppFlowReference.cs`, declare (using the confirmed attributes):

```csharp
// Codegen tool: VIXEN/codegen (GpuStruct-style dotnet emitter) — confirmed Task 1 Step 1.
// AppFlow Inc-1 reference vocabulary. VIXEN ships this minimal module (design §9 option a).
namespace Vixen.AppFlow.Reference
{
    // States — members become FlowStateId (pinned, append-only).
    [FlowStateEnum]
    public enum FlowState { Editing = 0, Simulating = 1, Paused = 2 }

    // Guards — declared predicate opcodes.
    [FlowGuardEnum]
    public enum FlowGuard { DocumentValid = 0 }

    // Actions — members become FlowActionId (pinned, append-only).
    [FlowActionEnum]
    public enum FlowAction { ToggleLayer = 0 }

    // Param wire types — mirror undertow's UiParamType (String/Int/Float/EntityRef),
    // the generalized UI-action param contract (design §7c).
    [FlowParamTypeEnum]
    public enum FlowParamType { String = 0, Int = 1, Float = 2, EntityRef = 3 }

    // Action param signature — ToggleLayer takes one Int param `layerIndex`. Declared
    // so the typed-param path (validate + carry) is proven non-vacuously in Inc 1.
    // Mirrors UiActionRegistry's UiParamSchema[] signature.
    [FlowActionParams(nameof(FlowAction.ToggleLayer))]
    public static class ToggleLayerParams
    {
        // name → type; the generator emits a param-schema table entry per action.
        public const string Param0Name = "layerIndex";
        public const int    Param0Type = (int)FlowParamType.Int;
    }

    // Footprint struct for ToggleLayer — a GpuStruct-style serializable blob so the
    // runtime can snapshot it generically (Inc 2 uses this; Inc 1 emits it only).
    [FlowStateStruct]
    public struct LayerState { public uint enabledMask; }

    // One transition table entry, declared as data.
    [FlowTransition] // from=Editing to=Simulating guard=DocumentValid
    public static class Transitions
    {
        public const int From = (int)FlowState.Editing;
        public const int To = (int)FlowState.Simulating;
        public const int Guard = (int)FlowGuard.DocumentValid;
    }
}
```

*(If Task 1 Step 1 selects the Yeroket generator instead, translate these to `[SdfCoreKernel]`-style static-member declarations per that generator's conventions, keeping the same names/values.)*

- [ ] **Step 3: Build the codegen tool to verify the declaration compiles.**

Run: `~/.dotnet/dotnet build -c Release` in the codegen tool's project dir (per Task 1 Step 1).
Expected: 0 errors — the attributes + declaration are valid C#.

- [ ] **Step 4: Commit.**

```bash
git add VIXEN/codegen/appflow-schemas/ VIXEN/codegen/CMakeLists.txt
git commit -m "feat(appflow): Inc1 reference vocabulary declaration (states, ToggleLayer, LayerState)"
```

---

### Task 2: Generate `AppFlow.g.h` and lock it with a golden test

**Files:**
- Create (generated): `VIXEN/libraries/AppFlow/include/generated/AppFlow.g.h`
- Create: `VIXEN/libraries/AppFlow/tests/test_appflow_golden.cpp`
- Modify: `VIXEN/codegen/CMakeLists.txt` (regen target writes into the AppFlow include dir)

**Interfaces:**
- Produces `AppFlow.g.h` with these exact C++ symbols (later tasks include this header):
  - `namespace Vixen::AppFlow::Generated {`
  - `enum class FlowStateId : uint16_t { Editing=0, Simulating=1, Paused=2 };`
  - `enum class FlowGuardId : uint16_t { DocumentValid=0 };`
  - `enum class FlowActionId : uint16_t { ToggleLayer=0 };`
  - `enum class FlowParamType : uint8_t { String=0, Int=1, Float=2, EntityRef=3 };`  *(mirrors undertow `UiParamType`)*
  - `struct LayerState { uint32_t enabledMask; };`
  - `struct FlowParamSchema { const char* name; FlowParamType type; };`  *(mirrors undertow `UiParamSchema`)*
  - `struct AppFlowActionDecl { FlowActionId id; uint32_t footprintBytes; bool hasInvert; const FlowParamSchema* params; uint32_t paramCount; };`
  - `struct AppFlowTransition { FlowStateId from; FlowStateId to; FlowGuardId guard; };`
  - `inline constexpr FlowParamSchema kToggleLayerParams[] = { {"layerIndex", FlowParamType::Int} };`
  - `inline constexpr AppFlowActionDecl kActionDecls[] = { {FlowActionId::ToggleLayer, sizeof(LayerState), true, kToggleLayerParams, 1} };`
  - `inline constexpr AppFlowTransition kTransitions[] = { {FlowStateId::Editing, FlowStateId::Simulating, FlowGuardId::DocumentValid} };`
  - `struct AppFlowContainerView { static constexpr std::span<const AppFlowActionDecl> actions() { return kActionDecls; } static constexpr std::span<const AppFlowTransition> transitions() { return kTransitions; } };`  *(`#include <span>`)*
  - `}` — plus the provenance banner line at top.

- [ ] **Step 1: Run codegen to emit the header.**

Run: `~/.dotnet/dotnet run --project <codegen-tool>` (writes `AppFlow.g.h`), per Task 1's tool.
Expected: `VIXEN/libraries/AppFlow/include/generated/AppFlow.g.h` created, containing the exact symbols in the Interfaces block, with the provenance banner `// <provenance: generated from AppFlowReference — do not edit by hand>`.

- [ ] **Step 2: Write the golden test (asserts the generated header matches a committed golden string).**

```cpp
#include <gtest/gtest.h>
#include <fstream>
#include <sstream>
#include <string>

// Reads the committed generated header and asserts key invariants: the provenance
// banner, pinned enum values, and the constexpr tables. A declaration change that
// regenerates the header MUST update this test (that is the guard).
static std::string readFile(const std::string& p) {
    std::ifstream f(p);
    std::stringstream ss; ss << f.rdbuf();
    return ss.str();
}

TEST(AppFlowGolden, HeaderHasProvenanceBanner) {
    const std::string h = readFile(APPFLOW_GENERATED_HEADER_PATH);
    EXPECT_NE(h.find("do not edit by hand"), std::string::npos);
}

TEST(AppFlowGolden, EnumValuesArePinned) {
    const std::string h = readFile(APPFLOW_GENERATED_HEADER_PATH);
    EXPECT_NE(h.find("Editing=0"), std::string::npos);
    EXPECT_NE(h.find("Simulating=1"), std::string::npos);
    EXPECT_NE(h.find("Paused=2"), std::string::npos);
    EXPECT_NE(h.find("ToggleLayer=0"), std::string::npos);
}

TEST(AppFlowGolden, ActionDeclTablePresent) {
    const std::string h = readFile(APPFLOW_GENERATED_HEADER_PATH);
    EXPECT_NE(h.find("kActionDecls"), std::string::npos);
    EXPECT_NE(h.find("kTransitions"), std::string::npos);
}

TEST(AppFlowGolden, ParamSignatureEmitted) {
    // The typed param signature (generalized from undertow UiParamSchema) is core, not deferred.
    const std::string h = readFile(APPFLOW_GENERATED_HEADER_PATH);
    EXPECT_NE(h.find("FlowParamType"), std::string::npos);
    EXPECT_NE(h.find("kToggleLayerParams"), std::string::npos);
    EXPECT_NE(h.find("\"layerIndex\""), std::string::npos);
}
```

- [ ] **Step 3: Wire `APPFLOW_GENERATED_HEADER_PATH`** as a compile definition in `tests/CMakeLists.txt` (added in full in Task 7; for now the test is written, it will compile once the test target exists). Note in the test file header: *this test is built by Task 7's CMake.*

- [ ] **Step 4: Commit** (header + test; the test runs green after Task 7 builds it).

```bash
git add VIXEN/libraries/AppFlow/include/generated/AppFlow.g.h VIXEN/libraries/AppFlow/tests/test_appflow_golden.cpp VIXEN/codegen/CMakeLists.txt
git commit -m "feat(appflow): generate AppFlow.g.h mirror + golden guard"
```

---

### Task 3: `AppFlowEvents.h` + `AppFlowResults.h` (the notification + result types)

**Files:**
- Create: `VIXEN/libraries/AppFlow/include/AppFlowEvents.h`
- Create: `VIXEN/libraries/AppFlow/include/AppFlowResults.h`

**Interfaces:**
- Consumes: EventBus `BaseEventMessage`, `MessageType`, `EventCategory`, `AUTO_MESSAGE_TYPE()` (from `Message.h`); the generated `FlowStateId`/`FlowActionId`.
- Produces:
  - `struct AppFlowChangedEvent : Vixen::EventBus::BaseEventMessage` with `Kind { StateChanged, ActionApplied, ActionUndone, ActionRedone }`, `FlowStateId state`, `FlowActionId action`, `uint32_t group`.
  - `enum class LoadResult { Ok, EmptyArtifact, BadTransitionRef, UnknownAction };`
  - `enum class DispatchResult { Ok, RejectedByState, GuardFailed, NothingToUndo, NothingToRedo };`

- [ ] **Step 1: Write `AppFlowResults.h`.**

```cpp
#pragma once
namespace Vixen::AppFlow {
enum class LoadResult { Ok, EmptyArtifact, BadTransitionRef, UnknownAction };
enum class DispatchResult { Ok, RejectedByState, GuardFailed, NothingToUndo, NothingToRedo };
} // namespace Vixen::AppFlow
```

- [ ] **Step 2: Write `AppFlowEvents.h`.**

```cpp
#pragma once
#include "Message.h"                 // EventBus BaseEventMessage
#include "generated/AppFlow.g.h"     // FlowStateId, FlowActionId
namespace Vixen::AppFlow {
using ::Vixen::AppFlow::Generated::FlowStateId;
using ::Vixen::AppFlow::Generated::FlowActionId;

struct AppFlowChangedEvent : public Vixen::EventBus::BaseEventMessage {
    static constexpr Vixen::EventBus::MessageType TYPE = AUTO_MESSAGE_TYPE();
    static constexpr Vixen::EventBus::EventCategory CATEGORY =
        Vixen::EventBus::EventCategory::ApplicationState;

    enum class Kind { StateChanged, ActionApplied, ActionUndone, ActionRedone };
    Kind kind;
    FlowStateId  state;
    FlowActionId action;
    uint32_t     group;

    AppFlowChangedEvent(Vixen::EventBus::SenderID sender, Kind k,
                        FlowStateId s, FlowActionId a, uint32_t g)
        : BaseEventMessage(CATEGORY, TYPE, sender), kind(k), state(s), action(a), group(g) {}
};
} // namespace Vixen::AppFlow
```

- [ ] **Step 3: Commit.**

```bash
git add VIXEN/libraries/AppFlow/include/AppFlowEvents.h VIXEN/libraries/AppFlow/include/AppFlowResults.h
git commit -m "feat(appflow): AppFlowChangedEvent + result enums"
```

*(No standalone test — these types are exercised by Tasks 4-6 tests.)*

---

### Task 4: `FlowStateMachine` — guarded transition

**Files:**
- Create: `VIXEN/libraries/AppFlow/include/FlowStateMachine.h`
- Create: `VIXEN/libraries/AppFlow/src/FlowStateMachine.cpp`
- Create: `VIXEN/libraries/AppFlow/tests/test_flow_state_machine.cpp`

**Interfaces:**
- Consumes: `FlowStateId`, `FlowGuardId`, `AppFlowTransition`, `AppFlowContainerView` (generated); `DispatchResult`.
- Produces:
  - `class FlowStateMachine` with:
    - `void LoadTransitions(const AppFlowTransition* table, size_t count);`
    - `void SetGuardResult(FlowGuardId g, bool pass);` — Inc-1 stub: guards are set externally (no real guard-opcode VM yet).
    - `FlowStateId Current() const;`
    - `void SetCurrent(FlowStateId s);`
    - `DispatchResult Request(FlowStateId to);` — finds a `(current→to)` transition, evaluates its guard via the guard-result map; on pass sets current and returns `Ok`, else `RejectedByState`/`GuardFailed`.

- [ ] **Step 1: Write the failing test.**

```cpp
#include <gtest/gtest.h>
#include "FlowStateMachine.h"
#include "generated/AppFlow.g.h"
using namespace Vixen::AppFlow;
using namespace Vixen::AppFlow::Generated;

TEST(FlowStateMachine, TransitionPassesWhenGuardTrue) {
    FlowStateMachine fsm;
    fsm.LoadTransitions(AppFlowContainerView::transitions().data(),
                        AppFlowContainerView::transitions().size());
    fsm.SetCurrent(FlowStateId::Editing);
    fsm.SetGuardResult(FlowGuardId::DocumentValid, true);
    EXPECT_EQ(fsm.Request(FlowStateId::Simulating), DispatchResult::Ok);
    EXPECT_EQ(fsm.Current(), FlowStateId::Simulating);
}

TEST(FlowStateMachine, TransitionFailsWhenGuardFalse) {
    FlowStateMachine fsm;
    fsm.LoadTransitions(AppFlowContainerView::transitions().data(),
                        AppFlowContainerView::transitions().size());
    fsm.SetCurrent(FlowStateId::Editing);
    fsm.SetGuardResult(FlowGuardId::DocumentValid, false);
    EXPECT_EQ(fsm.Request(FlowStateId::Simulating), DispatchResult::GuardFailed);
    EXPECT_EQ(fsm.Current(), FlowStateId::Editing);
}

TEST(FlowStateMachine, UndeclaredTransitionRejected) {
    FlowStateMachine fsm;
    fsm.LoadTransitions(AppFlowContainerView::transitions().data(),
                        AppFlowContainerView::transitions().size());
    fsm.SetCurrent(FlowStateId::Editing);
    EXPECT_EQ(fsm.Request(FlowStateId::Paused), DispatchResult::RejectedByState);
}
```

- [ ] **Step 2: Run to verify it fails** (target built once Task 7's CMake lands; if running standalone earlier, expect a link/compile error for missing `FlowStateMachine`). Run: `ctest -R FlowStateMachine` — Expected: FAIL (undefined `FlowStateMachine`).

- [ ] **Step 3: Write the implementation.** Header declares the class; `.cpp` implements `Request` as a linear scan of the loaded transitions matching `(current, to)`, checking the guard-result map (default: a guard not set → treated as pass, so a transition with an unset guard still fires; document this Inc-1 simplification in a comment). `AppFlowContainerView::transitions()` returns a `std::span`/array — expose `.data()`/`.size()` via a small `constexpr std::span` in the generated header (add to Task 2's header if not already a span). Store `std::vector<AppFlowTransition>`, `FlowStateId current_`, `std::unordered_map<uint16_t,bool> guardResults_`.

- [ ] **Step 4: Run to verify it passes.** Run: `ctest -R FlowStateMachine` — Expected: 3 PASS.

- [ ] **Step 5: Commit.**

```bash
git add VIXEN/libraries/AppFlow/include/FlowStateMachine.h VIXEN/libraries/AppFlow/src/FlowStateMachine.cpp VIXEN/libraries/AppFlow/tests/test_flow_state_machine.cpp
git commit -m "feat(appflow): FlowStateMachine guarded transition + tests"
```

---

### Task 5: `ActionStack` — dispatch, undo, redo, one group

**Files:**
- Create: `VIXEN/libraries/AppFlow/include/ActionStack.h`
- Create: `VIXEN/libraries/AppFlow/src/ActionStack.cpp`
- Create: `VIXEN/libraries/AppFlow/tests/test_action_stack.cpp`

**Interfaces:**
- Consumes: `FlowActionId`, `AppFlowActionDecl`, `AppFlowContainerView` (generated); `DispatchResult`.
- Produces:
  - `class ActionStack` with:
    - `void LoadActions(const AppFlowActionDecl* table, size_t count);`
    - `using ApplyFn = std::function<void(bool /*forward*/)>;` — Inc-1: the caller supplies a forward/inverse toggle callback (the real opcode VM is Inc 2). `forward=true` applies, `false` inverts.
    - `void BeginGroup(uint32_t group);`
    - `DispatchResult Dispatch(FlowActionId id, ApplyFn apply);` — runs `apply(true)`, records the entry under the current group.
    - `void EndGroup();`
    - `DispatchResult Undo();` — pops the last group, runs each entry's `apply(false)` in reverse; `NothingToUndo` if empty.
    - `DispatchResult Redo();` — re-applies the last-undone group forward; `NothingToRedo` if empty.
    - `size_t UndoDepth() const; size_t RedoDepth() const;`

- [ ] **Step 1: Write the failing test.**

```cpp
#include <gtest/gtest.h>
#include "ActionStack.h"
#include "generated/AppFlow.g.h"
using namespace Vixen::AppFlow;
using namespace Vixen::AppFlow::Generated;

TEST(ActionStack, DispatchThenUndoRestores) {
    ActionStack st;
    st.LoadActions(AppFlowContainerView::actions().data(),
                   AppFlowContainerView::actions().size());
    int value = 0;
    auto flip = [&](bool fwd){ value += fwd ? 1 : -1; };
    st.BeginGroup(1); st.Dispatch(FlowActionId::ToggleLayer, flip); st.EndGroup();
    EXPECT_EQ(value, 1);
    EXPECT_EQ(st.Undo(), DispatchResult::Ok);
    EXPECT_EQ(value, 0);
}

TEST(ActionStack, RedoReapplies) {
    ActionStack st;
    st.LoadActions(AppFlowContainerView::actions().data(),
                   AppFlowContainerView::actions().size());
    int value = 0; auto flip = [&](bool fwd){ value += fwd ? 1 : -1; };
    st.BeginGroup(1); st.Dispatch(FlowActionId::ToggleLayer, flip); st.EndGroup();
    st.Undo();
    EXPECT_EQ(st.Redo(), DispatchResult::Ok);
    EXPECT_EQ(value, 1);
}

TEST(ActionStack, GroupUndoneAsOneUnit) {
    ActionStack st;
    st.LoadActions(AppFlowContainerView::actions().data(),
                   AppFlowContainerView::actions().size());
    int value = 0; auto flip = [&](bool fwd){ value += fwd ? 1 : -1; };
    st.BeginGroup(1);
    st.Dispatch(FlowActionId::ToggleLayer, flip);
    st.Dispatch(FlowActionId::ToggleLayer, flip);
    st.EndGroup();
    EXPECT_EQ(value, 2);
    EXPECT_EQ(st.Undo(), DispatchResult::Ok);   // one Undo reverts BOTH
    EXPECT_EQ(value, 0);
    EXPECT_EQ(st.UndoDepth(), 0u);
}

TEST(ActionStack, UndoEmptyReturnsNothingToUndo) {
    ActionStack st;
    st.LoadActions(AppFlowContainerView::actions().data(),
                   AppFlowContainerView::actions().size());
    EXPECT_EQ(st.Undo(), DispatchResult::NothingToUndo);
}
```

- [ ] **Step 2: Run to verify it fails.** Run: `ctest -R ActionStack` — Expected: FAIL (undefined `ActionStack`).

- [ ] **Step 3: Write the implementation.** A group is `struct Group { uint32_t id; std::vector<Entry> entries; }`, `Entry { FlowActionId id; ApplyFn apply; }`. `undo_` and `redo_` are `std::vector<Group>`. `Dispatch` runs `apply(true)` and appends to the open group (auto-open a singleton group if `BeginGroup` wasn't called). `Undo` moves the top group from `undo_`→`redo_`, running entries' `apply(false)` in reverse order. `Redo` moves top `redo_`→`undo_`, running `apply(true)` in forward order. Dispatch clears `redo_` (new action invalidates the redo branch). Guard: a dispatch of an id not in the loaded action table returns `RejectedByState`.

- [ ] **Step 4: Run to verify it passes.** Run: `ctest -R ActionStack` — Expected: 4 PASS.

- [ ] **Step 5: Commit.**

```bash
git add VIXEN/libraries/AppFlow/include/ActionStack.h VIXEN/libraries/AppFlow/src/ActionStack.cpp VIXEN/libraries/AppFlow/tests/test_action_stack.cpp
git commit -m "feat(appflow): ActionStack dispatch/undo/redo/grouping + tests"
```

---

### Task 5b: `BindingStore` — generalized UI-action registry + binding resolution (undertow's `UiActionRegistry`/`UiBindingTable`, engine-owned)

This is the walking-skeleton's proof that AppFlow **generalizes** undertow's UI-action system: register actions with a typed param signature, resolve a `selector → action` binding with param-name validation, warn-skip inert bindings, first-win. Semantics are copied verbatim from undertow's `UiActionRegistry`/`UiBindingTable`/`LoadUiBindingsInto` (design §7c) — this is a lift into the engine, not a redesign.

**Files:**
- Create: `VIXEN/libraries/AppFlow/include/BindingStore.h`
- Create: `VIXEN/libraries/AppFlow/src/BindingStore.cpp`
- Create: `VIXEN/libraries/AppFlow/tests/test_binding_store.cpp`

**Interfaces:**
- Consumes: `FlowActionId`, `FlowParamSchema`, `AppFlowActionDecl`, `AppFlowContainerView` (generated); `DispatchResult`.
- Produces:
  - `struct BoundAction { FlowActionId action; std::string on; std::vector<std::pair<std::string,std::string>> params; };` — a resolved binding (mirrors undertow `BoundUiAction`: action + event + `{name, source}` params).
  - `class BindingStore` with:
    - `void RegisterActions(std::span<const AppFlowActionDecl> decls);` — populate the action registry from the generated table (name via `FlowActionId`, param signature via each decl's `params`/`paramCount`).
    - `struct BindingSpec { std::string selector; FlowActionId action; std::string on; std::vector<std::pair<std::string,std::string>> params; };` — an authored binding to resolve (the consumer-neutral form of a `ui_binding`).
    - `bool AddBinding(const BindingSpec& spec, std::string& warn);` — undertow's `LoadUiBindingsInto` algorithm: verify `action` is registered (else `warn="unknown action … inert"`, return false); validate every param name against the action's signature (else `warn="unknown param … inert"`, return false); first-win `Add` under `selector` (already-present selector → return false, no overwrite). Never throws.
    - `bool TryGetForSelector(const std::string& selector, BoundAction& out) const;` — mirror undertow `UiBindingTable::TryGetForSelector`.
    - `size_t BindingCount() const;`

- [ ] **Step 1: Write the failing test.**

```cpp
#include <gtest/gtest.h>
#include "BindingStore.h"
#include "generated/AppFlow.g.h"
using namespace Vixen::AppFlow;
using namespace Vixen::AppFlow::Generated;

static BindingStore makeStore() {
    BindingStore s;
    s.RegisterActions(AppFlowContainerView::actions());
    return s;
}

TEST(BindingStore, ResolvesSelectorToBoundAction) {
    auto s = makeStore();
    std::string warn;
    BindingStore::BindingSpec spec{"#layer-0-toggle", FlowActionId::ToggleLayer, "click",
                                   {{"layerIndex", "dom:attr:data-layer"}}};
    EXPECT_TRUE(s.AddBinding(spec, warn));
    EXPECT_TRUE(warn.empty());
    BoundAction out;
    ASSERT_TRUE(s.TryGetForSelector("#layer-0-toggle", out));
    EXPECT_EQ(out.action, FlowActionId::ToggleLayer);
    EXPECT_EQ(out.on, "click");
    ASSERT_EQ(out.params.size(), 1u);
    EXPECT_EQ(out.params[0].first, "layerIndex");
}

TEST(BindingStore, UnknownParamWarnsAndSkips) {
    auto s = makeStore();
    std::string warn;
    BindingStore::BindingSpec spec{"#x", FlowActionId::ToggleLayer, "click",
                                   {{"notAParam", "dom:attr:foo"}}};
    EXPECT_FALSE(s.AddBinding(spec, warn));
    EXPECT_NE(warn.find("unknown param"), std::string::npos);
    BoundAction out;
    EXPECT_FALSE(s.TryGetForSelector("#x", out));   // inert — never landed
}

TEST(BindingStore, FirstWinKeepsExisting) {
    auto s = makeStore();
    std::string warn;
    BindingStore::BindingSpec a{"#sel", FlowActionId::ToggleLayer, "click", {}};
    BindingStore::BindingSpec b{"#sel", FlowActionId::ToggleLayer, "dblclick", {}};
    EXPECT_TRUE(s.AddBinding(a, warn));
    EXPECT_FALSE(s.AddBinding(b, warn));            // selector taken → first-win, no overwrite
    BoundAction out; ASSERT_TRUE(s.TryGetForSelector("#sel", out));
    EXPECT_EQ(out.on, "click");                     // the first binding survives
}
```

- [ ] **Step 2: Run to verify it fails.** Run: `ctest -R BindingStore` — Expected: FAIL (undefined `BindingStore`).

- [ ] **Step 3: Write the implementation.** Registry: `std::unordered_map<uint16_t /*FlowActionId*/, std::vector<FlowParamSchema>>` filled from the decls. Bindings: `std::unordered_map<std::string, BoundAction>`. `AddBinding` follows undertow's algorithm exactly (registered-action check → param-name validation loop over the signature → first-win insert). Param validation: every spec param name must appear in the action's schema (undertow `ValidateParams`). Empty/duplicate selector → false. Never throw.

- [ ] **Step 4: Run to verify it passes.** Run: `ctest -R BindingStore` — Expected: 3 PASS.

- [ ] **Step 5: Commit.**

```bash
git add VIXEN/libraries/AppFlow/include/BindingStore.h VIXEN/libraries/AppFlow/src/BindingStore.cpp VIXEN/libraries/AppFlow/tests/test_binding_store.cpp
git commit -m "feat(appflow): BindingStore — generalized UI-action registry + resolution (from undertow UiBindingTable)"
```

---

### Task 6: `AppFlowLoader` + `AppFlowRuntime` — ingest the mirror + façade that broadcasts

**Files:**
- Create: `VIXEN/libraries/AppFlow/include/AppFlowLoader.h` + `src/AppFlowLoader.cpp`
- Create: `VIXEN/libraries/AppFlow/include/AppFlowRuntime.h` + `src/AppFlowRuntime.cpp`
- Create: `VIXEN/libraries/AppFlow/tests/test_appflow_loader.cpp`

**Interfaces:**
- Consumes: `AppFlowContainerView`, generated enums; `FlowStateMachine`, `ActionStack`, `BindingStore` (Task 5b); `LoadResult`/`DispatchResult`; EventBus `MessageBus`, `AppFlowChangedEvent`.
- Produces:
  - `struct AppFlowLoader { static LoadResult Load(const AppFlowContainerView& view, FlowStateMachine& fsm, ActionStack& stack, BindingStore& bindings); };` — validates (non-empty tables; every transition's from/to are valid enum values; every action id appears in the enum) and loads all three (fsm transitions, stack actions, `bindings.RegisterActions(view.actions())`). Returns `EmptyArtifact` if action table empty, `BadTransitionRef` on an out-of-range state, else `Ok`.
  - `class AppFlowRuntime` — owns a `FlowStateMachine` + `ActionStack` + `BindingStore`, holds a `MessageBus* bus_` (nullable), `SenderID sender_`. Methods: `LoadResult Load();` (uses the generated `AppFlowContainerView`), `DispatchResult RequestState(FlowStateId);` (delegates to fsm, on Ok publishes `AppFlowChangedEvent{StateChanged}`), `DispatchResult DispatchAction(FlowActionId, ActionStack::ApplyFn);` (delegates, on Ok publishes `ActionApplied`), **`DispatchResult DispatchBySelector(const std::string& selector, ActionStack::ApplyFn);`** (the end-to-end spine: `bindings_.TryGetForSelector` → dispatch the bound action through the stack → publish; miss → `RejectedByState`), `bool AddBinding(const BindingStore::BindingSpec&, std::string& warn);` (pass-through so tests/consumers author bindings), `Undo()`/`Redo()` (publish `ActionUndone`/`ActionRedone`). All publishes are `if (bus_)`-guarded and `noexcept`-safe (no throw across the boundary).

- [ ] **Step 1: Write the failing test.**

```cpp
#include <gtest/gtest.h>
#include "AppFlowRuntime.h"
#include "AppFlowLoader.h"
#include "MessageBus.h"
#include "AppFlowEvents.h"
using namespace Vixen::AppFlow;
using namespace Vixen::AppFlow::Generated;

TEST(AppFlowLoader, LoadsGeneratedViewOk) {
    FlowStateMachine fsm; ActionStack st; BindingStore bindings;
    EXPECT_EQ(AppFlowLoader::Load(AppFlowContainerView{}, fsm, st, bindings), LoadResult::Ok);
}

TEST(AppFlowRuntime, StateChangePublishesEvent) {
    Vixen::EventBus::MessageBus bus;
    int seen = 0;
    bus.Subscribe(AppFlowChangedEvent::TYPE,
        [&](const Vixen::EventBus::BaseEventMessage&){ ++seen; return true; });
    AppFlowRuntime rt(&bus, /*sender*/1);
    ASSERT_EQ(rt.Load(), LoadResult::Ok);
    rt.SetGuardResult(FlowGuardId::DocumentValid, true);
    rt.SetCurrent(FlowStateId::Editing);
    EXPECT_EQ(rt.RequestState(FlowStateId::Simulating), DispatchResult::Ok);
    EXPECT_EQ(seen, 1);          // PublishImmediate — no drain needed (see Step 1 note)
}

// THE walking-skeleton spine, end to end: a UI selector resolves (via the generalized
// binding store) to a bound action, which dispatches undoably through the stack.
TEST(AppFlowRuntime, DispatchBySelectorRunsBoundActionUndoably) {
    AppFlowRuntime rt(nullptr, /*sender*/1);
    ASSERT_EQ(rt.Load(), LoadResult::Ok);
    std::string warn;
    ASSERT_TRUE(rt.AddBinding(
        {"#layer-0-toggle", FlowActionId::ToggleLayer, "click",
         {{"layerIndex", "dom:attr:data-layer"}}}, warn));
    int value = 0;
    auto flip = [&](bool fwd){ value += fwd ? 1 : -1; };
    EXPECT_EQ(rt.DispatchBySelector("#layer-0-toggle", flip), DispatchResult::Ok);
    EXPECT_EQ(value, 1);
    EXPECT_EQ(rt.Undo(), DispatchResult::Ok);
    EXPECT_EQ(value, 0);
    EXPECT_EQ(rt.DispatchBySelector("#no-binding", flip), DispatchResult::RejectedByState);
}
```

*(Step 1 note: confirm the exact `MessageBus` drain method name — `ProcessQueue`/`Dispatch`/`Update` — from `MessageBus.h` when implementing; the test uses whatever drains queued `Publish`es, or switch to `PublishImmediate` in the runtime to avoid the drain. Pick `PublishImmediate` for Inc-1 determinism and drop the drain call.)*

- [ ] **Step 2: Run to verify it fails.** Run: `ctest -R AppFlow` — Expected: FAIL (undefined `AppFlowLoader`/`AppFlowRuntime`).

- [ ] **Step 3: Write the implementation.** `AppFlowLoader::Load` validates + calls `fsm.LoadTransitions`, `stack.LoadActions`, and `bindings.RegisterActions(view.actions())`. `AppFlowRuntime` ctor takes `(MessageBus*, SenderID)` and owns a `FlowStateMachine` + `ActionStack` + `BindingStore`; `Load()` constructs an `AppFlowContainerView` and calls the loader with all three; `AddBinding` forwards to the store; `DispatchBySelector` does `bindings_.TryGetForSelector` → on hit `stack_.Dispatch(bound.action, apply)` + publish `ActionApplied`, on miss returns `RejectedByState`; the delegating methods publish via `bus_->PublishImmediate(AppFlowChangedEvent{...})` guarded by `if (bus_)`. Expose `SetGuardResult`/`SetCurrent` pass-throughs to the fsm for the test.

- [ ] **Step 4: Run to verify it passes.** Run: `ctest -R AppFlow` — Expected: PASS (loader + runtime tests incl. `DispatchBySelectorRunsBoundActionUndoably`).

- [ ] **Step 5: Commit.**

```bash
git add VIXEN/libraries/AppFlow/include/AppFlowLoader.h VIXEN/libraries/AppFlow/src/AppFlowLoader.cpp VIXEN/libraries/AppFlow/include/AppFlowRuntime.h VIXEN/libraries/AppFlow/src/AppFlowRuntime.cpp VIXEN/libraries/AppFlow/tests/test_appflow_loader.cpp
git commit -m "feat(appflow): AppFlowLoader ingest + AppFlowRuntime façade (DispatchBySelector spine) with event broadcast"
```

---

### Task 7: CMake — build the `AppFlow` library + tests, wire into the tree, full green

**Files:**
- Create: `VIXEN/libraries/AppFlow/CMakeLists.txt`
- Create: `VIXEN/libraries/AppFlow/tests/CMakeLists.txt`
- Modify: `VIXEN/libraries/CMakeLists.txt` (add `add_subdirectory(AppFlow)`)

**Interfaces:**
- Consumes: the `EventBus` target (public link, for `MessageBus`/`BaseEventMessage`), `GTest::gtest_main`.
- Produces: targets `AppFlow` (STATIC) and the four test executables; `APPFLOW_GENERATED_HEADER_PATH` compile-def for the golden test.

- [ ] **Step 1: Write `AppFlow/CMakeLists.txt`** (mirror `EventBus/CMakeLists.txt`):

```cmake
cmake_minimum_required(VERSION 3.20...4.2)
project(AppFlow VERSION 1.0.0 LANGUAGES CXX)

set(APPFLOW_HEADERS
    include/generated/AppFlow.g.h
    include/AppFlowEvents.h
    include/AppFlowResults.h
    include/AppFlowLoader.h
    include/ActionStack.h
    include/FlowStateMachine.h
    include/BindingStore.h
    include/AppFlowRuntime.h
)
set(APPFLOW_SOURCES
    src/AppFlowLoader.cpp
    src/ActionStack.cpp
    src/FlowStateMachine.cpp
    src/BindingStore.cpp
    src/AppFlowRuntime.cpp
)
add_library(AppFlow STATIC ${APPFLOW_HEADERS} ${APPFLOW_SOURCES})
target_include_directories(AppFlow PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)
target_compile_features(AppFlow PUBLIC cxx_std_23)
set_target_properties(AppFlow PROPERTIES FOLDER "Libraries")
target_link_libraries(AppFlow PUBLIC EventBus)   # MessageBus + BaseEventMessage

if(BUILD_TESTS)
    add_subdirectory(tests)
endif()
```

- [ ] **Step 2: Write `AppFlow/tests/CMakeLists.txt`** (mirror `EventBus/tests/CMakeLists.txt`, one executable per test, with the golden header path define):

```cmake
cmake_minimum_required(VERSION 3.15)
project(AppFlowTests)

set(APPFLOW_GEN_HDR "${CMAKE_CURRENT_SOURCE_DIR}/../include/generated/AppFlow.g.h")

foreach(t test_appflow_golden test_action_stack test_flow_state_machine test_binding_store test_appflow_loader)
    add_executable(${t} ${t}.cpp)
    target_link_libraries(${t} PRIVATE GTest::gtest_main AppFlow)
    set_target_properties(${t} PROPERTIES FOLDER "Tests/AppFlow Tests")
endforeach()

# Golden test needs the on-disk header path.
target_compile_definitions(test_appflow_golden PRIVATE
    APPFLOW_GENERATED_HEADER_PATH="${APPFLOW_GEN_HDR}")

if(COMMAND gtest_discover_tests)
    foreach(t test_appflow_golden test_action_stack test_flow_state_machine test_binding_store test_appflow_loader)
        gtest_discover_tests(${t})
    endforeach()
endif()
```

- [ ] **Step 3: Register the library.** Add `add_subdirectory(AppFlow)` to `VIXEN/libraries/CMakeLists.txt` (place it alphabetically / next to other libs; confirm the file lists subdirectories explicitly).

- [ ] **Step 4: Configure + build.**

Run (from the VIXEN build dir, per project build convention):
`cmake --build build --target AppFlow test_appflow_golden test_action_stack test_flow_state_machine test_binding_store test_appflow_loader`
Expected: all targets compile, 0 errors.

- [ ] **Step 5: Run the whole Inc-1 suite.**

Run: `ctest --test-dir build -R "AppFlow|ActionStack|FlowStateMachine|BindingStore"` (and `test_appflow_golden`)
Expected: **all tests PASS** — golden (4) + FlowStateMachine (3) + ActionStack (4) + BindingStore (3) + loader/runtime (3).

- [ ] **Step 6: Commit.**

```bash
git add VIXEN/libraries/AppFlow/CMakeLists.txt VIXEN/libraries/AppFlow/tests/CMakeLists.txt VIXEN/libraries/CMakeLists.txt
git commit -m "build(appflow): CMake library + tests wired into the tree; Inc1 suite green"
```

---

### Task 8: Inc-1 close-out — verify + document

**Files:**
- Modify: `VIXEN/Vixen-Docs/01-Architecture/AppFlow-Framework-Inc1-Plan-2026-07.md` (mark Inc-1 DONE + record the codegen-tool decision from Task 1)
- Modify: `VIXEN/Vixen-Docs/04-Development/Known-Issues.md` (if any Inc-1 gap surfaced — else skip)

- [ ] **Step 1: Full-suite verification from fresh output.** Re-run `ctest --test-dir build -R "AppFlow|ActionStack|FlowStateMachine|BindingStore"` and paste the pass count into the plan's status line. (Per the *live-verification-authoritative* rule; Inc-1 is offline so ctest is the authority — no GPU gate until Inc 2.)

- [ ] **Step 2: Record the codegen-tool decision** (VIXEN `codegen/` vs. Yeroket generator) as a one-line note at the top of this plan and in the design doc §3, so Inc 2 doesn't re-litigate it.

- [ ] **Step 3: Commit the close-out.**

```bash
git add VIXEN/Vixen-Docs/01-Architecture/AppFlow-Framework-Inc1-Plan-2026-07.md
git commit -m "docs(appflow): Inc1 walking skeleton COMPLETE — suite green, codegen-tool decision recorded"
```

---

## Self-Review

**Spec coverage (design §3–§8 vs. Inc-1 scope):**
- Three-tier contract, incl. typed param signature (§7c core) → Tasks 1–2 (declare + generate + golden `ParamSignatureEmitted`). ✓
- UI-action generalization (§7c): engine-owned action registry + `BindingStore` (from undertow `UiActionRegistry`/`UiBindingTable`) + resolution algorithm → Task 5b, with the end-to-end `DispatchBySelector` spine in Task 6. ✓
- ActionStack (inverse path + grouping) → Task 5; snapshot-fallback path is explicitly Inc 2 (footprint struct is emitted in Inc 1, unused). ✓ (deferral is intentional + documented)
- FlowStateMachine → Task 4. ✓
- LayerController / ModuleController / PanelLayout → Inc 2–4 per the roadmap (not Inc 1). ✓ (intentional)
- undertow migration (§8 deferred) → Inc 5+ roadmap; Inc 1 shapes the contract for it (param sig + BindingStore) but does not touch undertow. ✓ (intentional)
- EventBus broadcast → Task 3 + Task 6. ✓
- Error model (typed results, no throw) → Task 3 enums + Task 6 loader; BindingStore warn-skip-inert → Task 5b. ✓
- Testing gates (golden, offline units, loader, binding resolution) → Tasks 2/4/5/5b/6/7; live gate is Inc 2. ✓
- UI-action consolidation (§7b) → Inc 1 proves the selector→binding→action→undo spine end-to-end (Task 6 `DispatchBySelectorRunsBoundActionUndoably`); editor rewire is Inc 2. ✓

**Placeholder scan:** No TBD/TODO. Two flagged confirmations (codegen tool in Task 1; MessageBus drain method in Task 6 — defaulted to `PublishImmediate`) are explicit decisions with a stated default, not placeholders.

**Type consistency:** `FlowStateId`/`FlowActionId`/`FlowGuardId`/`FlowParamType`, `FlowParamSchema`, `AppFlowActionDecl`/`AppFlowTransition`/`AppFlowContainerView`, `LoadResult`/`DispatchResult`, `ActionStack::ApplyFn`, `BindingStore::BindingSpec`/`BoundAction`, `AppFlowChangedEvent::Kind` are used identically across Tasks 2–7. `AppFlowContainerView::actions()/transitions()` returns `std::span` with `.data()/.size()` — used consistently in Tasks 4/5/5b/6 (span accessor added to the generated header in Task 2). `AppFlowLoader::Load` takes `(view, fsm, stack, bindings)` consistently in Tasks 5b/6. ✓

---

## Execution Handoff

Inc-1 plan complete. Given the program's size and the cross-repo codegen dependency, this is a good fit for the **post-brainstorm-context-manager** pipeline (milestone-chunked, fresh worker per task, Opus validation) that ran the SDF and config-codegen programs — but the standard subagent-driven or inline options apply too.
