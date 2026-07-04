# Inc2a — Bake Center-Offset Fix Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make `BakeRecipeInstructionsToSdfWorld`'s recipe-instruction bake path honor `center` the same way the analytic path (`BakeRecipeToSdfWorld`/`evalSdf`) already does — object-centered authoring (`p - center` at eval), migrating every existing hand-placed-at-grid-center recipe/test to object-centered coordinates — so Slice 3 (drawn layers/brushes) and future document authoring never need the "author your primitive at the grid-center value" workaround again.

**Architecture:** One-line fix in `BakeRecipeInstructionsToSdfWorld`'s eval closure (`SdfBake.h`) to subtract `center` before calling `evalRecipe`, mirroring `evalSdf`'s existing `p - center` convention exactly. Every call site that currently hand-authors SDF primitive coordinates at the grid-center value (today `(32,32,32)` or `(8,8,8)` depending on grid size `n`) is migrated to author at `(0,0,0)` instead, relying on the now-correct `center` parameter to place the geometry. `vixen_editor`'s `EditorApplication::ApplyDocumentToScene` and M4's `test_editor_document_render.cpp` gate are re-verified: because the M4 golden document (`sample_tri_layer.vxd`) is *already* object-centered (its Box/Sphere/Cylinder sit near `(0,0,0)`, which is exactly why M4 had to camera-frame the positive-octant corner as a workaround), this fix should make the *entire* golden geometry visible and centered in the grid — a strictly more correct render than M4's workaround, verified by re-deriving the ablation gate's camera/expectations from scratch rather than assuming M4's old numbers still apply.

**Tech Stack:** C++23/CMake/Vulkan (VIXEN), WSL build, gtest, lavapipe headless render gates.

## Global Constraints

