# Instancing Increment (AR#31 increment 1) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax.

**Goal:** Render N cubes (one mesh, N per-instance transforms) via hardware instancing, in an isolated env-var demo graph (`VIXEN_INSTANCING_DEMO`), leaving the live voxel-compute path untouched.

**Architecture:** A new `InstanceBufferNode` produces an SSBO of N `mat4` model matrices (a grid). `Draw.vert` reads `model[gl_InstanceIndex]`. `GeometryRenderNode` draws with `INSTANCE_COUNT=N` (already supported). An isolated `BuildInstancingDemoGraph()` (mirroring `BuildUIGraph`) wires the raster chain + the instance SSBO and is selected by an env var.

**Tech stack:** C++23, Vulkan 1.3, VIXEN RenderGraph node system (`CONSTEXPR_NODE_CONFIG`, `TypedNode<Config>`, `NodeTypeRegistry`), GLSL→SPIR-V, GoogleTest, glm.

**Design source:** `Vixen-Docs/01-Architecture/Instancing-Increment-Design-2026-06.md`.

**Build:** `"/mnt/c/Program Files/CMake/bin/cmake.exe" --build build --config Debug --parallel 16` (note: a lone `MSB3073`/`gtest_discover_tests` timeout is flaky — rebuild clears it).

