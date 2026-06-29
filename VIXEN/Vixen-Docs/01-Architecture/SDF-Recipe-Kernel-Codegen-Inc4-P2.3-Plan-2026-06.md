# P2.3 — Edit → Re-materialize loop (runtime recipe edit → re-bake → re-render) — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: superpowers:subagent-driven-development / executing-plans. Checkbox (`- [ ]`) steps. **VIXEN-only**, branch `feat/sdf-recipe-codegen-p2` (stacks on P2.2). Reuses the P2.1 recipe bake + the Inc2/Inc3 Stored render unchanged; closes the Materialization edit loop.

**Goal:** Make `BodyOctreeSceneNode::SetBakeRecipe` *live* — editing the recipe at runtime (after `Compile`) re-bakes octree 0 and re-uploads its GPU buffers in place, so the next render shows the edited shape **without a graph rebuild or app restart** — proven by a lavapipe gate that renders shape A (single sphere), edits to shape B (peanut), and re-renders B.

**Architecture:** Mirror the existing `SetInstances`→dirty-flag→`ExecuteImpl` seam (the per-tick recompile cascade is deliberately avoided — it is the documented WSL/Dozen VM-panic trap). `SetBakeRecipe` sets `recipeDirty_`; `CompileImpl` clears it (a fresh compile already baked the current recipe); `ExecuteImpl`, when `recipeDirty_`, runs `Rematerialize()` — `vkDeviceWaitIdle` (rare edit path, same guard as the ring-grow), reset `octreesBuilt_`, re-bake + re-concatenate via `EnsureOctreesBuilt()`, `DestroyOctreeBuffers()` (the 6 octree/channel buffers only — the instance ring is untouched), `CreateOctreeBuffers()`, then re-emit the 6 octree output slots so consumers re-read the new handles. No shader, no GPU-pipeline, no graph-wiring changes.

**Tech Stack:** C++23, GoogleTest, CMake (`vixen-wsl` preset), glslang/Vulkan (lavapipe for the render gate), glm.

## Global Constraints

- **Non-dirty path byte-behaviour-identical.** `recipeDirty_` defaults `false`; `CompileImpl` clears it. With no runtime edit, `ExecuteImpl` is byte-identical to today. `DestroyOctreeBuffers()` is a *factored subset* of the existing `DestroyBuffers()` (the same 6 destroys), so existing teardown is unchanged. Every existing test must stay green: `RenderRecipeBakedBody`, `RenderStoredSdfBodiesNoHoles`, `RenderStoredSdfMultiChannel`, the instance-ring render tests, `test_recipe_bake`, `test_sdf_bake`, `test_soa_sdf_serialize`.
- **No GPU resource freed on the per-tick path.** Re-materialize frees the octree buffers ONLY behind `vkDeviceWaitIdle`, and ONLY on the rare frame where `recipeDirty_` is set (an explicit edit) — never every tick. The instance ring is never touched by re-materialize. (Honors the lifecycle invariant in `BodyOctreeSceneNode.h:57-61`.)
- **No `MarkNeedsRecompile`.** The edit seam is dirty-flag + in-`Execute` re-materialize, exactly like `SetInstances`. The recompile cascade stays dead.
- **Recipe format = P0's** `Vixen::SVO::Recipe::SdfInstruction` / `evalRecipe` (Sphere+Union — enough; no new opcodes this slice; catalog breadth is P2.4).
- **Material stays synthesized** (cos-band color + Y-stripe roughness, as today). Authored per-voxel material is deferred.
- **Live gate authoritative** for the render (lavapipe offscreen + controller/validator read BOTH PNGs), per the project rule.
- **Repo:** VIXEN worktree `/mnt/c/cpp/VBVS--VIXEN/.claude/worktrees/sdf-recipe-codegen-p0` (on branch `feat/sdf-recipe-codegen-p2`). Build: `cmake --preset vixen-wsl && cmake --build build-wsl --target <t>`.

## Scope boundary (deliberate — ponytail deferral)

This slice proves the loop at the **node seam + direct-driver render gate** (how every prior Inc2/Inc3/P2.x render gate is structured — the test reads `node->GetOutput(...)->GetHandle()` and builds its own descriptor). It does **not** wire the octree output slots for per-frame re-bind through a full-graph `DescriptorSetNode`. Reason: no consumer edits recipes through the live app graph yet (UNDERTOW embeds; there is no runtime recipe-edit UI). When one appears, that becomes its own slice — and `INSTANCE_BUFFER` already proves the per-frame re-emit→`DescriptorSetNode::ExecuteImpl` re-bind mechanism to copy. // ponytail: full-graph per-frame octree re-bind deferred until a live-app recipe-edit consumer exists; INSTANCE_BUFFER is the template when it does.

