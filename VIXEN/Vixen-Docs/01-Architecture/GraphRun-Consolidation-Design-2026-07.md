# graph.Run() Render-Loop Consolidation — Design

**Status (reverified 2026-09-01):** COMPLETE — `Run()`/`Tick()` consolidation shipped through M1–M3
(`2ab4c534`). Increment of the AppFlow framework program (design §7d — the render-loop-lifecycle
sibling of the app-flow-state consolidation). VIXEN-only.

**Goal (one sentence):** Give VIXEN a canonical engine-owned `Run()` and host-owned `Tick()` on the application base class so the three dispatch entry points stop hand-rolling the render loop, without changing any rendering behavior.

---

## 1. Motivation & context

`RenderGraph::RenderFrame()` is the per-frame tick, but the *loop* around it — `Update()` → `Render()` (which internally does `glfwPollEvents` → `RenderFrame` → `VK_ERROR_DEVICE_LOST` recovery) → shutdown/frame-limit check — is hand-rolled and duplicated across three dispatch entry points:

- `VIXEN/application/main/source/main.cpp` (standalone app; has frame-timer instrumentation + `VIXEN_EXIT_AFTER_FRAMES`),
- `VIXEN/application/editor/source/main.cpp` (editor; `VIXEN_EXIT_AFTER_FRAMES`, no frame-timer),
- `/home/liory/Github/undertow/vixen/app/src/main.cpp` (undertow's native host loop — a **separate repo**; rich per-frame sim prologue; no frame-limit, no explicit device-loss check).

There is no canonical lifecycle owner. Every entry point re-implements the same plumbing and can get it subtly wrong.

**Prior grounding.** This was scoped in `Architecture-Review-Game-Renderer-2026-06-12.md`: "stable engine facade with **host-owned frame loop**" (readiness), "**inverted frame-loop control**" (target-state), and the named hard liability "**loop ticking dead in the `RenderFrame()` path**". The AppFlow design §7d folded it into this program as its own increment, planned after the Inc-1 walking skeleton.

**Ground-truth findings that shaped this design** (from a code-map done during brainstorming; correcting the §7d sketch):

1. **The loop lives entirely in each `main.cpp`; the app class has no loop.** `VulkanApplicationBase`/`VulkanGraphApplication` provide the per-tick methods (`Update`, `Render`) + one-time setup/teardown (`Initialize`, `Prepare`, `DeInitialize`) but no loop method.
2. **`RenderGraph` has no `Run()`/`Tick()`** — only `RenderFrame()` (+ `UpdateTime`, `ProcessEvents`, `RecompileDirtyNodes`). It is a pure per-frame node executor with no window/poll/shutdown concept.
3. **Device-loss policy already lives in `VulkanGraphApplication::Render()`** (`.cpp:281–295`): on `RenderFrame()` returning `VK_ERROR_DEVICE_LOST` it calls `renderGraph->RecoverFromDeviceLoss()`; success → keep looping (`return true`), failure → set `lastError_` + `return false`. **All three loops already inherit this transparently** through the returned `bool`; none of the three mains references `DEVICE_LOST`.
4. **`glfwPollEvents()` already lives inside `Render()`** (`.cpp:273`), not in any `main.cpp`.
5. The stop signal is the private `bool shutdownRequested` (set by the `WindowCloseEvent` handler), read by `Render()` at `.cpp:262`. There is no public `ShouldClose()`/`isWindowOpen()`.

Because device-loss policy and `glfwPollEvents` are **already centralized in `Render()`**, what is actually duplicated across the mains is thin: the `while(isWindowOpen){Update();Render();++frame;limit-check;}` shape, the `try/catch`, `Initialize/Prepare/DeInitialize`, and the frame-limit env var. The consolidation seam is therefore the **app base class**, not `RenderGraph`.

---

## 2. Decisions (locked during brainstorming)

| # | Decision | Choice |
|---|----------|--------|
| D1 | **Loop owner** | `VulkanApplicationBase` (the abstract base both mains + the undertow host drive). NOT `RenderGraph` (no window/poll/shutdown concept) and NOT a new `EngineContext` (would duplicate what the app base already is). |
| D2 | **Hook seam** | Virtual `PreTick()` / `PostTick()` (default no-op) around the Update/Render core. Hosts subclass and override — matches the existing override pattern (`EditorApplication` already overrides `Update()`). No `std::function` indirection. |
| D3 | **Scope** | **VIXEN-only.** Add `Run()`/`Tick()`/`PreTick()`/`PostTick()` + `TickStatus` + `RunOptions` to the app base; collapse the two VIXEN mains onto `app->Run(opts)`; move the editor's scripted-action injector to `PreTick()`. **Leave the undertow repo untouched** — but make `TickStatus` rich enough that undertow can adopt `Tick()`+`PreTick()` later on its own schedule. |
| D4 | **Behavior parity** | **Exact parity, richer status only.** `Tick()` drives the identical sequence today's loop does (same poll→Update→Render→device-loss policy, same give-up conditions) — a pure refactor, byte-identical rendering. The ONLY addition is that `Tick()` returns a descriptive `TickStatus` and `Run()` logs the stop reason. No error-model overhaul (the architecture-review's fatal-error rework stays a separate future increment). |
| D5 | **Injector move** | The editor's `VIXEN_EDITOR_SCRIPT` scripted-action injector + capture-frame dumps move from `EditorApplication::Update()` into an `EditorApplication::PreTick()` override. Its Ctrl+Z/Y edge-detected input handling **stays in `Update()`**. The windowed gate must pass byte-identically to prove the move is behavior-neutral. |