**Reference templates (read before implementing):**
- Producer node (allocates a Vulkan resource, FR-7 lifecycle, registered): `libraries/RenderGraph/include/Data/Nodes/RenderTargetNodeConfig.h`, `include/Nodes/RenderTargetNode.h`, `src/Nodes/RenderTargetNode.cpp` (AR#28).
- SSBO/buffer creation + memory-type selection: `libraries/RenderGraph/src/Nodes/VertexBufferNode.cpp`, `libraries/RenderGraph/include/NodeHelpers/BufferHelpers.h` (`FindMemoryType`).
- Node registration sites: `libraries/RenderGraph/src/Core/NodeTypeRegistry.cpp`, `application/main/source/VulkanGraphApplication.cpp` (`registry.Register<...>()`), `VixenBenchmark/.../BenchmarkRunner.cpp`; CMake source lists for RenderGraph.
- Alternate-graph template + wiring: `application/main/source/VulkanGraphApplication.cpp` `BuildUIGraph()` (~line 526), the `VIXEN_UI_DEMO` gate (~617), the disabled raster blocks (~640-657 nodes, ~698-756 params), `ConnectionBatch` usage (~561+).
- Descriptor SSBO binding pattern (compute path): `DescriptorResourceGathererNode` + `DescriptorSetNode` wiring in `BuildRenderGraph`.

---

### Task 1: `InstanceBufferNode` (producer node)

**Files:**
- Create: `libraries/RenderGraph/include/Data/Nodes/InstanceBufferNodeConfig.h`
- Create: `libraries/RenderGraph/include/Nodes/InstanceBufferNode.h`
- Create: `libraries/RenderGraph/src/Nodes/InstanceBufferNode.cpp`
- Modify: `libraries/RenderGraph/src/Core/NodeTypeRegistry.cpp` (register `InstanceBufferNodeType`)
- Modify: RenderGraph CMake source list (add `InstanceBufferNode.cpp`) — find the list that includes `RenderTargetNode.cpp`
- Modify: `application/main/source/VulkanGraphApplication.cpp` + `VixenBenchmark/.../BenchmarkRunner.cpp` (`registry.Register<InstanceBufferNodeType>()` beside other registrations)

- [ ] **Step 1: Config** — mirror `RenderTargetNodeConfig.h`. Inputs: `VULKAN_DEVICE_IN (VulkanDevice*, Required)`. Outputs: `INSTANCE_BUFFER (VkBuffer)`, `INSTANCE_COUNT (uint32_t)`. Params: `PARAM_GRID_DIM ("gridDim")`, `PARAM_SPACING ("spacing")`. Use a fresh node type ID (next after RenderTargetNode's 115 — verify the current max and pick the next free).

- [ ] **Step 2: Header** — `class InstanceBufferNode : public TypedNode<InstanceBufferNodeConfig>` + `InstanceBufferNodeType`. Members: `VkBuffer buffer_`, `VkDeviceMemory memory_`, `uint32_t instanceCount_`, `uint32_t gridDim_`, `float spacing_`, `VulkanDevice* device_`. Declare SetupImpl/CompileImpl/CleanupImpl + `CreateBuffer`/`DestroyBuffer` helpers. (No ExecuteImpl needed — static buffer.)

- [ ] **Step 3: Impl** — `CompileImpl`: read params (`gridDim_` default 8, `spacing_` default 2.0); `instanceCount_ = gridDim_*gridDim_` (planar grid for clear visibility); FR-7 guard (`if buffer_ == VK_NULL_HANDLE` create once); allocate `VkBuffer` (`VK_BUFFER_USAGE_STORAGE_BUFFER_BIT`) + host-visible+coherent memory via real `FindMemoryType`; map, write `gridDim_ x gridDim_` `glm::mat4` translations (`glm::translate(glm::mat4(1), {(x - gridDim_/2)*spacing_, (y - gridDim_/2)*spacing_, 0})`), unmap; `ctx.Out(INSTANCE_BUFFER, buffer_)`, `ctx.Out(INSTANCE_COUNT, instanceCount_)`. `CleanupImpl`: `if (ctx.reason != CleanupReason::FinalTeardown) return;` else `DestroyBuffer`. Use `glm` (already a dependency; mirror includes used elsewhere in RenderGraph).

- [ ] **Step 4: Register + build** — register in NodeTypeRegistry.cpp + the app/benchmark registries; add to CMake source list. Build green: `cmake --build build --config Debug --parallel 16`.

- [ ] **Step 5: Commit** — `feat(rendergraph): InstanceBufferNode — SSBO of N instance transforms (AR#31)`

---

### Task 2: `InstanceBufferNode` unit test

**Files:**
- Create: `libraries/RenderGraph/tests/Nodes/test_instance_buffer_node.cpp`
- Modify: `libraries/RenderGraph/tests/test_critical_nodes.cmake` (add `test_instance_buffer_node`, mirror `test_render_target_node` block)

- [ ] **Step 1: Tests** — mirror `test_render_target_node.cpp`: config assertions (INPUT/OUTPUT counts, slot indices, param name constants, type IDs) and, if a pure transform-grid helper is factored out, assert N = gridDim² and a couple of expected translation matrices. Keep device-dependent allocation as a deferred placeholder (like the render-target test) — the config + grid math are the unit-testable surface.
- [ ] **Step 2: Build + run** — `test_instance_buffer_node.exe --gtest_brief=1` → all pass.
- [ ] **Step 3: Commit** — `test(rendergraph): InstanceBufferNode config + grid (AR#31)`

---

### Task 3: `Draw.vert` per-instance model (SSBO)

**Files:**
- Modify: `shaders/Draw.vert`
- Modify: `VixenBenchmark/shaders/Draw.vert` (keep the copy in sync)

- [ ] **Step 1: Edit both** to the instanced version from the design doc (add `layout(std430, binding=1) readonly buffer Instances { mat4 model[]; } instances;` and `gl_Position = myBufferVals.mvp * instances.model[gl_InstanceIndex] * pos;`). `Draw.frag` unchanged.
- [ ] **Step 2: Verify it compiles** — if shaders are compiled at build/run, confirm no SPIR-V compile error (build, or run the demo graph in Task 4). Expected: binding 1 appears in reflection as a storage buffer.
- [ ] **Step 3: Commit** — `feat(shaders): Draw.vert per-instance model via gl_InstanceIndex (AR#31)`

---

### Task 4: `BuildInstancingDemoGraph()` (re-light + wire)

**Files:**
- Modify: `application/main/source/VulkanGraphApplication.cpp` (new method + env-var gate)
- Modify: `application/main/include/.../VulkanGraphApplication.h` (declare `void BuildInstancingDemoGraph();`)

- [ ] **Step 1: Gate** — at the top of `BuildRenderGraph()`, beside the `VIXEN_UI_DEMO` check, add: `if (std::getenv("VIXEN_INSTANCING_DEMO")) { BuildInstancingDemoGraph(); return; }`.
- [ ] **Step 2: Method** — implement `BuildInstancingDemoGraph()` mirroring `BuildUIGraph()`. Add nodes: Instance, Window, Device, SwapChain, CommandPool, RenderPass, Framebuffer, DepthBuffer, VertexBuffer (cube: VERTEX_COUNT=36, stride=sizeof(VertexWithUV), INDEX_COUNT=0), `InstanceBufferNode` (gridDim param), ShaderLibrary (Draw.vert/frag — copy the builder from the disabled block ~line 761), DescriptorResourceGatherer, DescriptorSet, GraphicsPipeline (depth on, cull Back, TriangleList), FrameSync, GeometryRender, Present. Use the disabled raster blocks + `BuildUIGraph` as the wiring reference.
- [ ] **Step 3: Wire** with `ConnectionBatch` (mirror the UI graph's infra connections + the disabled raster connections). Connect `InstanceBufferNode.INSTANCE_BUFFER` into the descriptor path so `Draw.vert` binding 1 is satisfied (follow the compute SSBO descriptor-gatherer pattern). Set `GeometryRenderNode INSTANCE_COUNT` to `InstanceBufferNode`'s count (param or connection). Camera/MVP: reuse the existing camera/push-constant or UBO source that fed the single cube; the cubes only need to be visible.
- [ ] **Step 4: Build green.**
- [ ] **Step 5: Commit** — `feat(app): VIXEN_INSTANCING_DEMO graph renders N instanced cubes (AR#31)`

---

### Task 5: Verify + finish

- [ ] **Step 1: App smoke** — `cd binaries && VIXEN_INSTANCING_DEMO=1 timeout 15 ./VIXEN.exe > /tmp/inst.log 2>&1`. Assert: 0 `VK_ERROR`/`VUID`; a draw recorded with `instanceCount` == N (>1) (add/confirm a log line if needed); render activity > 0. If the dormant raster path errors, debug to green (systematic-debugging) — this is the expected discovery risk.
- [ ] **Step 2: Regression** — default run (no env var) unchanged: `timeout 12 ./VIXEN.exe` → voxel path, 0 VK errors, render activity.
- [ ] **Step 3: Docs** — mark AR#31 increment 1 progress in `Vixen-Docs/05-Progress/Maturation-Backlog-2026-06.md` (the many-entity item: increment 1 done, follow-ups = multi-mesh draw lists + drawIndirect); flip the design doc status to DONE.
- [ ] **Step 4: Finish branch** — superpowers:finishing-a-development-branch (merge to main + push, per session pattern).
