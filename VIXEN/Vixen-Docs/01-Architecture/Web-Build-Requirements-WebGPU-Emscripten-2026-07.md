---
tags: [architecture, porting, web, webgpu, emscripten, itch-io, feasibility]
date: 2026-07-01
status: reference
priority: not-active
---

# VIXEN Web Builds: Requirements for Website Applications & itch.io Hosting

**Status.** Reference / future-knowledge. **Not an active work item** — captured so the
constraints are on record when (if) web delivery becomes a goal. No code targets this today.

**Question answered.** *What would it take for VIXEN to produce web builds for website-based
applications and itch.io HTML5 hosting?*

Related: [[Architecture-Review-Game-Renderer-2026-06-12]], [[RenderTarget-Design-2026-06]],
[[Hardware-RT]], [[capability-requirement-matrix]], [[RenderGraph-System]].

---

## 1. Executive Summary

A "web build" is **not a compile flag** for VIXEN. VIXEN is a native **Vulkan 1.3/1.4** engine
end-to-end, and **browsers do not expose Vulkan** — the only GPU APIs available in a browser are
**WebGL2** and **WebGPU**. There is no `libvulkan` in the browser and Emscripten does not provide
one. Web delivery therefore requires a **second rendering backend**, not a port of the existing one.

**Verdict:** feasible but a multi-month engine effort. The render-graph *node model* is an asset
(it gives a place to slot a second backend), but resources are currently raw `Vk*` handles, so the
prerequisite is an **RHI abstraction seam**. Once that exists, a WebGPU backend + offline shaders +
an Emscripten CMake target gets us to an itch.io-uploadable artifact. Hardware ray tracing cannot
ship to web at all.

---

## 2. Why It's Not a Flag — Coupling Evidence

Verified against the codebase (2026-07-01):

- **Resources are Vulkan handles.** The render-graph typed-resource variant registry stores
  `VkImage`, `VkFramebuffer`, etc. directly. Nodes consume/produce raw `Vk*` — there is no
  device-agnostic resource layer to retarget.
- **Hardware ray tracing is wired in.** `TraceRaysNode`, `AccelerationStructureNode`,
  `RayTracingPipelineNode` use `VK_KHR_acceleration_structure` / `vkCmdTraceRays`. **No WebGPU or
  WebGL equivalent exists** — these pipelines are web-impossible (see §5).
- **Runtime shader compilation via glslang.** `BUILD_SPV_ON_COMPILE_TIME` compiles GLSL→SPIR-V at
  runtime using the Vulkan SDK's glslang. glslang does not run in-browser; the web target must use
  **offline** shader compilation.
- **Threads + filesystem everywhere.** ~100+ TUs use `std::thread` / `std::async` /
  `std::filesystem` (async cachers, profiler). Browser WASM needs `SharedArrayBuffer` + cross-origin
  isolation for threads, and has no real filesystem.
- **GLFW windowing/surface.** `WindowNode`, `SwapChainNode`, `InstanceNode`, `VulkanSwapChain`
  select the platform surface via GLFW + `glfwCreateWindowSurface`. On web this maps to an HTML5
  canvas, not a `VkSurface`.
- **CMake assumes desktop.** `CMakeLists.txt` branches on MSVC / `WIN32` / `find_package(Vulkan)`
  and self-provisions the Vulkan SDK. There is no `EMSCRIPTEN` path.

---

## 3. Requirements, Ordered by Difficulty

### 3.1 Toolchain — Emscripten (WebAssembly)  *(small)*
- Add an `EMSCRIPTEN` branch to the CMake tree that **skips Vulkan SDK provisioning** entirely
  (mirror the existing `VULKAN_TRIMMED_BUILD` gating pattern).
- Configure via `emcmake cmake … / emmake`.
- Link flags to expect: `-sUSE_WEBGPU` (or a WebGL2 path), async main loop via
  `emscripten_set_main_loop` (or `-sASYNCIFY`), `-sALLOW_MEMORY_GROWTH`, `-sMAX_WEBGL_VERSION=2`,
  and `--preload-file` to pack `BuiltAssets/` + shaders into the `.data` virtual FS.

