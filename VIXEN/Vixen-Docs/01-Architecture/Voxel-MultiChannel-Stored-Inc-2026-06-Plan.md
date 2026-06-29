# Generic Multi-Channel Stored Provider — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: use superpowers:subagent-driven-development or the
> post-brainstorm-context-manager pipeline to implement this plan milestone-by-milestone. Steps use
> checkbox (`- [ ]`) syntax. **GPU/shader milestones are verified by the controller** (WSL/lavapipe
> offscreen render gate + MSVC no-regression build); CPU milestones are gtest-verified. WSL build:
> `cmake --preset vixen-wsl` (auto-provisions the SDK) → build the test target; run with
> `VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json VK_LAYER_PATH=<sdk>/x86_64/share/vulkan/explicit_layer.d`.
> Windows app/no-regression: `cmd.exe /c "C:\cpp\_wt_build.bat VIXEN"` (sandbox-off). The SPIR-V
> custom command now tracks `.glsl` includes; if in doubt `touch shaders/BodyInstanceRayMarch.comp`.

**Goal:** Generalize the Stored provider from "SDF + material-id" to a VIXEN-owned, schema-derived
**multi-channel SoA channel pool** the shader reads **by semantic**, wiring `sdf`+`color`+`roughness`
end-to-end so the later Materialization bake is generic from day one.

**Architecture:** One SSBO channel pool (SDF becomes channel 0), per-brick SoA, addressed by a
per-channel descriptor table (semantic + elemCount + channelBase + fieldKind) in the `OctreeConfig`
tail. The channel vocabulary (`SEM_*`/`FK_*`) + descriptor are VIXEN engine abstractions, defined
once in a VIXEN header mirrored 1:1 in GLSL. Reuses the M6 ESVO-leaf-hit traversal unchanged; only
channel *storage/addressing* generalizes.

**Tech Stack:** C++23, GLSL compute (std430/std140), glm, GoogleTest, CMake (vixen-wsl + vixen-ninja),
Vulkan 1.3 (lavapipe for the offscreen gate).

**Design of record:** [[Voxel-MultiChannel-Stored-Inc-2026-06-Design]] (+ contract
[[Voxel-Content-Format-Contract-Design-2026-06]]). **Ownership invariant:** format is VIXEN's; a game
(UNDERSET) only conforms — see [[vixen-owns-content-format-not-consumer]].

---

## Milestone Map

- **M1 — VIXEN channel vocabulary + descriptor** ✅ DONE (Tasks 1–2) · CPU + GLSL · gate: C++ compiles + glslc
  compiles the shader; `static_assert`s hold. The engine-owned `SEM_*`/`FK_*` enums + the extended
  `OctreeConfig` channel table, single-sourced CPU↔GLSL.
- **M2 — Generic SoA pool serialize + bake + GPU wiring** ✅ DONE (Tasks 3–5) · CPU · gate: `test_soa_sdf_serialize`
  extended green (pool layout, per-channel base, round-trip sdf+color+roughness) + `VIXEN.exe` links.
- **M3 — Shader generic channel reads + roughness lighting** ✅ DONE (Tasks 6–8) · GLSL · gate: glslc compiles;
  correctness deferred to the M4 live gate.
- **M4 — Offscreen render gate (authoritative)** ✅ DONE (Tasks 9–10) · controller · gate: lavapipe render shows
  per-voxel color gradient + roughness shading + SDF solid (no-regression); MSVC binary no-regression.

### Channel set this increment
`sdf` (Float, `SEM_SDF`/`FK_DISTANCE`, channel 0) · `color` (Vec3, `SEM_COLOR`) · `roughness`
(Float, `SEM_ROUGHNESS`). `brickStrideFloats = (1+3+1)·512 = 2560`; `channelBase` = sdf 0, color 512,
roughness 2048.

---

## File Structure

**Create:**
- `VIXEN/libraries/SVO/include/VoxelChannelFormat.h` — VIXEN-owned vocabulary: `SemanticId` (`SEM_*`)
  + `FieldKind` (`FK_*`) enums + `ChannelDesc` POD + helpers. Single source of truth (CPU side).
