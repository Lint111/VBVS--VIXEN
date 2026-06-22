# Stored SDF Provider — Increment 2 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development or the post-brainstorm-context-manager pipeline to implement this plan milestone-by-milestone. Steps use checkbox (`- [ ]`) syntax. **Each milestone's GPU/shader work is verified by the controller** (Windows ninja build + live gate); CPU work is gtest-verified. See the Inc1 plan's "Critical project facts" for the worktree-build protocol (sandbox-off, `cmake --preset vixen-ninja` from the worktree, artifact+test ground-truth, never overlap builds).

**Goal:** Add the **Stored** SDF provider — bodies carrying a per-voxel signed-distance field, rendered to a smooth trilinear iso-surface with cross-brick sampling — gated by baking the Inc1 recipe into Stored bricks and A/B-ing it against the Procedural render.

**Architecture:** Bake an `SdfRecipes` recipe into a `GaiaVoxelWorld` of narrow-band SDF voxels (SDF carried as the `Density` float) → `SVOBuilder` octree. `ShellOctreeGpu::Serialize` emits a per-brick **SoA-SDF** GPU buffer + serializes the octree's existing **`brickGridToBrickView`** grid→brick map + writes a layout descriptor (`formatId=STORED_SDF`, channel offset) into the `OctreeConfig` tail (bytes 200–255). `BodyInstanceRayMarch.comp` gains a Stored-SDF handler (trilinear iso-surface + cross-brick neighbor fetch via the lookup + SDF-gradient normal), selected by `formatId`. Binary ESVO + Inc1 Procedural paths untouched.

**Tech Stack:** C++23, GLSL compute (std430 SSBO), glm, GoogleTest, CMake ninja preset, Vulkan 1.3.

**Design of record:** `Voxel-Stored-SDF-Provider-Inc2-Design-2026-06.md` (+ contract doc `Voxel-Content-Format-Contract-Design-2026-06.md`). **Reuses:** `SdfRecipes.h`/`.glsl` (Inc1).

---

## Milestone Map

> context-manager pipeline (confirmed 2026-06-22). Controller owns the Windows ninja build +
> live gate (path-pinned/PDB-hazard); Sonnet implementers write code + commit; Opus validates each.
> Worktree-isolated build via `C:\cpp\_wt_build.bat <targets>` (points at `.claude/worktrees/stored-sdf`).

