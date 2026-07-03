# Fail-Scenario Simulation — Inc 1 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Per-node, macro-declared fail scenarios (compiled out of real builds) + a headless runner that boots the REAL default app graph, injects every declared scenario, and asserts graceful handling — first scenarios cover the fullscreen/resize crash class and migrate the device-loss harness.

**Architecture:** A `VIXEN_FAIL_SCENARIOS` compile flag gates: typed scenario declarations self-registered from node .cpp TUs (mirroring `VIXEN_REGISTER_NODE`; the existing whole-archive of `RenderGraphNodes` keeps registrars alive), a graph-owned `FaultInjector` that filters `VkResult`s at three named sites, a window-stimulus seam, and a gtest runner that links the existing `VixenApp` static library to drive `VulkanGraphApplication` (Initialize → Prepare → Update/Render loop) with a hidden window.

**Tech Stack:** C++23, Vulkan 1.3, GLFW, GoogleTest, CMake (MSVC on Windows + GCC on WSL; live gates run on WSL/lavapipe).

**Spec:** `Vixen-Docs/01-Architecture/Fail-Scenario-Simulation-Design-2026-07.md` (approved 2026-07-02).

## Global Constraints

- Zero footprint when `VIXEN_FAIL_SCENARIOS` is OFF: all new code inside `#if VIXEN_FAIL_SCENARIOS` (or macro no-op branches). Verified empirically in Task 8.
- Nodes use the base `NodeInstance::device` member via `SetDevice`/`GetDevice` — never a private `device_`.
- Node logging via `NODE_LOG_INFO/ERROR(...)`; graph logging via `GRAPH_LOG_*`.
- New registrars mirror `VIXEN_REGISTER_NODE` (`Core/NodeRegistration.h`): file-scope bool + `__COUNTER__`, thunk list replayed on demand. Registrars live at the BOTTOM of the node's own .cpp.
- Build (WSL): `cmake -B build-wsl -DVIXEN_FAIL_SCENARIOS=ON <existing args>` then `cmake --build build-wsl --target <t> -- -k 0`. Tests: `ctest --test-dir build-wsl -R <regex> --output-on-failure`.
- `rtk`-wrapped git masks exit codes — after any `git checkout`/`merge`, verify `git rev-parse HEAD` explicitly before depending on it.
- Live-run gates are authoritative: a task claiming render behavior must show a live run's output, not just compilation.
- Work on branch `feat/fail-scenario-sim` (create in Task 1). Commit per task; do NOT push.

## Milestone Map (post-brainstorm-context-manager · locked 2026-07-02)

- M1 — Foundations: Tasks 1-3 (registry+macros, FaultInjector+sites, window seam) — Sonnet implementer
- M2 — Live harness: Tasks 4-5 (AppHarness, sweep runner) — Sonnet implementer
- M3 — Scenarios + proof: Tasks 6-8 (declarations, DeviceLostRecovery, compile-out + docs) — Sonnet implementer

Worktree: `.claude/worktrees/fail-scenario-sim` · branch `feat/fail-scenario-sim` (docs commit `eeda8aae`).

## Progress Log

(appended per milestone by the controller)