## File Structure

**Modify:**
- `VIXEN/libraries/RenderGraph/include/Nodes/BodyOctreeSceneNode.h` — add `bool recipeDirty_`; declare `void Rematerialize();` and `void DestroyOctreeBuffers();`.
- `VIXEN/libraries/RenderGraph/src/Nodes/BodyOctreeSceneNode.cpp` — `SetBakeRecipe` sets `recipeDirty_`; `CompileImpl` clears it; `ExecuteImpl` re-materializes + re-emits when dirty; factor `DestroyOctreeBuffers()` out of `DestroyBuffers()`; implement `Rematerialize()`.

**Create:**
- A new `TEST_F(BodyInstanceRayMarchRenderTest, RematerializeEditLoop)` in `VIXEN/libraries/RenderGraph/tests/Nodes/test_body_instance_raymarch_render.cpp` (reuses the fixture + `RenderToRgba` + the body-pixel scan; adds a horizontal-extent helper).

---

## Milestone Map

> Persisted for the context-manager pipeline. Two milestones, sequential. VIXEN-only, branch `feat/sdf-recipe-codegen-p2`.

- [x] **M1 `[VIXEN]` — Re-materialize node seam (Task 1).** Node-only change. Gate: full build green + **all existing render/CPU tests still pass** (proves the non-dirty path is byte-identical and nothing regressed). Implementer **Sonnet**, validator **Opus** (tamper-check: confirm `recipeDirty_` defaults false + `CompileImpl` clears it; confirm `DestroyOctreeBuffers` leaves the ring alone).
- [x] **M2 `[VIXEN]` — Live edit→re-render gate (Task 2).** Gate: new `RematerializeEditLoop` renders sphere (A) then, after a runtime `SetBakeRecipe`, the wider peanut (B) on lavapipe — `widthB > 1.15·widthA`, both `fillRatio > 0.97`; controller/validator read `/tmp/glsl_sdf_remat_A.png` + `/tmp/glsl_sdf_remat_B.png`. No-regression on `RenderStoredSdf*`. Implementer **Sonnet**, validator **Opus** (reads BOTH PNGs; tamper-check: stub out the `recipeDirty_` set → B must equal A → test fails).

Validators: **Opus** per milestone. Controller: Opus, thin.

## Progress Log

- **M1 `[VIXEN]` (Task 1): DONE** · commit `253facb8` · Opus validator APPROVED (built + ran himself) · 2026-06-27 — `recipeDirty_` (defaults false; `SetBakeRecipe` sets, `CompileImpl` clears) + `Rematerialize()` (vkDeviceWaitIdle→reset octreesBuilt_→EnsureOctreesBuilt→DestroyOctreeBuffers→CreateOctreeBuffers) + `ExecuteImpl` re-materializes-on-dirty + re-emits the 6 OCTREE_* slots; `DestroyOctreeBuffers()` factored out (ring untouched). Non-dirty path byte-identical. Gates: `test_recipe_bake`/`test_sdf_bake`/`test_soa_sdf_serialize` green; lavapipe `RenderStoredSdfBodiesNoHoles`(0.9895)/`RenderStoredSdfMultiChannel`/`RenderRecipeBakedBody`(0.9969) green. **Env note for M2:** lavapipe needs BOTH `VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json` AND `VK_LAYER_PATH` to the provisioned 1.4.350.1 SDK (`~/.vulkan-sdk/1.4.350.1/x86_64/share/vulkan/explicit_layer.d`); ctest discovery is stale (run exes directly).
- **M2 `[VIXEN]` (Task 2): DONE** · commit `8af50a4e` · Opus validator APPROVED (built + ran the gate + **read both PNGs**: A = single round sphere, B = two-lobe peanut, visibly wider) · 2026-06-27 — `RematerializeEditLoop` renders shape A (single sphere) → `SetBakeRecipe`(peanut) post-Compile → `Execute` re-materializes → re-reads handles → renders B. `[REMAT] A: width=176 px=23968  B: width=232 px=30286` (1.32×, gate ≥1.15×) PASS. Tamper-verified (disable `recipeDirty_=true` → B collapses to A → fails; restored). No-regression: `RenderStoredSdf*`/`RenderRecipeBakedBody` green. **P2.3 COMPLETE — the Materialization edit loop closes: runtime recipe edit → re-bake → re-upload → re-render, no graph rebuild.**
- **Doc nit (validator):** the `vixen-wsl` preset's `binaryDir` is `${sourceDir}/../build-wsl` (worktree ROOT), so from `VIXEN/` build with `cmake --build ../build-wsl` (the literal `build-wsl` is off by a dir).