---

## 3. Architecture

New surface on `VulkanApplicationBase` (`VIXEN/application/main/include/VulkanApplicationBase.h`):

```cpp
// One of the ways the loop decided to stop (or that it should continue).
enum class TickStatus {
    Running,                  // keep looping
    WindowClosed,             // shutdownRequested set (user X / WindowCloseEvent); clean exit
    FrameLimitReached,        // RunOptions.exitAfterFrames hit; clean exit
    RenderError,              // Render() returned false for a non-recoverable frame failure
    DeviceLostUnrecoverable,  // device lost and RecoverFromDeviceLoss() gave up
};

struct RunOptions {
    uint64_t exitAfterFrames = 0;    // 0 = unlimited. Absorbs VIXEN_EXIT_AFTER_FRAMES.
    bool     enableFrameTimer = false;  // opt-in p99/FPS rolling-window logging (standalone main only)
};

class VulkanApplicationBase {
public:
    // ... existing pure-virtual Prepare()/Update()/Render(), Initialize()/DeInitialize(),
    //     IsPrepared()/GetLastError() ...

    // NEW — host hook seam (default no-op; hosts override to inject a per-frame prologue/epilogue).
    virtual void PreTick()  {}
    virtual void PostTick() {}

    // NEW — one loop iteration: PreTick -> Update -> Render -> PostTick -> classify. Non-virtual.
    TickStatus Tick();

    // NEW — engine-owned loop: Initialize -> Prepare -> guard -> while(Tick()==Running) -> DeInitialize.
    //       Returns a process exit code (0 clean, -1 on RenderError/DeviceLostUnrecoverable/prepare-fail).
    int Run(const RunOptions& opts = {});

protected:
    uint64_t frameCounter_    = 0;  // NEW — moved out of the mains; incremented once per Tick().
    uint64_t exitAfterFrames_ = 0;  // NEW — set by Run() from RunOptions; 0 = unlimited. Read by Tick().
};
```

`RenderGraph` is **not** modified — no `Run`/`Tick` pushed down; it stays the per-frame node executor.

### 3.1 `Tick()` — one iteration (behavior-identical to today's loop body)

```cpp
TickStatus VulkanApplicationBase::Tick() {
    PreTick();                       // D2/D5: host prologue (default no-op)
    Update();                        // existing virtual: time, event pump, dirty recompile
    const bool ok = Render();        // existing virtual: glfwPollEvents + RenderFrame + device-loss policy
    PostTick();                      // host epilogue (default no-op)
    ++frameCounter_;

    if (ok) {
        if (exitAfterFrames_ > 0 && frameCounter_ >= exitAfterFrames_) {
            return TickStatus::FrameLimitReached;
        }
        return TickStatus::Running;
    }
    // ok == false: name WHY, by reading state the base already holds. (No new policy.)
    if (IsShutdownRequested())            return TickStatus::WindowClosed;
    if (renderGraph_ && renderGraph_->IsDeviceLost())
                                          return TickStatus::DeviceLostUnrecoverable;
    return TickStatus::RenderError;
}
```

