# SDF Body Rendering — Increment 1 (Procedural SDF Provider) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make body instances render as smooth analytic SDF primitives (perfect spheres / displaced spheres) by adding a Procedural provider to `BodyInstanceRayMarch.comp`, so the default 3-body scene's planets/stars look spherical instead of blocky — with no voxel storage.

**Architecture:** Each per-body GPU record (`BodyInstanceGpu`) gains a `providerKind` (0 = Stored/ESVO, 1 = Procedural), a `recipeId`, and `recipeParams[6]`. The shader branches per-instance: Procedural bodies sphere-trace an analytic SDF recipe (evaluated by a new `SdfRecipes.glsl`) within a bounding sphere and shade with the SDF-gradient normal; Stored bodies use the existing ESVO path **unchanged**. A 1:1 CPU mirror (`SdfRecipes.h`) is unit-tested with gtest; the live app run is the authoritative visual gate.

**Tech Stack:** C++23, GLSL compute (`#version 460`, std430 SSBO binding 10), glm, GoogleTest, CMake (VS-generator `build/` for unit tests; ninja preset for the app), Vulkan 1.3.

**Design of record:** `Vixen-Docs/01-Architecture/Voxel-Content-Format-Contract-Design-2026-06.md`.

---

## Milestone Map

> Execution grouping for the post-brainstorm-context-manager pipeline (confirmed 2026-06-22).
> Project-aware gate: the **controller** owns the Windows ninja build + live run (the preset is
> path-pinned to the main checkout and PDB-lock-hazardous); **Sonnet** implementers write code +
> commit; **Opus** validates each milestone. Builds stay worktree-isolated via
> `cmake --preset vixen-ninja` invoked from the worktree (binaryDir `${sourceDir}/../build-ninja`).

- **M1 — CPU recipe library + GLSL mirror** (Tasks 1–2) · implementer Sonnet · gate: `test_sdf_recipes` gtest green (controller-run) + Opus validator.
- **M2 — GPU integration** (Tasks 3–5: grow `BodyInstanceGpu` 32→64 B, shader provider branch, seed 3-body scene) · implementer Sonnet · gate: worktree ninja build — shaders compile + `static_assert`s hold (controller-run) + Opus validator.
- **M3 — Verification + live gate** (Tasks 6–7) · controller/interactive · authoritative live app run (smooth spheres, 0 syncval).

### Progress Log
- _(pending — M1 not yet started)_

---

## Critical project facts (read before starting)

- **`PackInstances` is a `memcpy`** of the `BodyInstanceGpu` array (`ShellOctreeGpu.h:425`), and the instance ring auto-sizes from `PackInstances(...).size()` (`BodyOctreeSceneNode.cpp:183-185`). So growing the struct requires updating only: the C++ struct + its `static_assert`, and the **GLSL `BodyInstance` struct** (they must stay byte-for-byte identical). No change to `PackInstances` or the node.
- **Binding 10 is a `std430` SSBO** (`BodyInstanceRayMarch.comp:127` `readonly buffer`). In std430 a `float[6]` member has 4-byte stride (NOT rounded to 16 as in std140), so the C++ `float recipeParams[6]` and GLSL `float recipeParams[6]` match exactly. This is correctness-critical — do not convert binding 10 to a UBO.
- **`vec3` is 16-byte aligned in std430.** The existing 32-byte record works because `renderScale`/`octreeIndex` fill the vec3 padding slots. The new 64-byte layout preserves this (verified field offsets in Task 3).
- **Live run is the authoritative gate for GPU/render work** (`[[live-verification-authoritative-for-gpu-work]]`; handoff §"Hard-won learnings"). Static review repeatedly passed real GPU bugs this project. Task 7 runs the app.
- **Two build trees:** unit tests build/run from the VS-generator `build/` tree (`commands.md`); the app builds via the ninja preset (`cmd.exe /c _ninja_preset_build.bat`, handoff §"How to run"). WSL env vars do NOT reach the Windows `.exe` — set them inside `cmd.exe`.

## File Structure

**Create:**
- `VIXEN/libraries/SVO/include/SdfRecipes.h` — CPU mirror of the SDF recipe library (glm; header-only). One responsibility: evaluate `evalSdf`/`sdfGradient`/`traceProcedural` for the recipe set.
- `VIXEN/libraries/SVO/tests/test_sdf_recipes.cpp` — gtest unit tests for the CPU recipe library.
- `VIXEN/shaders/SdfRecipes.glsl` — GLSL recipe library, **1:1 mirror** of `SdfRecipes.h`.