- `VIXEN/shaders/VoxelChannelFormat.glsl` — 1:1 GLSL mirror of the enums + channel-table accessors
  (the `SdfRecipes.h`↔`.glsl` pattern).

**Modify:**
- `VIXEN/libraries/SVO/include/ShellOctreeGpu.h` — `OctreeConfig` tail → channel table (poolBrickBase,
  channelCount, brickStrideFloats, channels[]); `SerializeSdf`/`ConcatenateSdf` → schema-driven SoA
  pool pack (replaces dedicated `sdfBricks`).
- `VIXEN/libraries/SVO/include/SdfBake.h` — bake synthesizes spatially-varying `color`+`roughness`
  (test content); full-brick population writes all channels.
- `VIXEN/shaders/StoredSdf.glsl` — generic pool addressing; `channelBaseFloats(sem)` +
  `sampleChannelScalar/Vec3Trilinear`; `_sampleSdfVoxel` via the generic reader.
- `VIXEN/shaders/BodyInstanceRayMarch.comp` — pool binding-11 decl; GLSL `OctreeConfig` channel table;
  `handleLeafHitInstancedSdf` samples color+roughness at the hit; include `VoxelChannelFormat.glsl`.
- `VIXEN/shaders/Lighting.glsl` — `computeLighting` gains a `roughness` param.
- `VIXEN/libraries/SVO/tests/test_soa_sdf_serialize.cpp` — pool-layout + multi-channel round-trip.
- `VIXEN/libraries/RenderGraph/tests/Nodes/test_body_instance_raymarch_render.cpp` — assert per-voxel
  color gradient + roughness shading; SDF no-regression.

**Unchanged (must not regress):** ESVO traversal + M6 leaf march logic, binary path + material palette,
Procedural path, the grid→brick `brickLookup[]`.

---

## Milestone M1 — VIXEN channel vocabulary + descriptor

### Task 1: Engine-owned vocabulary header + GLSL mirror

**Files:** Create `libraries/SVO/include/VoxelChannelFormat.h` + `shaders/VoxelChannelFormat.glsl`.

- [x] **Step 1:** Write `VoxelChannelFormat.h` — the VIXEN-owned vocabulary (no game concepts):
```cpp
#pragma once
#include <cstdint>
namespace Vixen::SVO {
// Semantic identity of a per-voxel channel (the engine's reusable vocabulary).
enum SemanticId : uint32_t {
    SEM_SDF = 0, SEM_COLOR = 1, SEM_ROUGHNESS = 2,
    SEM_NORMAL = 3, SEM_METALLIC = 4, SEM_EMISSION = 5, SEM_DENSITY = 6,
    SEM_COUNT
};
// How a SCALAR field is integrated by the renderer (declared by the data, not inferred).
enum FieldKind : uint32_t { FK_NONE = 0, FK_DISTANCE = 1, FK_DENSITY = 2 };
// Component count per voxel for a channel's element type.
inline uint32_t SemanticElemCount(SemanticId s) {
    return (s == SEM_COLOR || s == SEM_NORMAL || s == SEM_EMISSION) ? 3u : 1u;
}
constexpr uint32_t kVoxelsPerBrick = 512u;          // 8^3, no apron (Inc2 as-built)
constexpr uint32_t kMaxChannels    = 8u;            // fits the OctreeConfig tail (≤ ~12)
struct ChannelDesc { uint32_t semanticId, elemCount, channelBaseFloats, fieldKind; };  // = 1 uvec4
}  // namespace Vixen::SVO
```
- [x] **Step 2:** Write `shaders/VoxelChannelFormat.glsl` — 1:1 mirror (same values; verified by a parity test in Task 2):
```glsl
#ifndef VOXEL_CHANNEL_FORMAT_GLSL
#define VOXEL_CHANNEL_FORMAT_GLSL
#define SEM_SDF 0u
#define SEM_COLOR 1u
#define SEM_ROUGHNESS 2u
#define SEM_NORMAL 3u
#define SEM_METALLIC 4u
#define SEM_EMISSION 5u
#define SEM_DENSITY 6u
#define FK_NONE 0u
#define FK_DISTANCE 1u
#define FK_DENSITY 2u
#define VX_VOXELS_PER_BRICK 512u
#endif
```
- [x] **Step 3:** Commit `feat(svo): VIXEN-owned voxel channel vocabulary (SEM_*/FK_*) CPU+GLSL (Inc3 M1)`.

