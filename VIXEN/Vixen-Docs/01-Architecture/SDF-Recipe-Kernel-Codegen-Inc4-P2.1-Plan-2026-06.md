# P2.1 — Materialization (recipe → bake → Stored → render) — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: superpowers:subagent-driven-development / executing-plans. Checkbox steps. **VIXEN-only**, branch `feat/sdf-recipe-codegen-p2` (off P0). Reuses the P0 recipe CPU-VM + the Inc2/Inc3 Stored bake+render unchanged.

**Goal:** Bake an `SdfInstruction[]` recipe (via the P0 CPU stack-VM) into the existing Stored SoA pool and render it through the existing stored path — delivering materialization (procedural recipe → editable Stored voxels) end-to-end, with the analytic bake path untouched.

**Architecture:** Refactor `SdfBake.h` so the two-pass bake takes an **eval callable** (`float(glm::vec3)`); the analytic path passes a lambda over `evalSdf` (unchanged behaviour), a new recipe path passes a lambda over `Recipe::evalRecipe`. `BodyOctreeSceneNode` gains a recipe-injection so its existing bake→serialize→GPU-buffer path feeds a recipe body; the shipped `BodyInstanceRayMarch.comp` renders it via the existing Stored octree traversal — no shader/GPU-pipeline changes.

**Tech Stack:** C++23, GoogleTest, CMake (`vixen-wsl`), glslang/Vulkan (lavapipe for the render gate), glm.

## Global Constraints

- **Analytic path behaviour-preserving.** `BakeRecipeToSdfWorld(recipeId, …)` must remain byte-behaviour-identical (it becomes a thin wrapper over the new core). Existing `test_sdf_bake`, `test_soa_sdf_serialize`, and `RenderStoredSdf*` render tests must stay green.
- **No shader / GPU-pipeline changes.** Render reuses the shipped `BodyInstanceRayMarch.comp` + the Stored octree path. P2.1 is CPU bake + node wiring only.
- **Recipe format = P0's** `Vixen::SVO::Recipe::SdfInstruction` / `evalRecipe` (byte-compat, Sphere+Union opcodes — enough; no new opcodes this slice).
- **Material stays synthesized** (cos-band color + Y-stripe roughness, as today) — authored per-voxel material is deferred (not this slice).
- **Live gate authoritative** for the render (lavapipe offscreen + controller reads the PNG), per the project rule.
- **Repo:** VIXEN worktree `/mnt/c/cpp/VBVS--VIXEN/.claude/worktrees/sdf-recipe-codegen-p0` (on branch `feat/sdf-recipe-codegen-p2`). Build: `cmake --preset vixen-wsl`.

## File Structure

**Modify:**
- `VIXEN/libraries/SVO/include/SdfBake.h` — extract eval-callable core `BakeSdfWorld(eval, center, n, bandVoxels, brickDepth)`; `BakeRecipeToSdfWorld` → thin wrapper; add `BakeRecipeInstructionsToSdfWorld(prog, count, center, n, bandVoxels, brickDepth)`.
- `VIXEN/libraries/RenderGraph/.../BodyOctreeSceneNode.{h,cpp}` — recipe-injection setter + a bake branch in the SDF-demo bake path.
- `VIXEN/libraries/SVO/tests/CMakeLists.txt` — register `test_recipe_bake`.

**Create:**
- `VIXEN/libraries/SVO/tests/test_recipe_bake.cpp` — CPU parity (recipe-sphere bake ≡ analytic-sphere bake).
- A new `TEST_F(BodyInstanceRayMarchRenderTest, RenderRecipeBakedBody)` in `VIXEN/libraries/RenderGraph/tests/Nodes/test_body_instance_raymarch_render.cpp` (reuse the fixture + helpers).

---

## Milestone Map

> Persisted for the context-manager pipeline (2026-06-26). Two milestones, sequential. VIXEN-only.

- [x] **M1 `[VIXEN]` — Eval-callable bake core + recipe bake (Task 1).** CPU-only, low-risk. Gate: `test_recipe_bake` green (recipe-sphere ≡ analytic-sphere) + `test_sdf_bake`/`test_soa_sdf_serialize` no-regression. Implementer **Sonnet**.
- [x] **M2 `[VIXEN]` — Node recipe-injection + lavapipe render gate (Tasks 2–3).** Gate: new `RenderRecipeBakedBody` renders a recipe-baked body SOLID on lavapipe (fillRatio > 0.97) + controller/validator reads the PNG; `RenderStoredSdf*` no-regression. Implementer **Sonnet**.