**Modify:**
- `VIXEN/libraries/SVO/include/ShellOctreeGpu.h:222-228` — grow `BodyInstanceGpu` to 64 B + provider/recipe fields + constants; update `static_assert`.
- `VIXEN/libraries/SVO/tests/CMakeLists.txt` — register `test_sdf_recipes`.
- `VIXEN/shaders/BodyInstanceRayMarch.comp:120-129,200-205,564-659` — grow GLSL `BodyInstance` struct; include `SdfRecipes.glsl`; add provider constants; branch the per-instance loop.
- `VIXEN/application/main/source/graph/BuildRenderGraph.cpp:555-580` — seed the 3 default bodies as Procedural.

**Unchanged (must not regress):** the ESVO traversal path (`traverseOctreeInstanced`, `marchBrickInstanced`, `handleLeafHitInstanced`) and all Stored-body behavior.

---

## Task 1: CPU SDF recipe library + unit tests

**Files:**
- Create: `VIXEN/libraries/SVO/include/SdfRecipes.h`
- Create: `VIXEN/libraries/SVO/tests/test_sdf_recipes.cpp`
- Modify: `VIXEN/libraries/SVO/tests/CMakeLists.txt`

- [ ] **Step 1: Write the failing test**

Create `VIXEN/libraries/SVO/tests/test_sdf_recipes.cpp`:

```cpp
#include <gtest/gtest.h>
#include <glm/glm.hpp>
#include "SdfRecipes.h"

using namespace Vixen::SVO;

namespace {
constexpr float kEps = 1e-3f;
RecipeParams sphereParams(float r) { return RecipeParams{r, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f}; }
}

// --- Pure sphere SDF: distance is exact. ---
TEST(SdfRecipes, SphereSignedDistanceIsExact) {
    const glm::vec3 c(10.0f, 0.0f, 0.0f);
    const RecipeParams rp = sphereParams(4.0f);
    EXPECT_NEAR(evalSdf(RECIPE_SPHERE, c, c, rp), -4.0f, kEps);                       // centre: -radius
    EXPECT_NEAR(evalSdf(RECIPE_SPHERE, c + glm::vec3(4.0f,0,0), c, rp), 0.0f, kEps);  // surface: 0
    EXPECT_NEAR(evalSdf(RECIPE_SPHERE, c + glm::vec3(6.0f,0,0), c, rp), 2.0f, kEps);  // outside: +2
}

// --- Gradient at the surface is the outward radial normal. ---
TEST(SdfRecipes, SphereGradientIsRadialNormal) {
    const glm::vec3 c(0.0f);
    const RecipeParams rp = sphereParams(3.0f);
    const glm::vec3 dir = glm::normalize(glm::vec3(1.0f, 2.0f, -2.0f));
    const glm::vec3 surf = c + dir * 3.0f;
    const glm::vec3 n = sdfGradient(RECIPE_SPHERE, surf, c, rp);
    EXPECT_NEAR(n.x, dir.x, 2e-2f);
    EXPECT_NEAR(n.y, dir.y, 2e-2f);
    EXPECT_NEAR(n.z, dir.z, 2e-2f);
}

// --- Sphere-trace: a ray through the centre hits the near surface; normal radial. ---
TEST(SdfRecipes, TraceHitsNearSurface) {
    const glm::vec3 c(0.0f, 0.0f, 20.0f);
    const RecipeParams rp = sphereParams(5.0f);
    const glm::vec3 ro(0.0f);
    const glm::vec3 rd(0.0f, 0.0f, 1.0f);
    const TraceHit h = traceProcedural(RECIPE_SPHERE, ro, rd, c, rp);
    ASSERT_TRUE(h.hit);
    EXPECT_NEAR(h.t, 15.0f, 5e-2f);                 // 20 - radius 5
    EXPECT_NEAR(h.normal.z, -1.0f, 2e-2f);          // faces the camera
}

// --- Sphere-trace miss: ray that passes outside the bounding sphere. ---
TEST(SdfRecipes, TraceMissesWhenOffAxis) {
    const glm::vec3 c(0.0f, 0.0f, 20.0f);
    const RecipeParams rp = sphereParams(5.0f);
    const TraceHit h = traceProcedural(RECIPE_SPHERE, glm::vec3(0.0f), glm::vec3(0,1,0), c, rp);
    EXPECT_FALSE(h.hit);
}

// --- Displaced sphere: deterministic and bounded by the amplitude. ---
TEST(SdfRecipes, DisplacedSphereIsDeterministicAndBounded) {
    const glm::vec3 c(0.0f);
    const RecipeParams rp{6.0f, 0.5f, 0.7f, 0.0f, 0.0f, 0.0f};
    const glm::vec3 p = c + glm::vec3(6.0f, 0.0f, 0.0f);
    const float a = evalSdf(RECIPE_DISPLACED_SPHERE, p, c, rp);
    const float b = evalSdf(RECIPE_DISPLACED_SPHERE, p, c, rp);
    EXPECT_FLOAT_EQ(a, b);                                            // deterministic
    const float plain = evalSdf(RECIPE_SPHERE, p, c, rp);
    EXPECT_LE(std::abs(a - plain), rp.displaceAmp + kEps);            // bounded by amplitude
}
```