### Task 2: Extend `OctreeConfig` with the channel table + CPU↔GLSL parity test

**Files:** Modify `ShellOctreeGpu.h` (OctreeConfig); create `libraries/SVO/tests/test_channel_format.cpp`.

- [x] **Step 1: Failing test** — `test_channel_format.cpp`: assert the enum values are the agreed constants
  (guards the GLSL mirror, which must match by inspection) and `sizeof(OctreeConfig)==432`:
```cpp
#include <gtest/gtest.h>
#include "VoxelChannelFormat.h"
#include "ShellOctreeGpu.h"
using namespace Vixen::SVO;
TEST(ChannelFormat, EnumValuesAreStable) {
    EXPECT_EQ(SEM_SDF,0u); EXPECT_EQ(SEM_COLOR,1u); EXPECT_EQ(SEM_ROUGHNESS,2u);
    EXPECT_EQ(SEM_DENSITY,6u);
    EXPECT_EQ(FK_DISTANCE,1u); EXPECT_EQ(FK_DENSITY,2u);
    EXPECT_EQ(SemanticElemCount(SEM_COLOR),3u); EXPECT_EQ(SemanticElemCount(SEM_SDF),1u);
}
TEST(ChannelFormat, OctreeConfigStillMatchesShaderStride) {
    EXPECT_EQ(sizeof(OctreeConfig), 432u);
}
```
- [x] **Step 2: Build → FAIL** (no `VoxelChannelFormat.h` include wired / new fields absent).
- [x] **Step 3: Implement.** In `ShellOctreeGpu.h` `OctreeConfig`, replace `sdfBrickArrayBase`@208 +
  the tail with: `uint32_t poolBrickBase` (@208), `uint32_t channelCount` (@212),
  `uint32_t brickStrideFloats` (@216), then `ChannelDesc channels[kMaxChannels]` packed into the
  std140 tail. Keep `formatId`@200, `bricksPerAxis(Sdf)`@204. Re-derive the trailing pad so
  `sizeof==432`; add `static_assert(offsetof(...)==...)` for poolBrickBase/channelCount/brickStrideFloats/channels[0]
  and `static_assert(sizeof(OctreeConfig)==432)`. (Dump SPIR-V `ArrayStride` to confirm 432 holds — see
  the OctreeConfig-432 friction entry.)
- [x] **Step 4: Build+run → PASS.** Register `test_channel_format` in `libraries/SVO/tests/CMakeLists.txt`.
- [x] **Step 5: Commit** `feat(svo): OctreeConfig channel-descriptor table (Inc3 M1)`.

---

## Milestone M2 — Generic SoA pool serialize + bake + GPU wiring

### Task 3: Schema-driven SoA pool pack in `SerializeSdf`/`ConcatenateSdf`

**Files:** Modify `ShellOctreeGpu.h`.