Validators: **Opus** per milestone (M2 validator reads the PNG; tamper-check the parity test). Controller: Opus, thin.

## Progress Log

- **M1 `[VIXEN]` (Task 1): DONE** · commit `b75dd45d` · Opus validator APPROVED, **tamper-verified** (recipe radius +2 → parity failed by exactly 2.0 → restore → pass) · 2026-06-26 — `SdfBake` refactored to an eval-callable core `BakeSdfWorld<EvalFn>`; `BakeRecipeToSdfWorld` is now a thin wrapper (analytic path byte-identical, both band+populate eval sites converted, color/roughness untouched); `BakeRecipeInstructionsToSdfWorld` bakes a recipe via `Recipe::evalRecipe`. `test_recipe_bake` parity green + `test_sdf_bake` 2/2 + `test_soa_sdf_serialize` 11/11. **Note for M2:** `build-wsl` ctest discovery is stale (run test binaries directly, or `cmake --preset vixen-wsl` to regenerate); unused `#include <functional>` in SdfBake.h is a harmless nit.
- **M2 `[VIXEN]` (Tasks 2–3): DONE** · commit `ed4f6a81` · 2026-06-26 — `BodyOctreeSceneNode` gains `SetBakeRecipe`+`bakeRecipe_`; octree 0 bake branches on non-empty recipe (analytic path byte-preserved for empty/k>0). `RenderRecipeBakedBody` TEST_F: sphere∪sphere peanut → lavapipe bodyPx=27618, fillRatio=0.9969. No-regression: `RenderStoredSdfBodiesNoHoles`+`RenderStoredSdfMultiChannel` fillRatio=0.9895; `test_recipe_bake`/`test_sdf_bake`/`test_soa_sdf_serialize` all green. **Bonus fix:** `SdfCoreKernels.g.hpp` non-inline ODR violation (functions not marked `inline` → duplicate symbol when BodyOctreeSceneNode.o + test TU linked together); added `inline`. PNG at `/tmp/glsl_sdf_recipe_peanut.png`.
- **Inline-fix resolution** · Yeroket `7d94dfd0` + VIXEN `28db78d9` · Opus re-APPROVED — the bonus `inline` was initially a **hand-edit to the vendored generated header (band-aid)**; M2 validation flagged it as breaking P1's byte-identity / no-re-vendor invariant. Fixed at root: the Yeroket generator (`CppAstVisitor.EmitFunction`) now emits `inline`; the committed artifact was regenerated and VIXEN **re-vendored byte-identical** (bodies identical, provenance line aside). P1 tests tightened to assert the `inline` prefix + a guard that HLSL never inherits `inline`; `RegeneratedCpp_…MatchesCommittedArtifact` passes against the new artifact; Yeroket suite 87/4 (same 4 pre-existing, 0 new). ODR stays fixed across future re-vendors. **P2.1 COMPLETE.**

---

## Task 1 [M1]: Eval-callable bake core + recipe bake + CPU parity

**Files:** Modify `VIXEN/libraries/SVO/include/SdfBake.h`; Create `VIXEN/libraries/SVO/tests/test_recipe_bake.cpp`; Modify `…/SVO/tests/CMakeLists.txt`.

**Interfaces:**
- Consumes: `Vixen::SVO::Recipe::evalRecipe(const SdfInstruction*, uint32_t, glm::vec3)` + `SdfInstruction`/`SdfOpCode` (P0, `include/Recipe/`).
- Produces: `SdfBakeResult BakeRecipeInstructionsToSdfWorld(const Recipe::SdfInstruction* prog, uint32_t count, const glm::vec3& center, int n, float bandVoxels, int brickDepth = 3)`.