- [ ] **Step 2: Register the test in CMake**

Edit `VIXEN/libraries/SVO/tests/CMakeLists.txt` — after the `test_svo_types` block (mirrors its exact shape), add:

```cmake
add_executable(test_sdf_recipes
    test_sdf_recipes.cpp
)
if(TARGET SVO)
    target_link_libraries(test_sdf_recipes PRIVATE GTest::gtest_main SVO)
else()
    target_link_libraries(test_sdf_recipes PRIVATE GTest::gtest_main)
endif()
gtest_discover_tests(test_sdf_recipes)
```

- [ ] **Step 3: Build to verify it fails (header missing)**

Run: `cmake --build build --config Debug --target test_sdf_recipes --parallel 16 2>&1 | grep -v "warning LNK4099" | tail -20`
Expected: FAIL — `SdfRecipes.h` not found / `evalSdf` undeclared.

- [ ] **Step 4: Implement `SdfRecipes.h`**

Create `VIXEN/libraries/SVO/include/SdfRecipes.h`:

```cpp
#pragma once
// ============================================================================
// SdfRecipes.h — CPU mirror of shaders/SdfRecipes.glsl (Increment 1).
// Analytic SDF recipe library for the Procedural body provider.
// MUST stay 1:1 with the GLSL: same formulas, same operation order, same
// constants. Parity is unit-tested here and visually gated by the live app run.
// ============================================================================
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <cstdint>
#include <cmath>

namespace Vixen::SVO {

// Recipe ids (mirror SdfRecipes.glsl #defines).
enum RecipeId : uint32_t {
    RECIPE_SPHERE           = 0u,
    RECIPE_DISPLACED_SPHERE = 1u,
};

// Provider kinds (mirror BodyInstanceRayMarch.comp #defines + ShellOctreeGpu.h).
enum ProviderKind : uint32_t {
    PROVIDER_STORED     = 0u,   // existing ESVO octree path
    PROVIDER_PROCEDURAL = 1u,   // analytic SDF recipe (this file)
};

// recipeParams layout: .x = radius, .y = displaceAmp, .z = displaceFreq, rest spare.
struct RecipeParams {
    float radius;
    float displaceAmp;
    float displaceFreq;
    float spare3;
    float spare4;
    float spare5;
};

// Deterministic, LUT-free displacement (parity-friendly: pure transcendental).
inline float sdfDisplacement(const glm::vec3& d, float freq) {
    return std::sin(freq * d.x) * std::sin(freq * d.y) * std::sin(freq * d.z);
}

// Signed distance for a recipe, evaluated in WORLD space about `center`.
inline float evalSdf(uint32_t recipeId, const glm::vec3& p,
                     const glm::vec3& center, const RecipeParams& rp) {
    const glm::vec3 d = p - center;
    float dist = glm::length(d) - rp.radius;
    if (recipeId == RECIPE_DISPLACED_SPHERE) {
        dist += rp.displaceAmp * sdfDisplacement(d, rp.displaceFreq);
    }
    return dist;
}

// Outward surface normal = normalized gradient (central differences).
inline glm::vec3 sdfGradient(uint32_t recipeId, const glm::vec3& p,
                             const glm::vec3& center, const RecipeParams& rp) {
    const float h = 1e-3f;
    const glm::vec3 dx(h, 0, 0), dy(0, h, 0), dz(0, 0, h);
    const float gx = evalSdf(recipeId, p + dx, center, rp) - evalSdf(recipeId, p - dx, center, rp);
    const float gy = evalSdf(recipeId, p + dy, center, rp) - evalSdf(recipeId, p - dy, center, rp);
    const float gz = evalSdf(recipeId, p + dz, center, rp) - evalSdf(recipeId, p - dz, center, rp);
    return glm::normalize(glm::vec3(gx, gy, gz));
}

struct TraceHit {
    bool      hit    = false;
    float     t      = 0.0f;
    glm::vec3 point  = glm::vec3(0.0f);
    glm::vec3 normal = glm::vec3(0.0f, 1.0f, 0.0f);
};

// Sphere-trace the recipe within its bounding sphere. Returns the nearest hit.
inline TraceHit traceProcedural(uint32_t recipeId, const glm::vec3& ro, const glm::vec3& rd,
                                const glm::vec3& center, const RecipeParams& rp) {
    const float maxDisp = (recipeId == RECIPE_DISPLACED_SPHERE) ? rp.displaceAmp : 0.0f;
    const float boundR  = rp.radius + maxDisp + 0.01f;

    // Ray vs bounding sphere → [tNear, tFar].
    const glm::vec3 oc = ro - center;
    const float b  = glm::dot(oc, rd);
    const float c  = glm::dot(oc, oc) - boundR * boundR;
    const float disc = b * b - c;
    TraceHit miss;
    if (disc < 0.0f) return miss;
    const float sq    = std::sqrt(disc);
    const float tNear = std::max(-b - sq, 0.0f);
    const float tFar  = -b + sq;
    if (tFar < 0.0f) return miss;

    float t = tNear;
    const int   MAX_STEPS  = 128;
    const float EPS        = 1e-3f;
    const float stepScale  = (recipeId == RECIPE_DISPLACED_SPHERE) ? 0.7f : 1.0f; // Lipschitz guard
    for (int i = 0; i < MAX_STEPS; ++i) {
        const glm::vec3 p = ro + rd * t;
        const float d = evalSdf(recipeId, p, center, rp);
        if (d < EPS) {
            return TraceHit{true, t, p, sdfGradient(recipeId, p, center, rp)};
        }
        t += d * stepScale;
        if (t > tFar) break;
    }
    return miss;
}

}  // namespace Vixen::SVO
```

