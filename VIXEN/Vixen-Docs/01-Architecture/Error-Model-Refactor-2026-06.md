---
tags: [architecture, error-model, refactor, AR1, mod-facing]
created: 2026-06-13
status: phase 1 done (exit() de-fatal); phases 2-3 queued
related: ["[[Architecture-Review-Game-Renderer-2026-06-12]]", "[[consumer-feedback-undertow]]"]
---

# Error-Model Refactor (AR#1) — Design

> **Goal:** replace process-fatal error handling with proper propagation, so node/asset failures are
> recoverable + reportable instead of killing the process. The backlog's **prerequisite gate for
> everything mod-facing** — the root of UNDERTOW's "opaque crashes."

## 1. Landscape (as mapped 2026-06-13)

~423 fatal sites in non-test engine code: **396 `throw`** + **27 `exit()`**.

| Library | throws | exit() |
|---|---|---|
| RenderGraph | 236 | 0 |
| CashSystem | 56 | 0 |
| Profiler | 46 | 0 |
| VoxelData / ShaderManagement / VulkanResources / ... | ~58 | 21 engine + 6 CLI-tool |

Worst clusters: `TextureLoader.cpp` (17 `exit(1)`), `SwapChainNode.cpp` (16 throws),
`RayTracingPipelineNode` (15), `GeometryRenderNode` (12), `GraphicsPipelineNode` (11).

## 2. Existing infrastructure (underused, not absent)

The project already has the foundation — the refactor is **adoption + de-fatalizing**, not building:
- `libraries/VulkanResources/include/error/VulkanError.h`:
  - `struct VulkanError { VkResult code; std::string message; };`
  - `template<typename T> using VulkanResult = std::expected<T, VulkanError>;`
  - `using VulkanStatus = std::expected<void, VulkanError>;`
  - macros: `VK_CHECK(expr,msg)`, `VK_CHECK_FMT`, `VK_PROPAGATE_ERROR(result)`, plus log-only
    `VK_CHECK_LOG` / `VK_CHECK_RESULT`.
- **Deferred-recompilation** already catches per-node `Compile()` exceptions and retries next frame
  (`RenderGraph::RecompileDirtyNodes`, marks `isCompiled=false`). Swapchain `VK_ERROR_OUT_OF_DATE_KHR`
  is already handled via deferred recompile.

**Decision:** adopt the existing `std::expected`-based `VulkanResult`/`VulkanStatus` (chosen 2026-06-13).
A general (non-Vulkan) `vixen::Status` may be revisited at Phase 2 if non-Vulkan errors (shader compile,
validation) warrant it.

## 3. The gap

- Node lifecycle `*Impl` methods + the `Setup/Compile/Execute/Cleanup` wrappers return **void** and
  **throw** on failure.
- `RenderGraph::RenderFrame()` returns `VkResult` but **always `VK_SUCCESS`** — node failures can't
  propagate through it.
- The **initial** `Compile()` in `Prepare()` is **not** protected (first failure → `main` → exit −1),
  unlike recompiles.

## 4. Phased plan

### Phase 1 — de-fatal the process-fatal `exit()` calls ✅ DONE (2026-06-13)
Convert the 21 engine `exit()` calls to `VulkanResult`/`VulkanStatus` returns:
- **Texture loaders (20)** — `TextureLoader.cpp` ×17 + STB ×1 + GLI ×2. `Load()`→`VulkanResult<TextureData>`,
  `LoadPixelData()`→`VulkanResult<PixelData>`, `Upload*/Create*`→`VulkanStatus`. Leak-free error paths:
  `Load()` owns the `TextureData` and releases partial image/mem/view/sampler/cmd via one
  `DestroyPartialTexture()`; Upload* clean only internal staging/fence. (Loader is unwired today —
  `TextureCacher` TODO — so these were latent landmines.) Commit `ce4cab00`.
- **Swapchain (1, live path)** — `VulkanSwapChain::GetSurfaceCapabilitiesAndPresentMode` zero-extent
  `exit(-1)` → `std::unexpected(VK_ERROR_OUT_OF_DATE_KHR)`; `SwapChainNode::SetupFormatsAndCapabilities`
  throws a catchable error so the deferred-recompile path retries (window-minimized is recoverable, not
  fatal). Commit `1c68ed4d`.
- (The 6 `std::exit` in `shader_tool.cpp` are a standalone CLI tool, intentionally left.)

### Phase 2 — the propagation channel (next; wide, every-node — "its own session")
- Lifecycle `Execute()`/`Compile()` return `VulkanStatus`; `RenderFrame()` returns a status that reflects
  node failure (not always `VK_SUCCESS`).
- The app defers/reports/recovers instead of exiting; **protect the initial `Compile()`** in `Prepare()`
  the way recompiles already are (mark-for-retry / surface a clean error to the host).
- Recoverability targets: shader-compile failure, device-lost (`VK_ERROR_DEVICE_LOST`), OOM, asset-load
  failure — all currently fatal-ish, should be host-reportable.

### Phase 3 — the long tail
Adopt `VK_CHECK`/status across the 396 `throw`s where recovery (vs a clean fail-and-report) makes sense;
keep `throw` for genuine programmer-error invariants.

## 5. Acceptance

- ✅ No process-fatal `exit()` in the engine path (Phase 1).
- ⬜ A node failure propagates through `RenderFrame()` as a status the host can act on (Phase 2).
- ⬜ Initial graph compile failure is recoverable/reportable, not `exit(-1)` (Phase 2).
- ⬜ Shader-compile / device-lost / OOM are host-reportable (Phase 2-3).