---

## Task 1 [M1]: Re-materialize node seam

**Files:** Modify `BodyOctreeSceneNode.h`, `BodyOctreeSceneNode.cpp`.

**Interfaces:**
- Consumes: `EnsureOctreesBuilt()`, `CreateOctreeBuffers(VulkanDevice*)`, `GetDevice()` (all existing); the 6 octree buffer/memory members (`nodesBuffer_`/`nodesMemory_`, `bricksBuffer_`/`bricksMemory_`, `materialsBuffer_`/`materialsMemory_`, `configBuffer_`/`configMemory_`, `sdfBuffer_`/`sdfMemory_`, `brickLookupBuffer_`/`brickLookupMemory_`); `octreesBuilt_`, `bakeRecipe_`, `recipeDirty_`.
- Produces: `void Rematerialize()` (private), `void DestroyOctreeBuffers()` (private), `bool recipeDirty_` member. `ExecuteImpl` re-emits `OCTREE_NODES_BUFFER`/`OCTREE_BRICKS_BUFFER`/`OCTREE_MATERIALS_BUFFER`/`OCTREE_CONFIG_BUFFER`/`OCTREE_SDF_BUFFER`/`OCTREE_BRICKLOOKUP_BUFFER` after a re-materialize.

- [ ] **Step 1: Header — add the member + two method decls.** In `BodyOctreeSceneNode.h`, after `octreesBuilt_` (line ~116) add:
```cpp
    bool                                   octreesBuilt_ = false;
    bool                                   recipeDirty_  = false;  // P2.3: set by SetBakeRecipe post-Compile; re-materialize on next Execute
```
In the private methods block (after `void DestroyBuffers();`, line ~103) add:
```cpp
    void DestroyOctreeBuffers();   // P2.3: destroy ONLY the 6 octree/channel buffers (ring untouched)
    void Rematerialize();          // P2.3: re-bake octree 0 + recreate octree buffers (behind vkDeviceWaitIdle)
```

- [ ] **Step 2: Factor `DestroyOctreeBuffers()` out of `DestroyBuffers()`.** In `BodyOctreeSceneNode.cpp`, replace the body of `DestroyBuffers()` (lines ~467-488) with a call to the new factored method + the ring cleanup:
```cpp
void BodyOctreeSceneNode::DestroyOctreeBuffers() {
    if (!GetDevice()) return;
    VkDevice vkDevice = GetDevice()->device;

    auto destroy = [&](VkBuffer& buf, VkDeviceMemory& mem) {
        if (buf != VK_NULL_HANDLE) { vkDestroyBuffer(vkDevice, buf, nullptr); buf = VK_NULL_HANDLE; }
        if (mem != VK_NULL_HANDLE) { vkFreeMemory(vkDevice, mem, nullptr);    mem = VK_NULL_HANDLE; }
    };

    destroy(nodesBuffer_,         nodesMemory_);
    destroy(bricksBuffer_,        bricksMemory_);
    destroy(materialsBuffer_,     materialsMemory_);
    destroy(configBuffer_,        configMemory_);
    destroy(sdfBuffer_,           sdfMemory_);         // Inc2 M3
    destroy(brickLookupBuffer_,   brickLookupMemory_); // Inc2 M3
}

void BodyOctreeSceneNode::DestroyBuffers() {
    DestroyOctreeBuffers();

    // FR-7: destroy the instance ring via PerFrameResources (mirrors DynamicInstanceBufferNode).
    perFrame_.Cleanup();
    instanceRingCapacity_ = 0;

    NODE_LOG_INFO("[BodyOctreeSceneNode] All buffers destroyed");
}
```