- [ ] **Step 1: Write the failing parity test.** A single-sphere recipe must bake identically to the analytic sphere (same formula `length(p-center)-r`):
```cpp
// VIXEN/libraries/SVO/tests/test_recipe_bake.cpp
#include <gtest/gtest.h>
#include "SdfBake.h"
#include "Recipe/SdfInstruction.h"
#include <glm/glm.hpp>
using namespace Vixen::SVO;
static Recipe::SdfInstruction sphereInstr(glm::vec3 c, float r){
    Recipe::SdfInstruction in{}; in.opCode=(uint8_t)Recipe::SdfOpCode::Sphere;
    in.data[0]=c.x; in.data[1]=c.y; in.data[2]=c.z; in.data[3]=r; return in;
}
TEST(RecipeBake, RecipeSphereEqualsAnalyticSphere){
    const int n=64; const glm::vec3 c(32,32,32); const float r=26.0f, band=2.5f;
    auto analytic = BakeRecipeToSdfWorld(RECIPE_SPHERE, c, RecipeParams{r,0,0,0,0,0}, n, band, 3);
    Recipe::SdfInstruction prog[] = { sphereInstr(c, r) };
    auto recipe   = BakeRecipeInstructionsToSdfWorld(prog, 1, c, n, band, 3);
    int checked=0;
    for (glm::vec3 p : { glm::vec3(32,32,6), glm::vec3(32,32,32), glm::vec3(48,32,32), glm::vec3(10,10,10) }) {
        auto a = analytic.sampleStored(p); auto b = recipe.sampleStored(p);
        ASSERT_EQ(a.has_value(), b.has_value()) << "allocation differs at " << p.x<<","<<p.y<<","<<p.z;
        if (a.has_value()) { EXPECT_NEAR(a.value(), b.value(), 1e-4f); ++checked; }
    }
    EXPECT_GT(checked, 0);
}
```
- [ ] **Step 2: Run → FAIL** (no `BakeRecipeInstructionsToSdfWorld`). Register the test in `…/SVO/tests/CMakeLists.txt` (mirror `test_sdf_bake`).
  Run: `cmake --preset vixen-wsl && cmake --build build-wsl --target test_recipe_bake` → expect compile FAIL.
- [ ] **Step 3: Refactor `SdfBake.h`.** Extract the two-pass body (currently `BakeRecipeToSdfWorld`, calling `evalSdf(recipeId,p,center,rp)` at the band pass + populate pass) into a core templated on an eval callable:
```cpp
template<class EvalFn>
inline SdfBakeResult BakeSdfWorld(EvalFn&& eval, const glm::vec3& center, int n, float bandVoxels, int brickDepth=3) {
    // ... identical to current BakeRecipeToSdfWorld, but every `evalSdf(recipeId,p,center,rp)` → `eval(p)` ...
}
inline SdfBakeResult BakeRecipeToSdfWorld(uint32_t recipeId, const glm::vec3& center, const RecipeParams& rp,
                                          int n, float bandVoxels, int brickDepth=3) {
    return BakeSdfWorld([&](const glm::vec3& p){ return evalSdf(recipeId, p, center, rp); }, center, n, bandVoxels, brickDepth);
}
inline SdfBakeResult BakeRecipeInstructionsToSdfWorld(const Recipe::SdfInstruction* prog, uint32_t count,
                                          const glm::vec3& center, int n, float bandVoxels, int brickDepth=3) {
    return BakeSdfWorld([&](const glm::vec3& p){ return Recipe::evalRecipe(prog, count, p); }, center, n, bandVoxels, brickDepth);
}
```
  (`#include "Recipe/SdfRecipeEval.h"` in `SdfBake.h`. The color/roughness synthesis in the populate pass is unchanged — only the Density `eval` source changes.)
- [ ] **Step 4: Run → PASS.** `cmake --build build-wsl --target test_recipe_bake && ./build-wsl/libraries/SVO/tests/test_recipe_bake --gtest_brief=1` → PASS.
- [ ] **Step 5: No-regression (analytic path unchanged).** `ctest --test-dir build-wsl -R "test_sdf_bake|test_soa_sdf_serialize" --output-on-failure` → green.
- [ ] **Step 6: Commit** `feat(recipe): eval-callable bake core + BakeRecipeInstructionsToSdfWorld, parity-gated (P2.1 M1)`.

## Task 2 [M2]: Node recipe-injection + render a recipe-baked body

**Files:** Modify `BodyOctreeSceneNode.{h,cpp}`; add a `TEST_F` to `test_body_instance_raymarch_render.cpp`.

**Interfaces:**
- Produces: a way to inject a recipe into the node's bake — e.g. `void SetBakeRecipe(std::vector<Recipe::SdfInstruction> prog)`; when set, octree 0's bake uses `BakeRecipeInstructionsToSdfWorld` instead of the hardcoded analytic recipe.