- [ ] **Step 5: Build + run the test to verify it passes**

Run: `cmake --build build --config Debug --target test_sdf_recipes --parallel 16 2>&1 | grep -v "warning LNK4099" | tail -5 && ./build/libraries/SVO/tests/Debug/test_sdf_recipes.exe --gtest_brief=1`
Expected: PASS — `[  PASSED  ] 5 tests.`
(If the exe is under a different tree, locate with `find build -name test_sdf_recipes.exe`.)

- [ ] **Step 6: Commit**

```bash
git add VIXEN/libraries/SVO/include/SdfRecipes.h VIXEN/libraries/SVO/tests/test_sdf_recipes.cpp VIXEN/libraries/SVO/tests/CMakeLists.txt
git commit -m "feat(svo): CPU SDF recipe library + unit tests (sphere, displaced-sphere, sphere-trace)

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Task 2: GLSL recipe library (1:1 mirror)

**Files:**
- Create: `VIXEN/shaders/SdfRecipes.glsl`

- [ ] **Step 1: Write `SdfRecipes.glsl`**

Create `VIXEN/shaders/SdfRecipes.glsl` — formulas/constants identical to `SdfRecipes.h`:

```glsl
// ============================================================================
// SdfRecipes.glsl - analytic SDF recipe library for the Procedural body provider.
// 1:1 MIRROR of libraries/SVO/include/SdfRecipes.h. Keep formulas, operation
// order, and constants identical (parity is unit-tested on the CPU mirror and
// visually gated by the live app run).
// ============================================================================
#ifndef SDF_RECIPES_GLSL
#define SDF_RECIPES_GLSL

#define RECIPE_SPHERE           0u
#define RECIPE_DISPLACED_SPHERE 1u

// params = (radius, displaceAmp, displaceFreq)
float sdfDisplacement(vec3 d, float freq) {
    return sin(freq * d.x) * sin(freq * d.y) * sin(freq * d.z);
}

float evalSdf(uint recipeId, vec3 p, vec3 center, vec3 params) {
    vec3 d = p - center;
    float dist = length(d) - params.x;
    if (recipeId == RECIPE_DISPLACED_SPHERE) {
        dist += params.y * sdfDisplacement(d, params.z);
    }
    return dist;
}

vec3 sdfGradient(uint recipeId, vec3 p, vec3 center, vec3 params) {
    const float h = 1e-3;
    vec2 e = vec2(h, 0.0);
    float gx = evalSdf(recipeId, p + e.xyy, center, params) - evalSdf(recipeId, p - e.xyy, center, params);
    float gy = evalSdf(recipeId, p + e.yxy, center, params) - evalSdf(recipeId, p - e.yxy, center, params);
    float gz = evalSdf(recipeId, p + e.yyx, center, params) - evalSdf(recipeId, p - e.yyx, center, params);
    return normalize(vec3(gx, gy, gz));
}