- [ ] **Step 3: `SetBakeRecipe` sets the dirty flag.** In `SetBakeRecipe` (line ~141), after the stash add the dirty set:
```cpp
void BodyOctreeSceneNode::SetBakeRecipe(std::vector<Vixen::SVO::Recipe::SdfInstruction> prog) {
    bakeRecipe_  = std::move(prog);
    recipeDirty_ = true;   // P2.3: if already compiled, ExecuteImpl re-materializes on the next frame;
                           //       if pre-Compile, CompileImpl bakes fresh and clears this.
    NODE_LOG_INFO("[BodyOctreeSceneNode] SetBakeRecipe: " +
                  std::to_string(bakeRecipe_.size()) + " instructions — octree 0 will use recipe bake");
}
```

- [ ] **Step 4: `CompileImpl` clears the dirty flag.** At the END of `CompileImpl` (after the final `NODE_LOG_INFO`, line ~211) add:
```cpp
    // A fresh compile already baked the current recipe — no pending re-materialize.
    recipeDirty_ = false;
```

- [ ] **Step 5: Implement `Rematerialize()`.** Add after `EnsureRingAllocated` (or anywhere in the `Vixen::RenderGraph` namespace block in the .cpp):
```cpp
void BodyOctreeSceneNode::Rematerialize() {
    VulkanDevice* device = GetDevice();
    if (!device) {
        NODE_LOG_ERROR("[BodyOctreeSceneNode] Rematerialize called with no device");
        return;
    }
    NODE_LOG_INFO("[BodyOctreeSceneNode] Rematerialize: re-baking octree 0 from edited recipe");

    // Rare, explicit edit path — safe to stall (mirrors the ring-grow vkDeviceWaitIdle).
    // Guarantees no in-flight command buffer still references the octree buffers we free.
    vkDeviceWaitIdle(device->device);

    octreesBuilt_ = false;     // force EnsureOctreesBuilt to re-bake + re-concatenate all 3 octrees
    EnsureOctreesBuilt();      // octree 0 uses the new bakeRecipe_; octrees 1/2 unchanged

    DestroyOctreeBuffers();    // ring is NOT touched
    CreateOctreeBuffers(device);
}
```

- [ ] **Step 6: `ExecuteImpl` re-materializes + re-emits when dirty.** At the START of `ExecuteImpl` (before the `frameIndex` line, ~216) add the re-materialize check; remember to re-emit:
```cpp
    // P2.3: a runtime recipe edit (SetBakeRecipe after Compile) re-bakes + re-uploads here,
    // at the fence-waited safe point — never on the recompile cascade.
    bool octreeRepublished = false;
    if (recipeDirty_) {
        Rematerialize();
        recipeDirty_     = false;
        octreeRepublished = true;
    }
```
Then after the existing instance emit (after line ~241), re-publish the octree handles so consumers re-read the new buffers:
```cpp
    // Re-emit the octree slots with the freshly-created handles after a re-materialize,
    // so GetOutput()->GetHandle() (and any per-frame descriptor re-bind) sees the new buffers.
    if (octreeRepublished) {
        ctx.Out(BodyOctreeSceneNodeConfig::OCTREE_NODES_BUFFER,       nodesBuffer_);
        ctx.Out(BodyOctreeSceneNodeConfig::OCTREE_BRICKS_BUFFER,      bricksBuffer_);
        ctx.Out(BodyOctreeSceneNodeConfig::OCTREE_MATERIALS_BUFFER,   materialsBuffer_);
        ctx.Out(BodyOctreeSceneNodeConfig::OCTREE_CONFIG_BUFFER,      configBuffer_);
        ctx.Out(BodyOctreeSceneNodeConfig::OCTREE_SDF_BUFFER,         sdfBuffer_);
        ctx.Out(BodyOctreeSceneNodeConfig::OCTREE_BRICKLOOKUP_BUFFER, brickLookupBuffer_);
    }
```

- [ ] **Step 7: Build.** `cmake --preset vixen-wsl && cmake --build build-wsl --target test_body_instance_raymarch_render` → compiles clean.

- [ ] **Step 8: No-regression — existing render gates (lavapipe) + CPU suites green.** Run (lavapipe env as in the P2.1/P2.2 gates):
```
ctest --test-dir build-wsl -R "test_recipe_bake|test_sdf_bake|test_soa_sdf_serialize" --output-on-failure
# render no-regression (lavapipe):
./build-wsl/.../test_body_instance_raymarch_render --gtest_filter=*RenderStoredSdfBodiesNoHoles*:*RenderStoredSdfMultiChannel*:*RenderRecipeBakedBody*
```
Expected: all green (the non-dirty path is byte-identical).