### 3.2 RHI abstraction seam — **step 0, prerequisite**  *(high)*
The render graph must stop trafficking in raw `Vk*` handles and instead expose device-agnostic
resource/command abstractions, so a second backend can be added **without rewriting every node**.
This overlaps directly with the RenderTarget abstraction work already identified in
[[Architecture-Review-Game-Renderer-2026-06-12]] (decision #3 — "make the render target an
abstraction, not the swapchain"). Web is another consumer of the same seam.

### 3.3 A WebGPU rendering backend — **the real work**  *(high)*

| Path | Meaning | Viability |
|------|---------|-----------|
| **WebGPU backend** | Implement a WebGPU backend behind the RHI seam; Emscripten maps calls to browser `navigator.gpu`. Explicit, command-buffer model — closest conceptual match to Vulkan. | **Recommended** |
| **Vulkan→WebGPU translation layer** | Run Vulkan through a portability shim in-browser. No mature *browser-target* layer exists (Dawn/wgpu are native). | Not viable today |
| **WebGL2 backend** | Broadest browser reach, but **no compute shaders** → kills compute ray-marching. | Fallback only |

### 3.4 Shaders — move to offline WGSL  *(small–medium)*
WebGPU consumes **WGSL** (or SPIR-V via Tint/Naga depending on toolchain), not runtime GLSL/SPIR-V.
Ship **pre-translated** shader modules; disable the runtime-glslang path for web
(same switch as `VULKAN_TRIMMED_BUILD` uses).

### 3.5 Feature reductions the web cannot do  *(design decisions)*
- **Hardware RT: dropped.** Only **compute / fragment ray-marching** pipelines are portable.
  WebGPU compute is broadly available; WebGL2 has none.
- **Threading:** requires `-pthread` + `SharedArrayBuffer` + COOP/COEP cross-origin isolation, else
  fall back to single-threaded (async cachers must degrade gracefully).
- **Filesystem:** route cache/asset I/O through Emscripten VFS or IndexedDB — no native FS.

### 3.6 Windowing / input  *(small)*
GLFW → Emscripten HTML5 canvas backend (GLFW3 emscripten port, or `emscripten/html5.h`).
Swapchain/present maps to the canvas; input events come from the DOM.

---

## 4. itch.io Hosting Specifics *(easy once an Emscripten build exists)*
- Upload an **HTML5 game**: a zip whose **root** contains `index.html` (+ `.js`, `.wasm`, `.data`);
  mark it "This file will be played in the browser."
- **Size:** itch.io's per-upload limit is large (multi-GB), but the browser downloads the entire
  `.wasm` + `.data` on load. Voxel scene data will dominate — keep it lean.
- **Cross-origin isolation:** itch.io exposes a `SharedArrayBuffer` (COOP/COEP) toggle in the HTML5
  embed settings — **required** if compiled with `-pthread`. If unavailable, build single-threaded.
- **Viewport/fullscreen:** set canvas dimensions in the embed; wire an Emscripten fullscreen button.
- **WebGPU availability caveat:** enabled in Chrome/Edge and recent Firefox/Safari, not universal.
  A WebGL2 fallback broadens reach at the cost of compute pipelines.

---

## 5. What Cannot Ship to Web (hard limits)
- **Hardware ray tracing** (`VK_KHR_acceleration_structure` / `traceRays`) — no browser equivalent.
- **Runtime GLSL→SPIR-V (glslang)** — offline only.
- **True multithreading without cross-origin isolation** — single-threaded fallback required.
- **Native filesystem access** — VFS/IndexedDB only.

---

## 6. Recommended First Milestone (if pursued)
Do **not** attempt a full node-catalog port first. Prove the toolchain end-to-end:

> **Headless compute ray-march of one scene rendering to a WebGPU canvas via Emscripten,
> single-threaded, offline-compiled shaders.**

This exercises Emscripten + WebGPU + canvas present + asset packing + shader pipeline before any
investment in the broader RHI migration. If it renders a frame in a browser tab, the path is real.

### Rough sequencing
1. RHI seam (shared with RenderTarget abstraction, §3.2) — *prerequisite, high cost.*
2. WebGPU backend behind the seam (§3.3) — *high cost.*
3. Offline WGSL shader pipeline (§3.4) — *small–medium.*
4. Gate out HW-RT, thread down, VFS I/O (§3.5) — *design.*
5. Emscripten CMake target emitting `index.html` (§3.1) — *small.*
6. itch.io packaging (§4) — *trivial once (5) exists.*

Steps 1–2 are the multi-month core; 3–6 are comparatively small.