// Sphere-trace within the bounding sphere. Writes hitNormal/hitT on success.
bool traceProceduralBody(uint recipeId, vec3 center, vec3 params, vec3 ro, vec3 rd,
                         out vec3 hitNormal, out float hitT) {
    hitNormal = vec3(0.0, 1.0, 0.0);
    hitT      = 0.0;

    float maxDisp = (recipeId == RECIPE_DISPLACED_SPHERE) ? params.y : 0.0;
    float boundR  = params.x + maxDisp + 0.01;

    vec3  oc   = ro - center;
    float b    = dot(oc, rd);
    float c    = dot(oc, oc) - boundR * boundR;
    float disc = b * b - c;
    if (disc < 0.0) return false;
    float sq    = sqrt(disc);
    float tNear = max(-b - sq, 0.0);
    float tFar  = -b + sq;
    if (tFar < 0.0) return false;

    float t = tNear;
    const int   MAX_STEPS = 128;
    const float EPS       = 1e-3;
    float stepScale = (recipeId == RECIPE_DISPLACED_SPHERE) ? 0.7 : 1.0;
    for (int i = 0; i < MAX_STEPS; ++i) {
        vec3  p = ro + rd * t;
        float d = evalSdf(recipeId, p, center, params);
        if (d < EPS) {
            hitNormal = sdfGradient(recipeId, p, center, params);
            hitT      = t;
            return true;
        }
        t += d * stepScale;
        if (t > tFar) return false;
    }
    return false;
}

#endif // SDF_RECIPES_GLSL
```

- [ ] **Step 2: Verify it compiles (built in Task 4 with the shader; standalone parse check)**

The shader bundle compiles `.comp` translation units, not bare `.glsl` includes, so `SdfRecipes.glsl` is validated when included by `BodyInstanceRayMarch.comp` in Task 4. No build here.

- [ ] **Step 3: Commit**

```bash
git add VIXEN/shaders/SdfRecipes.glsl
git commit -m "feat(shaders): GLSL SDF recipe library (1:1 mirror of SdfRecipes.h)

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Task 3: Grow the per-body record to carry the provider

**Files:**
- Modify: `VIXEN/libraries/SVO/include/ShellOctreeGpu.h:222-228`
- Modify: `VIXEN/shaders/BodyInstanceRayMarch.comp:120-129`

- [ ] **Step 1: Grow `BodyInstanceGpu` (C++)**

In `ShellOctreeGpu.h`, replace the struct + `static_assert` (lines 222-228) with:

```cpp
struct BodyInstanceGpu {
    float worldPos[3];       // 0   : body centre (world space)
    float renderScale;       // 12  : Stored: grid scale; Procedural: unused
    float color[3];          // 16  : per-instance tint
    uint32_t octreeIndex;    // 28  : Stored: index into configs[]; Procedural: unused
    uint32_t providerKind;   // 32  : 0 = Stored/ESVO, 1 = Procedural
    uint32_t recipeId;       // 36  : Procedural recipe id (0 = sphere, 1 = displaced sphere)
    float recipeParams[6];   // 40..63 : params.xyz = (radius, displaceAmp, displaceFreq); 3 spare
};
// 64-byte std430 record. recipeParams[6] is valid because binding 10 is a std430
// SSBO (float[] stride = 4). providerKind defaults to 0 (Stored) under value-init,
// so a zeroed/legacy record renders via the unchanged ESVO path.
static_assert(sizeof(BodyInstanceGpu) == 64, "BodyInstanceGpu must be 64 bytes (std430 record)");
static_assert(offsetof(BodyInstanceGpu, providerKind) == 32, "providerKind @32");
static_assert(offsetof(BodyInstanceGpu, recipeId)     == 36, "recipeId @36");
static_assert(offsetof(BodyInstanceGpu, recipeParams) == 40, "recipeParams @40");
```

Ensure `#include <cstddef>` is present near the top of `ShellOctreeGpu.h` (for `offsetof`); add it if missing.

- [ ] **Step 2: Grow the GLSL `BodyInstance` struct to match**

In `BodyInstanceRayMarch.comp`, replace the struct (lines 120-129) with:

```glsl
struct BodyInstance {
    vec3  worldPos;          // 0
    float renderScale;       // 12
    vec3  color;             // 16
    uint  octreeIndex;       // 28
    uint  providerKind;      // 32  (0 = Stored/ESVO, 1 = Procedural)
    uint  recipeId;          // 36
    float recipeParams[6];   // 40..63  (params.xyz = radius, amp, freq)
};
// std430 SSBO record, 64 bytes — byte-for-byte identical to C++ BodyInstanceGpu.
```