- [ ] **Step 1: Add recipe injection to the node.** Find the `BakeRecipeToSdfWorld` call site in `BodyOctreeSceneNode`'s SDF bake path (`EnsureOctreesBuilt`, gated by `VIXEN_STORED_SDF_DEMO`). Add a member `std::vector<Recipe::SdfInstruction> bakeRecipe_;` + `SetBakeRecipe(...)`. In the bake path, **if `bakeRecipe_` is non-empty, bake octree 0 via `BakeRecipeInstructionsToSdfWorld(bakeRecipe_.data(), bakeRecipe_.size(), center, n, band, brickDepth)`** using the SAME `center/n/band/brickDepth` the analytic sphere uses; otherwise the existing hardcoded path (so all existing tests are unaffected). Keep octrees 1/2 on the existing path.
- [ ] **Step 2: Write the render test** — mirror `RenderStoredSdfBodiesNoHoles` (same fixture/helpers `MakeInstance`/`MakeCamera`/`RenderToRgba`/`ShaderBodyCentre`), but inject a **sphere∪sphere** recipe (two offset spheres in grid space → a peanut the hardcoded recipes can't make) before `Compile`:
```cpp
TEST_F(BodyInstanceRayMarchRenderTest, RenderRecipeBakedBody) {
    ASSERT_TRUE(softwareConfirmed_);
    // ... node setup identical to RenderStoredSdfBodiesNoHoles up to Setup() ...
    auto sph = [](glm::vec3 c, float r){ Vixen::SVO::Recipe::SdfInstruction in{};
        in.opCode=(uint8_t)Vixen::SVO::Recipe::SdfOpCode::Sphere;
        in.data[0]=c.x;in.data[1]=c.y;in.data[2]=c.z;in.data[3]=r; return in; };
    Vixen::SVO::Recipe::SdfInstruction uni{}; uni.opCode=(uint8_t)Vixen::SVO::Recipe::SdfOpCode::Union;
    node->SetBakeRecipe({ sph({26,32,32},16.0f), sph({38,32,32},16.0f), uni });   // peanut
    node->Setup();
    ::setenv("VIXEN_STORED_SDF_DEMO","1",1); ASSERT_NO_THROW(node->Compile()); ::unsetenv("VIXEN_STORED_SDF_DEMO");
    // ... render octree 0 via the same renderBody/fillRatio flow as RenderStoredSdfBodiesNoHoles,
    //     PNG -> /tmp/glsl_sdf_recipe_peanut.png ...
    EXPECT_GT(bodyPixels, 20000) << "recipe-baked body barely rendered";
    EXPECT_GT(fillRatio, 0.97)   << "recipe-baked body has interior holes";
}
```
- [ ] **Step 3: Build + run on lavapipe.**
  Run: `cmake --build build-wsl --target test_body_instance_raymarch_render` then run the binary with the lavapipe env (`VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json VK_LAYER_PATH=<sdk>/x86_64/share/vulkan/explicit_layer.d ./build-wsl/.../test_body_instance_raymarch_render --gtest_filter=*RenderRecipeBakedBody*`). Expect PASS (solid). **Controller reads `/tmp/glsl_sdf_recipe_peanut.png`** to confirm a solid two-lobe (peanut) body — a shape the hardcoded recipes cannot produce, proving recipe→bake→render end-to-end.

## Task 3 [M2]: No-regression + commit

- [ ] **Step 1: Existing render no-regression.** Run `*RenderStoredSdfBodiesNoHoles*` + `*RenderStoredSdfMultiChannel*` (lavapipe) → green (the node's no-recipe path + analytic bake unchanged).
- [ ] **Step 2: CPU suites.** `ctest --test-dir build-wsl -R "test_sdf_bake|test_soa_sdf_serialize|test_recipe_bake" --output-on-failure` → green.
- [ ] **Step 3: Commit** `feat(recipe): node recipe-injection + lavapipe render gate — materialization end-to-end (P2.1 M2)`.

## Self-Review

**Coverage:** eval-callable core + recipe bake → T1; node injection + render → T2; no-regression → T1.5/T3. The materialization claim (recipe→bake→Stored→render) is proven by T1 (data parity) + T2 (visual end-to-end). ✓
**Placeholders:** T2 Step 1 gives the node-injection shape + the call site lead (`EnsureOctreesBuilt`/`VIXEN_STORED_SDF_DEMO`); the implementer locates the exact line. Test code is concrete. No "TODO". ✓
**Type consistency:** `BakeSdfWorld`/`BakeRecipeInstructionsToSdfWorld`/`SetBakeRecipe`, `Recipe::SdfInstruction`/`SdfOpCode`/`evalRecipe` — consistent across tasks. ✓
**Risk:** M2's node change is the main surface; the `bakeRecipe_`-empty guard keeps the analytic path (all existing tests) untouched. Lavapipe render is the authoritative gate.

## Execution Handoff

Run via post-brainstorm-context-manager (2 milestones). M1 Sonnet+Opus (CPU parity, tamper-check). M2 Sonnet+Opus (lavapipe render — validator AND controller read the PNG).