- [ ] **Step 9: Commit** `feat(recipe): runtime re-materialize seam on BodyOctreeSceneNode — dirty-flag + Execute re-bake/re-upload (P2.3 M1)`.

## Task 2 [M2]: Live edit → re-render gate

**Files:** Modify `test_body_instance_raymarch_render.cpp` (add one `TEST_F` + a small horizontal-extent helper).

**Interfaces:**
- Consumes: the `BodyInstanceRayMarchRenderTest` fixture, `RenderToRgba(...)`, `MakeInstance`/`MakeCamera`/`ShaderBodyCentre`/`ShaderBodyRadius`, the per-row body-pixel scan (`r>24||g>24||bl>40`), `node->SetBakeRecipe`, `node->Execute()`, `node->GetOutput(...)->GetHandle<VkBuffer>()`.
- Produces: `TEST_F(BodyInstanceRayMarchRenderTest, RematerializeEditLoop)`.

- [ ] **Step 1: Write the failing gate.** This is `RenderRecipeBakedBody` (line ~1151) extended to render TWICE around a runtime edit. Copy that test verbatim, then change: (a) recipe A = a single sphere set BEFORE Compile; (b) factor the handle-read + render + measure into a lambda; (c) after Compile+Execute+renderA, `SetBakeRecipe` the peanut, `Execute` (re-materializes), renderB; (d) assert B is wider. Uses the real fixture API exactly (`NodeBuffers`, `SetHandleVal`/`SetInput`, `RenderToRgba(...,pc,kW,kH,rgba,ms)`, `MakeInstance`, `MakeCamera`, `ShaderBodyCentre`/`ShaderBodyRadius`, `stbi_write_png`):
```cpp
// P2.3 — runtime edit → re-materialize → re-render. Renders octree 0 as a single sphere (A),
// edits the recipe to a sphere∪sphere peanut (B) via SetBakeRecipe AFTER Compile, drives one
// Execute (which re-materializes), re-reads the (new) handles, and re-renders. B must be a
// WIDER body than A — proving the GPU saw the edit with no graph rebuild / restart.
TEST_F(BodyInstanceRayMarchRenderTest, RematerializeEditLoop) {
    ASSERT_TRUE(softwareConfirmed_);
    using C = BodyOctreeSceneNodeConfig;

    BodyOctreeSceneNodeType nodeType("BodyOctreeScene");
    auto nodeBase = nodeType.CreateInstance("body_render_remat");
    auto* node = dynamic_cast<BodyOctreeSceneNode*>(nodeBase.get());
    ASSERT_NE(node, nullptr);

    Resource deviceRes; SetHandleVal<VulkanDevice*>(deviceRes, deviceShell_.get());
    Resource poolRes;   SetHandleVal<VkCommandPool>(poolRes, commandPool_);
    Resource frameRes;  uint32_t frameIndex = 0; SetHandleVal<uint32_t>(frameRes, frameIndex);
    node->SetInput(C::VULKAN_DEVICE_IN_Slot::index,    0, &deviceRes);
    node->SetInput(C::COMMAND_POOL_Slot::index,        0, &poolRes);
    node->SetInput(C::CURRENT_FRAME_INDEX_Slot::index, 0, &frameRes);

    constexpr float kRS = 2.0f;
    const Vixen::SVO::BodyInstanceGpu frameInst = MakeInstance(0.0f, 0.0f, 0.0f, kRS, 0, 1.0f, 1.0f, 1.0f);
    node->SetInstances({ frameInst });

    auto makeSph = [](glm::vec3 c, float r) {
        Vixen::SVO::Recipe::SdfInstruction in{};
        in.opCode  = static_cast<uint8_t>(Vixen::SVO::Recipe::SdfOpCode::Sphere);
        in.data[0] = c.x; in.data[1] = c.y; in.data[2] = c.z; in.data[3] = r;
        return in;
    };
    Vixen::SVO::Recipe::SdfInstruction uni{};
    uni.opCode = static_cast<uint8_t>(Vixen::SVO::Recipe::SdfOpCode::Union);

    // Recipe A: a single centred sphere (one round lobe).
    node->SetBakeRecipe({ makeSph({32.0f, 32.0f, 32.0f}, 18.0f) });

    node->Setup();
    // Keep STORED_SDF_DEMO set across BOTH executes — the edit Execute's Rematerialize()
    // re-runs EnsureOctreesBuilt(), which gates on this env var.
    ::setenv("VIXEN_STORED_SDF_DEMO", "1", /*overwrite=*/1);
    ASSERT_NO_THROW(node->Compile());      // bakes recipe A; clears recipeDirty_
    ASSERT_NO_THROW(node->Execute());      // uploads instance; no re-materialize (not dirty)

    constexpr uint32_t kW = 512, kH = 512;
    const glm::vec3 focus = ShaderBodyCentre(frameInst);
    const float     R     = ShaderBodyRadius(frameInst);
    const glm::vec3 eye   = focus + glm::normalize(glm::vec3(0.3f, 0.25f, 1.0f)) * (R * 4.0f);
    const PushConstants pc = MakeCamera(eye, focus, kW, kH, 1);

    // Render octree 0 with the node's CURRENT buffers + measure body width (global maxX-minX
    // over body pixels) and pixel count. Re-reads handles each call so a re-materialized
    // (recreated) buffer is picked up.
    auto renderAndMeasure = [&](const char* png, int& outWidth, int& outBodyPx) {
        NodeBuffers b;
        b.nodes       = node->GetOutput(C::OCTREE_NODES_BUFFER_Slot::index, 0)->GetHandle<VkBuffer>();
        b.bricks      = node->GetOutput(C::OCTREE_BRICKS_BUFFER_Slot::index, 0)->GetHandle<VkBuffer>();
        b.materials   = node->GetOutput(C::OCTREE_MATERIALS_BUFFER_Slot::index, 0)->GetHandle<VkBuffer>();
        b.config      = node->GetOutput(C::OCTREE_CONFIG_BUFFER_Slot::index, 0)->GetHandle<VkBuffer>();
        b.sdf         = node->GetOutput(C::OCTREE_SDF_BUFFER_Slot::index, 0)->GetHandle<VkBuffer>();
        b.brickLookup = node->GetOutput(C::OCTREE_BRICKLOOKUP_BUFFER_Slot::index, 0)->GetHandle<VkBuffer>();
        VkBuffer inst = node->GetOutput(C::INSTANCE_BUFFER_Slot::index, 0)->GetHandle<VkBuffer>();
        ASSERT_NE(b.sdf, VK_NULL_HANDLE);
        ASSERT_NE(inst,  VK_NULL_HANDLE);

        std::vector<uint8_t> rgba; double ms = 0.0;
        ASSERT_NO_FATAL_FAILURE(RenderToRgba(b.nodes, b.bricks, b.materials, b.config, inst,
                                             b.sdf, b.brickLookup, pc, kW, kH, rgba, ms));
        {
            std::vector<uint8_t> rgb(static_cast<size_t>(kW) * kH * 3);
            for (uint32_t i = 0; i < kW * kH; ++i) { rgb[i*3+0]=rgba[i*4+0]; rgb[i*3+1]=rgba[i*4+1]; rgb[i*3+2]=rgba[i*4+2]; }
            EXPECT_NE(stbi_write_png(png, kW, kH, 3, rgb.data(), kW*3), 0) << "stbi_write_png failed for " << png;
        }
        int minX = int(kW), maxX = -1, bodyPx = 0;
        for (uint32_t y = 0; y < kH; ++y) for (uint32_t x = 0; x < kW; ++x) {
            const uint8_t* px = &rgba[(static_cast<size_t>(y)*kW + x)*4];
            if (px[0] > 24 || px[1] > 24 || px[2] > 40) { if (int(x)<minX) minX=int(x); if (int(x)>maxX) maxX=int(x); ++bodyPx; }
        }
        outWidth  = (maxX < minX) ? 0 : (maxX - minX + 1);
        outBodyPx = bodyPx;
    };

    // --- render A (single sphere) ---
    int widthA = 0, pxA = 0;
    renderAndMeasure("/tmp/glsl_sdf_remat_A.png", widthA, pxA);

    // --- EDIT at runtime: sphere∪sphere peanut (two offset lobes, wider than A) ---
    node->SetBakeRecipe({
        makeSph({24.0f, 32.0f, 32.0f}, 16.0f),
        makeSph({40.0f, 32.0f, 32.0f}, 16.0f),
        uni
    });
    frameIndex = 0; SetHandleVal<uint32_t>(frameRes, frameIndex);
    ASSERT_NO_THROW(node->Execute());        // recipeDirty_ -> Rematerialize + re-emit octree slots
    ::unsetenv("VIXEN_STORED_SDF_DEMO");

    // --- render B (re-reads the NEW handles) ---
    int widthB = 0, pxB = 0;
    renderAndMeasure("/tmp/glsl_sdf_remat_B.png", widthB, pxB);

    std::printf("[REMAT] A: width=%d px=%d  B: width=%d px=%d  (B = wider peanut)\n",
                widthA, pxA, widthB, pxB);
    EXPECT_GT(widthA, 0) << "shape A (sphere) did not render";
    EXPECT_GT(pxB, 20000) << "shape B barely rendered";
    EXPECT_GT(widthB, static_cast<int>(widthA * 1.15f))
        << "edit did NOT re-materialize: B not wider than A (widthA=" << widthA
        << " widthB=" << widthB << ")";
}
```
> **Implementer note:** every symbol above is real and copied from `RenderRecipeBakedBody` (line ~1151): `NodeBuffers` (fields `.nodes/.bricks/.materials/.config/.sdf/.brickLookup`), `Resource`/`SetHandleVal`/`SetInput`, `deviceShell_`/`commandPool_` (fixture members), `MakeInstance`, `MakeCamera`, `ShaderBodyCentre`/`ShaderBodyRadius`, `PushConstants`, `RenderToRgba(nodes,bricks,materials,config,inst,sdf,brickLookup,pc,w,h,rgba,ms)`, `stbi_write_png`. Do NOT invent helpers — diff against `RenderRecipeBakedBody` and keep everything except the A/edit/B structure.

