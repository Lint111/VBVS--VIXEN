---
tags: [feature, consumer-feedback, api-design, ergonomics, portability]
created: 2026-06-12
status: living
priority: medium
source: UNDERTOW integration (VIXEN's first external consumer)
---

# Consumer Feedback — UNDERTOW Integration

## What this is

UNDERTOW (a deterministic C# deep-sim) is currently **the only project that drives VIXEN as an
external consumer** — embedding it as a render engine + a moddable RmlUi front-end. That makes it
VIXEN's first real source of *consumer-side* friction: portability gaps, silent footguns, and
API-ergonomics rough edges that only show up when someone outside the engine wires it up.

This is a **living log**. When the integration hits a VIXEN issue, a UX/API papercut, or something
the engine could do better for a consumer, add an entry here. Keep each entry tight:
**Context / Issue / Consumer impact / Suggested fix / Status**. Mark `FIXED`, `WORKED-AROUND`
(graph-side, engine fix still wanted), or `OPEN`.

> Scope note: this is for **VIXEN engine** feature requests. Agent-workflow papercuts (tooling,
> grep surprises, schema dances) go to `~/.claude/friction.md` via the `report` skill instead.

Branch the fixes below landed on: `claude/wsl-build-portability` (the WSL/GCC bring-up branch).
Related: the engine renders its UI path on WSLg software Vulkan (llvmpipe) as of 2026-06-12.

---

## Portability / correctness (consumer-discovered)

### FR-1 — Vulkan validation is silently OFF on GCC/Linux builds
- **Context:** debugging a present-time crash on a Linux/GCC build of VIXEN.
- **Issue:** validation layers are requested under `#ifdef _DEBUG`, an **MSVC-only** macro
  (undefined on GCC). So a Linux/GCC consumer runs with **no validation by default**, and
  misconfigurations surface as opaque in-driver crashes instead of named VUIDs.
- **Consumer impact:** any non-MSVC consumer; cost a full debugging session before the root cause
  (the layers were simply never enabled) was found.
- **Suggested fix:** gate validation on a cross-platform define (CMake-driven
  `VIXEN_ENABLE_VALIDATION`, or `#ifndef NDEBUG`), not `_DEBUG`.
- **Status:** WORKED-AROUND (forced on via `VK_LOADER_LAYERS_ENABLE`); engine-side gate OPEN.

### FR-2 — Swapchain unconditionally requests `VK_IMAGE_USAGE_STORAGE_BIT`
- **Context:** bringing up the swapchain on software Vulkan (llvmpipe).
- **Issue:** the swapchain always requested `STORAGE` (for compute-to-swapchain), but software
  BGRA surface formats lack `STORAGE_IMAGE` → invalid swapchain.
- **Consumer impact:** any software/portability-layer consumer, or any future GPU whose surface
  format lacks STORAGE.
- **Suggested fix:** gate STORAGE on `vkGetPhysicalDeviceFormatProperties` (done in
  `VulkanSwapChain::GetSupportedFormats`).
- **Status:** FIXED.

### FR-3 — `FrameSyncNodeConfig::MAX_SWAPCHAIN_IMAGES = 3` is too low
- **Context:** llvmpipe's surface reports 4 images (`minImageCount + 1`).
- **Issue:** per-image `renderComplete`/present-fence arrays (indexed by `imageIndex`) overran at
  image 3 → corruption/crash.
- **Consumer impact:** any surface that grants >3 images.
- **Suggested fix:** raised 3 → 8; better, size the per-image arrays from the *actual* image count
  rather than a compile-time max.
- **Status:** FIXED (constant bumped); dynamic-sizing follow-up OPEN.

### FR-4 — Typed connections silently accept an implicit-conversion type mismatch
- **Context:** wiring the `PresentNode` swapchain input.
- **Issue:** connecting `SwapChainNode::SWAPCHAIN_PUBLIC` (`SwapChainPublicVariables*`) to a
  `VkSwapchainKHR` input *compiles and registers fine*, but the struct's implicit
  `operator VkSwapchainKHR` is **not invoked across the typed connection on GCC** (the struct has
  many conversion operators) → present receives the struct *pointer* and faults inside
  `vkQueuePresentKHR`. The graph never flagged the mismatch.
- **Consumer impact:** any consumer wiring present (or any slot where a struct offers a silent
  conversion). A genuinely nasty, silent footgun.
- **Suggested fix:** the connection/registration system should reject or warn on a src/dst slot
  type mismatch (no reliance on implicit conversions); or `SwapChainPublicVariables` shouldn't
  expose silent handle conversions. Consumers should wire the raw `SWAPCHAIN_HANDLE` output.
- **Status:** graph FIXED (wired `SWAPCHAIN_HANDLE`); connection-validation request OPEN.

### FR-5 — Public swapchain `Extent` desyncs from the actual image extent
- **Context:** chasing post-resize "garbage strips" on the bottom of the window.
- **Issue:** `SwapChainNode` set the public `Extent` to the *requested window size*, but the
  images are created at the resolved `surfCapabilities.currentExtent`. On any surface where those
  differ, framebuffers/renderArea (built from the public `Extent`) desync from the images, leaving
  the unrendered remainder as uninitialized garbage.
- **Consumer impact:** latent on most platforms (where currentExtent == window); bites software /
  HiDPI / compositor-managed surfaces.
- **Suggested fix:** set the public `Extent` from the resolved extent at the single point of
  resolution (`GetSurfaceCapabilitiesAndPresentMode`); never overwrite it with the window size.
- **Status:** FIXED.

### FR-6 — App shutdown / window handle is found by the magic name `"main_window"`
- **Context:** the window X-button didn't trigger app shutdown for a custom (UI-only) graph.
- **Issue:** `VulkanGraphApplication` looks up the window via
  `GetInstanceByName("main_window")` to get the GLFW handle for the close-poll. A consumer graph
  that names its window node anything else silently gets a null handle → the X-button does nothing.
- **Consumer impact:** any consumer building a custom graph with a differently-named window node.
- **Suggested fix:** discover the `WindowNode` by **type** (first/only instance), not by a magic
  string; or document the required name as part of the graph contract.
- **Status:** WORKED-AROUND (named the UI window `"main_window"`); engine type-discovery OPEN.

---

## Architecture / API ergonomics (open)

### FR-7 — Node recompile lifecycle is a footgun for stateful nodes
- **Context:** writing `UIRenderNode` (a stateful render node) and handling window resize.
- **Issue:** `CleanupImpl` is called on **both** recompile (resize) and shutdown, and the only
  signal to tell them apart is `NeedsRecompile()`. The natural first implementation — put
  `vkDeviceWaitIdle` + full teardown in `CleanupImpl` — then (a) **deadlocks on resize** (a submit
  blocked on an un-signalled acquire semaphore never completes, and the graph deliberately *skips*
  device waits during recompile, so the node must not add one) and (b) destroys persistent state
  that should survive a resize.
- **Consumer impact:** anyone authoring a stateful node; this is non-obvious and cost a long
  investigation (the resize "stall").
- **Suggested fix:** provide distinct lifecycle hooks (e.g. `OnSwapchainResize` vs `OnShutdown`),
  or document prominently: *recompile cleanup must be lightweight, no device wait; use
  `NeedsRecompile()` to keep persistent state across recompiles.*
- **Status:** OPEN (UIRenderNode now follows the correct pattern — usable as a reference).

### FR-8 — "Who owns swapchain-derived resources" isn't discoverable
- **Context:** deciding where framebuffers live for a render node.
- **Issue:** the natural instinct is to build the render pass + framebuffers *inside* the render
  node. The correct pattern is to consume `RENDER_PASS` (`RenderPassNode`) + `FRAMEBUFFERS`
  (`FramebufferNode`) as inputs, so the swapchain recompile cascade owns their resize lifecycle.
  This is only learnable by reading `GeometryRenderNode`.
- **Consumer impact:** anyone adding a render node; getting it wrong reproduces FR-5/FR-7.
- **Suggested fix:** a "render-to-swapchain" recipe in the node-authoring docs. The UI graph
  (`swapchain → RenderPassNode → FramebufferNode → UIRenderNode → PresentNode`, color-only) is now
  a clean minimal template.
- **Status:** OPEN.

### FR-9 — Graph wiring is verbose and silently mis-wireable
- **Context:** building the UI-only graph.
- **Issue:** a minimal color present target is ~10 nodes and ~20 explicit `Connect` calls, and
  slot mistakes (FR-4) are silent. There's no preset for "render a color target to the screen."
- **Consumer impact:** high friction + easy errors for every new graph.
- **Suggested fix:** higher-level presets / a `ColorPresentTarget` helper that wires
  renderpass+framebuffer+present in one call; and/or a compile-time validation pass that flags a
  render node with no framebuffer input, or a present node not fed a render-complete semaphore.
- **Status:** OPEN.

### FR-10 — No built-in runtime-asset staging
- **Context:** the RmlUi demo loads `assets/ui/{demo.rml,demo.rcss,font.ttf}` relative to the exe.
- **Issue:** staging runtime assets next to the executable required a hand-written
  `add_custom_command(... POST_BUILD copy_directory ...)`. Nothing in the engine helps.
- **Consumer impact:** every consumer shipping runtime assets (fonts, RML, shaders, data).
- **Suggested fix:** a documented CMake helper, e.g. `vixen_stage_assets(<target> <src-dir>)`, that
  copies into `$<TARGET_FILE_DIR>` post-build.
- **Status:** WORKED-AROUND (added a POST_BUILD copy for `assets/`); helper request OPEN.

---

## Render-graph resize correctness (consumer-discovered, fixed)

Window resize hung the render loop and never re-fit the swapchain. Four distinct engine bugs,
found by driving the resize path; all fixed on `claude/wsl-build-portability` (`f6c9d8e7`). These are
**general engine bugs** — they affect any graph that resizes (voxel path included), not just the UI demo.

### FR-11 — Message-type ids are not stable across translation units
- **Context:** window resize never reached `SwapChainNode` to trigger recompilation.
- **Issue:** `AUTO_MESSAGE_TYPE()` was `MESSAGE_TYPE_BASE + __COUNTER__`. `__COUNTER__` resets per TU
  and increments per use, so a `static constexpr TYPE` declared in a header resolves to *different*
  values depending on include order. A message published in one TU then fails to match the same
  type's subscription in another TU and is delivered to nobody. `WindowResizedMessage` is published
  in `WindowNode.cpp` and subscribed in `SwapChainNode.cpp`/`RenderGraph.cpp` — different TUs — so
  resize silently routed to no one. *This is the most serious one: it can mis-route ANY cross-TU
  message.*
- **Suggested fix:** derive the id from a translation-unit-stable hash of the definition site
  (`__FILE__` + `__LINE__`), not `__COUNTER__`. (Better long-term: hash the type name.)
- **Status:** FIXED (`Message.h`). EventBus unit tests still green.

### FR-12 — OUT_OF_DATE frame-skip doesn't propagate the invalid image index
- **Context:** the loop hung the instant a resized acquire returned `VK_ERROR_OUT_OF_DATE_KHR`.
- **Issue:** `SwapChainNode::ExecuteImpl` returned early on an out-of-date acquire **without** writing
  `IMAGE_INDEX`, so its output kept the previous (valid) index. The render node then recorded +
  submitted with that stale index, waiting on a per-flight acquire semaphore that acquire never
  signalled — the submit blocked, the queue never went idle, and the next fence/idle wait deadlocked.
- **Suggested fix:** output `IMAGE_INDEX = UINT32_MAX` before the skip-return (the render + present
  nodes already guard on `UINT32_MAX`).
- **Status:** FIXED (`SwapChainNode.cpp`).

### FR-13 — Recompile is deferred during a swapchain-recreation pause
- **Context:** after FR-12 was fixed, resize stopped hanging but froze permanently (paused).
- **Issue:** OUT_OF_DATE publishes `RenderPauseEvent(SwapChainRecreation, PAUSE_START)`, and
  `RecompileDirtyNodes` defers while `renderPaused`. But the recompile *is* the recreation, and it's
  what publishes the `PAUSE_END` that resumes — so deferring it left rendering paused forever.
- **Suggested fix:** don't defer the recompile when the pause reason is `SwapChainRecreation`.
- **Status:** FIXED (`RenderGraph.cpp/.h`).

### FR-14 — SwapChainNode re-subscribes on every recompile
- **Context:** each resize produced a growing storm of recompiles.
- **Issue:** the `WindowResizedMessage` subscription is created in `SetupImpl`, which re-runs on every
  recompile, so subscriptions accumulate and each one fires an extra `MarkNeedsRecompile`.
- **Suggested fix:** guard the subscription so it registers once (or unsubscribe on cleanup). More
  broadly: `SetupImpl` re-running on recompile is a footgun for any one-time subscription/registration.
- **Status:** FIXED (`SwapChainNode.cpp`, one-shot guard).

### FR-15 — No live rendering during a window-drag resize
- **Context:** during an active resize drag the content lags (strips appear, then snap to fit).
- **Issue:** GLFW's modal move/resize loop blocks the event loop on the main thread, so no frames
  render *during* the drag — the swapchain only catches up once each resize step lands. Functionally
  correct, but not smooth.
- **Suggested fix:** render during the modal resize via `glfwSetWindowRefreshCallback` (re-entrant
  render), or drive rendering from a second thread.
- **Status:** OPEN (cosmetic; not a correctness bug).

---

## Adding entries

Number sequentially (`FR-N`). When an entry is fixed engine-side, set `Status: FIXED` and note the
commit/branch. Keep this doc skimmable — detail belongs in the linked design doc or the code.
Cross-link related engine docs with `[[wikilinks]]` (e.g. [[RenderGraph-System-Architecture-Analysis]]).