- This is a **narrow, mechanical fix** — object-centered convention only. Do NOT build a panel/viewport abstraction, auto-fit-from-bounds, or any UI work in this increment (that is Inc2b, a documented follow-up, not started here).
- Append-only/format contracts (VDC1, VRC1) are UNCHANGED — this fix touches only bake-time C++ evaluation, never the document or recipe binary format.
- Do NOT touch `VIXEN/libraries/SVO/include/Recipe/generated/VoxelDocument.g.h`, `SdfOpCodes.g.h`, `VoxelDocumentFlattener.*`, or any Yeroket-repo file — none of those are implicated by this bug.
- Every existing test whose recipe geometry is hand-placed at the grid-center value must be migrated to object-centered coordinates in the SAME commit as the eval-closure fix (never leave the codebase in a state where the fix is live but old tests still assume the old broken behavior — that would silently break them).
- TDD: write a new failing test proving the bug (object-centered geometry currently lands at the wrong grid location) before the fix; migrate existing tests as a mechanical, verified-equivalent transform (same geometry, same expected results, just re-expressed in the new coordinate convention) — not a redesign of what they test.
- Build with the WSL preset — read `VIXEN/CLAUDE.md` and `.claude/skills/project-rules/rules/commands.md` for the exact configure/build/test invocation. This is CPU+GPU (lavapipe) work; the final gate needs a real render.
- rtk-wrapped git masks exit codes — verify `git log --oneline -1` + `git status -sb` explicitly after every commit. A prior session in this repo also hit a transient `push` failure specific to worktree paths (Windows credential-manager.exe subprocess failing to resolve the worktree's nested gitdir) — if `git push` from the worktree fails with "Failed to enumerate all Git configuration entries" / "not a git repository: .../.git/worktrees/...", retry the identical push from the MAIN checkout instead (`git -C <main-repo-root> push origin refs/heads/<branch>:refs/heads/<branch>`), which reads the same commits via the shared object store and works around the issue.
- Work in a fresh worktree (per `superpowers:using-git-worktrees` / the user's stated preference for worktree isolation) branched from the now-pushed `feat/voxel-authoring-inc1` (VIXEN, commit `92e85584`+merge `a87bb390`, already on `origin`) — do NOT reuse the old worktree dir, branch fresh as `feat/voxel-authoring-inc2a-bake-center`.

---

## Milestone Map

- **Milestone 1 (Tasks 1-4): Core fix + mechanical test migration.** TDD red test proving the bug (Task 1), the one-line eval-closure fix making it green (Task 2), then migrate `test_recipe_bake.cpp`/`test_octree_pool.cpp` (Task 3) and `test_recipe_baker.cpp`/`test_recipe_pool_render.cpp` (Task 4) to object-centered authoring. Produces a fully working, fully tested fix.
- **Milestone 2 (Tasks 5-7): Verification sweep + editor re-gate + docs closeout.** Confirm no hidden coordinate assumptions were missed (Task 5), re-verify `vixen_editor` + M4's live gate against corrected centering, re-deriving numbers from scratch if needed (Task 6), and update the design doc's Next-steps section (Task 7).

## Progress Log

- Milestone 1 (Tasks 1-4): DONE · commits d93b1e49..6cee271d · Opus validator APPROVED (clean tree, no findings, independent rebuild of test_recipe_bake_center/test_recipe_bake/test_octree_pool/test_recipe_baker/test_recipe_pool_render all green) · 2026-07-03
- Milestone 2 (Tasks 5-7): DONE · commits dae53bcd..56a0c93d · Opus validator APPROVED (clean tree, no findings; Task 5 legitimately expanded scope to migrate 2 additional files — test_recipe_authoring_gate.cpp, test_body_instance_raymarch_render.cpp — whose hidden hand-placed coordinates the plan's original grep missed but which would have silently broken post-fix; Task 6 re-derived the editor/M4 live-gate camera + pixel-count baseline from a fresh lavapipe run since center=(32,32,32) is non-zero; independent rebuild of test_recipe_bake_center/test_recipe_authoring_gate/test_body_instance_raymarch_render/test_editor_document_render reproduced the exact reported numbers) · 2026-07-03 · **Inc2a plan COMPLETE.**

---

### Task 1: Prove the bug — a failing test on the current (broken) behavior

**Files:**
- Create: `VIXEN/libraries/SVO/tests/test_recipe_bake_center.cpp`
- Modify: `VIXEN/libraries/SVO/tests/CMakeLists.txt` — register the new test binary (mirror how `test_recipe_bake.cpp` is registered; find and copy that exact block, renaming the target).

**Interfaces:**
- Consumes: `Vixen::SVO::BakeRecipeInstructionsToSdfWorld(const Recipe::SdfInstruction* prog, uint32_t count, const glm::vec3& center, int n, float bandVoxels, int brickDepth = 3)` (existing signature, `VIXEN/libraries/SVO/include/SdfBake.h:170`) — signature is UNCHANGED by this plan, only its internal behavior changes.
- Consumes: `Vixen::SVO::SdfBakeResult::sampleStored(const glm::vec3& gridPos) const -> std::optional<float>` (existing, `VIXEN/libraries/SVO/include/SdfBake.h:181`).
- Consumes: `Vixen::SVO::Recipe::SdfInstruction`, `Vixen::SVO::Recipe::SdfOpCode::Sphere` (existing, vendored types).
- Produces: nothing new — this is a pure test task establishing the red state.

- [x] **Step 1: Write the failing test**

```cpp
// VIXEN/libraries/SVO/tests/test_recipe_bake_center.cpp
//
// Proves BakeRecipeInstructionsToSdfWorld currently IGNORES `center` in its
// eval closure (unlike BakeRecipeToSdfWorld/evalSdf, which already applies
// `p - center`). An object-centered sphere instruction (authored at (0,0,0))
// should, after this task's fix, land at the grid's `center` — today it
// lands at raw grid-origin instead, which this test currently asserts AS
// THE (WRONG) OBSERED BEHAVIOR to document the bug, then gets flipped to
// assert the CORRECT behavior once Task 2 lands the fix.
#include <gtest/gtest.h>
#include <glm/glm.hpp>
#include "SdfBake.h"
#include "Recipe/RecipeStack.h"

using namespace Vixen::SVO;

namespace {
Recipe::SdfInstruction sphereAtOrigin(float r) {
    Recipe::SdfInstruction in{};
    in.opCode = static_cast<uint8_t>(Recipe::SdfOpCode::Sphere);
    in.data[0] = 0.0f; in.data[1] = 0.0f; in.data[2] = 0.0f; in.data[3] = r;
    return in;
}
} // namespace

// This test's name and assertion describe the CORRECT (post-fix) behavior.
// Run it now to confirm it FAILS (proving the bug), then it becomes the
// permanent regression test after Task 2's fix makes it pass.
TEST(RecipeBakeCenter, ObjectCenteredSphereBakesAtRequestedGridCenter) {
    const int n = 64;
    const glm::vec3 gridCenter(32.0f, 32.0f, 32.0f);
    const float radius = 10.0f;
    const float band = 2.5f;

    Recipe::SdfInstruction prog[] = { sphereAtOrigin(radius) };
    auto baked = BakeRecipeInstructionsToSdfWorld(prog, 1, gridCenter, n, band, 3);

    // A voxel AT the requested grid-center must be solid (inside the sphere,
    // since the sphere is authored with radius 10 about local-origin, and
    // local-origin should map to gridCenter after centering).
    auto atCenter = baked.sampleStored(gridCenter);
    ASSERT_TRUE(atCenter.has_value())
        << "expected a voxel to be allocated at the grid center for an "
           "object-centered sphere baked with center=" << gridCenter.x
        << "," << gridCenter.y << "," << gridCenter.z;
    EXPECT_LT(*atCenter, 0.0f) << "grid-center point should be INSIDE the "
                                   "sphere (negative signed distance)";

    // A voxel at raw grid-origin (0,0,0) — far outside the sphere's radius
    // from gridCenter — must NOT be solid, proving the geometry was placed
    // AT center, not at raw grid coordinates.
    auto atOrigin = baked.sampleStored(glm::vec3(0.0f, 0.0f, 0.0f));
    EXPECT_FALSE(atOrigin.has_value())
        << "grid-origin should be far outside the centered sphere; if this "
           "voxel IS allocated, `center` is still being ignored";
}
```

- [x] **Step 2: Register the test target**

Add to `VIXEN/libraries/SVO/tests/CMakeLists.txt` (find the existing block for `test_recipe_bake` and copy its structure exactly, e.g. `add_executable(test_recipe_bake_center test_recipe_bake_center.cpp)` + the same `target_link_libraries`/`gtest_discover_tests` calls with the new target name).

- [x] **Step 3: Build and run to confirm it FAILS (proving the bug)**

Run: `cmake --build <build-dir> --target test_recipe_bake_center -- -j8` then run the resulting binary directly.
Expected: **FAIL** — either `atCenter` has no value (sphere didn't land at grid-center) or `atOrigin` unexpectedly has a value (sphere is still sitting at raw grid-origin). This is the expected RED state proving the bug exists; do not proceed to Task 2 without seeing this fail first.

- [x] **Step 4: Commit the red test**

```bash
git add VIXEN/libraries/SVO/tests/test_recipe_bake_center.cpp VIXEN/libraries/SVO/tests/CMakeLists.txt
git commit -m "test(svo): failing test proving BakeRecipeInstructionsToSdfWorld ignores center

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_011A1Suqs5xYS7YkgURas58o"
git log --oneline -1
git status -sb
```

---

### Task 2: Fix `BakeRecipeInstructionsToSdfWorld` to apply `center`

**Files:**
- Modify: `VIXEN/libraries/SVO/include/SdfBake.h:169-178` (the `BakeRecipeInstructionsToSdfWorld` function).

**Interfaces:**
- Consumes: `Vixen::SVO::Recipe::evalRecipe(const Recipe::SdfInstruction* prog, uint32_t count, const glm::vec3& p) -> float` (existing, UNCHANGED — this fix does not touch `evalRecipe` itself, only what coordinate it's called with).
- Produces: `BakeRecipeInstructionsToSdfWorld`'s external signature and return type (`SdfBakeResult`) are UNCHANGED — this is a pure internal-behavior fix, callers do not need to change their call syntax (only the VALUES they pass, per Task 3+).

- [x] **Step 1: Apply the fix**

```cpp
// VIXEN/libraries/SVO/include/SdfBake.h — replace lines 169-178

// Recipe-instruction path — evaluates a P0 SdfInstruction[] program via the CPU stack-VM.
// The program is authored in OBJECT-CENTERED space (matching evalSdf's
// convention on the analytic path): `center` is subtracted from the grid
// sample point before evaluation, so a primitive authored at local (0,0,0)
// lands at `center` in the grid. This mirrors evalSdf's `p - center` exactly
// (SdfBake.h ~line 45) — the two bake paths are now consistent.
inline SdfBakeResult BakeRecipeInstructionsToSdfWorld(const Recipe::SdfInstruction* prog,
                                                      uint32_t count,
                                                      const glm::vec3& center,
                                                      int n, float bandVoxels,
                                                      int brickDepth = 3) {
    return BakeSdfWorld(
        [&](const glm::vec3& p) { return Recipe::evalRecipe(prog, count, p - center); },
        center, n, bandVoxels, brickDepth);
}
```

- [x] **Step 2: Run Task 1's test to confirm it now PASSES**

Run: `cmake --build <build-dir> --target test_recipe_bake_center -- -j8` then run the binary.
Expected: **PASS** — both assertions in `ObjectCenteredSphereBakesAtRequestedGridCenter` succeed.

- [x] **Step 3: Commit**

```bash
git add VIXEN/libraries/SVO/include/SdfBake.h
git commit -m "fix(svo): BakeRecipeInstructionsToSdfWorld now applies center (p - center)

Mirrors evalSdf's existing convention on the analytic bake path. Recipe
programs are now authored in object-centered space; center places them
in the grid at bake time, instead of requiring hand-computed grid-absolute
coordinates.

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_011A1Suqs5xYS7YkgURas58o"
git log --oneline -1
git status -sb
```

---

### Task 3: Migrate `test_recipe_bake.cpp` and `test_octree_pool.cpp` to object-centered authoring

**Files:**
- Modify: `VIXEN/libraries/SVO/tests/test_recipe_bake.cpp` (the `RecipeBake.RecipeSphereEqualsAnalyticSphere` test, ~lines 8-30).
- Modify: `VIXEN/libraries/SVO/tests/test_octree_pool.cpp` (the `sphereI`/`makeSphere` helpers, ~lines 18-30, and every call site of `makeSphere`).

**Interfaces:**
- Consumes: Task 2's fixed `BakeRecipeInstructionsToSdfWorld` (same signature, corrected behavior).
- Produces: nothing new — these are existing tests being migrated to the new authoring convention with IDENTICAL expected outcomes (same allocated/not-allocated voxels, same sampled distances), just re-expressed so the primitive is authored at local-origin and `center` does the placement instead of both being hard-coded to the same absolute value.

- [x] **Step 1: Read the current test bodies in full before editing**

Read `VIXEN/libraries/SVO/tests/test_recipe_bake.cpp` (full file, it's short — under 40 lines per the earlier grep) and `VIXEN/libraries/SVO/tests/test_octree_pool.cpp` (full file) to see every call site, not just the ones already found by grep — confirm there are no other hand-placed-at-center instructions beyond what's documented here.

- [x] **Step 2: Migrate `test_recipe_bake.cpp`**

Change the `RecipeSphereEqualsAnalyticSphere` test from:
```cpp
const glm::vec3 c(32, 32, 32);
// ...
Recipe::SdfInstruction prog[] = { sphereInstr(c, r) };
auto recipe = BakeRecipeInstructionsToSdfWorld(prog, 1, c, n, band, 3);
```
to:
```cpp
const glm::vec3 c(32, 32, 32);   // grid center — passed as `center`, not baked into the instruction
// ...
Recipe::SdfInstruction prog[] = { sphereInstr(glm::vec3(0, 0, 0), r) };  // object-centered
auto recipe = BakeRecipeInstructionsToSdfWorld(prog, 1, c, n, band, 3);
```
The `analytic` comparison line (`BakeRecipeToSdfWorld(RECIPE_SPHERE, c, RecipeParams{...}, n, band, 3)`) is UNCHANGED — that path already applies `center` correctly via `evalSdf`, so no edit needed there. The test's assertion loop (comparing `analytic.sampleStored(p)` vs `recipe.sampleStored(p)` at absolute grid points like `(32,32,6)`, `(32,32,32)`) is UNCHANGED — both bake results should still agree at those absolute grid points, since both paths now correctly place a radius-`r` sphere AT `c`.

- [x] **Step 3: Migrate `test_octree_pool.cpp`**

Change:
```cpp
static SdfBodyOctree makeSphere(float radius) {
    Recipe::SdfInstruction prog = sphereI(glm::vec3(8, 8, 8), radius);
    auto baked = BakeRecipeInstructionsToSdfWorld(&prog, 1,
                     glm::vec3(8, 8, 8), /*n=*/16, /*band=*/2.0f, /*brickDepth=*/3);
    return BuildSdfBodyOctree(baked, 3);
}
```
to:
```cpp
static SdfBodyOctree makeSphere(float radius) {
    const glm::vec3 gridCenter(8, 8, 8);   // n=16 grid, so its center is (8,8,8)
    Recipe::SdfInstruction prog = sphereI(glm::vec3(0, 0, 0), radius);  // object-centered
    auto baked = BakeRecipeInstructionsToSdfWorld(&prog, 1,
                     gridCenter, /*n=*/16, /*band=*/2.0f, /*brickDepth=*/3);
    return BuildSdfBodyOctree(baked, 3);
}
```
All call sites of `makeSphere(...)` in this file are unaffected (they only pass `radius`, the function signature is unchanged).

- [x] **Step 4: Build and run both test binaries, confirm identical PASS results to before this task**

Run: `cmake --build <build-dir> --target test_recipe_bake test_octree_pool -- -j8` then run both binaries.
Expected: **PASS**, same test count and names as before this migration (this is a behavior-preserving refactor of the test's own authoring, not a change to what's being verified).

- [x] **Step 5: Commit**

```bash
git add VIXEN/libraries/SVO/tests/test_recipe_bake.cpp VIXEN/libraries/SVO/tests/test_octree_pool.cpp
git commit -m "test(svo): migrate test_recipe_bake/test_octree_pool to object-centered authoring

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_011A1Suqs5xYS7YkgURas58o"
git log --oneline -1
git status -sb
```

---

### Task 4: Migrate `test_recipe_baker.cpp` and `test_recipe_pool_render.cpp` to object-centered authoring

**Files:**
- Modify: `VIXEN/libraries/SVO/tests/test_recipe_baker.cpp` (4 call sites at `sphereInstr(glm::vec3(32, 32, 32), ...)`, lines ~33/35/59/74 per the earlier grep — re-verify exact lines by reading the file first, line numbers may have shifted).
- Modify: `VIXEN/libraries/RenderGraph/tests/Nodes/test_recipe_pool_render.cpp` (the `makeSphere` lambda at line ~430 and its call site at line ~441 per the earlier grep — re-verify by reading the file first).

**Interfaces:**
- Consumes: `RecipeBakeConfig::center` (existing, `VIXEN/libraries/SVO/include/Recipe/RecipeBaker.h:23`, default `{32.f, 32.f, 32.f}`, UNCHANGED by this plan) — these tests rely on `RecipeBaker.h`'s `BakeRegistryToPool` which already threads `cfg.center` into `BakeRecipeInstructionsToSdfWorld` (confirmed at `RecipeBaker.h:73-76`), so once Task 2's fix lands, `cfg.center`'s default value will finally take real effect here.
- Produces: nothing new — behavior-preserving migration, same as Task 3.

- [x] **Step 1: Read both files in full before editing**

Read `VIXEN/libraries/SVO/tests/test_recipe_baker.cpp` and `VIXEN/libraries/RenderGraph/tests/Nodes/test_recipe_pool_render.cpp` in full to get exact current line numbers and confirm no other hand-placed coordinates exist beyond what the earlier grep found (the grep only searched for `glm::vec3(32` and `cfg.center`/`.center =` patterns — re-read to be sure nothing was missed, e.g. a differently-formatted literal).

- [x] **Step 2: Migrate `test_recipe_baker.cpp`**

For each of the 4 `sphereInstr(glm::vec3(32, 32, 32), <radius>)` call sites found, change the coordinate argument to `glm::vec3(0, 0, 0)`. Since these tests bake via `RecipeBaker.h`'s `BakeRegistryToPool` (which uses `RecipeBakeConfig::center`, default `(32,32,32)`), leaving the config's default center unchanged means the sphere still ends up at grid `(32,32,32)` post-fix — same effective geometry, now authored object-centered.

Confirm by reading the file: if any of these 4 tests explicitly construct a `RecipeBakeConfig` with a NON-default `center`, that config's `center` value (not `(32,32,32)`) is what the instruction's new `(0,0,0)`-centered coordinate should resolve against — use the actual config value from each test, not a blanket assumption.

- [x] **Step 3: Migrate `test_recipe_pool_render.cpp`**

Change:
```cpp
auto makeSphere = [](float cx, float cy, float cz, float r) {
    SdfI in{}; in.opCode = uint8_t(SdfOp::Sphere);
    // ... (existing body sets in.data[0..2] = cx,cy,cz and in.data[3] = r)
};
// ...
e.bytecode = { makeSphere(32.0f, 32.0f, 32.0f, r.radius) };
```
to pass `(0.0f, 0.0f, 0.0f)` instead of `(32.0f, 32.0f, 32.0f)` at the call site (the lambda itself does not need to change — only what it's called with):
```cpp
e.bytecode = { makeSphere(0.0f, 0.0f, 0.0f, r.radius) };
```
Read the surrounding context first to confirm this is baked via a path that uses `RecipeBakeConfig`'s default `(32,32,32)` center (e.g. via `RecipeRegistry`/`RecipeBaker.h`, not a raw direct call to `BakeRecipeInstructionsToSdfWorld` with a different center) — if it's a different bake entry point, use the actual center value that path applies, found by reading the code, not assumed.

- [x] **Step 4: Build and run both, confirm identical PASS results to before this task**

Run: `cmake --build <build-dir> --target test_recipe_baker test_recipe_pool_render -- -j8` then run both binaries (or via the RenderGraph tests target if `test_recipe_pool_render` requires the full graph test harness — check how it's normally invoked).
Expected: **PASS**, same test names/counts, same camera/geometry assertions passing as before (these tests' *expected outcomes* describe geometry at `(32,32,32)` in world/grid terms, which is preserved since `RecipeBakeConfig::center` still defaults to `(32,32,32)` — only the *instruction's own authored coordinate* changes from `(32,32,32)` to `(0,0,0)`).

- [x] **Step 5: Commit**

```bash
git add VIXEN/libraries/SVO/tests/test_recipe_baker.cpp VIXEN/libraries/RenderGraph/tests/Nodes/test_recipe_pool_render.cpp
git commit -m "test(svo,rendergraph): migrate test_recipe_baker/test_recipe_pool_render to object-centered authoring

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_011A1Suqs5xYS7YkgURas58o"
git log --oneline -1
git status -sb
```

---

### Task 5: Check `test_recipe_authoring_gate.cpp` and `BodyOctreeSceneNode.cpp`'s analytic-kind path for hidden coordinate assumptions

**Files:**
- Read (no hand-placed coordinates were found by the earlier grep, but this task confirms it, and migrates anything found): `VIXEN/libraries/RenderGraph/tests/Nodes/test_recipe_authoring_gate.cpp`.
- Read + verify only (do NOT change unless a real bug is found — `BakeRecipeToSdfWorld`/`evalSdf` already apply `center` correctly): `VIXEN/libraries/RenderGraph/src/Nodes/BodyOctreeSceneNode.cpp:350-380` (the `k>0` analytic-kind branch, `Vixen::SVO::BakeRecipeToSdfWorld(sk.recipeId, center, rp, kSdfN, kSdfBand)` call — this path is UNCHANGED by Task 2's fix since it doesn't go through `BakeRecipeInstructionsToSdfWorld`).

**Interfaces:**
- Consumes: nothing new.
- Produces: nothing new — pure verification task, migrating `test_recipe_authoring_gate.cpp` only if it turns out to have hand-placed coordinates the earlier grep missed (unlikely — the grep found zero matches — but this task's job is to CONFIRM that by reading the file, not trust the grep blindly).

- [x] **Step 1: Read `test_recipe_authoring_gate.cpp` in full**

Look for any `SdfInstruction`/`sphereInstr`/`Sphere`/`Box`/`Cylinder`-style hand-authored primitive with coordinates that assume the old grid-absolute convention. If the earlier grep's "0 matches" holds up (the file likely loads a pre-flattened recipe blob or constructs instructions via a different helper this plan hasn't seen), no code change is needed — just confirm and move on.

- [x] **Step 2: If hand-placed coordinates ARE found, migrate them**

Apply the same pattern as Task 3/4: change the primitive's own authored coordinate to `(0,0,0)` (or whatever offset from the test's actual local origin makes sense), relying on whatever `center` value that call path already uses to place it correctly. Show the actual before/after in your commit if this step does anything — if Step 1 found nothing, skip this step and Step 3 entirely, note "no hand-placed coordinates found, no change needed" in the final report.

- [x] **Step 3: Confirm `BodyOctreeSceneNode.cpp`'s analytic-kind branch (`k>0`, `BakeRecipeToSdfWorld`) is unaffected**

Read `VIXEN/libraries/RenderGraph/src/Nodes/BodyOctreeSceneNode.cpp:358-374` and confirm the `k>0` branch calls `Vixen::SVO::BakeRecipeToSdfWorld` (not `BakeRecipeInstructionsToSdfWorld`) — this function is untouched by Task 2, so this branch's behavior is unchanged. Confirm the `k==0` branch (`bakeRecipe_`, the recipe-instruction path M3/M4 actually exercise) is the only one affected by Task 2's fix, and that `center` there (the `center` local variable passed to `BakeRecipeInstructionsToSdfWorld` at line ~363) is whatever this node already computes — read enough surrounding context to know what value it is (likely a per-instance world position, not necessarily `(32,32,32)`), so Task 6's editor gate re-verification knows what to expect.

- [x] **Step 4: If Task 2 changes anything for this file, build and test; otherwise just document the read-only finding**

If Step 2 found and fixed anything, run the relevant test target and commit per the same pattern as Task 3/4. If Step 1/3 found nothing to change, no commit is needed for this task — fold the finding into Task 6's report instead.

---

### Task 6: Re-verify `vixen_editor` + M4's live gate against the now-correct centering

**Files:**
- Modify (if needed — see Step 1): `VIXEN/application/editor/source/EditorApplication.cpp` (the `ApplyDocumentToScene` method, and its `renderScale=5.0f` value + camera math, both of which M4 derived specifically to work around the uncentered bug).
- Modify (if needed — see Step 1): `VIXEN/libraries/RenderGraph/tests/Nodes/test_editor_document_render.cpp` (the ablation gate's camera coordinates and expected pixel-count baseline, both derived under the OLD buggy behavior).

**Interfaces:**
- Consumes: Task 2's fixed `BakeRecipeInstructionsToSdfWorld`; `Vixen::SVO::FlattenVoxelDocument` (M3, UNCHANGED — the fix is entirely inside the bake step, downstream of flattening); the existing golden `VIXEN/BuiltAssets/documents/sample_tri_layer.vxd` (UNCHANGED — its geometry was already authored object-centered near `(0,0,0)`, which is WHY M4 had to camera-frame the positive-octant corner as a workaround in the first place).
- Produces: nothing new — this task re-derives and re-verifies M4's live-gate numbers under corrected behavior; it does NOT change the golden document or the flattener.

- [x] **Step 1: Determine what `center` value `EditorApplication::ApplyDocumentToScene` currently passes to the bake call, and whether the fix changes what renders**

Read `VIXEN/application/editor/source/EditorApplication.cpp`'s `ApplyDocumentToScene` (and whatever it calls into — likely `BodyOctreeSceneNode`'s `bakeRecipe_`/`SetRecipePool` path from Task 5 Step 3) to find the actual `center` value used today. Two possible outcomes:
  - **If `center` is `(0,0,0)` today** (i.e. the editor never explicitly centers): before this fix, the golden's object-centered geometry (Box/Sphere/Cylinder near local `(0,0,0)`) landed at raw grid `(0,0,0)` too (since `center` was ignored either way) — so the fix is a no-op for THIS specific center value, and M4's positive-octant-corner workaround and its exact pixel numbers (`hitWithCut=10190 hitNoCut=96288 centreDiffPixels=5564`) should be UNCHANGED. In this case, skip to Step 4 (just re-run the existing gate to confirm the numbers are exact-match, no code change needed) and Step 2/3 are not needed.
  - **If `center` is non-zero** (e.g. grid-center `(32,32,32)` or similar, matching the `RecipeBaker.h` default): the fix will now correctly place the golden's object-centered geometry AT that center — meaning the geometry becomes fully visible and centered in the grid (a strictly BETTER outcome than M4's clipped-corner workaround), but M4's OLD camera coordinates and pixel-count baselines (derived specifically for the old clipped-corner case) will no longer be valid and must be re-derived. Proceed to Step 2.

- [x] **Step 2 (only if center is non-zero): Update `EditorApplication::ApplyDocumentToScene`'s renderScale/camera assumptions if needed**

If the editor's `renderScale=5.0f` (set by M4 specifically to make the clipped-corner geometry visible) is still appropriate now that the FULL golden geometry is visible and centered, no change is needed here — re-derive by inspecting the golden's actual extent (Box halfExtents=1, Sphere r=0.6, Cylinder halfHeight=1.5) against the same `kWorldGridSize=10`/grid-to-world factor math M4 used (documented in `Voxel-Authoring-App-Inc1-Design-2026-07.md` and M4's validated report) to confirm the object still fits sensibly in view. Adjust `renderScale` only if the math shows the object would be clipped or too small/large at the current value — show your derivation, don't guess.

- [x] **Step 3 (only if center is non-zero): Re-derive `test_editor_document_render.cpp`'s ablation gate numbers from scratch**

Re-run the gate test with the corrected bake. If the numbers differ from M4's old baseline (`hitWithCut=10190 hitNoCut=96288 centreDiffPixels=5564`), that is EXPECTED (the geometry is now rendered correctly/fully instead of clipped to a corner) — update the test's hard-coded expected constants to the new fresh values, and re-verify the ablation is STILL decisive (a large, non-marginal difference between cut-enabled and cut-disabled, ideally re-confirm visually via the PNG output the same way M4's validator did — read the produced PNG and describe what you see, don't just trust a number). If camera eye/target coordinates need updating because the object moved, re-derive them the same way M4 did (grid-to-world factor math, shown work, not guessed).

- [x] **Step 4: Build everything touched and run the full verification suite**

Run: full WSL build (`cmake --build <build-dir> --parallel 8`), then run in order: `test_recipe_bake_center` (Task 1, new), `test_recipe_bake`, `test_octree_pool`, `test_recipe_baker`, `test_recipe_pool_render`, `test_recipe_authoring_gate`, `test_body_octree_lifetime`, `test_body_instance_raymarch_render`, `test_voxel_document_flatten`, `test_editor_document_render`. ALL must pass. Paste real fresh output for the full list — this is the milestone's gate, do not skip any of these binaries.

- [x] **Step 5: Commit**

```bash
git add VIXEN/application/editor/source/EditorApplication.cpp VIXEN/libraries/RenderGraph/tests/Nodes/test_editor_document_render.cpp
git commit -m "fix(editor): re-verify vixen_editor + live gate against corrected bake centering

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_011A1Suqs5xYS7YkgURas58o"
git log --oneline -1
git status -sb
```
(If Step 1 determined no code change was needed — center was already `(0,0,0)` — this commit is skipped; note that explicitly in the final report instead.)

---

### Task 7: Update the design doc's Next-steps section

**Files:**
- Modify: `VIXEN/Vixen-Docs/01-Architecture/Voxel-Authoring-App-Inc1-Design-2026-07.md` (the `## 9. Next steps` section, item 2 — "Bake center-offset cleanup").

**Interfaces:**
- Consumes: nothing.
- Produces: nothing — pure docs task.

- [x] **Step 1: Update the doc**

Change item 2 of `## 9. Next steps` from describing the bake center-offset issue as a TODO to recording it as DONE (Inc2a), and add a new item documenting the panel/viewport auto-fit-from-bounds concept as the follow-up (Inc2b, not yet planned):

```markdown
2. **Bake center-offset — FIXED (Inc2a, 2026-07-0X):** `BakeRecipeInstructionsToSdfWorld` now applies
   `center` (`p - center` at eval, matching `evalSdf`'s existing convention on the analytic path).
   Recipe programs are authored object-centered; every existing hand-placed-at-grid-center test/recipe
   was migrated. See [[Voxel-Authoring-App-Inc2a-BakeCenter-Plan-2026-07]].
3. **Panel/viewport concept with auto-fit centering (Inc2b, NOT YET PLANNED):** a sub-window/viewport
   abstraction inside `vixen_editor` that owns its own render context and derives `center` automatically
   from the loaded document's actual geometry bounds (bounds-midpoint auto-fit) whenever a document loads
   or its layers change — removing the need for ANY caller (editor or future consumers) to pass `center`
   by hand. Deliberately deferred past Inc2a to keep that fix small and mechanical; plan properly once
   Slice 3 (drawn layers/brushes, item 1 below) creates a second real multi-view consumer to design
   against, rather than speculatively now.
```
(Renumber the remaining original items 3/4 — Slice 4/Slice 5 — to 4/5.)

- [x] **Step 2: Commit**

```bash
git add VIXEN/Vixen-Docs/01-Architecture/Voxel-Authoring-App-Inc1-Design-2026-07.md
git commit -m "docs(authoring): record Inc2a bake-center fix, document Inc2b panel/auto-fit follow-up

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_011A1Suqs5xYS7YkgURas58o"
git log --oneline -1
git status -sb
```

---

## Self-Review

**1. Spec coverage:** Task 1-2 fix the root cause with a red→green test. Task 3-4 migrate every hand-placed-at-center call site found via exhaustive grep across the whole `VIXEN/libraries` tree (7 files total touching the instruction-VM bake path; 2 have no coordinate literals to migrate). Task 5 explicitly re-verifies the one file the grep found zero matches for, rather than silently trusting a negative grep result, and confirms the untouched analytic-path branch stays correct. Task 6 handles both possible outcomes for the editor/M4 gate (no-op if center was already zero; full re-derivation if not) rather than assuming one. Task 7 closes the loop on the doc that motivated this whole increment.

**2. Placeholder scan:** No TBD/TODO/"add tests for the above" patterns — every task has real code or an explicit read-first-then-decide step with the actual decision criteria spelled out (Task 1 Step 1's full test body, Task 2 Step 1's full fix, Task 5/6's conditional logic states exactly what to check and what each outcome means).

**3. Type consistency:** `BakeRecipeInstructionsToSdfWorld`'s signature (`const Recipe::SdfInstruction*, uint32_t, const glm::vec3&, int, float, int`) is referenced identically across Tasks 1, 2, 3, 4, 6 — never altered, only its internal body (Task 2). `RecipeBakeConfig::center` (`glm::vec3`, default `{32.f,32.f,32.f}`) is referenced consistently in Task 4/5. No function is called with a different name or signature than where it was defined/found.
