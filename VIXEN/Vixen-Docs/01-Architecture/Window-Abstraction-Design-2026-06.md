---
tags: [architecture, design-sketch, window, lifecycle, refactor]
created: 2026-06-13
status: implemented (phases 1-4, 2026-06-13); phase 5 (multi-window rendering) deferred
related: ["[[Architecture-Review-Game-Renderer-2026-06-12]]", "[[Overview]]"]
---

# Window Abstraction Refactor — Design

> **Direction approved 2026-06-13.** Keep window ownership **inside the graph** (a *proper* WindowNode),
> not an app-level subsystem. This doc is the agreed design; an implementation plan follows.

## 1. Motivation

The window-close **shutdown hang** fixed in `ab8cce17` was a *symptom*. The disease: **window lifecycle
is spread across four owners with no single source of truth** — `WindowNode` owns the `GLFWwindow*`, the
app caches a raw copy (`windowHandle`, captured before it exists → null, dangling after the node
destroys it), the render loop polls that copy, and the shutdown handshake pokes it after it's freed. The
three patches it took (capture-after-compile, exit-on-`shutdownRequested`, stop touching the dead window)
were local fixes. Centralizing ownership **into the graph** dissolves the whole class.

## 2. Why in-graph (two enabling facts)

An earlier draft proposed an app-level `WindowManager`. Rejected — it fights VIXEN's graph-centric
architecture. Two facts make the in-graph design clean instead:

1. **The graph is strictly main-threaded.** `RenderGraph.h`: *"All RenderGraph methods must be called
   from the same thread (main thread); event handlers run synchronously on main thread."* GLFW requires
   `glfwCreateWindow`/`glfwPollEvents` on the main thread — so they belong in a **node**, no threading
   conflict.
2. **Recompile is dirty-selective.** `RecompileDirtyNodes()` runs `Cleanup→Setup→Compile` **only for
   dirty nodes**; full `ExecuteCleanup()` is *"ONLY for application shutdown."* So a window can **persist
   across swapchain recompiles** — it just needs a lifecycle that doesn't tear down on recompile.

## 3. The load-bearing change: a cleanup *reason* (persistent resources, first-class)

Today `Cleanup()` is `final`/no-arg and `CleanupContext` carries no reason, so a node can't distinguish
**recompile-cleanup** (release transient/swapchain-following resources, keep persistent ones) from
**final teardown** (release everything). This is *why* the window gets destroyed at the wrong time.

**Add the signal to the lifecycle:**

```cpp
enum class CleanupReason { Recompile, FinalTeardown };
// NodeInstance::Cleanup(CleanupReason) -> CleanupImpl(ctx) with ctx.reason set
```

- `RecompileDirtyNodes()` calls `Cleanup(Recompile)`; `ExecuteCleanup()` calls `Cleanup(FinalTeardown)`.
- **Most nodes ignore it** (their `CleanupImpl` releases everything either way — they get re-Setup on
  recompile, which is correct). Only nodes owning expensive-to-recreate **persistent** resources read it.
- This is a *general* graph improvement: persistent-across-recompile resources become a first-class
  concept. The swapchain (FR-3) already wants the `Recompile` half (destroy+recreate per recompile); the
  window wants the inverse (survive recompile). Same mechanism serves both.

This is the architecturally pure fix — more invasive than a "never mark the window dirty" hack, but it
gives the graph a correct lifecycle instead of relying on a node never being dirtied.

## 4. Proper WindowNode

With the cleanup reason in place, `WindowNode` becomes the single, correct owner — all in-graph:

- **Create-once, persist, destroy-at-final-teardown.** Window + surface created idempotently in
  `Setup`/`Compile` (create only if absent). `CleanupImpl(ctx)`:
  `if (ctx.reason == FinalTeardown) { glfwDestroyWindow; destroy surface; }` — on `Recompile`, **keep
  them**. Robust whether or not the node is ever dirtied.
- **Owns its window state.** `shouldClose`/`isResizing`/extent live on the node (already do); a resize
  **updates the extent outputs and dirties the swapchain**, never recreates the window.
- **Publishes events** (already does): `WindowCloseEvent` / `WindowResizeEvent` / `WindowStateChangeEvent`.

## 5. Input pump (one place, multi-window-safe)

`glfwPollEvents` is **global** (drives every window's callbacks), so it must run **exactly once per
frame**, on the main thread, **even when rendering is paused** (minimized). Put it in a dedicated
**`InputPumpNode`** that executes first in the order, *before* the render-pause skip in `RenderFrame()`
— not in `WindowNode::Execute` (which would double-pump with N windows and could be skipped on pause).
WindowNodes register their per-window GLFW callbacks; the single pump drives them all.

## 6. The app stops owning the window

The app no longer reaches into the graph for a handle. Delete: `windowHandle`, the FR-6 type-discovery,
the line-273 `glfwWindowShouldClose` poll, the app-level `glfwPollEvents`, and `CompleteShutdown`'s
window-poke. The loop collapses to:

```cpp
while (!shutdownRequested) {        // set by the WindowCloseEvent the graph already publishes
    appObj->Update();               // ProcessEvents (incl WindowCloseEvent→shutdownRequested) + recompile
    appObj->RenderFrame();          // InputPumpNode pumps glfwPollEvents, then renders (or skips, paused)
}
```

The app *listens to a graph event* to stop its own loop — proper decoupling, no handle ownership. (The
subscription already exists from the AR#16/hang work.)

## 7. Multi-window

`N` `WindowNode`s — the graph already instances nodes by type (`instancesByType`); the vestigial
`slotIndex` becomes real. Each window node owns its window+surface → its own swapchain sub-graph; the one
`InputPumpNode` serves all. **Multi-window *rendering*** (N swapchains/present paths) is deferred — it
reshapes graph topology and intersects the **"Everything is swapchain-bound"** P0 theme [AR#28 / FR-5] —
until a concrete editor/multi-view use case. Phase 1 structures for N, runs 1.

## 8. Migration plan (graph stays green each step)

1. **Lifecycle reason.** Add `CleanupReason` to `Cleanup()`/`CleanupContext`; plumb `Recompile` from
   `RecompileDirtyNodes`, `FinalTeardown` from `ExecuteCleanup`. No behavior change yet (nodes ignore
   it). Gate: full build + all RenderGraph tests green.
2. **WindowNode lifecycle.** Idempotent create; `CleanupImpl` honors the reason (destroy only on
   `FinalTeardown`). Resize updates extent + dirties swapchain, never recreates the window.
3. **InputPumpNode.** Move `glfwPollEvents` from the app loop into a first-executing pump node.
4. **De-own the app.** Delete `windowHandle` + FR-6 discovery + line-273 poll + `CompleteShutdown`
   window-poke; loop runs on `shutdownRequested`. Gate: render + window-close → clean process exit
   (the hang's acceptance test), no regression.
5. **(Deferred) Multi-window rendering** — when a use case lands.

## 9. Acceptance — what this dissolves

- ✗ App caches a raw window handle → ✓ app only listens to graph events.
- ✗ Window torn down on graph recompile / at the wrong time → ✓ persists via `CleanupReason`; only the
  swapchain follows recompiles.
- ✗ Capture-timing null + dangling handle + poll-vs-event race + the hang → ✓ structurally impossible.
- ✗ FR-6 discovery + `CompleteShutdown` window-poke + app `glfwPollEvents` → ✓ deleted.
- ✓ Multi-window becomes a matter of adding `WindowNode` instances.
- ✓ The graph gains a proper persistent-resource lifecycle (reusable beyond windows).