- [x] **Step 1: Failing test** — extend `test_soa_sdf_serialize.cpp`:
```cpp
TEST(SoaSdfSerialize, MultiChannelPoolLayout) {
    auto baked = BakeRecipeToSdfWorld(RECIPE_SPHERE, glm::vec3(32), RecipeParams{6,0,0,0,0,0}, 64, 2.5f);
    auto body  = BuildSdfBodyOctree(baked, 3);
    SerializedOctree out = SerializeSdf(body);            // now packs sdf+color+roughness
    // 3 channels: sdf(1)+color(3)+roughness(1) = 5 float-lanes/voxel
    EXPECT_EQ(out.channelCount, 3u);
    EXPECT_EQ(out.brickStrideFloats, (1u+3u+1u)*512u);    // 2560
    // channelBase: sdf 0, color 512, roughness 2048 (declaration order)
    EXPECT_EQ(out.channelBaseFloats(SEM_SDF), 0u);
    EXPECT_EQ(out.channelBaseFloats(SEM_COLOR), 512u);
    EXPECT_EQ(out.channelBaseFloats(SEM_ROUGHNESS), 2048u);
    EXPECT_EQ(out.channelPool.size(), out.brickCount * out.brickStrideFloats * sizeof(float));
    // a known surface voxel's sdf lane ≈ its baked Density
    float sdf = out.readPoolVoxel(SEM_SDF, /*brick*/0, /*voxel*/0, /*comp*/0);   // test helper
    EXPECT_TRUE(std::isfinite(sdf));
}
```
- [x] **Step 2: Build → FAIL.**
- [x] **Step 3: Implement.** Replace the dedicated `sdfBricks` emit with a generic pool:
  - Determine the channel list/order from the body's `VoxelConfig`/`AttributeRegistry` mapped to
    `SemanticId` (SDF = the `density` key channel; `color`, `roughness` if declared). Build the
    `ChannelDesc[]` table + `brickStrideFloats = Σ elemCount·512` + per-channel `channelBaseFloats`.
  - Allocate `channelPool` = `brickCount·brickStrideFloats` floats.
  - In the existing per-brick / `z*64+y*8+x` voxel loop, for each channel read the component
    (`getComponentValue<Density/Color/...>`, default on absence) and write to
    `pool[brick·brickStride + channelBase_c + comp·512 + v]`.
  - `SerializedOctree`: add `std::vector<uint8_t> channelPool; uint32_t channelCount, brickStrideFloats;
    ChannelDesc channels[kMaxChannels];` + `channelBaseFloats(sem)` / `readPoolVoxel(...)` helpers.
    Remove `sdfBricks`/`sdfBrickArrayBase` (SDF now lives in the pool). `ConcatenateSdf` concatenates
    pools with per-octree `poolBrickBase` (float offset) into the descriptor.
- [x] **Step 4: Build+run → PASS.**
- [x] **Step 5: Commit** `feat(svo): schema-driven multi-channel SoA pool serialize (Inc3 M2)`.

### Task 4: Bake synthesizes varying `color` + `roughness`

**Files:** Modify `SdfBake.h`.

- [x] **Step 1:** In `BakeRecipeToSdfWorld`, register `color` (Vec3) + `roughness` (Float) keys and, in
  the full-brick population (active+dilated bricks), set spatially-varying values so per-voxel variation
  is observable downstream:
```cpp
// inside the active-brick voxel write (p = grid pos):
glm::vec3 col = 0.5f + 0.5f * glm::cos(glm::vec3(p.x, p.y, p.z) * 0.12f
                + glm::vec3(0.0f, 2.094f, 4.188f));   // smooth RGB bands
float rough = glm::clamp(0.2f + 0.6f * glm::fract(p.y * 0.0625f), 0.0f, 1.0f);
comps += Color{col}, Roughness{rough};   // alongside Density{sd}
```
  (Add a `Roughness` component to `VoxelComponents.h` if absent — `VOXEL_COMPONENT_SCALAR(Roughness, "roughness", float, 0.5f)`.)
- [x] **Step 2:** Extend the M2 test to assert a known voxel's stored `color`/`roughness` ≈ the formula. _(done as range/finiteness check; tighten to exact-formula in M4 T10 — see Progress Log)_
- [x] **Step 3: Commit** `feat(svo): bake synthesizes varying color+roughness for multi-channel test (Inc3 M2)`.

### Task 5: GPU buffer wiring (pool on binding 11)

**Files:** Modify `BodyOctreeSceneNode.cpp` + `...Config.h`.

- [x] **Step 1:** Rename the SDF buffer member/output to the channel **pool** (same binding 11); upload
  `concatenated_.channelPool` (was `sdfBricks`). `brickLookup`/binding 12 unchanged. Update the buffer
  size + `CreateBuffer`. Build gate: `VIXEN.exe` links (no descriptor-layout assertion).