- [ ] **Step 3: Build the SVO library + a dependent to verify the static_asserts hold**

Run: `cmake --build build --config Debug --target SVO --parallel 16 2>&1 | grep -v "warning LNK4099" | tail -10`
Expected: PASS — no `static_assert` failure (a wrong offset/size fails compilation here).

- [ ] **Step 4: Verify no other code hardcodes the 32-byte stride**

Run: `grep -rnE 'BodyInstanceGpu.*32|sizeof\(BodyInstanceGpu\)|== 32' VIXEN/libraries VIXEN/application 2>/dev/null | grep -v build`
Expected: only the (now updated) `static_assert` and comments referencing the *old* size in prose. If any code computes a stride of 32, update it to `sizeof(BodyInstanceGpu)`.

- [ ] **Step 5: Commit**

```bash
git add VIXEN/libraries/SVO/include/ShellOctreeGpu.h VIXEN/shaders/BodyInstanceRayMarch.comp
git commit -m "feat(svo): extend BodyInstanceGpu with provider/recipe fields (32B -> 64B std430)

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Task 4: Branch the shader on the provider

**Files:**
- Modify: `VIXEN/shaders/BodyInstanceRayMarch.comp:200-205` (includes), `:564-659` (instance loop)

- [ ] **Step 1: Add provider constants + include the recipe library**

In `BodyInstanceRayMarch.comp`, in the SHARED INCLUDES block (after line 205, `#include "Lighting.glsl"`), add:

```glsl
#include "SdfRecipes.glsl"

// Provider kinds (mirror ShellOctreeGpu.h ProviderKind + SdfRecipes.h).
#define PROVIDER_STORED     0u
#define PROVIDER_PROCEDURAL 1u
```

- [ ] **Step 2: Branch the per-instance loop**

In `main()`, inside the instance loop, immediately after `uint oi = clamp(inst.octreeIndex, 0u, 2u);` (currently line 568), insert the Procedural branch BEFORE the existing globals/transform/ESVO code:

```glsl
        // --- Procedural provider: analytic SDF sphere-trace (no octree) ---
        if (inst.providerKind == PROVIDER_PROCEDURAL) {
            vec3 pCenter = inst.worldPos;
            vec3 pParams = vec3(inst.recipeParams[0], inst.recipeParams[1], inst.recipeParams[2]);
            vec3 pNormal;
            float pT;
            if (traceProceduralBody(inst.recipeId, pCenter, pParams, rayOrigin, rayDir,
                                    pNormal, pT)) {
                if (pT < bestT) {
                    bestT          = pT;
                    bestColor      = inst.color;   // procedural base colour = instance tint
                    bestNormal     = pNormal;      // smooth SDF-gradient normal
                    bestBrickIndex = 0u;
                    bestVoxelIdx   = 0u;
                    anyHit         = true;
                }
            }
            continue;  // procedural body fully handled; skip the ESVO path
        }
```

(The existing Stored/ESVO code — `g_octreeIdx = ...` onward — runs only for `PROVIDER_STORED` because of the `continue`.)

- [ ] **Step 3: Build the app's shaders to verify GLSL compiles**

Run: `cmd.exe /c _ninja_preset_build.bat` (FOREGROUND; `timeout: 600000`)
Expected: build succeeds; `shader_tool` compiles `BodyInstanceRayMarch.comp` (which now includes `SdfRecipes.glsl`) with no GLSL errors. Grep the tail for `error:` and confirm none.

- [ ] **Step 4: Commit**

