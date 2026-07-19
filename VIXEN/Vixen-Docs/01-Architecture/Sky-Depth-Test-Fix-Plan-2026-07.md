# Sky-Projection Depth-Test Fix — Plan (2026-07)

**Status:** DRAFT plan — not scheduled, queued to launch after Inc5 (Sampled Lighting amortized probe update + tuned defaults) wraps, so the two workstreams don't tangle in one worktree/gate cycle. User-reported bug, 2026-07-13, observed while an Inc5 M1 implementer ran the DDGI leak-gate demo.

**Bug:** the sky-projection pass renders stars/sky in front of near-scene geometry that should occlude them — distant stars visible through/over objects that are actually closer to the camera.

**Base:** main `ad17d45c` (or whatever main's tip is when this launches — not yet branched).

## Root cause (confirmed via direct source read, 2026-07-13, investigation only — not yet fixed)

The sky-projection pass has **no depth buffer to test against at all** — this is a structural gap, not a disabled flag:

1. **`shaders/SkyProjection.vert:93`** hardcodes `gl_Position.z = 0.5` for every sky point, regardless of actual distance — the shader's own doc comment confirms it deliberately projects only direction (via camera rotation), never derives a real depth value.
2. **`SkyProjectionNode` does not use `GraphicsPipelineNode`** — it hand-builds its own `VkPipeline` directly (`libraries/RenderGraph/src/Nodes/SkyProjectionNode.cpp:239-365`, `CreatePipeline`). `VkGraphicsPipelineCreateInfo::pDepthStencilState` is left null (confirmed at lines 348-361); an existing comment even says so explicitly: `"color-only Load-op pass, no depth"`.
3. **The render pass/framebuffer for this pass never wire a depth attachment**: `RenderPassNodeConfig::DEPTH_FORMAT` and `FramebufferNodeConfig::DEPTH_ATTACHMENT` — both real, general-purpose input slots this codebase already supports — are simply never connected for `skyProjectionRenderPassNode`/`skyProjectionFramebufferNode` (`BuildRenderGraph.cpp:3160-3168`, confirmed via grep: zero `DEPTH_FORMAT`/`DEPTH_ATTACHMENT` hits anywhere in that file).
4. **The compute voxel march has no depth-stencil-attachment-compatible image to feed a fix with.** Real per-pixel hit distance DOES exist — `HitRecord.glsl`'s `hitT` field (SSBO), and `WorldPosHistoryNode`'s `.w` channel (a `rgba32f` GENERAL-layout storage image, already fully consumed by `SpatialReuseShade.comp`'s reprojection) — but neither is a genuine `VK_FORMAT_D32_SFLOAT`-class depth attachment a graphics pass can `depthTest` against. Vulkan depth testing needs a real depth-stencil-attachment image, populated either by rasterized geometry or an explicit `gl_FragDepth` write from a full-screen pass — compute `imageStore` into a storage image doesn't satisfy this.

**Working precedent exists in-repo** (not currently wired into the live voxel pipeline, but proves the plumbing works): `application/main/source/graph/BuildInstancingDemoGraph.cpp` — a legacy/demo raster graph — shows the complete correct pattern: `DepthBufferNode` (allocates a real `VK_FORMAT_D32_SFLOAT` image+view) → `RenderPassNodeConfig::DEPTH_FORMAT`/`PARAM_DEPTH_LOAD_OP`/`PARAM_DEPTH_STORE_OP` → `FramebufferNodeConfig::DEPTH_ATTACHMENT` → `GraphicsPipelineNodeConfig::ENABLE_DEPTH_TEST`/`ENABLE_DEPTH_WRITE`. This demo graph builds its own standalone device/window/swapchain and is NOT part of the live app graph — it's a reference for the WIRING SHAPE only, not a resource to reuse directly.

## Fix shape (two parts — not a one-line change)

1. **Produce a real depth attachment from the march's already-existing hit-distance data.** Most likely shape: a small full-screen pass (reads `HitRecord.hitT` or `WorldPosHistoryNode.w`, writes `gl_FragDepth`) that runs AFTER the compute march / DirectLighting pass and BEFORE the sky-projection pass, populating a genuine `VK_FORMAT_D32_SFLOAT` `DepthBufferNode`-shaped image the sky pass can then depth-test against. (Alternative shapes exist — e.g. rasterizing actual hit-point geometry instead of a full-screen depth-reconstruction pass — a proper milestone should investigate which is cheaper/simpler against this specific compute-then-graphics pipeline shape before committing; this doc is not prescribing the exact mechanism, only confirming a depth SOURCE must be produced, it doesn't already exist as an attachment-compatible resource.)
2. **Wire the sky-projection pass to consume it**: `RenderPassNodeConfig::DEPTH_FORMAT` on `skyProjectionRenderPassNode`, `PARAM_DEPTH_LOAD_OP=Load` (NOT Clear — must preserve the march's depth, not reset it), `PARAM_DEPTH_STORE_OP` per whatever the UI composite pass (the next pass in the chain) needs to also see; `FramebufferNodeConfig::DEPTH_ATTACHMENT` on `skyProjectionFramebufferNode`; since `SkyProjectionNode` hand-rolls its pipeline, manually add a populated `VkPipelineDepthStencilStateCreateInfo` to `SkyProjectionNode::CreatePipeline` (`depthTestEnable=VK_TRUE`, `depthWriteEnable=VK_FALSE` — sky shouldn't occlude itself or write over real depth, `depthCompareOp` matching this engine's existing depth convention — check `BuildInstancingDemoGraph.cpp`'s own choice or the compute march's depth-sense convention for consistency); fix `SkyProjection.vert:93`'s hardcoded `gl_Position.z = 0.5` to emit a genuine "maximum/far" depth constant so sky points always lose the depth test against any real geometry while still passing where nothing was rendered (background/empty pixels).

## Open questions for the implementer to resolve (not pre-decided here)

- **Depth source mechanism**: full-screen `gl_FragDepth`-reconstruction pass from `HitRecord`/`WorldPosHistoryNode.w`, vs. some other approach — investigate cost/complexity of each against this specific compute-then-graphics chain shape before committing.
- **Depth convention**: confirm this engine's near/far clip-space sense (0=near/1=far, or reversed-Z) before choosing the sky's "always lose" depth constant and `depthCompareOp` — get this wrong and sky either still occlusion-fails or now occludes EVERYTHING.
- **Where exactly the depth-producing pass slots into the existing chain** (`march → DirectLighting → BlitNode → sky-projection → UI`, per `BuildRenderGraph.cpp:3171`'s own comment) — likely right before `BlitNode` or right before sky-projection itself; must not disturb the already-established, Opus-validated auto-sync edges for the existing passes in this chain (Inc3/Inc4's own fan-out/fan-in and disjoint-sibling work).
- **Regression surface**: this chain includes `BlitNode`, which several Inc3/Inc4 passes' own auto-sync topology already depends on (per Inc4 M5's own live SyncEdge-dump verification of `spatial_reuse → direct_lighting`, `probe_update → spatial_reuse`, etc.) — a new pass inserted into this exact chain needs the SAME live SyncEdge-dump verification discipline Inc4 M5 established, not just "it renders correctly."

## Gate

- **Live, visual**: a scene with near-scene geometry AND visible stars/sky in the same frame — confirm stars are now correctly occluded by nearer geometry (a thin-wall-style occluder test, similar in spirit to Inc4 M4's leak-test gate, would make this a real numeric/ablation-style gate rather than a purely visual "looks right" check: e.g. a foreground object placed directly between camera and a known star position, confirm the star pixel is NOT drawn where the object occludes it, IS drawn where it doesn't).
- **Regression**: default boot + all of Inc3/Inc4's established live gates (RESTIR_GATE_DEMO, FANIN_DEMO, DDGI_LEAK_GATE_DEMO, M5_SHADE_GATE_DEMO, DDGI_EDIT_LOOP_DEMO) unaffected — confirm via live SyncEdge dump that inserting a depth-producing pass into the `march→...→sky` chain doesn't introduce an unexpected barrier or break an existing one, per Inc4 M5's own established verification discipline.
- **Instance-level VUID check** (KI-027's established discipline) on any new resource/binding this fix introduces.

## Sequencing

Queued to launch AFTER Inc5 (amortized probe update + tuned defaults) completes and merges — both are real, independent fixes; running them concurrently in separate worktrees is fine if desired, but this doc's own author (the controller) will default to sequential unless asked otherwise, to keep each pipeline run's gate surface focused.

## Related

- `application/main/source/graph/BuildInstancingDemoGraph.cpp` — the working depth-buffer wiring precedent to model the fix's plumbing shape on.
- Inc4 M5's own live-SyncEdge-dump verification discipline (`Sampled-Lighting-Inc4-Plan-2026-07.md` M5 Progress Log) — the regression-verification technique this fix's own gate should reuse for whatever new pass gets inserted into the march→sky→UI chain.