- [x] **Step 2: Commit** `feat(rendergraph): upload generic channel pool to binding 11 (Inc3 M2)`.

---

## Milestone M3 — Shader generic channel reads + roughness lighting

### Task 6: Generic pool addressing + channel readers in `StoredSdf.glsl`

**Files:** Modify `StoredSdf.glsl`, `BodyInstanceRayMarch.comp`.

- [x] **Step 1:** In `.comp`: rename `sdfData[]`→`channelPool[]` (binding 11, `float`); add the channel
  table fields to the GLSL `OctreeConfig` (poolBrickBase, channelCount, brickStrideFloats, channels[])
  matching the C++ offsets; `#include "VoxelChannelFormat.glsl"`.
- [x] **Step 2:** In `StoredSdf.glsl`, add:
```glsl
// returns channelBaseFloats for a semantic in the active octree, or 0xFFFFFFFFu if absent.
uint channelBaseFloats(uint sem) {
    for (uint i = 0u; i < octreeConfig.channelCount; ++i)
        if (octreeConfig.channels[i].semanticId == sem) return octreeConfig.channels[i].channelBaseFloats;
    return 0xFFFFFFFFu;
}
float _samplePoolVoxel(uint base, ivec3 gridCoord, int comp, int octreeIdx) {
    // same brick lookup as _sampleSdfVoxel, but pool addressing:
    //   pool[poolBrickBase + brickIdx*brickStrideFloats + base + comp*512 + voxelIdx]
    ... (mirror _sampleSdfVoxel; return sentinel 1e9 for unallocated) ...
}
```
  Refactor `_sampleSdfVoxel` to call `_samplePoolVoxel(channelBaseFloats(SEM_SDF), …, 0, …)` — SDF march
  logic unchanged. Add `sampleChannelScalarTrilinear(uint sem, vec3 gridPos)` and
  `sampleChannelVec3Trilinear(uint sem, vec3 gridPos)` (8-corner trilinear over `_samplePoolVoxel`,
  per component); each returns a default when `channelBaseFloats==0xFFFFFFFFu`.
- [x] **Step 3:** glslc compile gate (controller); commit `feat(shader): generic channel-pool readers (Inc3 M3)`.

### Task 7: Sample color + roughness at the leaf hit

**Files:** Modify `BodyInstanceRayMarch.comp` (`handleLeafHitInstancedSdf`).

- [x] **Step 1:** After `marchBrickSdf` returns the hit `gridPos`, sample channels:
```glsl
hitColor     = sampleChannelVec3Trilinear(SEM_COLOR, gridHit);      // default vec3(1.0)
float rough  = sampleChannelScalarTrilinear(SEM_ROUGHNESS, gridHit); // default 0.5
```
  Output `hitColor` (per-voxel, no longer `vec3(1.0)` tint placeholder) + a new `out float hitRoughness`.
  Thread `hitRoughness` up through `traverseOctreeInstanced` to `main()` (add to the nearest-hit
  accumulator, default 0.5 for binary/procedural).
- [x] **Step 2:** glslc compile gate; commit `feat(shader): per-voxel color+roughness at Stored-SDF hit (Inc3 M3)`.

### Task 8: Roughness-aware lighting

**Files:** Modify `Lighting.glsl`, `BodyInstanceRayMarch.comp` (call site).

- [x] **Step 1:** `computeLighting(vec3 baseColor, vec3 normal, vec3 rayDir, float roughness)` — modulate
  the specular term by roughness (e.g. Blinn-Phong exponent `mix(64.0, 4.0, roughness)` + specular scale
  `1.0-roughness`). Default roughness 0.5 at existing call sites (binary/procedural) so they're visually
  unchanged. `main()` passes `bestRoughness`.
- [x] **Step 2:** glslc compile gate; commit `feat(shader): roughness-aware lighting (Inc3 M3)`.

---

## Milestone M4 — Offscreen render gate (authoritative)

### Task 9: Multi-channel render test