- `exitAfterFrames_` is a member set by `Run()` from `RunOptions` (so `Tick()` needs no arg and a host calling `Tick()` directly gets unlimited unless it sets the member — matching the undertow host, which has no frame limit today).
- `IsShutdownRequested()` is a small **new protected accessor** exposing the existing private `shutdownRequested` flag (needed because `Tick()` on the base must read it to classify; the flag itself and its `WindowCloseEvent` handler are unchanged).
- **Ordering invariant (critical, D5):** `PreTick()` runs *before* `Update()`. The editor injector sets `dirty_` (via `ToggleLayer`/`Undo`/`Redo`); `Update()`'s tail re-flattens when `dirty_` is set — so "inject → same-frame re-flatten" is preserved exactly as when the injector lived at the top of `Update()`. See §6 for the `updateTick_` counter placement.

### 3.2 `Run()` — engine-owned loop

```cpp
int VulkanApplicationBase::Run(const RunOptions& opts) {
    exitAfterFrames_ = opts.exitAfterFrames;
    try {
        Initialize();
        Prepare();
        if (!IsPrepared()) {
            LogError("[Run] Prepare failed: " + GetLastError());
            DeInitialize();
            return -1;
        }
        TickStatus st = TickStatus::Running;
        FrameTimer timer;                        // only used if opts.enableFrameTimer
        while ((st = Tick()) == TickStatus::Running) {
            if (opts.enableFrameTimer) timer.Record();
        }
        LogRunStopReason(st);                    // human-readable line naming the TickStatus
        DeInitialize();
        return (st == TickStatus::RenderError ||
                st == TickStatus::DeviceLostUnrecoverable) ? -1 : 0;
    } catch (const std::exception& e) {
        LogError(std::string("[Run] Uncaught exception: ") + e.what());
        return -1;
    } catch (...) {
        LogError("[Run] Uncaught unknown exception");
        return -1;
    }
}
```

- The `try/catch` that each main wraps its loop in **moves here** — one place preserving the "never throw past the entry point" contract (the reason it exists: a C++ exception across the undertow C ABI is UB).
- `FrameTimer` is a small helper struct encapsulating the standalone main's existing rolling-window p99/FPS/outlier logging (relocated verbatim, gated by `opts.enableFrameTimer`). This keeps the instrumentation without copy-paste and without imposing it on the editor.

### 3.3 Entry points collapse

Standalone `application/main/source/main.cpp`:
```cpp
auto app = std::make_unique<VulkanGraphApplication>();
return app->Run({ .exitAfterFrames = ParseEnvU64("VIXEN_EXIT_AFTER_FRAMES"),
                  .enableFrameTimer = true });
```

Editor `application/editor/source/main.cpp`:
```cpp
auto app = std::make_unique<EditorApplication>(documentPath);
app->LoadDocument();                              // editor-specific pre-lifecycle step (unchanged)
return app->Run({ .exitAfterFrames = ParseEnvU64("VIXEN_EXIT_AFTER_FRAMES") });
```

The `LoadDocument()` step stays outside `Run()` because it is editor-specific and must run before `Initialize()/Prepare()` (as today). Everything from `Initialize()` onward is inside `Run()`.

---

## 4. Components & responsibilities

| Unit | File | Responsibility |
|------|------|----------------|
| `TickStatus`, `RunOptions` | `VulkanApplicationBase.h` | The vocabulary: why the loop stopped; how to configure a run. |
| `VulkanApplicationBase::Tick()` | `VulkanApplicationBase.cpp` (new TU or inline) | One iteration; classify outcome. Pure relocation of the loop *body* + status naming. |
| `VulkanApplicationBase::Run()` | same | The loop + lifecycle + single try/catch + stop-reason logging + exit code. |
| `PreTick()`/`PostTick()` | `VulkanApplicationBase.h` | Virtual no-op hooks. The host-injection seam. |
| `IsShutdownRequested()` | `VulkanGraphApplication.h` (protected) | New accessor over the existing `shutdownRequested` flag so `Tick()` can classify `WindowClosed`. |
| `FrameTimer` | `VulkanApplicationBase.cpp` (file-local) or a small header | Encapsulates the standalone main's p99/FPS rolling-window instrumentation. Relocated verbatim. |
| `EditorApplication::PreTick()` | `application/editor/source/EditorApplication.cpp` | Override hosting the `VIXEN_EDITOR_SCRIPT` injector + capture-frame dumps (moved out of `Update()`). |
| standalone `main.cpp` | `application/main/source/main.cpp` | Collapses to `app->Run(opts)`. |
| editor `main.cpp` | `application/editor/source/main.cpp` | Collapses to `LoadDocument(); app->Run(opts)`. |