- [ ] **Step 2: Run → FAIL or PASS-by-luck check.** Build + run on lavapipe:
```
cmake --build build-wsl --target test_body_instance_raymarch_render
VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json \
  ./build-wsl/libraries/RenderGraph/tests/Nodes/test_body_instance_raymarch_render \
  --gtest_filter=*RematerializeEditLoop*
```
Expected with M1 present: PASS (widthB ≈ 1.4-1.6× widthA). **Tamper-check (validator):** comment out the `recipeDirty_ = true;` line in `SetBakeRecipe` → rebuild → B renders identical to A → `widthB ≈ widthA` → test FAILS. Restore.

- [ ] **Step 3: Controller/validator read both PNGs.** Confirm `/tmp/glsl_sdf_remat_A.png` is a single round blob and `/tmp/glsl_sdf_remat_B.png` is a two-lobe peanut — the runtime edit visibly changed the rendered geometry.

- [ ] **Step 4: No-regression.** `./...test_body_instance_raymarch_render --gtest_filter=*RenderStoredSdf*:*RenderRecipeBakedBody*` → green.

- [ ] **Step 5: Commit** `test(recipe): live edit->re-materialize->re-render gate — sphere->peanut at runtime (P2.3 M2)`.

## Self-Review

**Coverage:** the edit loop = (recipe edit at runtime) → (re-bake + re-upload) → (re-render shows the edit). Task 1 builds the seam (dirty-flag + `Rematerialize` + re-emit); Task 2 proves it live (A≠B, wider peanut) + tamper-checks it + no-regresses the static path. The D1 "full edit loop" deliverable from the Inc4 design (§2) is closed at the node seam. ✓
**Placeholders:** Task 1 gives every edited line with exact surrounding context + line anchors; Task 2 gives the full test, flagging `MakeInstances1`/`ConnectNodeInputs`/`Bufs` as "copy the wiring `RenderRecipeBakedBody` already does" (the implementer locates the real names — they exist). No "TODO". ✓
**Type consistency:** `recipeDirty_` (bool), `Rematerialize()`/`DestroyOctreeBuffers()` (void, no args), `SetBakeRecipe(std::vector<Recipe::SdfInstruction>)`, the 6 `OCTREE_*` slot ids — consistent across H/CPP/test. ✓
**Risk:** the only GPU-lifetime surface is `Rematerialize`'s `DestroyOctreeBuffers`; it runs ONLY behind `vkDeviceWaitIdle` and ONLY on an explicit edit frame — the documented VM-panic trap (free-in-flight) is structurally avoided. The instance ring is never touched. Lavapipe A≠B is the authoritative gate.

## Execution Handoff

Run via post-brainstorm-context-manager (2 milestones). M1 Sonnet+Opus (node seam; validator tamper-checks the dirty-default + ring-untouched). M2 Sonnet+Opus (lavapipe edit gate — validator AND controller read BOTH PNGs, confirm A=sphere → B=peanut).
