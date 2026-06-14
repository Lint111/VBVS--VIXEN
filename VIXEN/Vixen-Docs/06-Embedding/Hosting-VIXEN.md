---
title: Embedding VIXEN in a Host Application
aliases: [Hosting VIXEN, Embedding, find_package VIXEN, EngineContext embedding, AR#12]
tags: [implementation, embedding, sdk, engine-boundary, game-renderer, AR12]
created: 2026-06-14
status: active
---

# Embedding VIXEN in a Host Application

How an external program consumes VIXEN as a rendering engine: **`find_package(VIXEN)` →
construct an `EngineContext` → own the frame loop**. This is the engine-boundary path opened by
[AR#7] (instantiable [[#EngineContext]]) and [AR#2] (the consumable SDK), with no process-wide
singletons after [AR#8].

> [!summary] The shape in one breath
> VIXEN ships as a **fat `find_package(VIXEN)` SDK** of 14 static libs. A host links
> `Vixen::RenderGraph`, constructs a `Vixen::RenderGraph::EngineContext` (which stands up the
> registry, message bus, render graph, and calibration store with **no global state**), builds a
> node graph, and drives `Graph().RenderFrame()` from its own loop. The graph creates its **own**
> Vulkan instance + device via in-graph nodes — the host injects no device.

---

## Two consumption modes

| Mode | How | What you get | Use when |
|---|---|---|---|
| **Installed SDK** (this guide) | `find_package(VIXEN)` against an installed prefix | The **14 libraries** as `Vixen::*` imported targets (`Vixen::RenderGraph`, …). **Not** the `application` exe/lib. | The host is a separate build that wants a prebuilt VIXEN. |
| **Super-build** | `add_subdirectory(VIXEN)` | All targets directly, including the in-repo `VulkanGraphApplication` reference app. | The host vendors VIXEN's source (e.g. UNDERTOW's super-build). |

The SDK exports libraries, not the app. So an **installed-SDK host stands up the engine through
`EngineContext`** (in the exported `RenderGraph` lib), not through `VulkanGraphApplication` (which
is in the un-exported `application/` tree). `VulkanGraphApplication` remains the **in-repo
reference** for how to wire a graph and run the loop — read it, don't link it.

---

## Step 1 — Produce the SDK

The export is gated behind an option (default **OFF**, so the normal dev build is untouched) and
only fires for a **standalone** VIXEN build (not when VIXEN is `add_subdirectory`'d):

```bash
cmake -B build -DVIXEN_INSTALL_EXPORT=ON
cmake --install build --prefix /path/to/vixen-sdk
```

> [!warning] WSL ⇄ cmake.exe path trap
> `cmake.exe` is a **Windows** binary; a WSL `--prefix /tmp/sdk` lands at `C:\tmp\sdk`. Use a
> `/mnt/c/...` prefix for installs. See [[wsl-cmake-windows-paths]].

This produces, under the prefix: the 14 static libs, their headers, a unified `VixenTargets.cmake`
export set, and `VIXENConfig.cmake`. Machinery: `cmake/VixenInstall.cmake` +
`cmake/VIXENConfig.cmake.in`.

---

## Step 2 — Consume via `find_package(VIXEN)`

```cmake
cmake_minimum_required(VERSION 3.20)
project(my_host LANGUAGES CXX)

# Point CMake at the installed SDK prefix from Step 1.
list(APPEND CMAKE_PREFIX_PATH "/path/to/vixen-sdk")

find_package(VIXEN REQUIRED)

add_executable(my_host main.cpp)
target_compile_features(my_host PRIVATE cxx_std_23)
target_link_libraries(my_host PRIVATE Vixen::RenderGraph)
```

Linking `Vixen::RenderGraph` transitively pulls every VIXEN lib it depends on plus the **bundled**
vendored deps (glm, glfw, stb, VMA, magic_enum, nlohmann_json, miniz, rmlui_core, ProjectHash) —
the consumer does **not** `find_dependency()` those. Only genuinely-external deps are resolved from
the consumer's environment: **Threads, Vulkan, TBB**. (`gli`, `freetype`, and `gaia` ship their own
configs *inside* the SDK.)

> [!note] Gotchas baked into `VIXENConfig.cmake` (already handled — don't "fix")
> - VIXEN links some Vulkan SDK static libs by **bare name** + a `link_directories()`; the config
>   re-derives that dir from the consumer's resolved `Vulkan_LIBRARY`.
> - `gaia`'s upstream `gaia-config.cmake` is broken (`set_and_check(... "")`), so `VIXENConfig`
>   includes gaia's `-targets.cmake` directly instead of `find_dependency(gaia)`.
> - `freetype`/`gli` are forced to **CONFIG** mode so CMake's built-in `FindFreetype` doesn't pick
>   a system copy over the bundled one.

---

## Step 3 — Stand up the engine with `EngineContext`

`EngineContext` (`Core/EngineContext.h`) is the instantiable aggregate that replaced the former
`VulkanGraphApplication` singleton. Construct one from an `EngineConfig`; it builds — in the one
valid order — the `NodeTypeRegistry`, `MessageBus`, `RenderGraph`, and (optionally) the autonomous
`CalibrationStore`.

```cpp
#include <Core/EngineContext.h>
#include <Core/EngineConfig.h>
#include <Core/NodeTypeRegistry.h>
#include <Core/RenderGraph.h>
#include <Logger.h>

using namespace Vixen::RenderGraph;

EngineConfig cfg;
cfg.logger          = myLogger;          // Vixen::Log::Logger* (may be null)
cfg.calibrationDir  = "calibration";     // where task profiles persist
cfg.enableCalibration = true;            // stand up the autonomous CalibrationStore
cfg.mainCacher      = nullptr;           // null => EngineContext OWNS its own cacher (AR#8);
                                         // pass one only to share a cacher across contexts.

// The host supplies the node-type set — EngineContext registers nothing on its own, because the
// concrete node headers (and the graph topology) are host-specific. See VulkanGraphApplication's
// RegisterAllNodeTypes for the built-in set.
cfg.registerNodeTypes = [](NodeTypeRegistry& registry) {
    // registry.Register<InstanceNodeType>(); registry.Register<DeviceNodeType>(); ... etc.
    // (register the *NodeType classes, not the node instances)
};

EngineContext engine(cfg);   // no global state; construct as many as you need

NodeTypeRegistry&         registry = engine.Registry();
Vixen::EventBus::MessageBus& bus   = engine.Bus();
RenderGraph&              graph     = engine.Graph();
CalibrationStore*         calib     = engine.Calibration();   // null if enableCalibration == false
```

> [!important] No device injection
> The graph creates its **own** `VkInstance`/`VkDevice` via in-graph nodes (`InstanceNode →
> DeviceNode`), so `EngineContext` takes no device. You configure instance/device extensions and
> layers the same way `application/main/source/main.cpp` does (the global
> `instanceExtensionNames` / `deviceExtensionNames` / `layerNames` lists) before the graph compiles.

### Build your graph

`EngineContext` gives you the engine scaffolding; **you compose the graph**. Add node instances via
the registry and wire them with `TypedConnection`. This is the per-host part — the canonical
reference is `VulkanGraphApplication::BuildRenderGraph()` (the ~16-node compute voxel pipeline). The
embedding guide intentionally does not reproduce that wiring; copy/adapt the reference for your
scene, then `graph.Compile()`.

---

## Step 4 — Own the frame loop

The per-frame engine primitive is `RenderGraph::RenderFrame()` (returns a `VkResult`). The host
drives it from its own loop — VIXEN runs **no hidden internal loop**:

```cpp
// After the graph is built + compiled:
bool running = true;
while (running) {
    // ... host-side per-tick logic (input, simulation, marking nodes dirty) ...

    VkResult r = engine.Graph().RenderFrame();
    if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR) {
        // handle device-loss / out-of-date per the AR#1 error model
    }

    running = !hostWantsToQuit();
}
```

The in-repo reference loop is `main.cpp`: `app->Update(); running = app->Render();` — where
`Render()` wraps `RenderFrame()` and returns whether the window is still open. A super-build host
can drive `VulkanGraphApplication::Update()/Render()` directly; an SDK host drives
`Graph().RenderFrame()` as above.

---

## Step 5 — Shutdown

Publish an `ApplicationShuttingDownEvent` on the bus **before** the `EngineContext` is destroyed, so
the autonomous `CalibrationStore` flushes its profiles on the way down:

```cpp
#include <Message.h>   // EventBus::ApplicationShuttingDownEvent

// PublishImmediate (synchronous) so the CalibrationStore saves NOW, before teardown begins —
// a queued Publish() might not run before the context is destroyed. Mirrors
// VulkanGraphApplication::DeInitialize.
Vixen::EventBus::ApplicationShuttingDownEvent shutdownEvent{/*sender SenderID*/ 0};
engine.Bus().PublishImmediate(shutdownEvent);
// ... then let `engine` go out of scope. Member teardown is deterministic:
// calibration -> graph (node cleanup) -> bus -> registry -> owned cacher.
```

`EngineContext`'s destructor also calls `MainCacher::Shutdown()` before the bus dies, so an
owned-or-injected cacher unsubscribes cleanly (AR#8).

---

## Multiple engine instances (the point of AR#7/AR#8)

Because the registry, bus, graph, calibration store, **and the cacher** are per-`EngineContext`
(no singletons), a single process can hold more than one — e.g. a game view and an editor preview:

```cpp
EngineContext game(gameCfg);
EngineContext editor(editorCfg);   // independent caches, bus, graph — no cross-talk
```

Pass `EngineConfig::mainCacher` only when you deliberately want two contexts to **share** a cacher;
leave it null for full isolation.

> [!note] Host-owned *window* is not done yet
> Today each context's graph creates its own top-level window (via `WindowNode`). Rendering into a
> **host-supplied** window/surface (e.g. an editor panel) is `ExternalWindowNode` [AR#9] — evaluated
> 2026-06-14 and **parked** (no consumer need yet; would require decoupling `SwapChainNode` from
> GLFW). See [[Maturation-Backlog-2026-06]].

---

## Version & supported public headers

### Checking the VIXEN version

`<VixenVersion.h>` (generated from the SDK's `project()` version — AR#13) lets a host assert the
VIXEN it built against:

```cpp
#include <VixenVersion.h>

static_assert(VIXEN_VERSION >= VIXEN_MAKE_VERSION(0, 1, 0),
              "This host requires VIXEN >= 0.1.0");
// Also available: VIXEN_VERSION_MAJOR / _MINOR / _PATCH and VIXEN_VERSION_STRING ("0.1.0").
```

The CMake package carries the same version (one source of truth): `find_package(VIXEN 0.1.0
REQUIRED)` enforces it at configure time (SameMajorVersion compatibility).

### Supported public headers

These are the **stable, supported** entry points for an embedder. Include paths are relative to the
SDK's flattened `include/`:

| Header | For |
|---|---|
| `<VixenVersion.h>` | version / compatibility macros |
| `<Core/EngineContext.h>`, `<Core/EngineConfig.h>` | stand up + configure the engine |
| `<Core/RenderGraph.h>` | the graph: `RenderFrame()`, `Compile()`, `GetMainCacher()` |
| `<Core/NodeTypeRegistry.h>` | register node types |
| `<Core/TypedConnection.h>` | wire nodes |
| `<Core/CalibrationStore.h>` | the `Calibration()` accessor's type |
| `<Nodes/*.h>` (e.g. `InstanceNode.h`, `DeviceNode.h`, `SwapChainNode.h`, `VoxelGridNode.h`) | the node types you register + wire |
| `<Logger.h>` | the `Vixen::Log::Logger` passed in `EngineConfig` |
| `<Message.h>` | `EventBus::ApplicationShuttingDownEvent` + bus message types |

> [!warning] Everything else is internal
> Other headers ship in the SDK only because the libraries include each other by
> include-dir-relative paths; they are **implementation detail and may change without notice**.
> Depend on the set above. There is no umbrella `<Vixen.h>` at 0.1.0 — a curated public header is
> deferred until the API stabilizes (the second half of [AR#13]).

---

## Build-portability gotchas (consumer-discovered)

- **Validation is gated by `VIXEN_VULKAN_VALIDATION`, not `_DEBUG`.** `_DEBUG` is MSVC-only, so
  GCC/Clang hosts silently shipped validation OFF (UNDERTOW FR-1). The symbol is set cross-platform
  by `cmake/ProvisionVulkan.cmake` from the build type.
- **Instance extensions are filtered against availability** before `vkCreateInstance` (FR-22), so a
  limited ICD (e.g. Mesa Dozen on WSL2) doesn't abort on a missing optional extension.
- Run the app under a background timeout when smoke-testing on WSL:
  `cd binaries && timeout 20 ./VIXEN.exe` — exit 124/143 (timeout-killed) means it ran fine.
  `GRAPH_LOG_*` + `std::cout` reach captured stdout; `NODE_LOG_*` do not.

---

## File reference

| Thing | Where |
|---|---|
| `EngineContext` / `EngineConfig` | `libraries/RenderGraph/include/Core/EngineContext.h`, `EngineConfig.h` |
| Per-frame primitive | `RenderGraph::RenderFrame()` — `libraries/RenderGraph/include/Core/RenderGraph.h` |
| Reference graph wiring | `VulkanGraphApplication::BuildRenderGraph()` — `application/main/source/VulkanGraphApplication.cpp` |
| Reference loop | `application/main/source/main.cpp` |
| SDK export machinery | `cmake/VixenInstall.cmake`, `cmake/VIXENConfig.cmake.in`, `CMakeLists.txt` (`VIXEN_INSTALL_EXPORT`) |
| Shutdown event | `EventBus::ApplicationShuttingDownEvent` — `libraries/EventBus/include/Message.h` |

## Related

- [[RenderGraph-System]] — the node/slot/connection model you build graphs with
- [[Maturation-Backlog-2026-06]] — AR#2/#7/#8 status; AR#9 parked rationale
- [[wsl-cmake-windows-paths]] — the cmake.exe path trap
