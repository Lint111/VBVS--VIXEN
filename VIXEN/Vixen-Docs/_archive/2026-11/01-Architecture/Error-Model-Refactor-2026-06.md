---
tags: [architecture, error-model, refactor, AR1, mod-facing]
created: 2026-06-13
status: phases 1-3 done. P1 exit()-de-fatal + P2 host-facing status channel + P3 device-loss recovery (rebuild on a fresh device, validated). Merged to main 2026-06-13.
related: ["[[Architecture-Review-Game-Renderer-2026-06-12]]", "[[consumer-feedback-undertow]]", "[[Device-Loss-Recovery-2026-06]]"]
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

### Phase 2 — the host-facing propagation channel ✅ DONE (2026-06-13)
**Design choice: Option A (status at the host boundary), not the every-node `*Impl`→`VulkanStatus` rewrite.**
Rationale: UNDERTOW is a **C# host**, and C++ exceptions are undefined behaviour across that boundary — so
the requirement is *status at the host-facing API*, not eliminating internal exceptions (which stay within
C++ and, for recompiles, are already caught + deferred). Bounded change at the boundaries:
- **2a** — `RenderFrame()` catches node-Execute failures (sequential path + the parallel virtual-task
  executor) → logs + returns a non-success `VkResult`; the app's `Render()` already stops the loop on a
  non-success result. No exception escapes to the loop/host. Commit `754cfd51`.
- **2b** — `Prepare()` catches initial-`Compile()` failures (`std::exception` + `...`) → records a
  host-readable message + leaves `isPrepared=false`, no rethrow→exit; `main` aborts gracefully. Added
  `lastError_`/`GetLastError()` on `VulkanApplicationBase`. Commit `3be2a92c`.
- **2c** — `Render()`/`Update()` wrapped in catch-all guards → nothing escapes the host-facing tick
  (event-callback handlers, `ProcessEvents`, etc.). Commit `32e689ce`.

**Result:** no exception can escape `Prepare`/`Update`/`Render`/`RenderFrame` — the host-facing boundary
returns status only. (Deferred for a future increment: *recovering* the initial compile via mark-for-retry
like recompiles; richer per-failure recovery for device-lost / OOM lives in phase 3.)

### Phase 3 — surgical *recovery* (NOT a mass throw→status conversion) ✅ DONE (2026-06-13)
**Reframe (confirmed by categorizing the throws):** the backlog's "adopt status across the ~396 throws"
was mostly *inappropriate* — the majority are validation/invariant checks ("shader bundle not set",
"device input is null", …) that SHOULD stay `throw` (fail fast on a mis-wired graph), and post-Phase-2
they no longer crash the host. The genuine value is **recovery of the one truly-transient runtime
failure that matters for a long-running C# host: GPU device loss.**

Delivered (full design + root-causes in [[Device-Loss-Recovery-2026-06]]): detect `VK_ERROR_DEVICE_LOST`
→ rebuild the whole graph on a fresh device (ordering-correct teardown-reverse / rebuild-forward;
instance + surface + window persist, only the device + its children rebuild) → resume rendering.
Validated via a fault-injection harness (`VIXEN_SIMULATE_DEVICE_LOSS`): synthetic loss → full rebuild →
~73-89s of continuous post-recovery rendering, no crash, zero Vulkan validation errors. Also fixed two
pre-existing UAFs the rebuild surfaced (a dangling GPU-task-profile pointer via CalibrationStore.Load
replacing profile objects; a rebuildable-ConstantNode value). (OOM / shader-compile recovery + the
unrecoverable-loss terminal path remain a future increment — not blocking.)

## 5. Acceptance

- ✅ No process-fatal `exit()` in the engine path (Phase 1).
- ✅ A node Execute failure surfaces as a status, not a crash (Phase 2a).
- ✅ Initial graph compile failure is reportable via `IsPrepared()`/`GetLastError()`, not `exit(-1)` (Phase 2b).
- ✅ No exception escapes `Prepare`/`Update`/`Render`/`RenderFrame` to the C# host (Phase 2c).
- ✅ Device loss is detected and recovered: the graph rebuilds on a fresh device and resumes rendering
  (Phase 3 — validated; see [[Device-Loss-Recovery-2026-06]]).
- ⬜ OOM / shader-compile recovery + the unrecoverable-loss terminal path + defer-initial-compile-retry —
  future increment (not blocking; device loss was the high-value case).