- Milestone 1 (Tasks 1-3): DONE · commits a436f227..8b03f8ee · Opus validator OK (tests re-run green; nm zero-footprint proven) · 2026-07-02 · NOTES for later milestones: real buildable targets are RenderGraphCore/RenderGraphNodes/VIXEN (plan shorthand "RenderGraph"/"VixenApp"; VixenApp static lib still valid as a LINK target); ctest discovery is broken repo-wide pre-existing (root enable_testing() runs after add_subdirectory(libraries)) — run gtest binaries directly from build-wsl.
- Milestone 2 (Tasks 4-5): DONE · commits caa59f43..b65bf35c · Opus validator OK (live gate re-run: BootWarmupTeardown PASSED on WSLg + Dozen ICD real GPU; OFF-build clean, zero scenario symbols) · 2026-07-02 · NOTES: sweep target relocated to application/main/CMakeLists.txt:152 include of FailScenarios/test_fail_scenario_sweep.cmake (libraries/ configures before application/, so `if(TARGET VixenApp)` was vacuously false at the plan's placement — registry target unchanged); InstanceNode gained a flag-gated VK_EXT_debug_report callback (none existed) feeding FailScenario::ValidationErrorCount(); VACUITY: VK_LAYER_KHRONOS_validation is NOT installed on this box → the 0-validation-errors criterion is vacuous locally (layer-if-installed pattern holds; becomes real wherever the layer exists).
- Milestone 3 (Tasks 6-8) + merge + re-gate: DONE · commits 5686b5ba/58f50c0d/c54b2168 + merge 1df4ac4d (main 03a4880e in) + re-gate 8db687f1 · Opus validator OK (all gates re-run) · 2026-07-03 · OUTCOMES: 7 scenarios live — AcquireOutOfDate/AcquireSuboptimal HARD-PASS (were KI-003 crashes; fixed by main 9d95bd75 AbortCurrentFrame — the fullscreen-segfault fix — now permanently regression-gated); PresentOutOfDate + MaximizeLikeFullscreenButton PASS; ResizeLargeExtentJump + MinimizeThenRestore honest SKIP (WM refuses ops on hidden WSLg window — coverage gap, revisit with visible-window local mode/Xvfb); DeviceLostRecovery = OPEN KI-004 (recovery completes then downstream node records on dead command buffer → SIGABRT; RenderFrame's loop checks frameAborted_ but never deviceLost_ between nodes; FIX: wire AbortCurrentFrame() from FrameSyncNode's device-loss branch). Compile-out on fresh post-merge archives: OFF=0 / ON=86 PASS. GATE SEMANTICS: whole-binary sweep run cannot go green while a KI-gated scenario CRASHES (process dies before the skip-report) — per-case invocation is the green path; fine once ctest per-case discovery works (root enable_testing() ordering bug, still open repo-wide).

## File Structure (locked)

```
libraries/RenderGraph/include/Core/FailScenario.h        NEW  — flag, descriptors, macros (both branches)
libraries/RenderGraph/src/Core/FailScenario.cpp          NEW  — registry + injector impl (whole file in #if)
libraries/RenderGraph/include/Core/RenderGraph.h         MOD  — flag-gated FaultInjector member + accessor
libraries/RenderGraph/src/Core/RenderGraph.cpp           MOD  — env hook migration (~line 699-715)
libraries/RenderGraph/src/Nodes/SwapChainNode.cpp        MOD  — Acquire fault filter (~line 298) + declarations
libraries/RenderGraph/include/Nodes/SwapChainNode.h      MOD  — GetWidth/GetHeight const getters
libraries/RenderGraph/src/Nodes/PresentNode.cpp          MOD  — Present fault filter (~line 129) + declarations
libraries/RenderGraph/src/Nodes/FrameSyncNode.cpp        MOD  — FenceWait fault filter (~line 147) + declaration
libraries/RenderGraph/include/Nodes/WindowNode.h         MOD  — WindowEvent → public; flag-gated inject API
libraries/RenderGraph/src/Nodes/WindowNode.cpp           MOD  — inject impl; hidden-window hint; declarations
libraries/RenderGraph/src/Nodes/InstanceNode.cpp         MOD  — flag-gated validation-error counter in debug callback
libraries/RenderGraph/tests/FailScenarios/ScenarioHarness.h   NEW — AppHarness + ScenarioContext
libraries/RenderGraph/tests/FailScenarios/ScenarioHarness.cpp NEW
libraries/RenderGraph/tests/FailScenarios/test_fail_scenario_registry.cpp NEW — CPU-only unit tests
libraries/RenderGraph/tests/FailScenarios/test_fail_scenario_sweep.cpp    NEW — live sweep (custom main)
libraries/RenderGraph/tests/test_fail_scenarios.cmake    NEW  — targets (included from tests/CMakeLists.txt)
libraries/RenderGraph/tests/CMakeLists.txt               MOD  — include(test_fail_scenarios.cmake)
CMakeLists.txt (repo root)                               MOD  — option(VIXEN_FAIL_SCENARIOS)
scripts/check_fail_scenario_compile_out.sh               NEW  — empirical zero-footprint proof
```

**Interface contract used by every task** (defined in Task 1, consumed everywhere — exact names):

```cpp
namespace Vixen::RenderGraph::FailScenario {
  enum class FaultSite : uint8_t { Acquire, Present, FenceWait };
  struct VkTransient   { FaultSite site; VkResult result; };
  struct WindowStimulus{ enum class Kind : uint8_t { ResizeTo, Maximize, Minimize, Restore }; 
                         Kind kind; uint32_t width = 0, height = 0; };
  // ABSTRACT interface, defined here in the library header so contract lambdas in node .cpps can
  // call it WITHOUT any test/gtest/VixenApp dependency (layering rule: engine TUs never include
  // test headers). Node types are forward-declared; contracts report via Fail()/Skip(), never
  // gtest macros. The test harness implements it (ScenarioContextImpl).
  class ScenarioContext {
  public:
    virtual ~ScenarioContext() = default;
    virtual bool RunFrames(uint32_t n) = 0;
    virtual void ArmFault(FaultSite, VkResult) = 0;
    virtual bool ApplyStimulus(const WindowStimulus&) = 0;   // false = WM refused (caller should Skip)
    virtual uint32_t ValidationErrors() const = 0;
    virtual SwapChainNode* SwapChain() = 0;                  // fwd-declared; node .cpp includes the real header
    virtual GLFWwindow* Window() = 0;
    virtual RenderGraph* Graph() = 0;
    virtual void Fail(const std::string& msg) = 0;           // non-fatal failure record (impl: ADD_FAILURE)
    [[noreturn]] virtual void Skip(const std::string& msg) = 0; // aborts scenario as skipped (impl: throws)
  };
  using Contract = std::function<void(ScenarioContext&)>;
  struct ScenarioDecl { std::string id; std::variant<WindowStimulus, VkTransient> stimulus;
                        Contract contract; const char* knownIssueId = nullptr; };
  class ScenarioRegistry {  // Meyers singleton, thunk-replay like NodeRegistrars
  public:
    static ScenarioRegistry& Instance();
    void Register(std::string nodeTypeName, std::vector<ScenarioDecl> decls);   // called by thunks
    const std::vector<ScenarioDecl>* Find(const std::string& nodeTypeName) const;
    void ForEach(const std::function<void(const std::string&, const ScenarioDecl&)>& fn) const;
  };
  void ReplayScenarioRegistrars();  // runs all thunks once (idempotent); call before Find/ForEach
  class FaultInjector {
  public:
    void ArmOnce(FaultSite site, VkResult forced);        // next Filter(site, ..) returns forced
    VkResult Filter(FaultSite site, VkResult real);       // pops armed result or passes real through
    bool IsArmed(FaultSite site) const;
  };
}
// Macros (ON branch shown; OFF branch expands to nothing):
//   VIXEN_FAIL_SCENARIOS_DECLARE(NodeTypeClass, ...)   — file-scope registrar in the node .cpp
//   VIXEN_SCENARIO(id, ...)                            — one ScenarioDecl (variadic; braces survive)
//   VIXEN_FAULT_FILTER(graphPtr, site, resultExpr)     — ON: injector filter; OFF: (resultExpr)
```

`RenderGraph` additions (flag-gated): `FailScenario::FaultInjector* GetFaultInjector();` (lazily-constructed member).
`WindowNode` additions: `struct WindowEvent` moves to `public:`; flag-gated `void InjectWindowEvent(WindowEvent::Type type, uint32_t w = 0, uint32_t h = 0);` and `size_t PendingEventCountForTest() const;`.
`SwapChainNode` additions (unconditional, trivial): `uint32_t GetWidth() const; uint32_t GetHeight() const;`.
Test-side (`ScenarioHarness.h`): `class AppHarness` — `bool Boot(); bool RunFrames(uint32_t n); VulkanGraphApplication& App(); Vixen::RenderGraph::RenderGraph& Graph(); GLFWwindow* Window(); SwapChainNode* SwapChain(); WindowNode* WindowNodePtr(); NodeInstance* FindByTypeName(const std::string&); uint32_t ValidationErrors() const; ~AppHarness()` — and `class ScenarioContextImpl : public FailScenario::ScenarioContext` wrapping it (`Fail` → `ADD_FAILURE() << msg`; `Skip` → `throw SkipScenario{msg}`, caught by the runner → `GTEST_SKIP`; `ApplyStimulus` returns false if the WM refused the operation → runner/contract Skips honestly).

---

### Task 1: Flag, declaration schema, self-registering ScenarioRegistry (CPU-only)

**Files:**
- Create: `VIXEN/libraries/RenderGraph/include/Core/FailScenario.h`
- Create: `VIXEN/libraries/RenderGraph/src/Core/FailScenario.cpp`
- Create: `VIXEN/libraries/RenderGraph/tests/FailScenarios/test_fail_scenario_registry.cpp`
- Create: `VIXEN/libraries/RenderGraph/tests/test_fail_scenarios.cmake`
- Modify: `VIXEN/CMakeLists.txt` (root — add option near the other `option(...)` lines)
- Modify: `VIXEN/libraries/RenderGraph/tests/CMakeLists.txt` (add `include(test_fail_scenarios.cmake)` next to the existing includes)
- Modify: `VIXEN/libraries/RenderGraph/CMakeLists.txt` (add `src/Core/FailScenario.cpp` to the RenderGraph core source list)

**Interfaces:**
- Consumes: `Core/NodeRegistration.h` idiom (read it first — mirror the thunk pattern exactly).
- Produces: everything in the "Interface contract" block above except `FaultInjector` (Task 2) and `ScenarioContext` (Task 4).

- [x] **Step 0:** `git checkout -b feat/fail-scenario-sim` (verify: `git rev-parse --abbrev-ref HEAD` prints the branch — rtk masks exit codes).

- [x] **Step 1: Write the failing test** (`test_fail_scenario_registry.cpp`):

```cpp
#include "Core/FailScenario.h"
#include "Core/NodeType.h"
#include <gtest/gtest.h>

using namespace Vixen::RenderGraph;
using namespace Vixen::RenderGraph::FailScenario;

// A minimal NodeType so the macro's NodeTypeClass().GetTypeName() works without Vulkan.
namespace { struct FakeNodeType : NodeType { FakeNodeType() : NodeType("FakeTestNode") {} 
    std::unique_ptr<NodeInstance> CreateInstance(const std::string&) const override { return nullptr; } }; }

VIXEN_FAIL_SCENARIOS_DECLARE(FakeNodeType,
    VIXEN_SCENARIO(FakeAcquireFault,
        VkTransient{ .site = FaultSite::Acquire, .result = VK_ERROR_OUT_OF_DATE_KHR },
        [](ScenarioContext&) {}),
    VIXEN_SCENARIO(FakeResize,
        WindowStimulus{ .kind = WindowStimulus::Kind::ResizeTo, .width = 1920, .height = 1080 },
        [](ScenarioContext&) {})
);

TEST(FailScenarioRegistry, MacroRegistersTypedScenariosUnderNodeTypeName) {
    ReplayScenarioRegistrars();
    const auto* decls = ScenarioRegistry::Instance().Find("FakeTestNode");
    ASSERT_NE(decls, nullptr);
    ASSERT_EQ(decls->size(), 2u);
    EXPECT_EQ((*decls)[0].id, "FakeAcquireFault");
    const auto& vt = std::get<VkTransient>((*decls)[0].stimulus);
    EXPECT_EQ(vt.site, FaultSite::Acquire);
    EXPECT_EQ(vt.result, VK_ERROR_OUT_OF_DATE_KHR);
    const auto& ws = std::get<WindowStimulus>((*decls)[1].stimulus);
    EXPECT_EQ(ws.width, 1920u);
    EXPECT_EQ((*decls)[1].knownIssueId, nullptr);
}

TEST(FailScenarioRegistry, ReplayIsIdempotent) {
    ReplayScenarioRegistrars();
    ReplayScenarioRegistrars();
    EXPECT_EQ(ScenarioRegistry::Instance().Find("FakeTestNode")->size(), 2u);
}

TEST(FailScenarioRegistry, FindUnknownTypeReturnsNull) {
    ReplayScenarioRegistrars();
    EXPECT_EQ(ScenarioRegistry::Instance().Find("NoSuchNode"), nullptr);
}
```

- [x] **Step 2: CMake wiring.** Root `CMakeLists.txt`: `option(VIXEN_FAIL_SCENARIOS "Compile fail-scenario declarations, injection seams, and harness" OFF)` and, when ON, `add_compile_definitions(VIXEN_FAIL_SCENARIOS=1)` (global — nodes, app, and tests all need one consistent view). New `test_fail_scenarios.cmake` (mirror `test_graph_systems.cmake` structure):

```cmake
if(NOT VIXEN_FAIL_SCENARIOS)
    message(STATUS "⊗ fail-scenario tests skipped (VIXEN_FAIL_SCENARIOS=OFF)")
    return()
endif()
if(TARGET GTest::gtest_main)
    add_executable(test_fail_scenario_registry FailScenarios/test_fail_scenario_registry.cpp)
    target_include_directories(test_fail_scenario_registry PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../include)
    target_link_libraries(test_fail_scenario_registry PRIVATE GTest::gtest_main RenderGraph)
    set_target_properties(test_fail_scenario_registry PROPERTIES FOLDER "Tests/RenderGraph Tests")
    gtest_discover_tests(test_fail_scenario_registry)
    message(STATUS "✓ test_fail_scenario_registry configured")
endif()
```

- [x] **Step 3: Run the test to verify it fails** (header doesn't exist yet):
`cmake -B build-wsl -DVIXEN_FAIL_SCENARIOS=ON` (keep all pre-existing cache args) then `cmake --build build-wsl --target test_fail_scenario_registry`. Expected: FAIL — `Core/FailScenario.h: No such file`.

- [x] **Step 4: Implement** `FailScenario.h`:

```cpp
#pragma once
// Fail-scenario declarations (design: Fail-Scenario-Simulation-Design-2026-07.md).
// EVERYTHING here is compiled out when VIXEN_FAIL_SCENARIOS is off — the OFF branch
// defines only empty macros so node .cpps can declare scenarios unconditionally.
#if defined(VIXEN_FAIL_SCENARIOS) && VIXEN_FAIL_SCENARIOS

#include <vulkan/vulkan.h>
#include <cstdint>
#include <functional>
#include <string>
#include <variant>
#include <vector>

struct GLFWwindow;  // handle-only, same pattern as WindowNode.h

namespace Vixen::RenderGraph {
class RenderGraph;
class SwapChainNode;  // contracts return node pointers; the declaring .cpp includes the real header

namespace FailScenario {

enum class FaultSite : uint8_t { Acquire, Present, FenceWait };

struct VkTransient    { FaultSite site; VkResult result; };
struct WindowStimulus { enum class Kind : uint8_t { ResizeTo, Maximize, Minimize, Restore };
                        Kind kind; uint32_t width = 0, height = 0; };

// Abstract on purpose: contract lambdas compile inside ENGINE node TUs, so this interface must
// carry zero test/gtest/VixenApp dependency. The sweep harness implements it (ScenarioContextImpl).
// Contracts report via Fail()/Skip() — never gtest macros in library code.
class ScenarioContext {
public:
    virtual ~ScenarioContext() = default;
    virtual bool RunFrames(uint32_t n) = 0;
    virtual void ArmFault(FaultSite site, VkResult result) = 0;
    virtual bool ApplyStimulus(const WindowStimulus& ws) = 0;  // false = WM refused the op
    virtual uint32_t ValidationErrors() const = 0;
    virtual SwapChainNode* SwapChain() = 0;
    virtual GLFWwindow* Window() = 0;
    virtual RenderGraph* Graph() = 0;
    virtual void Fail(const std::string& msg) = 0;
    [[noreturn]] virtual void Skip(const std::string& msg) = 0;
};
using Contract = std::function<void(ScenarioContext&)>;

struct ScenarioDecl {
    std::string id;
    std::variant<WindowStimulus, VkTransient> stimulus;
    Contract contract;
    const char* knownIssueId = nullptr;  // set → runner reports the failure but does not gate on it
};

// Variadic so designated-init braces (which contain commas) pass through into a real
// C++ function-call parse — same trick as forwarding __VA_ARGS__ into a call.
template <typename Stim>
inline ScenarioDecl MakeDecl(const char* id, Stim stim, Contract c, const char* knownIssue = nullptr) {
    return ScenarioDecl{ id, std::move(stim), std::move(c), knownIssue };
}

class ScenarioRegistry {
public:
    static ScenarioRegistry& Instance();
    void Register(std::string nodeTypeName, std::vector<ScenarioDecl> decls);
    const std::vector<ScenarioDecl>* Find(const std::string& nodeTypeName) const;
    void ForEach(const std::function<void(const std::string&, const ScenarioDecl&)>& fn) const;
private:
    std::vector<std::pair<std::string, std::vector<ScenarioDecl>>> entries_;
};

namespace detail {
    // Mirrors NodeRegistration.h: Meyers-singleton thunk list, appended at dynamic-init,
    // replayed on demand. Node TUs are whole-archived (RenderGraphNodes), so registrars
    // placed in them are never linker-stripped — same guarantee VIXEN_REGISTER_NODE relies on.
    std::vector<std::function<void()>>& ScenarioRegistrars();
    bool AddScenarioRegistrar(std::function<void()> thunk);
}
void ReplayScenarioRegistrars();  // idempotent

class FaultInjector {  // Task 2 wires it into the graph; declared here so RenderGraph.h needs one include
public:
    void ArmOnce(FaultSite site, VkResult forced);
    VkResult Filter(FaultSite site, VkResult real);
    bool IsArmed(FaultSite site) const;
private:
    struct Armed { bool armed = false; VkResult forced = VK_SUCCESS; };
    Armed slots_[3];
    static constexpr size_t Idx(FaultSite s) { return static_cast<size_t>(s); }
};

} // namespace FailScenario
} // namespace Vixen::RenderGraph

#define VIXEN_FAIL_SCENARIO_CONCAT2(a, b) a##b
#define VIXEN_FAIL_SCENARIO_CONCAT(a, b) VIXEN_FAIL_SCENARIO_CONCAT2(a, b)

#define VIXEN_SCENARIO(id, ...) \
    ::Vixen::RenderGraph::FailScenario::MakeDecl(#id, __VA_ARGS__)

#define VIXEN_FAIL_SCENARIOS_DECLARE(NodeTypeClass, ...)                                   \
    namespace {                                                                            \
        const bool VIXEN_FAIL_SCENARIO_CONCAT(s_vixen_fail_scenarios_, __COUNTER__) =      \
            ::Vixen::RenderGraph::FailScenario::detail::AddScenarioRegistrar([]() {        \
                ::Vixen::RenderGraph::FailScenario::ScenarioRegistry::Instance().Register( \
                    NodeTypeClass().GetTypeName(), { __VA_ARGS__ });                       \
            });                                                                            \
    }

// resultExpr is variadic so real calls with commas in their argument lists pass through intact.
#define VIXEN_FAULT_FILTER(graphPtr, site, ...)                                            \
    ((graphPtr) != nullptr                                                                 \
        ? (graphPtr)->GetFaultInjector()->Filter(                                          \
              ::Vixen::RenderGraph::FailScenario::FaultSite::site, (__VA_ARGS__))          \
        : (__VA_ARGS__))

#else  // ─────────── VIXEN_FAIL_SCENARIOS off: zero footprint ───────────

#define VIXEN_SCENARIO(id, ...)
#define VIXEN_FAIL_SCENARIOS_DECLARE(NodeTypeClass, ...)
#define VIXEN_FAULT_FILTER(graphPtr, site, ...) (__VA_ARGS__)

#endif
```

`FailScenario.cpp` (whole file wrapped in `#if defined(VIXEN_FAIL_SCENARIOS) && VIXEN_FAIL_SCENARIOS` / `#endif`):

```cpp
#include "Core/FailScenario.h"
#if defined(VIXEN_FAIL_SCENARIOS) && VIXEN_FAIL_SCENARIOS
namespace Vixen::RenderGraph::FailScenario {

ScenarioRegistry& ScenarioRegistry::Instance() { static ScenarioRegistry r; return r; }

void ScenarioRegistry::Register(std::string name, std::vector<ScenarioDecl> decls) {
    for (auto& e : entries_)
        if (e.first == name) { e.second = std::move(decls); return; }  // idempotent replay
    entries_.emplace_back(std::move(name), std::move(decls));
}
const std::vector<ScenarioDecl>* ScenarioRegistry::Find(const std::string& name) const {
    for (const auto& e : entries_) if (e.first == name) return &e.second;
    return nullptr;
}
void ScenarioRegistry::ForEach(const std::function<void(const std::string&, const ScenarioDecl&)>& fn) const {
    for (const auto& e : entries_) for (const auto& d : e.second) fn(e.first, d);
}

namespace detail {
    std::vector<std::function<void()>>& ScenarioRegistrars() {
        static std::vector<std::function<void()>> v; return v;
    }
    bool AddScenarioRegistrar(std::function<void()> thunk) {
        ScenarioRegistrars().push_back(std::move(thunk)); return true;
    }
}
void ReplayScenarioRegistrars() {
    for (auto& t : detail::ScenarioRegistrars()) t();  // Register() is replace-idempotent
}

void FaultInjector::ArmOnce(FaultSite s, VkResult f) { slots_[Idx(s)] = { true, f }; }
VkResult FaultInjector::Filter(FaultSite s, VkResult real) {
    auto& a = slots_[Idx(s)];
    if (!a.armed) return real;
    a.armed = false;
    return a.forced;
}
bool FaultInjector::IsArmed(FaultSite s) const { return slots_[Idx(s)].armed; }

} // namespace
#endif
```

- [x] **Step 5: Run test to verify it passes:** `cmake --build build-wsl --target test_fail_scenario_registry && ctest --test-dir build-wsl -R FailScenarioRegistry --output-on-failure`. Expected: 3/3 PASS.

- [x] **Step 6: OFF-branch compile check:** configure a scratch dir `cmake -B build-wsl-off -DVIXEN_FAIL_SCENARIOS=OFF <same args>` and build only `RenderGraph`. Expected: clean build (macros expand to nothing; no new symbols). Delete `build-wsl-off` after Task 8 reuses it.

- [x] **Step 7: Commit** `feat(fail-scenarios): declaration schema + self-registering ScenarioRegistry behind VIXEN_FAIL_SCENARIOS (Task 1)`.

---

### Task 2: FaultInjector wiring — graph member + three fault points + env-hook migration

**Files:**
- Modify: `VIXEN/libraries/RenderGraph/include/Core/RenderGraph.h` (member + accessor)
- Modify: `VIXEN/libraries/RenderGraph/src/Core/RenderGraph.cpp:698-715` (env hook)
- Modify: `VIXEN/libraries/RenderGraph/src/Nodes/SwapChainNode.cpp:290-336` (`AcquireNextImage`)
- Modify: `VIXEN/libraries/RenderGraph/src/Nodes/PresentNode.cpp:126-134` (`Present`)
- Modify: `VIXEN/libraries/RenderGraph/src/Nodes/FrameSyncNode.cpp:143-159` (`ExecuteImpl` fence wait)
- Test: extend `test_fail_scenario_registry.cpp` (injector unit tests, CPU-only)

**Interfaces:**
- Consumes: `FaultInjector`, `VIXEN_FAULT_FILTER` from Task 1.
- Produces: `RenderGraph::GetFaultInjector()` (flag-gated); the three live sites filter through it. `VIXEN_SIMULATE_DEVICE_LOSS` env now arms `FenceWait` instead of latching directly.

- [x] **Step 1: Failing unit tests** (append to `test_fail_scenario_registry.cpp`):

```cpp
TEST(FaultInjector, ArmOnceFiresExactlyOncePerSite) {
    FaultInjector fi;
    EXPECT_EQ(fi.Filter(FaultSite::Acquire, VK_SUCCESS), VK_SUCCESS);       // unarmed: passthrough
    fi.ArmOnce(FaultSite::Acquire, VK_ERROR_OUT_OF_DATE_KHR);
    EXPECT_TRUE(fi.IsArmed(FaultSite::Acquire));
    EXPECT_FALSE(fi.IsArmed(FaultSite::Present));                           // per-site isolation
    EXPECT_EQ(fi.Filter(FaultSite::Present, VK_SUCCESS), VK_SUCCESS);       // other site unaffected
    EXPECT_EQ(fi.Filter(FaultSite::Acquire, VK_SUCCESS), VK_ERROR_OUT_OF_DATE_KHR);
    EXPECT_EQ(fi.Filter(FaultSite::Acquire, VK_SUCCESS), VK_SUCCESS);       // once only
}
```

Run: expected PASS immediately (impl landed in Task 1) — these lock the semantics before the live sites depend on them. If any fail, fix `FaultInjector` first.

- [x] **Step 2: Graph member.** `RenderGraph.h` — inside `class RenderGraph`, next to the device-loss members:

```cpp
#if defined(VIXEN_FAIL_SCENARIOS) && VIXEN_FAIL_SCENARIOS
public:
    // Fail-scenario fault injection (test builds only): dormant unless a scenario arms it.
    FailScenario::FaultInjector* GetFaultInjector() {
        if (!faultInjector_) faultInjector_ = std::make_unique<FailScenario::FaultInjector>();
        return faultInjector_.get();
    }
private:
    std::unique_ptr<FailScenario::FaultInjector> faultInjector_;
#endif
```

Add `#include "Core/FailScenario.h"` to `RenderGraph.h` (the header is self-neutralizing when off).

- [x] **Step 3: The three sites.** Each is a one-line filter on the REAL result (the real call still executes — same healthy-device fidelity argument as `VIXEN_SIMULATE_DEVICE_LOSS`, documented in the spec §3.3):

`SwapChainNode.cpp` `AcquireNextImage` — wrap the existing call (line ~298-305) so the OFF branch reduces to exactly the original expression (no self-assignment):
```cpp
    VkResult result = VIXEN_FAULT_FILTER(GetOwningGraph(), Acquire,
        swapChainWrapper->fpAcquireNextImageKHR(
            devicePtr->device,
            swapChainWrapper->scPublicVars.swapChain,
            UINT64_MAX, // Timeout
            presentCompleteSemaphore,
            VK_NULL_HANDLE, // Fence
            &currentImageIndex
        ));
```

`PresentNode.cpp` `Present` — line ~129:
```cpp
    lastResult = VIXEN_FAULT_FILTER(GetOwningGraph(), Present, fpQueuePresent(device->queue, &presentInfo));
```

`FrameSyncNode.cpp` `ExecuteImpl` — line ~147:
```cpp
    VkResult waitResult = VIXEN_FAULT_FILTER(GetOwningGraph(), FenceWait,
                              vkWaitForFences(device->device, 1, &currentFence, VK_TRUE, UINT64_MAX));
```

Add `#include "Core/FailScenario.h"` to each .cpp.

- [x] **Step 4: Migrate the env hook.** In `RenderGraph.cpp` `RenderFrame()` (lines 698-715): keep the env parse, but replace the direct `NotifyDeviceLost(...)` latch with arming the injector — the loss now surfaces through the REAL detection path (FrameSyncNode's fence wait), which is strictly more faithful. Wrap the whole hook in the flag (spec decision: dormant runtime instrumentation moves behind the compile flag):

```cpp
#if defined(VIXEN_FAIL_SCENARIOS) && VIXEN_FAIL_SCENARIOS
    // Fail-scenario migration of the AR#1 Phase-3 harness: VIXEN_SIMULATE_DEVICE_LOSS=<frame> arms a
    // one-shot FenceWait fault, so the synthetic loss is DETECTED by FrameSyncNode's real fence-wait
    // path (NotifyDeviceLost fires from the node, not from here). Compiled out of real builds.
    if (simulateDeviceLossFrame_ == -2) {
        const char* env = std::getenv("VIXEN_SIMULATE_DEVICE_LOSS");
        simulateDeviceLossFrame_ = -1;
        if (env) { int parsed = std::atoi(env); simulateDeviceLossFrame_ = (parsed > 0) ? parsed : 120; }
    }
    if (!deviceLossSimulated_ && simulateDeviceLossFrame_ >= 0 &&
        globalFrameIndex >= static_cast<uint64_t>(simulateDeviceLossFrame_)) {
        deviceLossSimulated_ = true;
        GetFaultInjector()->ArmOnce(FailScenario::FaultSite::FenceWait, VK_ERROR_DEVICE_LOST);
    }
#endif
```

Also wrap the `simulateDeviceLossFrame_`/`deviceLossSimulated_` member declarations in `RenderGraph.h` in the same `#if` (they are now scenario-build-only state).

- [x] **Step 5: Full library rebuild both ways.** `cmake --build build-wsl --target RenderGraph VixenApp -- -k 0` (flag ON) and the same in `build-wsl-off` (flag OFF). Expected: both clean. The OFF build proves the sites reduce to the bare expressions.

- [x] **Step 6: Run existing render tests for no-regression** (the touched nodes are on every render path): `ctest --test-dir build-wsl -R "render|swapchain|frame_sync" --output-on-failure`. Expected: same pass set as before the change (compare against a pre-change run if unsure — no new failures).

- [x] **Step 7: Commit** `feat(fail-scenarios): graph-owned FaultInjector + Acquire/Present/FenceWait fault points; VIXEN_SIMULATE_DEVICE_LOSS arms FenceWait (Task 2)`.

---

### Task 3: Window-stimulus seam on WindowNode

**Files:**
- Modify: `VIXEN/libraries/RenderGraph/include/Nodes/WindowNode.h`
- Modify: `VIXEN/libraries/RenderGraph/src/Nodes/WindowNode.cpp`
- Test: extend `test_fail_scenario_registry.cpp`

**Interfaces:**
- Consumes: nothing new.
- Produces: `WindowNode::WindowEvent` public; `InjectWindowEvent(WindowEvent::Type, uint32_t w = 0, uint32_t h = 0)` and `PendingEventCountForTest()` (both flag-gated); hidden-window support via `VIXEN_HIDDEN_WINDOW=1` env (flag-gated).

- [x] **Step 1: Failing test:**

```cpp
#include "Nodes/WindowNode.h"
TEST(WindowSeam, InjectQueuesEventsThreadSafely) {
    Vixen::RenderGraph::WindowNodeType type;
    auto node = type.CreateInstance("test_window");
    auto* wn = static_cast<Vixen::RenderGraph::WindowNode*>(node.get());
    using WE = Vixen::RenderGraph::WindowNode::WindowEvent;
    wn->InjectWindowEvent(WE::Type::Resize, 1920, 1080);
    wn->InjectWindowEvent(WE::Type::Maximize);
    EXPECT_EQ(wn->PendingEventCountForTest(), 2u);
}
```

Run: FAIL — `WindowEvent` is private / methods missing.

- [x] **Step 2: Implement.** In `WindowNode.h`: move the `struct WindowEvent {...}` block (currently private, lines ~71-76) into the `public:` section (type visibility only — no state exposed, no ABI change). Below the state queries add:

```cpp
#if defined(VIXEN_FAIL_SCENARIOS) && VIXEN_FAIL_SCENARIOS
    // Fail-scenario seam: enqueue a synthetic event exactly where the GLFW callbacks do; the next
    // ExecuteImpl drains it through the production path (width/height update → MessageBus publish).
    void InjectWindowEvent(WindowEvent::Type type, uint32_t w = 0, uint32_t h = 0);
    size_t PendingEventCountForTest() const;
#endif
```

In `WindowNode.cpp`:

```cpp
#if defined(VIXEN_FAIL_SCENARIOS) && VIXEN_FAIL_SCENARIOS
void WindowNode::InjectWindowEvent(WindowEvent::Type type, uint32_t w, uint32_t h) {
    std::lock_guard<std::recursive_mutex> lock(eventMutex);
    pendingEvents.push_back(WindowEvent{ type, w, h });
}
size_t WindowNode::PendingEventCountForTest() const {
    std::lock_guard<std::recursive_mutex> lock(const_cast<std::recursive_mutex&>(eventMutex));
    return pendingEvents.size();
}
#endif
```

(`eventMutex` must be declared `mutable` OR keep the `const_cast` — prefer `mutable std::recursive_mutex eventMutex;`, it is already used for logically-const queue protection.)

- [x] **Step 3: Hidden-window hint** (needed by Task 4's unattended runner). In `CompileImpl`, right before `glfwCreateWindow` (line ~72):

```cpp
#if defined(VIXEN_FAIL_SCENARIOS) && VIXEN_FAIL_SCENARIOS
        // Fail-scenario runner: create the window hidden so the sweep runs unattended.
        if (const char* hid = std::getenv("VIXEN_HIDDEN_WINDOW"); hid && hid[0] == '1')
            glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
#endif
```

- [x] **Step 4: Run tests:** registry target rebuild + `ctest -R "WindowSeam|FailScenarioRegistry|FaultInjector"`. Expected: all PASS. Also rebuild `build-wsl-off` `RenderGraph` — clean (proves the seam vanishes).

- [x] **Step 5: Commit** `feat(fail-scenarios): WindowNode stimulus seam + hidden-window env (Task 3)`.

---

### Task 4: AppHarness — boot the real app graph under gtest (the environment-risk task)

**Files:**
- Create: `VIXEN/libraries/RenderGraph/tests/FailScenarios/ScenarioHarness.h`
- Create: `VIXEN/libraries/RenderGraph/tests/FailScenarios/ScenarioHarness.cpp`
- Create: `VIXEN/libraries/RenderGraph/tests/FailScenarios/test_fail_scenario_sweep.cpp`
- Modify: `VIXEN/libraries/RenderGraph/tests/test_fail_scenarios.cmake` (second target)
- Modify: `VIXEN/libraries/RenderGraph/src/Nodes/InstanceNode.cpp` (validation counter)

**Interfaces:**
- Consumes: `VixenApp` library (`VulkanGraphApplication`: `Initialize/Prepare/IsPrepared/GetLastError/Update/Render/GetRenderGraph/GetWindowHandle`); `TestVkValidation.h` `EnabledValidationLayers()`; global `deviceExtensionNames`/`instanceExtensionNames`/`layerNames` (from `VulkanGlobalNames.h`, initialized exactly like `main.cpp:18-43`).
- Produces: `AppHarness` and `ScenarioContext` per the interface contract block. `FailScenario::ValidationErrorCount()` (flag-gated global counter).

- [x] **Step 1: Failing test** (`test_fail_scenario_sweep.cpp` — custom main, no gtest_main):

```cpp
#include "ScenarioHarness.h"
#include <gtest/gtest.h>

TEST(FailScenarioSweep, BootWarmupTeardown) {
    AppHarness h;
    ASSERT_TRUE(h.Boot()) << h.BootFailureReason();
    EXPECT_TRUE(h.RunFrames(30));                      // warmup: 30 frames, watchdog-bounded
    EXPECT_EQ(h.ValidationErrors(), 0u);
}   // ~AppHarness tears down cleanly (DeInitialize)

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
```

- [x] **Step 2: CMake target** (append to `test_fail_scenarios.cmake`, inside the existing gates):

```cmake
if(TARGET VixenApp AND TARGET GTest::gtest)
    add_executable(test_fail_scenario_sweep
        FailScenarios/test_fail_scenario_sweep.cpp
        FailScenarios/ScenarioHarness.cpp)
    target_include_directories(test_fail_scenario_sweep PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../include
        ${CMAKE_CURRENT_SOURCE_DIR}/Nodes          # TestVkValidation.h
        ${CMAKE_CURRENT_SOURCE_DIR}/FailScenarios)
    target_link_libraries(test_fail_scenario_sweep PRIVATE GTest::gtest VixenApp glfw)
    set_target_properties(test_fail_scenario_sweep PROPERTIES FOLDER "Tests/RenderGraph Tests")
    gtest_discover_tests(test_fail_scenario_sweep PROPERTIES TIMEOUT 300)
    message(STATUS "✓ test_fail_scenario_sweep configured")
endif()
```

(If `glfw` isn't a visible target name here, check how `RenderGraph`'s CMake links it and reuse that — VixenApp propagates most of it PUBLICly; try without the explicit `glfw` first.)

- [x] **Step 3: Verify it fails to build** (ScenarioHarness.h missing), then **implement**. `ScenarioHarness.h`:

```cpp
#pragma once
#include "Core/FailScenario.h"
#include "Core/RenderGraph.h"
#include "Nodes/SwapChainNode.h"
#include "Nodes/WindowNode.h"
#include "VulkanGraphApplication.h"
#include <chrono>
#include <string>

using namespace Vixen::RenderGraph;
using namespace Vixen::RenderGraph::FailScenario;

// Boots the REAL default application graph (VixenApp) with a hidden window and drives its
// Update/Render loop exactly as main.cpp does. One AppHarness per scenario (boot-per-scenario
// isolation — spec §3.5).
class AppHarness {
public:
    AppHarness();                       // sets VIXEN_HIDDEN_WINDOW=1 + extension/layer globals
    ~AppHarness();                      // DeInitialize via app dtor
    bool Boot();                        // Initialize + Prepare; false (with reason) on failure
    const std::string& BootFailureReason() const { return bootFailure_; }
    // Runs n frames (Update+Render). Returns false if Render() reports stop OR the wall-clock
    // watchdog (default 60 s) expires — a hang IS a failure (historical resize deadlock class).
    bool RunFrames(uint32_t n, std::chrono::seconds watchdog = std::chrono::seconds(60));
    VulkanGraphApplication& App() { return *app_; }
    Vixen::RenderGraph::RenderGraph& Graph() { return *app_->GetRenderGraph(); }
    GLFWwindow* Window() { return app_->GetWindowHandle(); }
    SwapChainNode* SwapChain();         // first instance whose type name == "SwapChain", else nullptr
    WindowNode*   WindowNodePtr();      // first instance whose type name == "Window", else nullptr
    uint32_t ValidationErrors() const;  // FailScenario::ValidationErrorCount()
    static bool DisplayAvailable();     // DISPLAY or WAYLAND_DISPLAY set
private:
    std::unique_ptr<VulkanGraphApplication> app_;
    std::string bootFailure_;
};

// Thrown by Skip(); the runner catches it and converts to GTEST_SKIP.
struct SkipScenario { std::string reason; };

class ScenarioContextImpl final : public FailScenario::ScenarioContext {
public:
    explicit ScenarioContextImpl(AppHarness& h) : h_(h) {}
    bool RunFrames(uint32_t n) override { return h_.RunFrames(n); }
    void ArmFault(FaultSite s, VkResult r) override { h_.Graph().GetFaultInjector()->ArmOnce(s, r); }
    // Applies a stimulus via REAL GLFW window ops (resize/iconify/maximize/restore) so the real
    // callbacks fire; returns false if the WM visibly refused (callers Skip — never fake-pass).
    bool ApplyStimulus(const WindowStimulus& ws) override;
    SwapChainNode* SwapChain() override { return h_.SwapChain(); }
    GLFWwindow* Window() override { return h_.Window(); }
    Vixen::RenderGraph::RenderGraph* Graph() override { return &h_.Graph(); }
    uint32_t ValidationErrors() const override { return h_.ValidationErrors(); }
    void Fail(const std::string& msg) override { ADD_FAILURE() << msg; }
    [[noreturn]] void Skip(const std::string& msg) override { throw SkipScenario{msg}; }
private:
    AppHarness& h_;
};
```

`ScenarioHarness.cpp` — the load-bearing bits:

```cpp
#include "ScenarioHarness.h"
#include "TestVkValidation.h"
#include "VulkanGlobalNames.h"
#include <GLFW/glfw3.h>
#include <cstdlib>

AppHarness::AppHarness() {
#ifdef _WIN32
    _putenv_s("VIXEN_HIDDEN_WINDOW", "1");
#else
    setenv("VIXEN_HIDDEN_WINDOW", "1", 1);
#endif
    // Mirror main.cpp:18-43 (the InstanceNode/DeviceNode read these globals), but gate the
    // validation layer on availability (TestVkValidation pattern) instead of a compile def.
    deviceExtensionNames = { VK_KHR_SWAPCHAIN_EXTENSION_NAME,
                             VK_EXT_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME,
                             VK_KHR_MAINTENANCE_6_EXTENSION_NAME };
    instanceExtensionNames = { VK_KHR_SURFACE_EXTENSION_NAME,
                               VK_EXT_SURFACE_MAINTENANCE_1_EXTENSION_NAME,
                               VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME };
    layerNames = EnabledValidationLayers();
    if (!layerNames.empty()) instanceExtensionNames.push_back(VK_EXT_DEBUG_REPORT_EXTENSION_NAME);
}

bool AppHarness::Boot() {
    if (!DisplayAvailable()) { bootFailure_ = "no DISPLAY/WAYLAND_DISPLAY (GLFW needs a display server)"; return false; }
    app_ = std::make_unique<VulkanGraphApplication>();
    app_->Initialize();
    app_->Prepare();
    if (!app_->IsPrepared()) { bootFailure_ = "Prepare failed: " + app_->GetLastError(); return false; }
    return true;
}

bool AppHarness::RunFrames(uint32_t n, std::chrono::seconds watchdog) {
    const auto deadline = std::chrono::steady_clock::now() + watchdog;
    for (uint32_t i = 0; i < n; ++i) {
        if (std::chrono::steady_clock::now() > deadline) return false;   // hang = fail
        app_->Update();
        if (!app_->Render()) return false;                               // loop stopped
    }
    return true;
}
```

`SwapChain()`/`WindowNodePtr()`: walk `Graph()` instances by index (`GetNodeCount()` + `GetInstance(NodeHandle{i})`, the `MarkNodeNeedsRecompile({i})` handle pattern from `RenderGraph.cpp:846`) comparing `GetNodeType()->GetTypeName()` to `"SwapChain"` / `"Window"`; if those accessors turn out non-public, add a flag-gated `const std::vector<...>&`-free `ForEachInstance(std::function<void(NodeInstance*)>)` to `RenderGraph` instead of exposing internals. `DisplayAvailable()`: `std::getenv("DISPLAY") || std::getenv("WAYLAND_DISPLAY")`.

`ApplyStimulus` (real GLFW ops on the hidden window; poll + verify; never fake):

```cpp
bool ScenarioContextImpl::ApplyStimulus(const WindowStimulus& ws) {
    GLFWwindow* w = h_.Window();
    if (!w) return false;
    using K = WindowStimulus::Kind;
    switch (ws.kind) {
        case K::ResizeTo: glfwSetWindowSize(w, (int)ws.width, (int)ws.height); break;
        case K::Maximize: glfwMaximizeWindow(w); break;
        case K::Minimize: glfwIconifyWindow(w);  break;
        case K::Restore:  glfwRestoreWindow(w);  break;
    }
    glfwPollEvents();  // let the WM round-trip; callbacks enqueue into WindowNode
    // Honesty check: did the op actually take effect?
    switch (ws.kind) {
        case K::ResizeTo: { int fw = 0, fh = 0; glfwGetFramebufferSize(w, &fw, &fh);
                            return fw == (int)ws.width && fh == (int)ws.height; }
        case K::Maximize: return glfwGetWindowAttrib(w, GLFW_MAXIMIZED) == GLFW_TRUE;
        case K::Minimize: return glfwGetWindowAttrib(w, GLFW_ICONIFIED) == GLFW_TRUE;
        case K::Restore:  return glfwGetWindowAttrib(w, GLFW_ICONIFIED) == GLFW_FALSE;
    }
    return false;
}
```

- [x] **Step 4: Validation-error counter.** Read `InstanceNode.cpp`, find the `VK_EXT_DEBUG_REPORT` callback it installs (it logs validation messages). Add to `FailScenario.h` (ON branch): `uint32_t ValidationErrorCount(); void ResetValidationErrorCount(); namespace detail { void BumpValidationError(); }` backed by a `std::atomic<uint32_t>` in `FailScenario.cpp`; in the callback, under `#if VIXEN_FAIL_SCENARIOS`, call `detail::BumpValidationError()` when the flags contain `VK_DEBUG_REPORT_ERROR_BIT_EXT`. If InstanceNode installs NO callback (only the layer's stderr), install one under the flag when `VK_EXT_debug_report` was enabled — follow whatever the file already does; keep it minimal and flag-gated.

- [x] **Step 5: Live gate.** `cmake --build build-wsl --target test_fail_scenario_sweep && ctest --test-dir build-wsl -R BootWarmupTeardown --output-on-failure`. Expected: PASS on WSL (WSLg display + lavapipe), zero validation errors, clean teardown, no window visible. If `DisplayAvailable()` is false in your environment, the test must `GTEST_SKIP()` with the boot-failure reason (wire that: `if (!AppHarness::DisplayAvailable()) GTEST_SKIP() << "...";` at the top of the test) — but on this project's WSLg box it should RUN; a skip here means investigate before proceeding.

- [x] **Step 6: Commit** `feat(fail-scenarios): AppHarness boots real app graph hidden + validation-error counter (Task 4)`.

---

### Task 5: Sweep runner — dynamic per-scenario gtest cases with global pass criteria

**Files:**
- Modify: `VIXEN/libraries/RenderGraph/tests/FailScenarios/test_fail_scenario_sweep.cpp`

**Interfaces:**
- Consumes: `ScenarioRegistry::ForEach`, `ReplayScenarioRegistrars`, `AppHarness`, `ScenarioContext`.
- Produces: one gtest case per declared scenario, named `FailScenarioSweep/<NodeType>.<ScenarioId>` — parallelizable, filterable; each boots its own harness (isolation), applies the stimulus/fault, enforces the global criteria, then runs the declaration's contract. Scenarios whose node type is absent from the assembled graph are skipped (that IS the enumerator: registry × assembled graph).

- [x] **Step 1: Write the runner** (this is infrastructure — its "failing test" is Step 2's run against zero declarations, which must produce zero cases and pass; declarations arrive in Task 6):

```cpp
// Dynamic registration: one test per declared scenario (gtest RegisterTest).
#include "ScenarioHarness.h"
#include <gtest/gtest.h>

namespace {

class ScenarioCase : public ::testing::Test {
public:
    ScenarioCase(std::string nodeType, ScenarioDecl decl)
        : nodeType_(std::move(nodeType)), decl_(std::move(decl)) {}
    void TestBody() override {
        if (!AppHarness::DisplayAvailable()) GTEST_SKIP() << "no display server";
        AppHarness h;
        ASSERT_TRUE(h.Boot()) << h.BootFailureReason();
        if (!h.FindByTypeName(nodeType_))
            GTEST_SKIP() << "node type '" << nodeType_ << "' not present in the assembled graph";
        ASSERT_TRUE(h.RunFrames(30)) << "warmup did not complete";           // criterion 3 (pre)
        FailScenario::ResetValidationErrorCount();
        ScenarioContextImpl ctx(h);

        try {
            if (auto* vt = std::get_if<VkTransient>(&decl_.stimulus)) {
                ctx.ArmFault(vt->site, vt->result);
            } else {
                const auto& ws = std::get<WindowStimulus>(decl_.stimulus);
                if (!ctx.ApplyStimulus(ws))
                    ctx.Skip("window manager refused stimulus '" + decl_.id +
                             "' on a hidden window — cannot exercise honestly");
            }

            if (decl_.knownIssueId) {
                // Known-issue mode: reproduce and REPORT, never gate. (A crash still fails this one
                // ctest case only — gtest_discover_tests runs each case as its own process.)
                const bool progressed = h.RunFrames(30);
                GTEST_SKIP() << "known issue " << decl_.knownIssueId << " — observed: progressed="
                             << progressed << ", validationErrors=" << h.ValidationErrors();
            }

            EXPECT_TRUE(h.RunFrames(30))                                      // inject → observe
                << "rendering did not continue 30 frames post-injection (hang or stop)";
            EXPECT_EQ(h.ValidationErrors(), 0u) << "Vulkan validation errors post-injection"; // criterion 2
            if (decl_.contract) decl_.contract(ctx);                          // criterion 4
        } catch (const SkipScenario& s) {
            GTEST_SKIP() << s.reason;
        }
        // criterion 1 (no crash / nothing escapes host boundary) is implicit: we are still here and
        // Render() returned statuses; a crash fails the whole test process loudly.
    }
private:
    std::string nodeType_;
    ScenarioDecl decl_;
};

void RegisterAllScenarioCases() {
    ReplayScenarioRegistrars();
    ScenarioRegistry::Instance().ForEach([](const std::string& nodeType, const ScenarioDecl& d) {
        ::testing::RegisterTest(
            ("FailScenarioSweep_" + nodeType).c_str(), d.id.c_str(), nullptr, nullptr,
            __FILE__, __LINE__,
            [nodeType, d]() -> ::testing::Test* { return new ScenarioCase(nodeType, d); });
    });
}

} // namespace

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    RegisterAllScenarioCases();
    return RUN_ALL_TESTS();
}
```

Notes for the implementer: (a) keep the Task-4 `BootWarmupTeardown` TEST — it is the canary that isolates environment breakage from scenario breakage. (b) the `FindByTypeName` skip completes the enumerator semantics: registry (declared scenarios) × assembled graph (types present) = the swept matrix. (c) `gtest_discover_tests` discovers dynamic tests via `--gtest_list_tests` at build time — it runs the binary, which is safe: listing does not boot Vulkan (registration only touches the registry). Verify `ctest -N` shows the cases after Task 6 adds declarations.

- [x] **Step 2: Run with zero declarations:** rebuild + `ctest --test-dir build-wsl -R FailScenario --output-on-failure`. Expected: registry unit tests + `BootWarmupTeardown` pass; zero `FailScenarioSweep_*` cases (none declared yet); exit success.

- [x] **Step 3: Commit** `feat(fail-scenarios): dynamic per-scenario sweep runner with global pass criteria (Task 5)`.

---

### Task 6: First real declarations — SwapChainNode + WindowNode (the fullscreen class)

**Files:**
- Modify: `VIXEN/libraries/RenderGraph/src/Nodes/SwapChainNode.cpp` (bottom of file)
- Modify: `VIXEN/libraries/RenderGraph/include/Nodes/SwapChainNode.h` (two getters)
- Modify: `VIXEN/libraries/RenderGraph/src/Nodes/WindowNode.cpp` (bottom of file)

**Interfaces:**
- Consumes: everything above.
- Produces: 6 live scenarios. `SwapChainNode::GetWidth()/GetHeight()`.

- [x] **Step 1: Getters.** `SwapChainNode.h` public section: `uint32_t GetWidth() const { return width; } uint32_t GetHeight() const { return height; }` (unconditional — trivial const accessors).

- [x] **Step 2: SwapChainNode declarations** (bottom of `SwapChainNode.cpp`, after the existing `VIXEN_REGISTER_NODE` if present there — otherwise just at file end; add `#include "Core/FailScenario.h"` at top):

```cpp
// ====== Fail scenarios (compiled out of real builds — see Fail-Scenario-Simulation-Design-2026-07) ======
// Contracts use ScenarioContext::Fail/Skip, NEVER gtest macros (this is an engine TU).
#if defined(VIXEN_FAIL_SCENARIOS) && VIXEN_FAIL_SCENARIOS
namespace FS = Vixen::RenderGraph::FailScenario;
VIXEN_FAIL_SCENARIOS_DECLARE(Vixen::RenderGraph::SwapChainNodeType,
    VIXEN_SCENARIO(AcquireOutOfDate,
        FS::VkTransient{ .site = FS::FaultSite::Acquire, .result = VK_ERROR_OUT_OF_DATE_KHR },
        [](FS::ScenarioContext& c) {
            // Recovery contract: the deferred-recompile path recreates the swapchain and
            // rendering continues (global criteria already assert progress); the node must
            // not be stuck skipping frames — image index becomes valid again.
            auto* sc = c.SwapChain();
            if (!sc) { c.Fail("no SwapChain node reachable from harness"); return; }
            if (sc->GetCurrentImageIndex() == UINT32_MAX)
                c.Fail("swapchain never recovered from OUT_OF_DATE (image index still invalid)");
        }),
    VIXEN_SCENARIO(AcquireSuboptimal,
        FS::VkTransient{ .site = FS::FaultSite::Acquire, .result = VK_SUBOPTIMAL_KHR },
        [](FS::ScenarioContext& c) {
            auto* sc = c.SwapChain();
            if (!sc) { c.Fail("no SwapChain node reachable from harness"); return; }
            if (sc->GetCurrentImageIndex() == UINT32_MAX)
                c.Fail("swapchain stuck on invalid image index after SUBOPTIMAL");
        })
);
#endif
```

(The `#if` guard wraps alias + declarations together in every declaring .cpp: the `namespace FS` alias references
`FailScenario` symbols that only exist when the flag is on — the macros alone compiling to nothing is not enough.)

- [x] **Step 3: PresentNode declaration** (bottom of `PresentNode.cpp`, next to its `VIXEN_REGISTER_NODE`):

```cpp
#if defined(VIXEN_FAIL_SCENARIOS) && VIXEN_FAIL_SCENARIOS
namespace FS = Vixen::RenderGraph::FailScenario;
VIXEN_FAIL_SCENARIOS_DECLARE(Vixen::RenderGraph::PresentNodeType,
    VIXEN_SCENARIO(PresentOutOfDate,
        FS::VkTransient{ .site = FS::FaultSite::Present, .result = VK_ERROR_OUT_OF_DATE_KHR },
        // Minimal contract: no crash + continued progress (global criteria). NOTE (from planning
        // exploration): PresentNode currently IGNORES the present result — nothing consumes
        // PRESENT_RESULT to trigger recreation. This scenario documents today's tolerated behavior;
        // when present-driven recreation is implemented, tighten this contract to assert it.
        [](FS::ScenarioContext&) {})
);
#endif
```

- [x] **Step 4: WindowNode declarations** (bottom of `WindowNode.cpp`) — the motivating class:

```cpp
#if defined(VIXEN_FAIL_SCENARIOS) && VIXEN_FAIL_SCENARIOS
namespace FS = Vixen::RenderGraph::FailScenario;
VIXEN_FAIL_SCENARIOS_DECLARE(Vixen::RenderGraph::WindowNodeType,
    VIXEN_SCENARIO(ResizeLargeExtentJump,   // deterministic fullscreen-class repro (extent jump)
        FS::WindowStimulus{ .kind = FS::WindowStimulus::Kind::ResizeTo, .width = 1920, .height = 1080 },
        [](FS::ScenarioContext& c) {
            auto* sc = c.SwapChain();
            if (!sc) { c.Fail("no SwapChain node reachable from harness"); return; }
            if (sc->GetWidth() != 1920u || sc->GetHeight() != 1080u)
                c.Fail("swapchain never recreated at the new extent: got " +
                       std::to_string(sc->GetWidth()) + "x" + std::to_string(sc->GetHeight()) +
                       ", expected 1920x1080");
        }),
    VIXEN_SCENARIO(MaximizeLikeFullscreenButton,   // the literal user gesture (WM-dependent)
        FS::WindowStimulus{ .kind = FS::WindowStimulus::Kind::Maximize },
        [](FS::ScenarioContext& c) {
            // Extent after maximize is WM-decided: assert swapchain matches the REAL framebuffer.
            int fw = 0, fh = 0;
            glfwGetFramebufferSize(c.Window(), &fw, &fh);
            auto* sc = c.SwapChain();
            if (!sc) { c.Fail("no SwapChain node reachable from harness"); return; }
            if (sc->GetWidth() != static_cast<uint32_t>(fw) || sc->GetHeight() != static_cast<uint32_t>(fh))
                c.Fail("swapchain extent " + std::to_string(sc->GetWidth()) + "x" +
                       std::to_string(sc->GetHeight()) + " does not match maximized framebuffer " +
                       std::to_string(fw) + "x" + std::to_string(fh));
        }),
    VIXEN_SCENARIO(MinimizeThenRestore,
        FS::WindowStimulus{ .kind = FS::WindowStimulus::Kind::Minimize },
        [](FS::ScenarioContext& c) {
            // Minimized: must not crash or hang (global criteria cover the frames while iconified).
            // Then restore and require rendering to resume.
            if (!c.ApplyStimulus(FS::WindowStimulus{ .kind = FS::WindowStimulus::Kind::Restore }))
                c.Skip("WM refused restore on the hidden window");
            if (!c.RunFrames(30)) c.Fail("rendering did not resume after restore");
        })
);
#endif
```

(`WindowNode.cpp` already includes GLFW ✓; add `#include "Core/FailScenario.h"` and `#include "Nodes/SwapChainNode.h"` — the contracts call `SwapChainNode` methods, and the forward declaration in FailScenario.h is not enough for member access.)

- [x] **Step 5: Live sweep — the moment of truth.** Rebuild, then `ctest --test-dir build-wsl -R FailScenarioSweep --output-on-failure`. Expected outcomes and what each means:
  - `AcquireOutOfDate`/`AcquireSuboptimal`: PASS (this path has a historical fix and should hold).
  - `PresentOutOfDate`: PASS (trivially — documented above).
  - `ResizeLargeExtentJump`: **this is the fullscreen-crash-class probe.** If it FAILS (crash, hang, validation errors, or stale extent) → the harness has reproduced the bug class headlessly: capture the full output, file a `KI-003` entry in `Vixen-Docs/04-Development/Known-Issues.md` (symptom = the test output; this is the fullscreen-button crash's class), set `.knownIssueId = "KI-003"` on the scenario (4th `VIXEN_SCENARIO` argument) so the gate reports-but-does-not-block, and re-run to green. Do NOT fix the underlying bug in this task — root-causing it is the user's parallel debugging work; the scenario becomes its permanent regression gate once fixed (then remove `knownIssueId`).
  - `MaximizeLikeFullscreenButton`/`MinimizeThenRestore`: PASS or SKIP (WM-refused, reported honestly). A SKIP is acceptable on WSLg; note it in the commit message.
- [x] **Step 6: No-regression:** run the render-test subset from Task 2 Step 6 again — unchanged results.
- [x] **Step 7: Commit** `feat(fail-scenarios): first 6 scenarios — swapchain transients + window stimuli (fullscreen class) (Task 6)` — include the live sweep summary (pass/skip/known-issue counts) in the body.

---

### Task 7: DeviceLostRecovery scenario (migrates the device-loss harness into the sweep)

**Files:**
- Modify: `VIXEN/libraries/RenderGraph/src/Nodes/FrameSyncNode.cpp` (bottom of file)

**Interfaces:**
- Consumes: `FenceWait` fault point (Task 2), runner (Task 5). The app's `Render()` already routes `VK_ERROR_DEVICE_LOST` → `RecoverFromDeviceLoss()` → continue (`VulkanGraphApplication.cpp:309-323`).
- Produces: the automated device-loss test that Device-Loss-Recovery Inc 3 deferred.

- [x] **Step 1: Declaration** (bottom of `FrameSyncNode.cpp`; add the FailScenario include):

```cpp
#if defined(VIXEN_FAIL_SCENARIOS) && VIXEN_FAIL_SCENARIOS
namespace FS = Vixen::RenderGraph::FailScenario;
VIXEN_FAIL_SCENARIOS_DECLARE(Vixen::RenderGraph::FrameSyncNodeType,
    VIXEN_SCENARIO(DeviceLostRecovery,
        FS::VkTransient{ .site = FS::FaultSite::FenceWait, .result = VK_ERROR_DEVICE_LOST },
        // The one-shot forced VK_ERROR_DEVICE_LOST drives the REAL detection path
        // (this node's fence wait → NotifyDeviceLost → RenderFrame returns DEVICE_LOST →
        // app Render() → RecoverFromDeviceLoss teardown-reverse/rebuild-forward). On the
        // healthy lavapipe device the rebuild succeeds — the global criteria then prove
        // 30 frames of continuous post-recovery rendering with zero validation errors,
        // which is exactly the manual VIXEN_SIMULATE_DEVICE_LOSS gate, automated.
        [](FS::ScenarioContext& c) {
            if (c.Graph()->IsDeviceLost())
                c.Fail("device-lost latch still set — recovery did not complete");
        })
);
#endif
```

(If `IsDeviceLost()` is not public on `RenderGraph`, it exists per Device-Loss Inc 1 — check `RenderGraph.h`; add a `bool IsDeviceLost() const { return deviceLost_; }` if it was never exposed.)

- [x] **Step 2: Live gate:** `ctest --test-dir build-wsl -R DeviceLostRecovery --output-on-failure`. Expected: PASS — log shows the full teardown-reverse/rebuild-forward ("RECOVERY COMPLETE") followed by 30 clean frames. This is the promotion of the manual harness to an automated test.
- [x] **Step 3: Confirm the env var still works** (it now arms the same site): `VIXEN_SIMULATE_DEVICE_LOSS=60 ./build-wsl/<path>/test_fail_scenario_sweep --gtest_filter='*BootWarmupTeardown*'` — wait, warmup is only 30 frames; instead run the standalone app briefly if convenient, or set `=10`: `VIXEN_SIMULATE_DEVICE_LOSS=10 ... --gtest_filter='*BootWarmupTeardown*'`. Expected: recovery fires during warmup and the test still passes (Render() self-heals). This proves the migration preserved the manual tool.
- [x] **Step 4: Update docs:** `Device-Loss-Recovery-2026-06.md` — mark Inc 3's "promote the fault-injection harness into an automated test" ✅ with a pointer to this plan; note the env hook now arms `FenceWait` (flag-gated, absent from real builds).
- [x] **Step 5: Commit** `feat(fail-scenarios): DeviceLostRecovery scenario — device-loss harness promoted to automated gate (Task 7)`.

---

### Task 8: Empirical compile-out proof + wrap-up

**Files:**
- Create: `VIXEN/scripts/check_fail_scenario_compile_out.sh`
- Modify: `VIXEN/Vixen-Docs/01-Architecture/Fail-Scenario-Simulation-Design-2026-07.md` (status line)

**Interfaces:** none new.

- [x] **Step 1: The proof script:**

```bash
#!/usr/bin/env bash
# Empirical zero-footprint proof (spec §8): a VIXEN_FAIL_SCENARIOS=OFF build's artifacts must
# contain NO scenario/injector/seam symbols; an ON build MUST contain them (proves the probe works —
# an empty grep on both sides would be a broken probe, not a passing gate).
set -e
OFF_DIR="${1:-build-wsl-off}"; ON_DIR="${2:-build-wsl}"
SYMS='ScenarioRegistry|FaultInjector|InjectWindowEvent|ReplayScenarioRegistrars|s_vixen_fail_scenarios_'
find_syms() { find "$1" \( -name 'libRenderGraph*.a' -o -name 'libRenderGraphNodes*.a' -o -name 'libVixenApp*.a' \) \
              -exec nm -C {} + 2>/dev/null | grep -cE "$SYMS" || true; }
OFF_COUNT=$(find_syms "$OFF_DIR"); ON_COUNT=$(find_syms "$ON_DIR")
echo "OFF-build scenario symbols: $OFF_COUNT (must be 0)"
echo "ON-build  scenario symbols: $ON_COUNT (must be > 0)"
[ "$OFF_COUNT" -eq 0 ] && [ "$ON_COUNT" -gt 0 ] && echo "COMPILE-OUT PROOF: PASS" || { echo "FAIL"; exit 1; }
```

- [x] **Step 2: Run it.** Build `RenderGraph VixenApp` in `build-wsl-off` (OFF) if stale, then `bash scripts/check_fail_scenario_compile_out.sh build-wsl-off build-wsl`. Expected: `COMPILE-OUT PROOF: PASS`. (On MSVC artifacts the same check would use `dumpbin /symbols`; WSL/GCC proof satisfies Inc 1 — note MSVC parity as an Inc 2 CI item.)
- [x] **Step 3: Full suite sanity:** `cmake --build build-wsl -- -k 0` (whole tree, flag ON) and `ctest --test-dir build-wsl -R "FailScenario|render" --output-on-failure`. Expected: everything green (or the documented knownIssue skips).
- [x] **Step 4: Spec status update:** design doc frontmatter `status:` → "Inc 1 implemented ({date}); scenarios live in the sweep (see Fail-Scenario-Simulation-Inc1-Plan). Inc 2 (coverage + gate target + tamper self-test) next." Also add the plan doc to `related:`.
- [x] **Step 5: Commit** `feat(fail-scenarios): Inc 1 complete — empirical compile-out proof + docs (Task 8)`. Do not push; do not merge — integration is a user decision (superpowers:finishing-a-development-branch).

---

## Self-Review (performed at plan-writing time)

1. **Spec coverage (Inc 1 items → tasks):** flag+macros+registry → T1; FaultInjector + Acquire/Present/FenceWait → T2; InjectWindowEvent seam → T3; ScenarioContext+runner+gtest on default app graph → T4+T5; SwapChainNode/WindowNode first declarations incl. fullscreen class → T6; device-loss migration → T7; empirical compile-out → T8. Gate target (`VIXEN_GATE_FAIL_SCENARIOS`), tamper fixture, Submit/Allocate sites, MSVC symbol proof = Inc 2 (per spec §5), intentionally absent.
2. **Placeholders:** none — every step has code or an exact command with expected output. Two deliberate runtime-discovery points are stated as explicit conditionals with both branches specified (instance-walk accessors in T4; InstanceNode callback in T4 Step 4).
3. **Type consistency:** `VIXEN_FAIL_SCENARIOS_DECLARE`/`VIXEN_SCENARIO`/`VIXEN_FAULT_FILTER`, `FaultSite::{Acquire,Present,FenceWait}`, `ScenarioDecl{id,stimulus,contract,knownIssueId}`, `AppHarness::{Boot,RunFrames,SwapChain,WindowNodePtr,FindByTypeName,ValidationErrors,BootFailureReason}`, abstract `ScenarioContext::{RunFrames,ArmFault,ApplyStimulus,ValidationErrors,SwapChain,Window,Graph,Fail,Skip}` implemented by `ScenarioContextImpl` — used identically across T1-T7. Two deliberate namings recorded so nobody "fixes" them back: (a) the spec's illustrative `VIXEN_FAIL_SCENARIOS(...)` macro is `VIXEN_FAIL_SCENARIOS_DECLARE(...)` in code because the plain name collides with the preprocessor flag itself; (b) `ScenarioContext` is an ABSTRACT interface in the library header — contract lambdas compile in engine TUs and must never touch gtest or test headers, so contracts use `Fail()`/`Skip()`, and gtest macros appear only in `ScenarioHarness.*`/`test_*.cpp`. Task 1's unit-test lambdas take `ScenarioContext&` but have empty bodies, so the abstract type is fine there.
4. **Layering:** engine → `FailScenario.h` only (self-neutralizing); tests → `VixenApp` + engine. No engine TU includes a test header; `RenderGraph.h` gains one flag-gated include + member. Whole-archive of `RenderGraphNodes` (pre-existing, `RenderGraph/CMakeLists.txt:400-407`) is what keeps both node registrars AND scenario registrars alive — no new link machinery.