```bash
git add VIXEN/shaders/BodyInstanceRayMarch.comp
git commit -m "feat(shaders): Procedural SDF provider branch in BodyInstanceRayMarch

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Task 5: Seed the default 3-body scene as Procedural

**Files:**
- Modify: `VIXEN/application/main/source/graph/BuildRenderGraph.cpp:555-580`

- [ ] **Step 1: Replace the default-body seeding block**

In `BuildRenderGraph.cpp`, replace the `placeCentered` lambda + `defaultBodies` block (lines 555-580; the existing one offsets `worldPos = centre - kHalf` for the Stored shell) with a Procedural seeding block. Procedural bodies use the TRUE world centre (no `kHalf` offset) and carry the recipe params:

```cpp
    {
        // Procedural SDF bodies (Increment 1): true smooth spheres, no octree.
        // worldPos = world centre; recipeParams = (radius, displaceAmp, displaceFreq).
        // Radius 24 matches the prior Stored shells' on-screen size (kHalf=24), so the
        // default camera frames all three. providerKind=1 selects the Procedural path.
        constexpr float kRadius = 24.0f;
        auto placeProcedural = [&](float cx, float cy, float cz,
                                   float r, float g, float b,
                                   uint32_t recipeId, float amp, float freq) {
            Vixen::SVO::BodyInstanceGpu inst{};
            inst.worldPos[0] = cx;
            inst.worldPos[1] = cy;
            inst.worldPos[2] = cz;
            inst.renderScale = 1.0f;            // unused by Procedural
            inst.color[0]    = r;
            inst.color[1]    = g;
            inst.color[2]    = b;
            inst.octreeIndex = 0u;              // unused by Procedural
            inst.providerKind = 1u;             // PROVIDER_PROCEDURAL
            inst.recipeId     = recipeId;       // 0 = sphere, 1 = displaced sphere
            inst.recipeParams[0] = kRadius;
            inst.recipeParams[1] = amp;
            inst.recipeParams[2] = freq;
            return inst;
        };
        std::vector<Vixen::SVO::BodyInstanceGpu> defaultBodies = {
            placeProcedural( 14.0f, 64.0f, 64.0f, 1.00f, 0.95f, 0.85f, 0u, 0.0f, 0.0f),  // left   — smooth star/sphere
            placeProcedural( 64.0f, 64.0f, 64.0f, 0.55f, 0.75f, 1.00f, 1u, 2.0f, 0.5f),  // centre — displaced planet
            placeProcedural(114.0f, 64.0f, 64.0f, 0.85f, 0.90f, 1.00f, 0u, 0.0f, 0.0f),  // right  — smooth sphere
        };
        if (auto* bodyScene = static_cast<BodyOctreeSceneNode*>(renderGraph->GetInstance(bodyOctreeSceneNode))) {
            bodyScene->SetInstances(std::move(defaultBodies));
            mainLogger->Info("[BuildRenderGraph] Seeded 3 Procedural SDF body instances (standalone fallback)");
        }
    }
```

- [ ] **Step 2: Build the app**

Run: `cmd.exe /c _ninja_preset_build.bat` (FOREGROUND; `timeout: 600000`)
Expected: build succeeds, no errors. (`BodyInstanceGpu` is already the 64-byte struct from Task 3.)

- [ ] **Step 3: Commit**

```bash
git add VIXEN/application/main/source/graph/BuildRenderGraph.cpp
git commit -m "feat(app): seed default 3-body scene as Procedural SDF (smooth spheres)

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Task 6: No-regression check for the Stored path

**Files:** none (verification only)

- [ ] **Step 1: Confirm the ESVO path is unreachable only for Procedural bodies**

Re-read `BodyInstanceRayMarch.comp` `main()`: confirm the `continue;` in the Procedural branch is the ONLY new exit, that `PROVIDER_STORED` (0) bodies fall through to the unchanged `g_octreeIdx`/`traverseOctreeInstanced` code, and that `bestT`/`anyHit` accumulation is shared (so Procedural and Stored bodies compose in one scene by nearest-hit).

- [ ] **Step 2: Run the SVO unit suite to confirm no C++ regression from the struct change**

Run: `cd build/libraries/SVO/tests/Debug && for t in test_sdf_recipes.exe test_gpu_parity.exe test_brick_traversal.exe; do ./$t --gtest_brief=1; done`
Expected: all PASS. (These exercise the octree/brick path + the new recipes; the 64-byte record must not perturb them.)

- [ ] **Step 3: Commit (if Step 1/2 required any fix; otherwise skip)**