**Files:** Modify `test_body_instance_raymarch_render.cpp`.

- [x] **Step 1:** Extend `RenderStoredSdfBodiesNoHoles` (or add `RenderStoredSdfMultiChannel`): the bake
  now produces a color gradient + varying roughness. Render the smooth sphere (binding 11 = pool;
  bindings 11/12 already bound). Assert: (a) SDF still solid (`fillRatio > 0.97`, no-regression);
  (b) **per-voxel color varies** — sample a row of body pixels across the disk and assert the RGB is NOT
  constant (range above a threshold), proving the `color` channel flows; (c) write PNG for inspection.
- [x] **Step 2:** Build the render test (WSL preset) + run on lavapipe; **controller reads the PNG** to
  confirm the color gradient + roughness shading appear and the sphere is solid. Record in this plan's
  Progress Log + `memory-bank/activeContext.md`.
- [x] **Step 3: Commit** `test(rendergraph): multi-channel Stored render gate (Inc3 M4)`.

### Task 10: No-regression + descriptor parity

**Files:** none new.

- [x] **Step 1:** Run the binary `RenderRealShaderNearViewToPng` + `RenderMultiKindBodiesProvesStrideFix`
  (unchanged output) + `test_soa_sdf_serialize` + `test_sdf_bake` + `test_channel_format` — all green.
- [x] **Step 2:** MSVC: `cmd.exe /c "C:\cpp\_wt_build.bat VIXEN"` green (the OctreeConfig/serialize
  changes compile under MSVC — watch for `near`/`far`/`min`/`max` keyword + `windows.h` traps).
- [x] **Step 3: Commit** `docs(progress): Inc3 multi-channel gate — per-voxel color+roughness render, no-regression`.

---

## Progress Log

- **Milestone M1 (Tasks 1–2): DONE** · commits `49c24f48..63b98643` · Opus validator OK · 2026-06-23
  — VIXEN-owned channel vocabulary (`VoxelChannelFormat.h` + `VoxelChannelFormat.glsl` mirror, value-parity
  verified) + `OctreeConfig` channel-descriptor table (`poolBrickBase`@208 / `channelCount`@212 /
  `brickStrideFloats`@216 / `ChannelDesc channels[8]`@220 / `_tailPad[21]`@348; `sizeof==432` preserved,
  `sizeof(ChannelDesc)==16` + offset `static_assert`s). Legacy `sdfBrickArrayBaseOf`/setters kept as thin
  shims over the new named fields (Inc2 callers unbroken). Gate: `test_channel_format` 2/2 +
  `test_soa_sdf_serialize` 8/8 green (re-run by validator after clean rebuild).

- **Milestone M2 (Tasks 3–5): DONE** · commits `c58f0f38..79c1ebff` · Opus validator OK · 2026-06-23
  — `sdfBricks` SSBO replaced by a generic SoA `channelPool` (stride 2560 floats/brick; bases sdf 0 /
  color 512 / roughness 2048 from a hardcoded canonical `kChannelSpecs[]` = deterministic order).
  `SerializeSdf` stamps the `OctreeConfig` channel table (`channelCount`/`brickStrideFloats`/`channels[]`,
  SDF=`FK_DISTANCE`, color+roughness=`FK_NONE`); `ConcatenateSdf` copies it intact + sets per-octree
  `poolBrickBase`; `BodyOctreeSceneNode` uploads `channelPool` (binding 11) + the stamped configs verbatim
  (432-B stride). **Descriptor-propagation traced clean to the GPU UBO by the Opus validator** (no zeroing
  gap). `Roughness` component added; bake synthesizes varying color (RGB cos-bands) + roughness (Y-stripe).
  Gate: `test_soa_sdf_serialize` 10/10 green + `VIXEN` target links (re-run by validator).