**Note on where `Tick()`/`Run()` are defined:** `VulkanApplicationBase` is currently header-only-ish abstract. If it has no `.cpp`, add `VulkanApplicationBase.cpp` under `application/main/source/`; both methods are non-virtual and defined once on the base (they only call the existing virtuals). This avoids duplicating the loop in `VulkanGraphApplication`.

---

## 5. Error handling & exit conditions

No error-model change (D4). The increment makes existing stop reasons legible; it does not alter any of them.

| Stop cause | Today | After | Exit code |
|---|---|---|---|
| Window close (`shutdownRequested`) | `Render()`→false; main can't tell why | `Tick()`→`WindowClosed`; `Run()` logs it | 0 |
| `VIXEN_EXIT_AFTER_FRAMES` | in-loop counter, duplicated in 2 mains | `Tick()`→`FrameLimitReached` (counter in base) | 0 |
| Frame-render failure | `Render()`→false | `Tick()`→`RenderError` | -1 |
| Device-loss give-up | `Render()`→false (via `Render()`'s policy) | `Tick()`→`DeviceLostUnrecoverable` | -1 |
| Device-loss **recovered** | `Render()`→true; loop continues | `Tick()`→`Running`; loop continues (**unchanged**) | — |
| Exception in loop body | each main's `try/catch` | one `try/catch` in `Run()` | -1 |
| `Prepare()` failure | main checks `IsPrepared()` | `Run()` checks `IsPrepared()` | -1 |

Invariants preserved verbatim:
- **Device-loss policy stays inside `Render()`** (`RecoverFromDeviceLoss`→give-up). `Tick()` never re-implements it — it only reads `Render()`'s `bool` + `renderGraph_->IsDeviceLost()` to *name* the outcome. KI-013's device-loss-recovery hard gate is untouched.
- **Recovered device-loss keeps the loop alive** — `Render()` returns `true` in that case, so `Tick()` returns `Running`. No change.
- **No throw past the entry point** — the `catch(...)` moves into `Run()`, which *is* the entry point for the two VIXEN mains. Same contract.

---

## 6. The ordering-parity risk (call it out explicitly)

Moving the editor injector from `Update()` to `PreTick()` is the one behavior-adjacent change. The risk is an off-by-one on the frame the script names — exactly the class hit in AppFlow Inc-2b M3.

- **Today:** the injector runs at the top of `EditorApplication::Update()`, before the `if(dirty_)` re-flatten tail, and reads `updateTick_`. `updateTick_` is incremented at the **end** of the `Update()` override (the Inc-2b fix).
- **After:** the injector runs in `PreTick()`, which `Tick()` calls immediately before `Update()`. For the injector to still see the same tick number on the same frame, the tick counter it reads must advance at the same logical point. **Plan mandate:** the injector's frame counter (`updateTick_`, or its successor) must be incremented at the **end of `Tick()`** (equivalently: not before `PreTick()`/`Update()` observe it), so frame N still means frame N. The plan uses `frameCounter_` on the base as the single source and has the editor injector read it, OR keeps `updateTick_` in the editor incremented at the end of `Update()` (which still runs every `Tick()`); either is acceptable as long as the windowed gate stays byte-identical.
- **Regression net:** the Inc-2b `test_editor_toggle_undo_capture` gate must pass with **byte-identical** PNGs at the same capture frames (5/45/75/105): undo==baseline and redo==toggle, exact. Any ordering shift changes which frame an action lands on and breaks the byte-exact assertion — so the gate *is* the proof the move is neutral.

---

## 7. Testing

1. **Editor windowed gate (authoritative regression net).** `test_editor_toggle_undo_capture` (Inc-2b) must pass **byte-identically** after the injector→`PreTick()` move + the `Run()` collapse: undo==baseline / redo==toggle byte-exact PNGs at frames 5/45/75/105 on the real GPU (D3D12/dzn). This proves both the ordering move (§6) and that `Run()`/`Tick()` drive the editor identically to the old loop.
2. **Standalone parity (real GPU).** Run the standalone app under `VIXEN_EXIT_AFTER_FRAMES=N` via `Run({.exitAfterFrames=N,.enableFrameTimer=true})`; confirm it renders and exits clean (exit 0, `FrameLimitReached` logged). Optionally capture a frame and compare to a pre-refactor capture for byte-parity.
3. **`Tick()` status classification (offline, no GPU).** A focused gtest driving a **stub** `VulkanApplicationBase` subclass (overriding the virtuals with canned outcomes) through each branch:
   - `Render()`→true, under limit → `Running`.
   - `Render()`→true, `frameCounter_ >= exitAfterFrames_` → `FrameLimitReached`.
   - `Render()`→false, shutdown flag set → `WindowClosed`.
   - `Render()`→false, `IsDeviceLost()` true → `DeviceLostUnrecoverable`.
   - `Render()`→false, neither → `RenderError`.
   - Verify `PreTick()`/`PostTick()` are each called exactly once per `Tick()`, in order PreTick→Update→Render→PostTick (record a call log in the stub).
   This is the one genuinely new *logic* to cover; it needs no Vulkan (the stub's `Render()` returns a canned bool and toggles canned flags).
4. **Full existing offline suite stays green.** No library-level API is removed; the change is additive on the base + a relocation in the editor + two thinned mains.

---

## 8. File structure

**Modify:**
- `VIXEN/application/main/include/VulkanApplicationBase.h` — add `TickStatus`, `RunOptions`, `PreTick()`/`PostTick()`, `Tick()`, `Run()`, `frameCounter_`.
- `VIXEN/application/main/source/VulkanApplicationBase.cpp` — **create if absent** — define `Tick()`/`Run()` + the `FrameTimer` helper.
- `VIXEN/application/main/include/VulkanGraphApplication.h` — add protected `IsShutdownRequested()` accessor.
- `VIXEN/application/main/source/main.cpp` — collapse loop to `app->Run(opts)`.
- `VIXEN/application/editor/include/EditorApplication.h` — declare `PreTick()` override.
- `VIXEN/application/editor/source/EditorApplication.cpp` — move injector + capture dumps from `Update()` into `PreTick()`.
- `VIXEN/application/editor/source/main.cpp` — collapse loop to `LoadDocument(); app->Run(opts)`.
- CMake: register the new `VulkanApplicationBase.cpp` (if created) in the app target's sources; register the new `Tick()` classification gtest. **CMake source/link registration must land in the same task that creates each file** (Inc-2 durable lesson — an offline syntax gate cannot catch a missing link).

**Create:**
- `VIXEN/application/main/source/VulkanApplicationBase.cpp` (if the base has no TU today).
- A gtest for `Tick()` status classification — location following the app-tests convention (or `libraries/RenderGraph/tests/` if the app has no test target; the plan confirms where app-level tests live and picks accordingly; if no app-level test target exists, put the stub test where it can link the app base without a GPU).

**Untouched (explicitly):** `RenderGraph` (all of it), `VulkanGraphApplication::Render()`'s device-loss policy, `RecoverFromDeviceLoss`, `WindowNode`/`WindowCloseEvent`, the undertow repo.

---

## 9. Global constraints

- **VIXEN-only.** Do not modify `/home/liory/Github/undertow` or the submodule pin. `TickStatus` must be expressive enough for undertow to adopt `Tick()`+`PreTick()` later, but that adoption is a separate, later, cross-repo increment.
- **Exact render parity.** No frame's rendered output may change. The two GPU gates (editor byte-exact PNGs; standalone renders+exits) are the parity proof.
- **No error-model change.** Do not touch the `RenderFrame()`-returns-`VK_SUCCESS` channel, the `exit()` sites, or the result-type generalization — those are a separate future increment (architecture-review §Process-fatal error model).
- **C++23, Vulkan 1.3, CMake/MSVC.** Follow existing app-class patterns; `Nodes use base NodeInstance::device` etc. remain in force where relevant (not central here).
- **KI-013 device-loss-recovery hard gate stays intact** — the policy is read, never re-implemented, by `Tick()`.

---

## 10. Increment boundary & roadmap position

This is the `graph.Run()` increment from AppFlow design §7d — sequenced after Inc-1/Inc-2/Inc-2b (all shipped to local main). It is orthogonal to the AppFlow state/action contract: a RenderGraph-adjacent app-lifecycle refactor. After this, the remaining AppFlow roadmap is Inc-3 (BindingStore selector-resolution + ModuleController), Inc-4 (PanelLayout), Inc-5+ (undertow migration). Undertow's *host-loop* adoption of `Tick()`/`PreTick()` is a natural follow-on to both this increment and the undertow migration, and can happen whenever undertow chooses.