- **M1 — Bake path** (Tasks 1–2) · Sonnet · gate: `test_sdf_bake` gtest green (controller-run) + Opus validator. · **✅ DONE 2026-06-22**
- **M2 — SoA-SDF serialize + lookup + descriptor** (Tasks 3–4) · Sonnet · gate: `test_soa_sdf_serialize` gtest green (controller-run) + Opus validator. · **✅ DONE 2026-06-22** — descriptor in `Vixen::SVO::OctreeConfig` tail (432 B struct, SPIR-V ArrayStride 432): `formatId`@200, `bricksPerAxis`@204, `sdfBrickArrayBase`@208. M4 GLSL must match these offsets.
- **M3 — GPU + shader integration** (Tasks 5–9: 2 buffers + descriptor bindings + shader bindings 11/12 + Stored-SDF handler) · Sonnet · gate: `VIXEN.exe` links + shader runtime-compiles (controller-run) + Opus validator; full correctness at the **M5 live gate**. *(M3+M4 merged 2026-06-22 — buffers, descriptor bindings, and shader bindings are one coupled unit; the descriptor layout needs the shader to declare bindings 11/12, and the GLSL `OctreeConfig` tail must match M2's byte offsets formatId@200/bricksPerAxis@204/sdfBrickArrayBase@208.)*
- **M5 — Gate + verify** (Tasks 10–11) · controller/interactive · live A/B (Stored matches Procedural, 0 syncval) + no-regression.

### Progress Log
- Milestone 1 (Tasks 1–2): **DONE** · commits `4bcdf5d2`..`b9497543` · gate `test_sdf_bake` [PASSED 2] · Opus validator OK (1 prescribed test-compile fix applied) · 2026-06-22
- Milestone 2 (Tasks 3–4): **DONE** · commit `0f3e4bb3` · gate `test_soa_sdf_serialize` [PASSED 8] · Opus validator OK (verified SPIR-V ArrayStride 432 == upload stride; descriptor sound) · 2026-06-22
- _(Milestone 3 — GPU wiring — in progress)_

---

## Critical surfaces (grounded 2026-06-22)

- **Body serializer = `ShellOctreeGpu::Serialize`/`Concatenate`** (`libraries/SVO/include/ShellOctreeGpu.h`): per-voxel pack at `:304–318` (`world.getEntityByWorldSpace(pos)` → `Material`); `OctreeConfig` emit at `:334–373`; `SerializedOctree` struct `:184`, `ConcatenatedOctrees` `:203`. **NOT `VoxelSceneCacher`** (that's the VoxelGridNode path).
- **Grid→brick lookup exists:** `SVOBuilder::brickGridToBrickView` (`SVOBuilder.h:59–61`, lookup `:111`) — `unordered_map<uint32_t key, uint32_t brickViewIdx>`, key = packed brick-grid coord. Serialize this to a GPU buffer for cross-brick sampling.
- **Per-voxel SDF source:** voxels carry a `Density` (Float) component (`AttributeRegistry` key; `world.getComponentValue<Density>(entity)` — see `SVORebuild.cpp:158`). The bake writes signed distance there; serialize reads it (instead of binarizing).
- **`OctreeConfig` tail:** the body `OctreeConfig` (shader struct in `BodyInstanceRayMarch.comp:91–109`) uses bytes 192/196 for `nodeArrayBase`/`brickArrayBase`, then `_padding4_tail[14]` (bytes 200–255, 56 B free) — home for the layout descriptor.
- **GPU buffers/bindings:** `BodyOctreeSceneNode::CreateOctreeBuffers` (`.cpp:279–313`) + `CreateBuffer` helper (`:48–67`); `OUTPUT_SLOT`s in `BodyOctreeSceneNodeConfig.h:61–85` map to shader bindings (nodes=1, bricks=2, materials=3, config=5, instance=10). New buffers mirror this pattern.

## File Structure

**Create:**
- `VIXEN/libraries/SVO/include/SdfBake.h` — bake `SdfRecipes` recipe → `GaiaVoxelWorld` narrow-band SDF voxels (+ octree). One responsibility: recipe→Stored voxel content.
- `VIXEN/libraries/SVO/tests/test_sdf_bake.cpp` — gtest: bake round-trip + narrow-band correctness.
- `VIXEN/libraries/SVO/tests/test_soa_sdf_serialize.cpp` — gtest: SoA-SDF pack/offset + grid-lookup serialize + descriptor.
- `VIXEN/shaders/StoredSdf.glsl` — trilinear-fetch + cross-brick + iso-surface march helpers.

**Modify:**
- `VIXEN/libraries/SVO/include/ShellOctreeGpu.h` — `SerializedOctree`/`ConcatenatedOctrees` gain `sdfBricks` + `brickGridLookup` byte buffers; `Serialize`/`Concatenate` emit them + write the descriptor.
- `VIXEN/libraries/RenderGraph/include/Data/Nodes/BodyOctreeSceneNodeConfig.h` — 2 new `OUTPUT_SLOT`s (OCTREE_SDF_BUFFER, OCTREE_BRICKLOOKUP_BUFFER).
- `VIXEN/libraries/RenderGraph/src/Nodes/BodyOctreeSceneNode.cpp` — 2 new buffer members + `CreateBuffer` + outputs.
- `VIXEN/shaders/BodyInstanceRayMarch.comp` — new bindings (11=SDF, 12=lookup); `formatId` dispatch → Stored-SDF handler.
- `VIXEN/application/main/source/graph/BuildRenderGraph.cpp` — `VIXEN_STORED_SDF_DEMO` gate (bake the 3 bodies) + wire the 2 new buffers into the dispatch descriptor set.

**Unchanged (must not regress):** binary ESVO path, Inc1 Procedural provider + `SdfRecipes`.

---

## Milestone M1 — Bake path (CPU): recipe → narrow-band SDF voxels

**Files:** Create `SdfBake.h` + `test_sdf_bake.cpp`; register in `libraries/SVO/tests/CMakeLists.txt` (mirror the `test_sdf_recipes` block).

### Task 1: `BakeRecipeToSdfWorld`

- [ ] **Step 1: Failing test** — `test_sdf_bake.cpp`:
```cpp
#include <gtest/gtest.h>
#include <glm/glm.hpp>
#include "SdfBake.h"
#include "SdfRecipes.h"
using namespace Vixen::SVO;

// Bake a sphere recipe into a [0,n]^3 grid; every voxel in the narrow band must
// hold the recipe's signed distance (within the grid quantization).
TEST(SdfBake, NarrowBandMatchesRecipe) {
    const int n = 16;
    const glm::vec3 center(8.0f, 8.0f, 8.0f);
    RecipeParams rp{6.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};   // sphere r=6 in grid units
    const float bandVoxels = 2.0f;
    SdfBakeResult baked = BakeRecipeToSdfWorld(RECIPE_SPHERE, center, rp, n, bandVoxels);
    // A voxel near the surface (grid pos on the +x axis at radius 6) is in-band and
    // its stored SDF ≈ evalSdf at that grid point.
    glm::vec3 p(14.0f, 8.0f, 8.0f);   // distance 6 from center → ~surface
    auto sd = baked.sampleStored(p);
    ASSERT_TRUE(sd.has_value());
    EXPECT_NEAR(*sd, evalSdf(RECIPE_SPHERE, p, center, rp), 0.6f);   // grid-cell tolerance
    // A far-exterior voxel is NOT stored (outside the band → unallocated).
    EXPECT_FALSE(baked.sampleStored(glm::vec3(0.0f, 0.0f, 0.0f)).has_value());
}
```

- [ ] **Step 2: Build+run → FAIL** (no `SdfBake.h`). Controller runs: `cmd.exe /c "C:\cpp\_wt_build.bat test_sdf_bake"` then runs the exe.

- [ ] **Step 3: Implement `SdfBake.h`** — sample the recipe over the integer grid `[0,n)^3`; for each cell whose `|evalSdf| <= bandVoxels` (narrow band), create a voxel entity carrying `Density = signedDistance` (+ a Material for fallback shading). Provide `SdfBakeResult { std::unique_ptr<GaiaVoxelWorld> world; std::unique_ptr<AttributeRegistry> registry; std::optional<float> sampleStored(glm::vec3) const; }`. Mirror `ShellOctree.h::BuildShellOctree`'s world/registry/entity-creation idiom (registerKey "density" Float, createVoxel with `Density{sd}`), but store the *real* signed distance, not `1.0`. (Read `ShellOctree.h:59–110` for the exact `GaiaVoxelWorld`/`createVoxel` calls.)
```cpp
#pragma once
#include "SdfRecipes.h"
#include "GaiaVoxelWorld.h"
#include "VoxelComponents.h"
#include "AttributeRegistry.h"
#include <glm/glm.hpp>
#include <memory>
#include <optional>

namespace Vixen::SVO {
struct SdfBakeResult {
    std::unique_ptr<Vixen::GaiaVoxel::GaiaVoxelWorld> world;
    std::unique_ptr<AttributeRegistry> registry;
    int n = 0;
    glm::vec3 center{0};
    // sampleStored: returns the baked Density (signed distance) at integer grid pos, or
    // nullopt if that cell is outside the narrow band (unallocated).
    std::optional<float> sampleStored(const glm::vec3& gridPos) const;   // impl below
};

inline SdfBakeResult BakeRecipeToSdfWorld(uint32_t recipeId, const glm::vec3& center,
                                          const RecipeParams& rp, int n, float bandVoxels) {
    SdfBakeResult r;
    r.n = n; r.center = center;
    r.registry = std::make_unique<AttributeRegistry>();
    r.registry->registerKey("density", Vixen::VoxelData::AttributeType::Float, 0.0f);
    r.registry->addAttribute("color", Vixen::VoxelData::AttributeType::Vec3, glm::vec3(1.0f));
    r.world = std::make_unique<Vixen::GaiaVoxel::GaiaVoxelWorld>();
    for (int z = 0; z < n; ++z)
      for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x) {
            glm::vec3 p((float)x, (float)y, (float)z);
            float sd = evalSdf(recipeId, p, center, rp);
            if (std::abs(sd) <= bandVoxels) {
                const Vixen::GaiaVoxel::ComponentQueryRequest comps[] = {
                    Vixen::GaiaVoxel::Density{sd},
                    Vixen::GaiaVoxel::Color{glm::vec3(1.0f)},
                    Vixen::GaiaVoxel::Material{1u},
                };
                r.world->createVoxel(Vixen::GaiaVoxel::VoxelCreationRequest{p, comps});
            }
        }
    return r;
}

inline std::optional<float> SdfBakeResult::sampleStored(const glm::vec3& gridPos) const {
    auto e = world->getEntityByWorldSpace(gridPos);
    if (!world->exists(e)) return std::nullopt;
    auto d = world->getComponentValue<Vixen::GaiaVoxel::Density>(e);
    return d.has_value() ? std::optional<float>(d.value()) : std::nullopt;
}
}  // namespace Vixen::SVO
```
(Verify the exact `Density`/`Color`/`Material` component types + `getComponentValue`/`getEntityByWorldSpace`/`exists` signatures against `ShellOctree.h` + `SVORebuild.cpp:158`; adjust names if the API differs.)

- [ ] **Step 4: Build+run → PASS** (`test_sdf_bake` green).
- [ ] **Step 5: Commit** `feat(svo): SDF bake — recipe → narrow-band Density voxels (Inc2 M1)`.

### Task 2: build the octree from the baked world
- [ ] **Step 1:** add `BuildSdfBodyOctree(const SdfBakeResult&, int brickDepth=3)` to `SdfBake.h` returning a `ShellOctree`-shaped owning bundle (world+registry+`LaineKarrasOctree`), mirroring `BuildShellOctree` (`ShellOctree.h:59–110`) but over the baked world. Test: octree builds, `castRay`/brickViews non-empty. Commit `feat(svo): build body octree from baked SDF world (Inc2 M1)`.

---

## Milestone M2 — SoA-SDF serialize + grid-lookup + descriptor (CPU)

**Files:** modify `ShellOctreeGpu.h`; create `test_soa_sdf_serialize.cpp`.

### Task 3: extend `SerializedOctree`/`ConcatenatedOctrees` + emit SoA-SDF bricks
- [ ] **Step 1: Failing test** — `test_soa_sdf_serialize.cpp`: bake a sphere (M1), build the octree, `SerializeSdf(...)`, assert: `out.sdfBricks.size() == brickCount * 512 * sizeof(float)`; the SoA SDF for a known surface voxel ≈ its baked Density.
```cpp
#include <gtest/gtest.h>
#include "SdfBake.h"
#include "ShellOctreeGpu.h"
using namespace Vixen::SVO;
TEST(SoaSdfSerialize, BrickHoldsSignedDistance) {
    auto baked = BakeRecipeToSdfWorld(RECIPE_SPHERE, glm::vec3(8,8,8),
                                      RecipeParams{6,0,0,0,0,0}, 16, 2.0f);
    auto body = BuildSdfBodyOctree(baked, 3);
    SerializedOctree out = Serialize(body, /*emitSdf=*/true);
    ASSERT_FALSE(out.sdfBricks.empty());
    EXPECT_EQ(out.sdfBricks.size(), out.brickCount * SerializedOctree::kVoxelsPerBrick * sizeof(float));
    EXPECT_GT(out.brickGridLookup.size(), 0u);                 // lookup serialized
    EXPECT_EQ(out.config._padding4_tailFormatId(), STORED_SDF); // descriptor written (helper)
}
```
- [ ] **Step 2: Build → FAIL.**
- [ ] **Step 3: Implement.** In `ShellOctreeGpu.h`: add `std::vector<uint8_t> sdfBricks; std::vector<uint8_t> brickGridLookup;` to `SerializedOctree` (+ concatenated equivalents). Add an `emitSdf` param (or a `SerializeSdf`): in the brick loop (`:304–318`), additionally read `Density` (float) per voxel into a `std::vector<float> sdfWords` (same z*64+y*8+x order), `memcpy` to `sdfBricks`. Serialize `SVOBuilder::brickGridToBrickView` (the octree's `oct->...` map — read its accessor) as a flat `uint32` array (key, brickViewIdx pairs, or a dense grid table sized `bricksPerAxis^3`). Write the descriptor into `OctreeConfig` tail: define a small POD overlaying `_padding4` → `{ uint32 formatId; uint32 sdfChannelStride; uint32 bricksPerAxis; ... }`; add `STORED_SDF=1u` constant + a `_padding4_tailFormatId()` accessor for the test. Update `Concatenate` to append `sdfBricks`/`brickGridLookup` with per-octree base offsets (extend the descriptor with `sdfBrickArrayBase`).
- [ ] **Step 4: Build+run → PASS.**
- [ ] **Step 5: Commit** `feat(svo): SoA-SDF brick + grid-lookup serialize + OctreeConfig descriptor (Inc2 M2)`.

### Task 4: dense grid-lookup table for O(1) GPU neighbor resolution
- [ ] Decide the lookup GPU form: a **dense `bricksPerAxis^3` `uint32` table** (brick-grid coord → brickIndex, `0xFFFFFFFF` = unallocated) is simplest for the shader (direct index, no hashing). Populate it from `brickGridToBrickView` during Serialize. Test: every allocated brick's grid cell maps back to its index; empties = sentinel. Commit `feat(svo): dense brick-grid lookup table for cross-brick sampling (Inc2 M2)`.

---

## Milestone M3 — GPU wiring (2 new buffers + bindings)

**Files:** `BodyOctreeSceneNodeConfig.h`, `BodyOctreeSceneNode.cpp`.

- [ ] **Task 5:** Mirror the existing buffer pattern EXACTLY (read `BodyOctreeSceneNode.cpp:279–313` + `Config.h:61–85`):
  - Add members `VkBuffer sdfBuffer_/brickLookupBuffer_` + `VkDeviceMemory` (next to `bricksBuffer_`).
  - In `CreateOctreeBuffers`, `CreateBuffer(...)` both from `concatenated_.sdfBricks` / `.brickGridLookup` (pad-to-min like the others; STORAGE_BUFFER usage).
  - Add `OUTPUT_SLOT(OCTREE_SDF_BUFFER, VkBuffer, 6, ...)` + `OCTREE_BRICKLOOKUP_BUFFER, 7` + `INIT_OUTPUT_DESC` + bump the output count (6→8) + `static_assert`s.
  - `ctx.Out(...)` both in `CompileImpl`.
  - Build gate (controller): `VIXEN.exe` links; no descriptor-layout assertion failures. Commit `feat(rendergraph): wire SoA-SDF + brick-lookup buffers into BodyOctreeSceneNode (Inc2 M3)`.
- [ ] **Task 6:** Wire the 2 new outputs into the dispatch descriptor set (the graph edge `BodyOctreeSceneNode → ComputeDispatchNode` in `BuildRenderGraph.cpp` — read how OCTREE_NODES_BUFFER→binding 1 is wired and replicate for binding 11/12). Build gate. Commit `feat(app): bind SDF+lookup buffers to BodyInstanceRayMarch (Inc2 M3)`.

---

## Milestone M4 — Shader Stored-SDF handler

**Files:** create `StoredSdf.glsl`; modify `BodyInstanceRayMarch.comp`.

- [ ] **Task 7:** Add bindings + descriptor struct to `.comp`:
```glsl
layout(std430, binding = 11) readonly buffer SdfBrickBuffer { float sdfData[]; };
layout(std430, binding = 12) readonly buffer BrickLookupBuffer { uint brickLookup[]; };
#define FORMAT_BINARY 0u
#define FORMAT_STORED_SDF 1u
```
Read `formatId` from the octree config tail (add the field to the GLSL `OctreeConfig` struct in the `_padding4_tail` region — keep byte offsets matching the C++ descriptor).
- [ ] **Task 8:** `StoredSdf.glsl` — `float sampleSdfTrilinear(vec3 gridPos, ...)` (8-corner trilinear; each corner: in-brick → `sdfData[base+voxelIdx]`, out-of-brick → resolve neighbor brick via `brickLookup[gridToIndex(...)]`, sentinel `1e9` if unallocated); `vec3 sdfGradientStored(...)` (central differences using `sampleSdfTrilinear`); `bool marchStoredSdf(...)` (step the ray, sphere-trace on the trilinear field to the zero crossing, refine, return hit + gradient normal). Reuse Inc1's downstream (`computeLighting`).
- [ ] **Task 9:** In `main()`'s instance loop, dispatch on `formatId`: `FORMAT_STORED_SDF` → `marchStoredSdf` (parallels the Inc1 Procedural branch, `continue`s); else existing binary ESVO. Controller build gate (shader runtime-compiles at app launch — verified in M5). Commit per task.

---

## Milestone M5 — Gate (bake the 3-body scene as Stored SDF) + verify

**Files:** `BuildRenderGraph.cpp`.

- [ ] **Task 10:** Behind `VIXEN_STORED_SDF_DEMO`, replace the body seed: bake each of the 3 recipes (M1) into Stored bodies, mark their `OctreeConfig.formatId=STORED_SDF`, feed `BodyOctreeSceneNode`. Keep Procedural as the default (no env var).
- [ ] **Task 11 (live gate — controller/interactive):** Build (`_wt_build.bat`), run `cmd.exe /c "set VIXEN_STORED_SDF_DEMO=1&& set VIXEN_VULKAN_VALIDATION=1&& <worktree>\VIXEN\binaries\VIXEN.exe"`, confirm: **smooth spheres via the Stored path** that **match the Procedural render** (A/B oracle), 0 VUID/validation. Also run default (Procedural) → unchanged. No-regression: `test_gpu_parity`, brick traversal still pass. Record result in this plan's Progress Log + `memory-bank/activeContext.md`. Commit `docs(progress): Inc2 live gate — Stored SDF matches Procedural, 0 syncval`.

---

## Self-Review

**Spec coverage:** §3.1 SoA cacher → M2 Task 3; §3.2 bake → M1; §3.3 shader handler → M4; §3.4 gate → M5; §2 cross-brick sampling → M2 Task 4 (lookup) + M4 Task 8 (shader fetch); §4 data model → M2 + M4 Task 7; §6 testing → M1/M2 tests + M5 oracle. ✓

**Open at execution time (read referenced code to finalize exact signatures):** the `GaiaVoxelWorld`/`Density` component API (M1 — verify vs `ShellOctree.h`/`SVORebuild.cpp:158`); the `brickGridToBrickView` accessor on the built octree (M2); the `OCTREE_NODES_BUFFER→binding 1` descriptor wiring to replicate (M3 Task 6); the GLSL `OctreeConfig` tail field offsets matching the C++ descriptor (M4 Task 7). These are concrete (real file:line refs), not open design — the implementer confirms signatures against the cited code.

**Type consistency:** `SdfBakeResult`, `BakeRecipeToSdfWorld`, `BuildSdfBodyOctree`, `SerializedOctree.sdfBricks`/`brickGridLookup`, `STORED_SDF`/`FORMAT_STORED_SDF=1u`, bindings 11 (SDF) / 12 (lookup), slots 6/7 — used consistently across tasks. ✓

**Note:** M3/M4 (GPU wiring + shader) are the highest-risk; the live gate (M5) is authoritative for them since the shader runtime-compiles. Recommend the context-manager pipeline with controller-run Windows build+live gates (as Inc1).