- **Deferred Minor cleanups → do in M4 Task 10** (validator-flagged, non-blocking): (a) tighten
  `test_soa_sdf_serialize.cpp` `MultiChannelBakedColorRoughness` (~L297-303) to assert the **exact baked
  color/roughness formula** at a known voxel, not just `[0,1]` range (closes a color↔roughness-swap gap);
  (b) scrub stale comments still saying `sdfBricks` (`ShellOctreeGpu.h:483,490-491,504,757-769`) and
  "256-byte OctreeConfig" (`BodyOctreeSceneNode.cpp:384`) → `channelPool` / 432-B.

- **Milestone M3 (Tasks 6–8 + std140 fix): DONE** · commits `0f56a63a..5a62c268` · Opus validator OK · 2026-06-23
  — Shader now reads the generic channel pool by semantic. `channelPool[]` (binding 11); GLSL `OctreeConfig`
  carries `poolBrickBase`/`channelCount`/`brickStrideFloats`/`uvec4 channels[8]`; `channelBaseFloats(sem)` +
  `_samplePoolVoxel` + `sampleChannelScalar/Vec3Trilinear` (defaults for absent channels); `_sampleSdfVoxel`
  routes through the pool (SDF march byte-identical); per-voxel color+roughness sampled at the leaf hit and
  `hitRoughness` threaded to `main()`; `computeLighting` is roughness-aware (binary/procedural pass 0.5,
  unchanged look). **std140 fix (`0f56a63a`):** M1 had `channels[]`@220 but std140 aligns the UBO array to
  **224** — added `_padChannels`@220, `channels`@224, `_tailPad[20]`; sizeof stays 432. **Proven by `spirv-dis`
  (re-verified by validator on a fresh recompile):** `ArrayStride 432` + `channels Offset 224` + `uvec4 ArrayStride 16`.
  C++↔GLSL layouts provably agree. Gate: shader compiles; `test_channel_format` 2/2 + `test_soa_sdf_serialize`
  10/10 green. Note (lighting): specular scale is `(1-roughness)*0.4` (magnitude tweak, monotonic — within §7).
- **⚠️ For M4:** the render test's `gtest_discover_tests` hits a 5s discovery timeout (Vulkan static-init to LIST
  cases) — this is NOT a compile/link failure (the exe links, shader compiles). M4 must RUN the render binary
  directly (or via ctest with a raised `DISCOVERY_TIMEOUT`), not rely on test discovery.

- **Milestone M4 (Tasks 9–10 + deferred cleanups): DONE** · commits `563a9fa5..c527b8bd` · 2026-06-23
  — **Authoritative live render gate PASSED.** New `RenderStoredSdfMultiChannel` (lavapipe): `fillRatio=0.9879`
  (solid, == the `RenderStoredSdfBodiesNoHoles` baseline → no solidity regression) + per-voxel color range
  R 0.710 / G 0.514 / B 0.412 (≫ 0.10 → color channel flows). **Controller read `/tmp/glsl_sdf_multichannel.png`:**
  smooth multi-hue color gradient (RGB cos-bands) + roughness-aware 3D lit shading on a solid sphere — Inc3
  deliverable visually confirmed. No-regression: `RenderRealShaderNearViewToPng` + `RenderMultiKindBodiesProvesStrideFix`
  (kind0/1/2 = 21743/17804/21743, matches Inc2) + `test_soa_sdf_serialize` 10/10 + `test_sdf_bake` 2/2 +
  `test_channel_format` 2/2 all green. **MSVC `_wt_build.bat VIXEN` rc=0** (55 targets). Deferred M2 cleanups done:
  `MultiChannelBakedColorRoughness` tightened to exact-formula `EXPECT_NEAR` at voxel p=(38,32,32) (catches a
  color↔roughness swap); stale `sdfBricks`/"256-byte" comments scrubbed → `channelPool`/432-B.
  CMake: render test set to `DISCOVERY_MODE PRE_TEST` (build no longer trips the gtest 5s discovery timeout).
- **Pre-existing (NOT Inc3): minor shell-octree surface artifacts** (a few edge notches + a brick-seam square)
  visible in the render. `fillRatio` is byte-identical to the Inc2 baseline and the SDF march is unchanged, so
  these are the project's known/deferred "shell-octree artifacts" render-quality item, out of Inc3 scope.

