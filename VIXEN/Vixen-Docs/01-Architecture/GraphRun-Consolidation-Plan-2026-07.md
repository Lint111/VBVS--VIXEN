# graph.Run() Render-Loop Consolidation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a canonical engine-owned `Run()` and one-iteration `Tick()` (with `PreTick()`/`PostTick()` hooks) to `VulkanApplicationBase` so the two VIXEN dispatch entry points stop hand-rolling the render loop — with byte-identical rendering.

**Architecture:** `Tick()` runs the exact per-frame sequence the mains do today (`PreTick`→`Update`→`Render`→`PostTick`) and classifies the outcome into a `TickStatus`; `Run(opts)` is the engine-owned loop (`Initialize`→`Prepare`→guard→`while(Tick()==Running)`→`DeInitialize`) with a single try/catch. Both live on `VulkanApplicationBase` (which already has a `.cpp`); `RenderGraph` is untouched. The two mains collapse to `app->Run(opts)`; the editor's scripted-action injector moves from `Update()` into a `PreTick()` override.

**Tech Stack:** C++23, Vulkan 1.3, CMake/MSVC. Design doc: `GraphRun-Consolidation-Design-2026-07.md`.

## Global Constraints

- **VIXEN-only.** Do NOT modify `/home/liory/Github/undertow` or any submodule pin. `TickStatus` must be expressive enough for undertow to adopt `Tick()`/`PreTick()` later, but that adoption is out of scope.
- **Exact render parity — no frame's rendered output may change.** The two GPU gates (editor byte-exact PNGs; standalone renders+exits clean) are the parity proof.
- **No error-model change.** Do NOT touch the `RenderFrame()`-returns-`VK_SUCCESS` channel, the `exit()` sites, or the result-type generalization. Device-loss policy stays inside `VulkanGraphApplication::Render()` verbatim — `Tick()` only *reads* its `bool` + `renderGraph->IsDeviceLost()` to name the outcome.
- **RenderGraph untouched.** No `Run`/`Tick` on `RenderGraph`; no new public accessor on it (`IsDeviceLost()` already exists and is enough).
- **KI-013 device-loss-recovery hard gate stays intact** — policy read, never re-implemented.
- **CMake source/link registration must land in the SAME task that creates/needs a file** (Inc-2 durable lesson — an offline syntax gate cannot catch a missing link).
- **Build/test Windows-side first** (much faster than WSL for this env); GPU gates run on the real D3D12/dzn GPU (this env is NOT lavapipe). First configure with `-DBUILD_TESTS=ON` ~500s; reconfigure ~115s; a GPU render test ~50s. Never launch overlapping builds of one target (they race + truncate the binary). Poll long builds on a foreground interval; don't blind-wait.

---

## Milestone Map