Only if a fix was needed:
```bash
git add -A && git commit -m "fix: <describe>  (no-regression for Stored path)

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Task 7: Live gate — run the app and confirm smooth spheres (AUTHORITATIVE)

**Files:** none (the authoritative GPU verification; see `[[live-verification-authoritative-for-gpu-work]]`)

- [ ] **Step 1: Capture a baseline (optional)**

A pre-change blocky reference already exists at repo root: `m5_hud_BEFORE.png`. If absent, skip — the goal is unambiguous (cube-faceted vs smooth).

- [ ] **Step 2: Build the app (fresh)**

Run: `cmd.exe /c _ninja_preset_build.bat` (FOREGROUND; `timeout: 600000`)
Expected: success.

- [ ] **Step 3: Run the standalone app with validation**

Run: `cmd.exe /c "set VIXEN_VULKAN_VALIDATION=1&& C:\cpp\VBVS--VIXEN\VIXEN\binaries\VIXEN.exe"` (background or a separate terminal)
Notes: the default app does ~90s GaiaVoxelWorld generation before the render loop (be patient, ~110s). WSL env vars do NOT reach the exe — they MUST be set inside `cmd.exe`.

- [ ] **Step 4: Screenshot + observe**

Foreground the VIXEN window, then capture via PowerShell `CopyFromScreen` (PrintWindow returns black for the Vulkan swapchain):
```
powershell.exe -Command "Add-Type -AssemblyName System.Windows.Forms,System.Drawing; $b=New-Object Drawing.Bitmap 1280,720; $g=[Drawing.Graphics]::FromImage($b); $g.CopyFromScreen(0,0,0,0,$b.Size); $b.Save('C:\\cpp\\VBVS--VIXEN\\sdf_inc1_AFTER.png')"
```
Expected: the three bodies render as **smooth, round spheres** with smooth diffuse shading (no cube facets, no disco-ball normals); the centre body shows gentle displacement; **no surface holes/speckle**. Confirm the validation output reports **0 errors** (the project's sync/render gate).

- [ ] **Step 5: Reap the app**

Run: `cmd.exe /c "taskkill /F /IM VIXEN.exe"`

- [ ] **Step 6: Record the result + update progress**

Append the AFTER screenshot path + a one-line result to `VIXEN/Vixen-Docs/01-Architecture/SDF-Body-Rendering-Inc1-Plan-2026-06.md` (a "Result" section) and to `memory-bank/activeContext.md` (files changed, live-gate outcome, 0-syncval confirmation). Commit:
```bash
git add -A && git commit -m "docs(progress): SDF Inc1 live-gate result (smooth spheres, 0 syncval)

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Cross-repo follow-up (out of scope for Inc 1, do not skip silently)

`BodyInstanceGpu` is a shared host↔renderer contract; the UNDERTOW host mirrors it in `vixen/render/scene_instances.h` (ShellOctreeGpu.h:218 note). Growing it to 64 B is an ABI change: the host must extend its mirror to the identical 64-byte layout and set `providerKind = 0` for its existing (Stored) bodies. Until then, the host's `SetBodyInstances` path would mis-stride. **Flag this to the host maintainer.** The standalone default scene (Task 5) is unaffected.

---

## Self-Review

**Spec coverage** (against `Voxel-Content-Format-Contract-Design-2026-06.md` §8 "Increment 1 scope"):
- "Add providerKind (+recipeId+params) to per-body record" → Task 3. ✓
- "Shader branch on providerKind; Procedural → sphere-trace; Stored → existing ESVO" → Tasks 2 + 4. ✓
- "evalSDF + central-difference gradient feeding computeLighting" → Tasks 1/2 (`evalSdf`/`sdfGradient`), Task 4 (`bestNormal` → existing `computeLighting`). ✓
- "Seed default 3-body scene with Procedural bodies" → Task 5. ✓
- "Keep Stored/ESVO path intact + selectable, no regression" → `continue` gating (Task 4) + Task 6. ✓
- "Testing: live gate authoritative; CPU/GPU parity unit tests; no-regression" → Task 1 (unit), Task 6 (regression), Task 7 (live gate). ✓

**Placeholder scan:** No "TBD/handle edge cases/similar to". Every code step shows complete code; every run step shows the command + expected output. ✓

**Type consistency:** `evalSdf`/`sdfGradient`/`traceProcedural`(CPU)/`traceProceduralBody`(GLSL), `RecipeParams{radius,displaceAmp,displaceFreq,...}`, `RECIPE_SPHERE=0`/`RECIPE_DISPLACED_SPHERE=1`, `PROVIDER_STORED=0`/`PROVIDER_PROCEDURAL=1`, and `recipeParams[6]` with `params.xyz=(radius,amp,freq)` are used identically across `SdfRecipes.h`, `SdfRecipes.glsl`, `ShellOctreeGpu.h`, `BodyInstanceRayMarch.comp`, and `BuildRenderGraph.cpp`. CPU trace fn is `traceProcedural`; GLSL is `traceProceduralBody` (named differently by design — the GLSL one writes `out` params + omits the point; the CPU one returns a `TraceHit`). ✓

**Known acceptable divergence:** the CPU `traceProcedural` returns a `TraceHit` (for unit testing) while the GLSL `traceProceduralBody` writes `out hitNormal/hitT` (shader-idiomatic). The shared, parity-critical math (`evalSdf`, `sdfGradient`, bounding-sphere, step loop, constants) is identical.