- **Inc3 hole-fix (post-M4): brick-fleck artifacts root-caused + fixed + merged** · commits `c390d737..f1bb2d74` · 2026-06-24
  — User flagged consistent brick-grid-aligned holes/flecks on the rendered Stored bodies (real GPU + lavapipe). Root cause
  (proven by a CPU march-mirror + GPU readback — `StoredSdfMarchMirror.RootCause_SentinelContaminationOrigin`):
  **`GaiaVoxelWorld::querySolidVoxels()` keeps a voxel only if `Density > 0`, but a Stored-SDF body's `Density` IS the
  signed distance (negative INSIDE)** → every fully-interior brick (all voxels sd≤0) was dropped from the octree → absent
  from BOTH `brickGridLookup` and the `channelPool` → surface trilinear/gradient stencils reaching those dropped interior
  neighbours read the unallocated sentinel → the flecks. (Octree 0: 508 active bricks, **72 interior dropped**, 101,206
  contaminated taps.) Earlier sign-aware-sentinel + honest-tap-gradient attempts were whack-a-mole (they patched the
  sentinel CONSUMERS — march, normal, color — not the dropped-brick cause). **Fix (`7db15496`, cleanly scoped, binary path
  byte-unchanged):** `GaiaVoxelWorld::queryOccupiedVoxels()` (occupancy, sign-agnostic) + `LaineKarrasOctree::setSignedDistanceField(true)`
  so the SDF-body rebuild bins bricks by OCCUPANCY not density>0; `BuildSdfBodyOctree` sets it. The interim sign-aware
  sentinel band-aids (`92c9a5c2`/`9c79edc2`) were then removed as dead code (`f1bb2d74`) — restored the single-positive-sentinel
  baseline. Verified: contaminated taps **101206→0**, all 508 bricks allocated; lavapipe smooth+displaced **CLEAN**
  (controller read the PNGs); binary no-regression (multikind 21743/17804/21743); `test_soa_sdf_serialize` 11/11, mirror 12/12.
  FF-merged to main (`main → f1bb2d74`). **Durable gotcha:** the "solid = Density>0" occupancy contract is a BINARY-voxel
  assumption; any signed-distance/Density-as-field body must select bricks by occupancy, or interior bricks vanish.

## Self-Review

**Spec coverage:** §3 pool → M2 T3; §4 descriptor+fieldKind → M1 T2; §5 schema-driven pack → M2 T3;
§5 bake content → M2 T4; §6 shader generic reads → M3 T6/T7; §7 lighting → M3 T8; §9 testing → M2 tests
+ M4 gate; §1.1 ownership (engine-owned vocabulary single-sourced) → M1 T1. ✓

**Open at execution (confirm signatures against cited code):** the `VoxelConfig`/`AttributeRegistry`
channel-enumeration API (M2 T3 — verify vs `VOXELCONFIG.md` + `AttributeRegistry.h`); the `Color`/
`Roughness` component types (`VoxelComponents.h`); the GLSL `OctreeConfig` channel-table std140 offsets
matching the C++ struct (M3 T6 — dump SPIR-V ArrayStride, must stay 432); threading `hitRoughness`
through `traverseOctreeInstanced` (M3 T7).

**Type consistency:** `SemanticId`/`FieldKind`/`ChannelDesc`, `channelPool`/`brickStrideFloats`/
`channelBaseFloats`, `SEM_SDF/COLOR/ROUGHNESS`, `_samplePoolVoxel`/`sampleChannelScalarTrilinear`/
`sampleChannelVec3Trilinear`, binding 11 = pool / 12 = lookup — used consistently across tasks. ✓

**Note:** M3 (shader) + M4 (gate) are highest-risk; the offscreen render gate (M4) is authoritative
(per the project's live-gate rule). Migrating SDF into the pool (M2/M3) re-touches the M6 path — keep
the ESVO traversal + `marchBrickSdf` march logic byte-identical; only `_sampleSdfVoxel`'s addressing
changes. Recommend the context-manager pipeline with controller-run WSL render gate + MSVC no-regression.