- **M1 — Core `Tick()`/`Run()` on the base + offline classification test** (Tasks 1–2): add the vocabulary + methods to `VulkanApplicationBase`, add the `IsShutdownRequested()` accessor to `VulkanGraphApplication`, prove `Tick()`'s status classification + hook order with an offline stub gtest. Deliverable: engine-owned loop surface, unit-proven, no entry point wired yet. **✅ DONE.**
- **M2 — Collapse the two mains onto `Run()` + `FrameTimer`** (Tasks 3–4): extract the standalone main's frame-timer into a `FrameTimer` helper gated by `RunOptions.enableFrameTimer`; collapse `application/main/source/main.cpp` and `application/editor/source/main.cpp` to `app->Run(opts)`. Deliverable: both mains thinned, standalone parity gate green. **✅ DONE.**
- **M3 — Editor injector → `PreTick()` + windowed gate + close-out** (Tasks 5–6): move the scripted-action injector from `EditorApplication::Update()` into a `PreTick()` override (capture dumps stay in `Update()`'s tail); prove the Inc-2b windowed gate passes byte-identically; update docs/known-issues + close out. Deliverable: full consolidation, byte-exact editor gate green.

---

## Reference: exact current shapes (read before starting)

**`VulkanApplicationBase`** (`application/main/include/VulkanApplicationBase.h`): abstract base. Has a `.cpp` (`application/main/source/VulkanApplicationBase.cpp`). Existing: `virtual void Initialize()`, `virtual void Prepare()=0`, `virtual void Update()=0`, `virtual bool Render()=0`, `virtual void DeInitialize()`, `bool IsPrepared() const`, `const std::string& GetLastError() const`, `std::shared_ptr<Logger> mainLogger`. Protected state block ends at `std::string lastError_;`.

**`VulkanGraphApplication`** (`application/main/include/VulkanGraphApplication.h`): derives the base. Owns `RenderGraph* renderGraph = nullptr;` (`:152`, protected), `bool shutdownRequested = false;` (`:162`, protected), `bool graphCompiled;` (`:158`). `RenderGraph* GetRenderGraph() const` (`:61`) is public. `RenderGraph::IsDeviceLost() const` (`RenderGraph.h:475`) is public → true while device is lost / after a failed recovery.

**`VulkanGraphApplication::Render()`** (`application/main/source/VulkanGraphApplication.cpp:257–312`): returns `false` on `!isPrepared||!graphCompiled||!renderGraph||shutdownRequested` (`:262`), else does `glfwPollEvents()` + `renderGraph->RenderFrame()` + the `VK_ERROR_DEVICE_LOST`→`RecoverFromDeviceLoss()`→give-up policy (`:281–295`). **Do not touch this method.**

**Standalone loop** (`application/main/source/main.cpp:112–152`): `while(isWindowOpen){ Update(); isWindowOpen=Render(); <frame-timer block>; <exitAfterFrames check>; }` wrapped in a `try` at `:72` with two `catch` (`:158`,`:162`). Frame-timer block = `:100–145` (kFrameWindow=120 rolling avg/p99/FPS + outlier logging).

**Editor loop** (`application/editor/source/main.cpp:96–105`): same shape, no frame-timer; `EditorApplication(documentPath)` ctor + `LoadDocument()` before `Initialize()`.

**Editor `Update()`** (`application/editor/source/EditorApplication.cpp:299–411`): calls `VulkanGraphApplication::Update()` (`:300`), then in a try/catch: one-time env parse (`:314–325`), **scripted-action injector** (`:331–338`), interactive input path (UI-click drain `:341–349`, Save-key `:352–359`, Ctrl+Z/Y `:364–371`), **dirty re-flatten tail** (`:378–383`), **capture dumps** (`:391–398`), `++updateTick_` (`:409`). Members (`EditorApplication.h`): `std::vector<ScriptedAction> scriptedActions_` (`:102`), `std::vector<long> captureFrames_` (`:103`), `std::string captureDir_="temp"` (`:104`), `bool scriptParsed_=false` (`:105`), `long updateTick_=0` (`:100`).

---

## Task 1: `TickStatus`/`RunOptions`/`PreTick`/`PostTick`/`Tick`/`Run` on `VulkanApplicationBase` + `IsShutdownRequested()`

**Files:**
- Modify: `VIXEN/application/main/include/VulkanApplicationBase.h`
- Modify: `VIXEN/application/main/source/VulkanApplicationBase.cpp`
- Modify: `VIXEN/application/main/include/VulkanGraphApplication.h` (add protected `IsShutdownRequested()` + override `IsShutdownRequestedImpl` — see below)
- Modify: `VIXEN/application/main/source/VulkanGraphApplication.cpp` (define the accessor)

**Interfaces:**
- Produces (consumed by Tasks 2–5):
  - `enum class TickStatus { Running, WindowClosed, FrameLimitReached, RenderError, DeviceLostUnrecoverable };`
  - `struct RunOptions { uint64_t exitAfterFrames = 0; bool enableFrameTimer = false; };`
  - `virtual void VulkanApplicationBase::PreTick() {}` / `virtual void PostTick() {}`
  - `TickStatus VulkanApplicationBase::Tick();`
  - `int VulkanApplicationBase::Run(const RunOptions& opts = {});`
  - Protected virtual hooks the base uses to read subclass state without knowing about RenderGraph:
    `virtual bool IsShutdownRequested() const { return false; }`
    `virtual bool IsDeviceLostState() const { return false; }`
    (base defaults; `VulkanGraphApplication` overrides both.)

**Design note (why the two protected virtuals):** `VulkanApplicationBase` has no `renderGraph`/`shutdownRequested` members — those live on `VulkanGraphApplication`. `Tick()` is defined on the base, so it reads the two facts it needs to classify (`WindowClosed`, `DeviceLostUnrecoverable`) through two protected virtual predicates the derived class overrides. This keeps `Tick()`/`Run()` on the base (design D1) without the base depending on RenderGraph.

- [ ] **Step 1: Write the failing test** (a fresh offline gtest with a stub subclass; this same file grows in Task 2). Create `VIXEN/application/main/tests/test_app_run_tick.cpp`:

```cpp
// Offline (no Vulkan/GPU) unit test for VulkanApplicationBase::Tick() status classification and
// the PreTick->Update->Render->PostTick hook order. Uses a stub subclass whose virtuals return
// canned outcomes so the loop logic is provable without a device.
#include <gtest/gtest.h>
#include "VulkanApplicationBase.h"
#include <vector>
#include <string>

namespace {

// Minimal stub: overrides every pure virtual with a canned outcome + records the call order.
class StubApp : public VulkanApplicationBase {
public:
    // Canned control knobs the tests set:
    bool  renderReturns    = true;    // what Render() returns this tick
    bool  shutdown         = false;   // what IsShutdownRequested() reports
    bool  deviceLost       = false;   // what IsDeviceLostState() reports
    std::vector<std::string> calls;   // records hook/method order

    void Prepare() override { calls.push_back("Prepare"); }
    void Update()  override { calls.push_back("Update"); }
    bool Render()  override { calls.push_back("Render"); return renderReturns; }
    void PreTick()  override { calls.push_back("PreTick"); }
    void PostTick() override { calls.push_back("PostTick"); }
protected:
    bool IsShutdownRequested() const override { return shutdown; }
    bool IsDeviceLostState()   const override { return deviceLost; }
public:
    using VulkanApplicationBase::Tick;   // expose for the test
    void SetExitAfterFrames(uint64_t n) { SetExitAfterFramesForTest(n); }
};

TEST(AppRunTick, HookOrderIsPreUpdateRenderPost) {
    StubApp app;
    app.Tick();
    ASSERT_EQ(app.calls, (std::vector<std::string>{"PreTick", "Update", "Render", "PostTick"}));
}

TEST(AppRunTick, RenderTrueUnderLimitReturnsRunning) {
    StubApp app;
    app.renderReturns = true;
    EXPECT_EQ(app.Tick(), TickStatus::Running);
}

TEST(AppRunTick, FrameLimitReachedWhenCounterHitsExitAfterFrames) {
    StubApp app;
    app.renderReturns = true;
    app.SetExitAfterFrames(1);       // limit of 1 frame
    EXPECT_EQ(app.Tick(), TickStatus::FrameLimitReached);  // after this single tick, counter==1>=1
}

TEST(AppRunTick, RenderFalseWithShutdownIsWindowClosed) {
    StubApp app;
    app.renderReturns = false;
    app.shutdown = true;
    EXPECT_EQ(app.Tick(), TickStatus::WindowClosed);
}

TEST(AppRunTick, RenderFalseWithDeviceLostIsDeviceLostUnrecoverable) {
    StubApp app;
    app.renderReturns = false;
    app.shutdown = false;
    app.deviceLost = true;
    EXPECT_EQ(app.Tick(), TickStatus::DeviceLostUnrecoverable);
}

TEST(AppRunTick, RenderFalseWithNeitherIsRenderError) {
    StubApp app;
    app.renderReturns = false;
    app.shutdown = false;
    app.deviceLost = false;
    EXPECT_EQ(app.Tick(), TickStatus::RenderError);
}

}  // namespace
```

- [ ] **Step 2: Register the test target so it fails to build (proving it's wired) — do this in the SAME step.** Create `VIXEN/application/main/tests/test_app_run_tick.cmake`:

```cmake
# Offline unit test for VulkanApplicationBase::Tick()/Run() classification. Links VixenApp (the app
# target) which only exists after application/main/CMakeLists.txt has defined it — mirrors why
# test_fail_scenario_sweep is configured from application/main/CMakeLists.txt, not the library.
# No GPU: the stub subclass overrides Render()/Update() with canned outcomes.
add_executable(test_app_run_tick ${CMAKE_CURRENT_LIST_DIR}/test_app_run_tick.cpp)
target_link_libraries(test_app_run_tick PRIVATE VixenApp GTest::gtest GTest::gtest_main)
set_target_properties(test_app_run_tick PROPERTIES FOLDER "Tests/Application")
gtest_discover_tests(test_app_run_tick)
message(STATUS "[Application Tests] Added: test_app_run_tick (Tick/Run classification, offline)")
```

Then add to `VIXEN/application/main/CMakeLists.txt` immediately after the existing `include(...test_fail_scenario_sweep.cmake)` line (`:152`):

```cmake
include(${CMAKE_CURRENT_LIST_DIR}/tests/test_app_run_tick.cmake)
```

> Confirm `VixenApp` is the app target name and `GTest::gtest`/`GTest::gtest_main` are the imported targets used elsewhere in this file. If the file uses a different target/link name for gtest (e.g. a `RENDERGRAPH_TEST_COMMON_LIBS`-style var or `gtest_main`), match THAT — read `test_fail_scenario_sweep.cmake` for the exact link pattern and copy it. The test must link the app object that defines `VulkanApplicationBase`.

- [ ] **Step 3: Run to verify it fails to build.**

Windows-side configure + build just this target:
```
cmd.exe /c "cd /d C:\cpp\VBVS--VIXEN\VIXEN && cmake --build build --config Debug --target test_app_run_tick"
```
Expected: FAIL — `TickStatus`/`Tick`/`Run`/`PreTick`/`SetExitAfterFramesForTest` undeclared. (If the target isn't found, the `.cmake` include didn't register — fix that first.)

- [ ] **Step 4: Implement the header additions.** In `VulkanApplicationBase.h`, add above `class VulkanApplicationBase`:

```cpp
// Why the loop stopped (or that it should keep running). Returned by Tick().
// Rich enough that a host (e.g. undertow) driving Tick() directly can branch on the reason,
// where today VulkanGraphApplication::Render()'s single bool collapses all of these.
enum class TickStatus {
    Running,                  // keep looping
    WindowClosed,             // shutdownRequested (user close / WindowCloseEvent); clean exit
    FrameLimitReached,        // RunOptions.exitAfterFrames hit; clean exit
    RenderError,              // Render() returned false for a non-recoverable frame failure
    DeviceLostUnrecoverable,  // device lost and RecoverFromDeviceLoss() gave up
};

// Configures an engine-owned Run().
struct RunOptions {
    uint64_t exitAfterFrames = 0;      // 0 = unlimited. Absorbs the VIXEN_EXIT_AFTER_FRAMES knob.
    bool     enableFrameTimer = false; // opt-in p99/FPS rolling-window logging (standalone main only)
};
```

Inside the class `public:` section (after `Render()`'s declaration), add:

```cpp
    // ====== Canonical run surface (graph.Run() consolidation) ======

    // Per-frame host-injection hooks. Default no-op. A host subclass overrides these to run a
    // prologue/epilogue (e.g. the editor's scripted-action injector, or undertow's sim tick) without
    // the engine knowing about host code. PreTick() runs BEFORE Update(); PostTick() AFTER Render().
    virtual void PreTick()  {}
    virtual void PostTick() {}

    // One loop iteration: PreTick -> Update -> Render -> PostTick, then classify the outcome.
    // Behavior-identical to the old hand-rolled loop body; the ONLY addition is the descriptive return.
    TickStatus Tick();

    // Engine-owned loop: Initialize -> Prepare -> IsPrepared guard -> while(Tick()==Running) ->
    // DeInitialize. One try/catch (never throws past the entry point — UB across the undertow C ABI).
    // Returns a process exit code: 0 clean, -1 on RenderError/DeviceLostUnrecoverable/Prepare-fail.
    int Run(const RunOptions& opts = {});
```

In the class `protected:` section, add the two classification predicates + the counter state + a test seam:

```cpp
    // Classification predicates Tick() consults. Base returns false; VulkanGraphApplication overrides
    // to expose its shutdownRequested flag and RenderGraph::IsDeviceLost(). Kept here (not concrete)
    // so Tick()/Run() live on the base without the base depending on RenderGraph.
    virtual bool IsShutdownRequested() const { return false; }
    virtual bool IsDeviceLostState()   const { return false; }

    // Test-only: lets an offline stub set the frame limit without going through Run().
    void SetExitAfterFramesForTest(uint64_t n) { exitAfterFrames_ = n; }
```

In the protected state block (after `std::string lastError_;`), add:

```cpp
    uint64_t frameCounter_    = 0;   // incremented once per Tick(); replaces the mains' local counter
    uint64_t exitAfterFrames_ = 0;   // set by Run() from RunOptions; 0 = unlimited; read by Tick()
```

- [ ] **Step 5: Implement `Tick()` and `Run()` in `VulkanApplicationBase.cpp`.** Add at the end of the file:

```cpp
TickStatus VulkanApplicationBase::Tick() {
    PreTick();                 // host prologue (default no-op)
    Update();                  // existing per-frame update (derived)
    const bool ok = Render();  // existing per-frame render + device-loss policy (derived)
    PostTick();                // host epilogue (default no-op)
    ++frameCounter_;

    if (ok) {
        if (exitAfterFrames_ > 0 && frameCounter_ >= exitAfterFrames_) {
            return TickStatus::FrameLimitReached;
        }
        return TickStatus::Running;
    }
    // ok == false: name why, from state the derived class exposes. No new policy.
    if (IsShutdownRequested())  return TickStatus::WindowClosed;
    if (IsDeviceLostState())    return TickStatus::DeviceLostUnrecoverable;
    return TickStatus::RenderError;
}

int VulkanApplicationBase::Run(const RunOptions& opts) {
    exitAfterFrames_ = opts.exitAfterFrames;
    try {
        Initialize();
        Prepare();
        if (!IsPrepared()) {
            if (mainLogger) mainLogger->Error("[Run] Prepare failed: " + GetLastError() + " - aborting before render loop");
            DeInitialize();
            return -1;
        }
        if (mainLogger) mainLogger->Info("[Run] Entering render loop...");

        // FrameTimer is added in Task 3; until then enableFrameTimer is honored by a no-op.
        TickStatus st = TickStatus::Running;
        while ((st = Tick()) == TickStatus::Running) {
            // (Task 3 inserts frame-timer recording here, gated by opts.enableFrameTimer.)
        }

        if (mainLogger) {
            const char* reason =
                st == TickStatus::WindowClosed           ? "window closed" :
                st == TickStatus::FrameLimitReached      ? "frame limit reached" :
                st == TickStatus::DeviceLostUnrecoverable? "device lost (unrecoverable)" :
                                                           "render error";
            mainLogger->Info(std::string("[Run] Render loop exited: ") + reason);
        }
        DeInitialize();
        return (st == TickStatus::RenderError || st == TickStatus::DeviceLostUnrecoverable) ? -1 : 0;
    } catch (const std::exception& e) {
        if (mainLogger) mainLogger->Error(std::string("[Run] Uncaught exception: ") + e.what());
        return -1;
    } catch (...) {
        if (mainLogger) mainLogger->Error("[Run] Uncaught unknown exception");
        return -1;
    }
}
```

- [ ] **Step 6: Override the two predicates in `VulkanGraphApplication`.** In `VulkanGraphApplication.h` `protected:` section, add:

```cpp
    // graph.Run() consolidation: expose the two facts the base Tick() classifies on.
    bool IsShutdownRequested() const override { return shutdownRequested; }
    bool IsDeviceLostState()   const override { return renderGraph && renderGraph->IsDeviceLost(); }
```

(These can be inline in the header since `shutdownRequested`/`renderGraph` are members and `RenderGraph::IsDeviceLost()` is already declared via the `Core/RenderGraph.h` include at `:4`. If the class prefers out-of-line, put them in `VulkanGraphApplication.cpp` instead — match the file's convention.)

- [ ] **Step 7: Build the test target and verify it passes.**
```
cmd.exe /c "cd /d C:\cpp\VBVS--VIXEN\VIXEN && cmake --build build --config Debug --target test_app_run_tick"
```
Then run:
```
cmd.exe /c "cd /d C:\cpp\VBVS--VIXEN\VIXEN && build\application\main\Debug\test_app_run_tick.exe --gtest_brief=1"
```
Expected: PASS, 6/6 (`HookOrderIsPreUpdateRenderPost`, `RenderTrueUnderLimitReturnsRunning`, `FrameLimitReachedWhenCounterHitsExitAfterFrames`, `RenderFalseWithShutdownIsWindowClosed`, `RenderFalseWithDeviceLostIsDeviceLostUnrecoverable`, `RenderFalseWithNeitherIsRenderError`). (The exe path may differ — locate it under `build/` if the above path misses; check where `test_fail_scenario_sweep.exe` lands and mirror it.)

- [ ] **Step 8: Commit.**
```bash
git add VIXEN/application/main/include/VulkanApplicationBase.h \
        VIXEN/application/main/source/VulkanApplicationBase.cpp \
        VIXEN/application/main/include/VulkanGraphApplication.h \
        VIXEN/application/main/source/VulkanGraphApplication.cpp \
        VIXEN/application/main/tests/test_app_run_tick.cpp \
        VIXEN/application/main/tests/test_app_run_tick.cmake \
        VIXEN/application/main/CMakeLists.txt
git commit -m "feat(app): canonical Tick()/Run() on VulkanApplicationBase + offline classification test"
```

---

## Task 2: (folded into Task 1) — no separate task

> The classification test and the methods are one testable unit; Task 1 delivers both. This heading is intentionally a no-op so the Milestone Map's "Tasks 1–2" reads cleanly; M1 = Task 1.

---

## Task 3: `FrameTimer` helper + wire into `Run()` gated by `enableFrameTimer`

**Files:**
- Modify: `VIXEN/application/main/source/VulkanApplicationBase.cpp` (add file-local `FrameTimer`; call it in `Run()`)
- Modify: `VIXEN/application/main/include/VulkanApplicationBase.h` (only if `FrameTimer` needs to be a member type — it does NOT; keep it file-local in the .cpp)

**Interfaces:**
- Consumes: `RunOptions::enableFrameTimer` (Task 1), `mainLogger` (base).
- Produces: identical `[FrameTimer]` log lines to today's standalone main (avg/p99/FPS every 120 frames + outlier lines), now emitted from inside `Run()`.

- [ ] **Step 1: Implement `FrameTimer` as a file-local helper in `VulkanApplicationBase.cpp`** (relocate the standalone main's `:100–145` logic verbatim in behavior). Add near the top of the .cpp (after includes), inside an anonymous namespace:

```cpp
#include <array>
#include <algorithm>
#include <chrono>
#include <cstdio>

namespace {
// Encapsulates the standalone app's rolling CPU frame-time instrumentation (relocated from
// application/main/source/main.cpp so it isn't copy-pasted per entry point). Behavior-identical:
// avg/p99/FPS summary every 120 frames + a per-frame OUTLIER line when a frame costs >3x the prior
// window's median AND >5ms. Enabled only when RunOptions.enableFrameTimer is set.
class FrameTimer {
public:
    // Call once per rendered frame. frameCounter is the post-increment count (1-based).
    void Record(uint64_t frameCounter, Logger* logger) {
        const auto now = std::chrono::steady_clock::now();
        const double thisFrameMs = std::chrono::duration<double, std::milli>(now - lastFrameStart_).count();
        frameTimesMs_[(frameCounter - 1) % kFrameWindow] = thisFrameMs;
        lastFrameStart_ = now;

        if (lastWindowMedian_ > 0.0 && thisFrameMs > 3.0 * lastWindowMedian_ && thisFrameMs > 5.0 && logger) {
            char obuf[128];
            std::snprintf(obuf, sizeof(obuf), "[FrameTimer] OUTLIER frame %llu: %.3f ms",
                          static_cast<unsigned long long>(frameCounter), thisFrameMs);
            logger->Info(obuf);
        }
        if (frameCounter % kFrameWindow == 0 && logger) {
            std::array<double, kFrameWindow> sorted = frameTimesMs_;
            std::sort(sorted.begin(), sorted.end());
            double sum = 0.0;
            for (double v : sorted) sum += v;
            const double avg = sum / static_cast<double>(kFrameWindow);
            const double p99 = sorted[(kFrameWindow * 99) / 100];
            const double median = sorted[kFrameWindow / 2];
            char buf[160];
            std::snprintf(buf, sizeof(buf),
                          "[FrameTimer] frames %llu-%llu: avg %.3f ms (%.1f FPS) | p99 %.3f ms",
                          static_cast<unsigned long long>(frameCounter - kFrameWindow),
                          static_cast<unsigned long long>(frameCounter),
                          avg, avg > 0.0 ? 1000.0 / avg : 0.0, p99);
            logger->Info(buf);
            lastWindowMedian_ = median;
        }
    }
private:
    static constexpr size_t kFrameWindow = 120;
    std::array<double, kFrameWindow> frameTimesMs_{};
    std::chrono::steady_clock::time_point lastFrameStart_ = std::chrono::steady_clock::now();
    double lastWindowMedian_ = 0.0;
};
}  // namespace
```

- [ ] **Step 2: Wire it into `Run()`.** Replace the `while` block placeholder from Task 1 Step 5 with:

```cpp
        FrameTimer frameTimer;
        TickStatus st = TickStatus::Running;
        while ((st = Tick()) == TickStatus::Running) {
            if (opts.enableFrameTimer) frameTimer.Record(frameCounter_, mainLogger.get());
        }
```

(`frameCounter_` is the base member Tick() increments — it is post-increment / 1-based here, matching the old `++frameCounter` placement in main.cpp.)

- [ ] **Step 3: Build the app to confirm it compiles** (no dedicated test — the frame-timer output is cosmetic logging; parity is proven by the Task 4 standalone run showing `[FrameTimer]` lines).
```
cmd.exe /c "cd /d C:\cpp\VBVS--VIXEN\VIXEN && cmake --build build --config Debug --target VIXEN"
```
Expected: links clean. (`VIXEN` is the standalone app exe target; confirm the name in `application/main/CMakeLists.txt` — it may be `VIXEN` or `VixenApp`; use the exe target that produces the standalone binary.)

- [ ] **Step 4: Commit.**
```bash
git add VIXEN/application/main/source/VulkanApplicationBase.cpp
git commit -m "feat(app): FrameTimer helper in Run() gated by RunOptions.enableFrameTimer (relocated from main.cpp)"
```

---

## Task 4: Collapse both mains onto `app->Run(opts)`

**Files:**
- Modify: `VIXEN/application/main/source/main.cpp` (replace `:91–156` loop+cleanup with a Run() call; keep the outer try only if still needed — Run() has its own, so remove the redundant outer try/catch)
- Modify: `VIXEN/application/editor/source/main.cpp` (replace `:91–105`-ish loop with a Run() call)

**Interfaces:**
- Consumes: `VulkanApplicationBase::Run(RunOptions)` (Task 1), `RunOptions{exitAfterFrames,enableFrameTimer}` (Task 1), `FrameTimer` via `enableFrameTimer` (Task 3).

- [ ] **Step 1: Rewrite the standalone `main.cpp` render section.** Replace the block from `Initialize()`/`Prepare()`/loop/`DeInitialize()` (currently `:78–156`, all inside the `:72` try) with:

```cpp
        auto app = std::make_unique<VulkanGraphApplication>();

        uint64_t exitAfterFrames = 0;
        if (const char* env = std::getenv("VIXEN_EXIT_AFTER_FRAMES")) {
            exitAfterFrames = static_cast<uint64_t>(std::strtoull(env, nullptr, 10));
        }

        // Engine-owned loop: Initialize -> Prepare -> loop -> DeInitialize, with the standalone
        // app's frame-timer instrumentation enabled. All lifecycle + try/catch now lives in Run().
        return app->Run({ .exitAfterFrames = exitAfterFrames, .enableFrameTimer = true });
```

Remove the now-redundant outer `try{...}catch(...)` wrapper at `:72`/`:158–165` (Run() owns the exception boundary). Keep the pre-app logger setup and the final "Exiting normally" log only if it still makes sense; simplest is to `return app->Run(...)` directly. Delete the now-unused `#include`s that only the frame-timer used (`<array>`, `<algorithm>`, `<chrono>`) **only if** nothing else in the file needs them — check before deleting.

- [ ] **Step 2: Rewrite the editor `main.cpp` render section.** Replace its `Initialize()`/`Prepare()`/loop with (preserving the editor-specific `LoadDocument()` before the run):

```cpp
        auto app = std::make_unique<EditorApplication>(documentPath);
        app->LoadDocument();   // editor-specific: must run before Initialize()/Prepare()

        uint64_t exitAfterFrames = 0;
        if (const char* env = std::getenv("VIXEN_EXIT_AFTER_FRAMES")) {
            exitAfterFrames = static_cast<uint64_t>(std::strtoull(env, nullptr, 10));
        }
        return app->Run({ .exitAfterFrames = exitAfterFrames });   // no frame-timer for the editor
```

Remove the editor main's own `try/catch` loop wrapper (Run() owns it). Keep any editor-specific pre-`Initialize()` setup (document-path parsing, logger) unchanged.

> If either main constructs the app as a `VulkanApplicationBase*` view (`appObj`) for the `Update()/Render()` calls, that view is no longer needed — call `Run()` on the concrete `app` (or the base pointer; both work since `Run()` is on the base). Keep whichever compiles cleanly.

- [ ] **Step 3: Build both exes.**
```
cmd.exe /c "cd /d C:\cpp\VBVS--VIXEN\VIXEN && cmake --build build --config Debug --target VIXEN"
cmd.exe /c "cd /d C:\cpp\VBVS--VIXEN\VIXEN && cmake --build build --config Debug --target vixen_editor"
```
Expected: both link clean. (Confirm exe target names in the two CMakeLists.)

- [ ] **Step 4: Standalone parity gate (real GPU).** Run the standalone app for a bounded number of frames and confirm it renders + exits clean through `Run()`:
```
cmd.exe /c "cd /d C:\cpp\VBVS--VIXEN\VIXEN && set VIXEN_EXIT_AFTER_FRAMES=120&& build\application\main\Debug\VIXEN.exe"
```
Expected: process exits 0; log contains `[FrameTimer] frames 0-120: avg ...` (frame-timer still works) AND `[Run] Render loop exited: frame limit reached`. Watch the run on a foreground interval (it's ~seconds for 120 frames on this GPU); do not blind-wait. (Adjust the exe path to wherever the standalone binary lands.)

- [ ] **Step 5: Commit.**
```bash
git add VIXEN/application/main/source/main.cpp VIXEN/application/editor/source/main.cpp
git commit -m "refactor(app): collapse standalone + editor mains onto app->Run(opts)"
```

---

## Task 5: Move the editor scripted-action injector from `Update()` into `PreTick()`

**Files:**
- Modify: `VIXEN/application/editor/include/EditorApplication.h` (declare `void PreTick() override;`)
- Modify: `VIXEN/application/editor/source/EditorApplication.cpp` (move the injector; leave capture dumps + interactive input + re-flatten tail in `Update()`)

**Interfaces:**
- Consumes: `VulkanApplicationBase::PreTick()` virtual (Task 1). Members `scriptedActions_`, `captureFrames_`, `captureDir_`, `scriptParsed_`, `updateTick_` (existing).

**What moves vs. stays (critical — the ordering-parity contract, design §6):**
- **Moves to `PreTick()`:** the one-time env parse (`EditorApplication.cpp:314–325`) AND the scripted-action injector loop (`:331–338`). Rationale: `PreTick()` runs immediately before `Update()`; the injector calls `ToggleLayer`/`rt_.Undo`/`rt_.Redo` which set `dirty_`, and `Update()`'s existing `if(dirty_)` tail (`:378–383`) then re-flattens **the same tick** — identical to today where the injector ran at the top of `Update()` before that tail.
- **Stays in `Update()`:** the interactive input path (UI-click drain, Save-key, Ctrl+Z/Y — `:341–372`), the dirty re-flatten tail (`:378–383`), the **capture dumps** (`:391–398`), and `++updateTick_` (`:409`). Capture MUST stay after the re-flatten tail and read `updateTick_`, so it cannot move to `PreTick()`.
- **`updateTick_` semantics unchanged:** it is still incremented at the END of `Update()` (`:409`). `PreTick()` reads `updateTick_` for the injector's `action.frame != updateTick_` check — and since `PreTick()` runs before `Update()` within the same `Tick()`, and `updateTick_` only advances at the end of `Update()`, the injector in `PreTick()` sees the SAME `updateTick_` value it saw when it lived at the top of `Update()`. Frame N still means frame N. This is the exact invariant the byte-exact gate verifies.

- [ ] **Step 1: Declare the override.** In `EditorApplication.h`, next to `void Update() override;` (`:36`), add:

```cpp
    void PreTick() override;   // graph.Run(): scripted-action injector runs here, before Update()
```

- [ ] **Step 2: Add `PreTick()` in `EditorApplication.cpp`** (place it just above `EditorApplication::Update()` at `:299`). Move the env-parse + injector out of `Update()` into it:

```cpp
void EditorApplication::PreTick() {
    // graph.Run() consolidation: the scripted-action injector runs in PreTick() (before Update())
    // instead of at the top of Update(). Behavior-identical: PreTick() is called by
    // VulkanApplicationBase::Tick() immediately before Update(), and updateTick_ only advances at
    // the END of Update() -- so the injector sees the same updateTick_ it saw when it lived inside
    // Update(), and the dirty_ it sets is re-flattened by Update()'s existing dirty tail the same
    // tick. Own try/catch (mirrors Update()'s) so a malformed script never throws across the tick.
    try {
        // One-time env parse (unset envs -> empty vectors -> every check below is a no-op, so the
        // interactive editor is unaffected).
        if (!scriptParsed_) {
            scriptParsed_ = true;
            if (const char* scriptEnv = std::getenv("VIXEN_EDITOR_SCRIPT")) {
                scriptedActions_ = ParseEditorScript(scriptEnv, logger_.get());
            }
            if (const char* captureEnv = std::getenv("VIXEN_EDITOR_CAPTURE_FRAMES")) {
                captureFrames_ = ParseCaptureFrames(captureEnv, logger_.get());
            }
            if (const char* dirEnv = std::getenv("VIXEN_EDITOR_CAPTURE_DIR")) {
                captureDir_ = dirEnv;
            }
        }

        // Inject any scripted action due this tick through the SAME methods the interactive input
        // path calls (ToggleLayer / rt_.Undo / rt_.Redo) -- exercising the real
        // click-equivalent -> ActionStack -> re-flatten -> undo dispatch, not a shortcut.
        for (const auto& action : scriptedActions_) {
            if (action.frame != updateTick_) continue;
            switch (action.kind) {
                case ScriptedAction::Kind::Toggle: ToggleLayer(action.layerIndex); break;
                case ScriptedAction::Kind::Undo:   rt_.Undo(); break;
                case ScriptedAction::Kind::Redo:   rt_.Redo(); break;
            }
        }
    } catch (const std::exception& e) {
        lastEditorError_ = std::string("PreTick: ") + e.what();
        if (logger_) logger_->Error("[EditorApplication] PreTick exception: " + lastEditorError_);
    } catch (...) {
        lastEditorError_ = "PreTick: unknown exception";
        if (logger_) logger_->Error("[EditorApplication] PreTick unknown exception");
    }
}
```

- [ ] **Step 3: Delete the moved code from `Update()`.** Remove `EditorApplication.cpp:314–338` (the `if(!scriptParsed_){...}` block AND the `for (const auto& action : scriptedActions_)` loop) from `Update()`. Leave everything else in `Update()` exactly as-is: the base call (`:300`), the try/catch wrapper, the interactive input path (`:341–372`), the dirty re-flatten tail (`:378–383`), the capture dumps (`:391–398`), and `++updateTick_` (`:409`). The `Update()` try/catch now wraps a smaller body — that's fine.

> After the edit, `Update()`'s try block starts with the interactive input path (UI-click drain), NOT the env parse. Verify no dangling reference to `scriptParsed_`/`scriptedActions_` remains in `Update()`.

- [ ] **Step 4: Build the editor exe + the windowed gate test.**
```
cmd.exe /c "cd /d C:\cpp\VBVS--VIXEN\VIXEN && cmake --build build --config Debug --target vixen_editor"
cmd.exe /c "cd /d C:\cpp\VBVS--VIXEN\VIXEN && cmake --build build --config Debug --target test_editor_toggle_undo_capture"
```
Expected: both link clean.

- [ ] **Step 5: Regenerate the capture PNGs via the unattended editor run, then run the byte-exact gate.**
```
cmd.exe /c "cd /d C:\cpp\VBVS--VIXEN\VIXEN && VIXEN\temp\run_editor_script.bat"
cmd.exe /c "cd /d C:\cpp\VBVS--VIXEN\VIXEN && build\libraries\RenderGraph\tests\Debug\test_editor_toggle_undo_capture.exe --gtest_brief=1"
```
Expected: `run_editor_script.bat` exits 0 at frame 120, regenerating `temp/editor_capture_{5,45,75,105}.png`; the gate then PASSES 1/1 — `boreDiffPixels(png5,png45) > 4`, `png75==png5` byte-exact, `png105==png45` byte-exact. **This is the authoritative proof the injector move is behavior-neutral** (design §6). If png75!=png5 or png105!=png45, the ordering shifted — do NOT relax the gate; fix the `updateTick_`/`PreTick` ordering. Watch the ~seconds run on a foreground interval; delete the 4 stale PNGs first so a stale pass can't mask a regression. (Confirm the gate exe path; mirror where it built.)

- [ ] **Step 6: Commit.**
```bash
git add VIXEN/application/editor/include/EditorApplication.h \
        VIXEN/application/editor/source/EditorApplication.cpp
git commit -m "refactor(editor): move scripted-action injector from Update() to PreTick() (byte-exact gate green)"
```

---

## Task 6: Docs + known-issues + close-out

**Files:**
- Modify: `VIXEN/Vixen-Docs/01-Architecture/GraphRun-Consolidation-Plan-2026-07.md` (Progress Log — done per-milestone during the run)
- Modify: `VIXEN/Vixen-Docs/04-Development/Known-Issues.md` (only if the run surfaced a new known issue; otherwise skip)
- Modify: `VIXEN/application/editor/source/EditorApplication.cpp` (only if a stale comment referencing the injector's old location in `Update()` remains — fix prose to point at `PreTick()`)

- [ ] **Step 1: Sweep for stale comments.** Grep the editor + mains for comments that still describe the old hand-rolled loop or the injector's old home:
```
cmd.exe /c "cd /d C:\cpp\VBVS--VIXEN && findstr /s /n /i \"while(isWindowOpen) hand-rolled injector.*Update\" VIXEN\application\*.cpp"
```
Fix any comment prose that now misdescribes the code (e.g. a `main.cpp` comment describing the render loop it no longer contains; an `EditorApplication` comment saying the injector is "at the top of Update()"). Prose-only — no behavior change.

- [ ] **Step 2: Full offline suite green.** Run the existing offline tests to confirm no regression:
```
cmd.exe /c "cd /d C:\cpp\VBVS--VIXEN\VIXEN && ctest --test-dir build -C Debug --output-on-failure -R "app_run_tick|editor_toggle_undo_capture|appflow""
```
Expected: all green. (If `ctest` discovers nothing — KI-014, `enable_testing()` ordering — run the gtest exes directly per CLAUDE.md; list them and run each.)

- [ ] **Step 3: Update the Progress Log** in this plan doc (append the M1/M2/M3 DONE entries with commit shorts + validator/gate results + date).

- [ ] **Step 4: Commit.**
```bash
git add VIXEN/Vixen-Docs/01-Architecture/GraphRun-Consolidation-Plan-2026-07.md
git commit -m "docs(graphrun): consolidation complete — Run()/Tick() canonical, mains collapsed, gates green"
```

---

## Progress Log

_(Appended per milestone during execution.)_

- **M1 (Task 1): DONE** · commit `1e696716` · offline `test_app_run_tick` 6/6 PASS (Opus validator independently rebuilt + re-ran) · Opus validator APPROVED · 2026-07-06. Note: surfaced + root-cause-fixed a pre-existing bug — `VulkanInstance::instance` was never initialized to `VK_NULL_HANDLE`, so the first destruct-without-`CreateInstance()` path (the offline stub app) crashed in `DestroyInstance()`; fixed with a member-init (authorized Task-1 file-set expansion). Two sound CMake corrections: `GTest::gtest_main` alone (not gtest+gtest_main) + a `BUILD_TESTS`/`TARGET` guard, matching repo convention.
- **M2 (Tasks 3–4): DONE** · commits `763b020c` (FrameTimer) + `d5b07047` (collapse mains) · standalone parity gate on real GPU (D3D12/dzn): exit 0, `[FrameTimer] frames 0-120` + `[Run] Render loop exited: frame limit reached` each once; offline 6/6 still green; both exes link clean · Opus validator independently re-ran the parity gate + traced the fix · Opus validator APPROVED · 2026-07-06. Note: the plan's Task-3 frame-timer wiring had an off-by-one — recording `Record()` INSIDE `while(Tick()==Running){...}` skips the terminal tick, so an exact `EXIT_AFTER_FRAMES=120` run dropped the `frames 0-120` window summary (`Record()` fires at `frameCounter%120==0`, exactly the FrameLimitReached tick). Fixed with `do{ st=Tick(); if(enableFrameTimer)Record(); }while(st==Running)` (records after every executed tick, matching old main.cpp's unconditional-per-iteration ordering); found live on the parity gate, validator-confirmed faithful, no over/under-count. Editor's real `bool LoadDocument(const std::string&)` + bool-check preserved before Run() (plan sketch's parameterless `LoadDocument()` was a simplification).
- **M3 (Tasks 5–6): DONE** · commit `<pending>` · editor byte-exact windowed gate `test_editor_toggle_undo_capture` 1/1 PASS on real GPU (D3D12/dzn): `boreDiffPixels(png5,png45)=6` (>4 threshold), `png75==png5` and `png105==png45` both byte-exact (`EXPECT_EQ` passed silently under `--gtest_brief=1`) — proves the injector→`PreTick()` move is behavior-neutral (design §6) · `run_editor_script.bat` exited 0 at frame 120, regenerating all 4 capture PNGs fresh (stale PNGs deleted first) · offline: `test_app_run_tick` still 6/6, all 7 AppFlow library tests green (27/27: golden 4, action_stack 4, flow_state_machine 3, binding_store 3, appflow_loader 3, layer_controller 4, snapshot_undo 6) · both `vixen_editor.exe` and `test_editor_toggle_undo_capture.exe` link clean · 2026-07-06. Moved to `PreTick()`: one-time env parse + scripted-action injector loop only. Left in `Update()`: interactive input (UI-click drain, Save-key, Ctrl+Z/Y), dirty re-flatten tail, capture dumps, `++updateTick_`. Deviation from plan: `temp/run_editor_script.bat` (pre-existing, git-tracked from Inc-2b) had a hardcoded `cd` to a different worktree (`appflow-inc2b`) baked in from where it was authored — fixed to `cd /d %~dp0..` (relative to the script's own location) so it works in any worktree; also swept its stale "main.cpp: `Update(); Render();`" comment to describe `VulkanApplicationBase::Tick()`'s `PreTick→Update→Render→PostTick` order. Also swept an identical stale comment in `EditorApplication.cpp`'s capture-frame-0 note.

---

## Self-review notes (author)

- **Spec coverage:** D1 (base owns Run/Tick) → Task 1. D2 (virtual PreTick/PostTick) → Task 1 + Task 5. D3 (VIXEN-only, rich TickStatus, undertow untouched) → Global Constraints + Task 1 enum. D4 (exact parity, richer status) → Task 1 `Tick()` reads Render()'s bool, never re-implements policy; §Testing gates. D5 (injector→PreTick, input stays) → Task 5 with the explicit moves/stays list. FrameTimer relocation → Task 3. Standalone parity → Task 4 Step 4. Editor byte-exact gate → Task 5 Step 5. Offline classification test → Task 1. Undertow untouched → Global Constraints.
- **Type consistency:** `TickStatus`{Running,WindowClosed,FrameLimitReached,RenderError,DeviceLostUnrecoverable}, `RunOptions`{exitAfterFrames:uint64_t, enableFrameTimer:bool}, `Tick()→TickStatus`, `Run(const RunOptions&)→int`, `PreTick()/PostTick()→void`, `IsShutdownRequested()/IsDeviceLostState()→bool` — used identically in Tasks 1–5.
- **No placeholders:** every code step carries full code; every run step has an exact command + expected output. The one intentional no-op is "Task 2" (folded into Task 1) — labeled as such, not a gap.
