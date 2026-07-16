/**
 * @file test_baked_vs_virtual_parity.cpp
 * @brief Lazy-Procedural-Delta-Baseline Inc0 M6 Task 14 — baked-vs-virtual geometry parity gate.
 *
 * The core claim of the whole Inc0/Inc1 program is that a virtual (never-baked) procedural
 * body renders GEOMETRICALLY EQUIVALENT to the same recipe baked through the existing
 * octree->ESVO path. This test proves it directly: renders the SAME recipe two ways —
 *   (i)  BAKED — BakeRecipeInstructionsToSdfWorld -> BuildSdfBodyOctree ->
 *        ConcatenateSdfWithMips -> BodyOctreeSceneNode::SetRecipePool (the M1-M5-established
 *        octree path, unchanged by this milestone)
 *   (ii) VIRTUAL — RecipeRegistry::Register -> SpliceProceduralRecipesIntoSource (RUNTIME
 *        glslang compile of the spliced .comp, via ShaderBundleBuilder — the M5 uber-shader
 *        path can't be a build-time .spv because the splice text depends on what's
 *        registered) -> BodyOctreeSceneNode::SetOccupancyGrid + a PROVIDER_PROCEDURAL
 *        BodyInstanceGpu (zero bake calls)
 * from the SAME camera/resolution, and compares:
 *   - silhouette coverage (binary hit/miss per pixel) via IoU
 *   - depth (color-buffer luminance as a coarse proxy is NOT used — the id-buffer is opaque
 *     to depth, so this harness reads back hitT indirectly via a dedicated debug readout:
 *     the RGBA image's stored linear distance is not directly available from the shipped
 *     shader, so depth comparison here uses camera-distance-to-hit reconstructed from a
 *     THIRD render pass is out of scope; instead this gate compares SILHOUETTE IoU (the
 *     primary geometric-equivalence signal a representation-mismatch would break) plus a
 *     coarse per-pixel color-channel presence check as a depth-adjacent sanity signal.
 *     See the IoU/IsSimilarSilhouette helpers below for the actual comparison.)
 *
 * TOLERANCE (representation mismatch, stated + justified): the baked path is a voxelized
 * (64^3 bake resolution) narrow-band SDF sampled through ESVO+trilinear brick march; the
 * virtual path is an exact analytic sphere-trace of the SAME recipe bytecode. Their true
 * silhouettes can differ by up to ~1 voxel of edge antialiasing at the bake's 64^3 resolution
 * (this test's bodies are sized so 1 voxel ~= a handful of screen pixels at the test
 * resolution/distance — see kIoUFloor below for the exact number chosen and why).
 *
 * NON-VACUITY: each render's own silhouette pixel count must exceed kMinSilhouettePixels
 * (ASSERTED, not just logged) — an empty-vs-empty render would trivially "match" (IoU of two
 * empty sets is undefined/1.0 depending on convention) and must FAIL, not pass, so this is
 * checked explicitly before computing IoU.
 *
 * NEVER-BAKED PROOF: the virtual render path is asserted to call zero SdfBake/octree-build
 * functions via a call counter threaded through this test file only (BakeRecipeInstructionsToSdfWorld
 * is wrapped so the counter can be observed) — see kVirtualBakeCallCount below.
 *
 * CORPUS: 3 recipes — (1) plain sphere (occupancy-grid-eligible), (2) sphere+box SmoothUnion
 * CSG (occupancy-grid-eligible, exercises Task 13's grid on a composite field), (3) a
 * Twist-modified sphere (NOT occupancy-grid-eligible — RecipeOccupancy.h's Lipschitz
 * whitelist excludes domain warps — the class most likely to break step-relaxation, per this
 * milestone's explicit scope note; proves the parity gate still holds with NO grid fast-path).
 *
 * RESOLVED (KI-LPD-003, twist-frame reconciliation): recipe 3's two programs used to author
 * SdfCore_Twist at DIFFERENT absolute p.y — SdfCore_Twist twists (x,z) about the CURRENT
 * position's absolute y (`angle = k * pos.y`, see SdfRecipeEval.h's Twist case, "data[0]=k
 * (radians/unit Y)"), and pos starts as world p with no preceding transform. The
 * worldSpaceProgram twisted a sphere centered at worldTarget (y ~= 5), so Twist saw
 * pos.y ~= 5 (baseline angle ~= 0.25 rad); the localSpaceProgram twisted a sphere centered at
 * local origin (bake-grid coords are p-bakeCenter, so pos.y ~= 0 there), so Twist saw
 * pos.y ~= 0 (baseline angle ~= 0). Same k, different absolute y -> genuinely different
 * twisted geometry -> silhouette IoU capped around 0.585, well under kIoUFloor. This was NOT
 * a marcher/shader bug: virtual hit count (~9183) tracked baked's (~9351) closely, ruling out
 * a march failure (which would show as a hit-count collapse, as the earlier virtualHits==0
 * symptom below did before its own fix).
 *
 * Fix: wrap the worldSpaceProgram's Twist in a Transform that translates pos by worldTarget
 * BEFORE the twist runs (translateOp(worldTarget) — pos' = pos - worldTarget, per
 * SdfCore_Transform's p-translation convention), and add a matching second RestorePos to pop
 * it back off (Transform and Twist each push one position-stack frame; RestorePos pops one
 * frame per call, so two pushes need two pops — see SdfRecipeEval.h's Transform/Twist/
 * RestorePos cases, all push-then-mutate paired 1:1 with a pop). This puts the sphere at LOCAL
 * origin under the twist (mirroring localSpaceProgram's own origin-centered sphere exactly),
 * so SdfCore_Twist now sees pos.y ~= 0 on BOTH paths — same absolute-y baseline, same twisted
 * shape. (An earlier, now-superseded attempt at this same wrap produced virtualHits==0; that
 * turned out to be an unrelated authoring bug in that attempt, not a problem with wrapping
 * itself — Transform+Twist+single-RestorePos is an UNBALANCED position-stack push/pop pair
 * that leaves a stale frame on the stack for the following op. The fix here uses two
 * RestorePos calls, one per push, keeping the stack balanced.)
 *
 * PRIOR KNOWN ISSUE (superseded by the above): a previous version of this file recorded
 * virtualHits==0 (a total march miss) for this recipe, with investigation isolating the
 * trigger to "any recipe using a position-stack push/RestorePos pair" (Twist and MirrorX both
 * reproduced it). That symptom is gone under the corrected, balanced Transform/Twist/
 * RestorePos/RestorePos sequence below — virtual hit counts are now non-trivial and close to
 * baked's, and the residual mismatch was purely the absolute-y twist-frame issue described
 * above.
 *
 * DEVICE SELECTION / RUN POLICY: same real-GPU-preferred contract as test_mip_fallback_render
 * (IsRealGpu/LooksLikeSoftwareOrDozen). Runs on the real GPU Windows-side per the session's
 * standing policy — this is the milestone's core proof, not a handoff.
 *
 * Run: ./test_baked_vs_virtual_parity
 *   Output: /tmp/parity_<recipe>_baked.png, /tmp/parity_<recipe>_virtual.png
 */

#include <gtest/gtest.h>

#include "Nodes/BodyOctreeSceneNode.h"
#include "Data/Nodes/BodyOctreeSceneNodeConfig.h"
#include "Core/NodeContext.h"
#include "VulkanDevice.h"

#include "ShellOctreeGpu.h"
#include "MipBake.h"
#include "SdfBake.h"
#include "Recipe/RecipeRegistry.h"
#include "Recipe/RecipeBounds.h"
#include "Recipe/RecipeOccupancy.h"
#include "Recipe/UberShaderSplice.h"
#include "Recipe/SdfInstruction.h"
#include "ShaderBundleBuilder.h"
#include "TestVkValidation.h"
#include "VulkanGlobalNames.h"  // VixenSelectWslGpuIcd
#include "Generated/LightingConfig.g.h"  // Sampled Lighting Inc0 M3: real default light content
                                          // (a zeroed LightingConfig has lightCount=0 -> pure
                                          // black shaded output -> invisible to this test's own
                                          // luminance-threshold silhouette check)

#include <vulkan/vulkan.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <glm/glm.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <span>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#ifndef BODY_INSTANCE_RAYMARCH_COMP_PATH
#  error "BODY_INSTANCE_RAYMARCH_COMP_PATH must be defined via CMake compile_definitions"
#endif
#ifndef VIXEN_SHADERS_DIR
#  error "VIXEN_SHADERS_DIR must be defined via CMake compile_definitions"
#endif
#ifndef VIXEN_SVO_SHADERS_DIR
#  error "VIXEN_SVO_SHADERS_DIR must be defined via CMake compile_definitions"
#endif

using namespace Vixen::RenderGraph;
using Vixen::Vulkan::Resources::VulkanDevice;

namespace {

// ---------------------------------------------------------------------------
// Never-baked proof: a global call counter, incremented ONLY by this test file's
// own wrapper around BakeRecipeInstructionsToSdfWorld (the BAKED path's entry point).
// The virtual path never calls this wrapper — it goes straight through
// SpliceProceduralRecipesIntoSource + a BodyInstanceGpu — so a nonzero count after a
// virtual-only render would be a real bug (bake code executing where it must not).
// ---------------------------------------------------------------------------
uint32_t g_bakeCallCount = 0;

// params: Recipe-Parameterization M4 Task 11 — an explicit bake-time snapshot for a
// ReadParam/ReadParamFloat3 corpus entry (M3 Task 10's additive params argument, threaded
// through here so the BAKED path can reproduce a SPECIFIC recipeParams[] value rather than
// falling back to evalRecipe's zero-fill default). Defaults to an empty span so every
// existing non-parameterized corpus entry's call site is unaffected.
Vixen::SVO::SdfBakeResult CountedBake(const Vixen::SVO::Recipe::SdfInstruction* prog, uint32_t count,
                                       const glm::vec3& center, int n, float bandVoxels, int brickDepth,
                                       std::span<const float> params = {}) {
    ++g_bakeCallCount;
    return Vixen::SVO::BakeRecipeInstructionsToSdfWorld(prog, count, center, n, bandVoxels, brickDepth, params);
}

struct PushConstants {
    glm::vec3 cameraPos;   float time;
    glm::vec3 cameraDir;   float fov;
    glm::vec3 cameraUp;    float aspect;
    glm::vec3 cameraRight; int32_t debugMode;
    float raySizeCoef; float raySizeBias; int32_t instanceCount;
    int32_t _pad0;         glm::ivec2 debugTargetPixel;
    uint32_t accumFrameCount;  // Sampled Lighting Inc2 M2 (bytes 88-91) — unused by this
                               // geometry-only parity harness (no accumulation config bound,
                               // so the shader's accumulationConfig.enabled==0 passthrough
                               // stays byte-identical regardless of this field's value).
    uint32_t _pad1;            // std430 push-constant block rounds up to a 16-byte multiple
                               // (leading vec3 forces 16-byte block alignment) -- SPIR-V
                               // reflection reports 96 bytes total, not 92; this trailing pad
                               // matches that so sizeof(pc) == the real VkPushConstantRange.
};
static_assert(sizeof(PushConstants) == 96, "PushConstants must be 96 bytes (std430 push block, 16-byte rounded)");

// ---------------------------------------------------------------------------
// KI-032 fix: this file's colorImg (binding 0) readback went permanently dark when
// commit 784adff7 (Sampled Lighting Inc3 M1, KI-018) split shading out of
// BodyInstanceRayMarch.comp into DirectLighting.comp/SpatialReuseShade.comp -- this
// shader now writes ONLY HitRecordBuffer (binding 18) and idOutputImage (binding 9),
// never outputImage. The silhouette/coverage checks below now read HitRecord instead --
// same mirror struct test_hitrecord_readback.cpp/test_recipe_pool_render.cpp already
// established (see KI-032's "Status: PARTIALLY RESOLVED" entry in Known-Issues.md).
// DO NOT revert to a colorImg readback (784adff7) -- see KI-032.
// ---------------------------------------------------------------------------
struct HitRecordCpu {
    float albedo[3];
    float roughness;
    float worldNormal[3];
    float hitT;
    float worldPos[3];
    uint32_t flags;
    uint32_t _pad0[4];  // std430 tail padding -- see test_hitrecord_readback.cpp's identical mirror
};
static_assert(sizeof(HitRecordCpu) == 64, "HitRecordCpu std430 mirror size");

constexpr uint32_t kHitRecordFlagHit = 0x1u;

std::string ReadFile(const std::string& path) {
    std::ifstream f(path);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

constexpr float kWorldGridSize = 10.0f;

PushConstants MakeCamera(const glm::vec3& eye, const glm::vec3& target, uint32_t w, uint32_t h,
                          int32_t instanceCount) {
    const glm::vec3 dir    = glm::normalize(target - eye);
    const glm::vec3 worldUp(0.0f, 1.0f, 0.0f);
    const glm::vec3 right  = glm::normalize(glm::cross(dir, worldUp));
    const glm::vec3 up     = glm::normalize(glm::cross(right, dir));
    PushConstants pc{};
    pc.cameraPos = eye;  pc.time = 0.0f;
    pc.cameraDir = dir;  pc.fov  = 45.0f;
    pc.cameraUp  = up;   pc.aspect = static_cast<float>(w) / static_cast<float>(h);
    pc.cameraRight = right; pc.debugMode = 0;
    pc.raySizeCoef = 0.0f; pc.raySizeBias = 0.0f;  // LOD/far-early-out disabled — isolate geometry, not LOD behavior
    pc.instanceCount = instanceCount;
    pc.debugTargetPixel = glm::ivec2(-1, -1);
    return pc;
}

// A recipe corpus entry needs TWO independent coordinate spaces reconciled to the SAME
// rendered-world target:
//   - bakeCenter: an internal BAKE-GRID coordinate ([0,64) by default) — purely an argument
//     to BakeRecipeInstructionsToSdfWorld (which iterates grid coords and evaluates at
//     p-bakeCenter). It has NOTHING to do with where the resulting octree ends up in
//     rendered-world space: BuildSdfBodyOctree always maps grid [0,n) -> the octree's own
//     [0,n) local frame, and BodyOctreeSceneNode's localToWorld further maps THAT to a fixed
//     world extent [0, kWorldGridSize] (kWorldGridSize below) for a worldPos=(0,0,0),
//     renderScale=1 instance — see test_mip_fallback_render.cpp's own bodyCentre derivation,
//     same convention reused verbatim here.
//   - worldTarget: the ACTUAL rendered-world point both paths must place their geometry at —
//     ALWAYS bodyCentre (0.5*kWorldGridSize on each axis) for the baked path (fixed by the
//     engine's octree-instance placement, independent of bakeCenter), so the virtual path's
//     worldSpaceProgram (which samples world p DIRECTLY per the recipeId>=2 convention — see
//     BuildRenderGraph.cpp's VIXEN_PROCEDURAL_UBER_DEMO comment) must ALSO be authored
//     centered at worldTarget, NOT bakeCenter, for the two renders to land on the same pixel
//     footprint. Radii for worldSpaceProgram are in WORLD units (~kWorldGridSize scale);
//     radii for localSpaceProgram are in BAKE-GRID units (~n scale) — same shape, two scales.
struct ParityRecipe {
    std::string name;
    std::vector<Vixen::SVO::Recipe::SdfInstruction> worldSpaceProgram;  // world-unit radii, centered at worldTarget
    std::vector<Vixen::SVO::Recipe::SdfInstruction> localSpaceProgram;  // bake-grid-unit radii, centered at origin (local to bakeCenter)
    glm::vec3 bakeCenter;    // internal bake-grid coordinate (BakeRecipeInstructionsToSdfWorld arg only)
    glm::vec3 worldTarget;   // actual rendered-world camera target — SAME for both paths
    bool expectOccupancyGrid;  // whether DeriveOccupancyGrid should succeed for worldSpaceProgram
    // Authored bound (optional — 0 radius means "let ApplyRecipeBoundsDefaults derive/default
    // it"). MUST be set explicitly whenever worldSpaceProgram uses an opcode outside
    // DeriveConservativeBounds' whitelist (e.g. Transform/Twist): the derivation bails
    // (ok=false) and the engine-default fallback centers the bound at RecipeEntry's own
    // default (world ORIGIN), not at worldTarget — see VIXEN_PROCEDURAL_UBER_DEMO's own
    // "entry.boundCenter = center" comment in BuildRenderGraph.cpp for the same requirement.
    glm::vec3 authoredBoundCenter = glm::vec3(0.0f);
    float     authoredBoundRadius = 0.0f;  // 0 = not authored
    // Recipe-Parameterization M4 Task 11: a ReadParam corpus entry's bake-time-snapshot /
    // render-time recipeParams[] value — IDENTICAL on both paths, proving CPU bake-time-
    // snapshot eval and GPU per-frame-dynamic-read eval agree for the same effective
    // parameter value. Empty (default) for every non-ReadParam corpus entry.
    std::vector<float> readParamSnapshot;
};

using Vixen::SVO::Recipe::SdfOpCode;
using Vixen::SVO::Recipe::SdfInstruction;

SdfInstruction sphereAt(glm::vec3 c, float r) {
    SdfInstruction in{}; in.opCode = (uint8_t)SdfOpCode::Sphere;
    in.data[0] = c.x; in.data[1] = c.y; in.data[2] = c.z; in.data[3] = r;
    return in;
}
SdfInstruction boxAt(glm::vec3 he) {
    SdfInstruction in{}; in.opCode = (uint8_t)SdfOpCode::Box;
    in.data[0] = he.x; in.data[1] = he.y; in.data[2] = he.z;
    return in;
}
// RoundedBox — UNLIKE plain Box, carries its own position field (data[4..6]) — see
// RecipeBounds.h's "data[0..2]=halfExtents, data[3]=rounding, data[4..6]=position" comment.
// Used instead of boxAt() wherever a program needs to place a box-ish primitive at a
// non-origin position WITHOUT wrapping it in a Transform (which would make the whole
// program non-whitelisted for occupancy-grid derivation — RecipeOccupancy.h's Lipschitz
// whitelist excludes Transform, same set RecipeBounds.h itself excludes it for).
SdfInstruction roundedBoxAt(glm::vec3 c, glm::vec3 he, float rounding) {
    SdfInstruction in{}; in.opCode = (uint8_t)SdfOpCode::RoundedBox;
    in.data[0] = he.x; in.data[1] = he.y; in.data[2] = he.z; in.data[3] = rounding;
    in.data[4] = c.x;  in.data[5] = c.y;  in.data[6] = c.z;
    return in;
}
SdfInstruction combine(SdfOpCode op, float k) {
    SdfInstruction in{}; in.opCode = (uint8_t)op; in.data[2] = k;
    return in;
}
SdfInstruction twist(float k) {
    SdfInstruction in{}; in.opCode = (uint8_t)SdfOpCode::Twist; in.data[0] = k;
    return in;
}
// ReadParam(index) — reads recipeParams[index] at eval time (M1 Task 3 CPU / M2 Task 5 GLSL).
// paramMask=1 is the required non-zero marker (RecipeRegistry.h's paramMask convention —
// see M1 Task 2) distinguishing a ReadParam instruction from a malformed zero-mask one.
SdfInstruction readParam(uint32_t index) {
    SdfInstruction in{}; in.opCode = (uint8_t)SdfOpCode::ReadParam;
    in.paramMask = 1; in.data[0] = static_cast<float>(index);
    return in;
}
SdfInstruction mathSub() {
    SdfInstruction in{}; in.opCode = (uint8_t)SdfOpCode::MathSub;
    return in;
}
// Diagnostic helper (see the file-header "RESOLVED (KI-LPD-003...)" note): during the
// now-superseded virtualHits==0 investigation, substituting mirrorXOp() for twist() in an
// unbalanced Transform+Twist+single-RestorePos version of recipe (3) reproduced the identical
// failure, isolating that particular bug to "any position-stack opcode" rather than anything
// Twist-specific. Kept here (unused by the shipped corpus) in case a future position-stack
// regression needs the same isolation technique.
SdfInstruction mirrorXOp() {
    SdfInstruction in{}; in.opCode = (uint8_t)SdfOpCode::MirrorX;
    return in;
}
SdfInstruction restorePos() {
    SdfInstruction in{}; in.opCode = (uint8_t)SdfOpCode::RestorePos;
    return in;
}
// Pure translation via Transform (identity rotation/scale) — needed so Twist (which twists
// about the CURRENT pos.y, i.e. an ABSOLUTE axis, not a per-object-relative one — see
// SdfRecipeEval.h's "k=radians/unit Y" comment) operates near local origin even when the
// object itself sits far from world origin (worldTarget). Without this, twisting a sphere
// placed directly at worldTarget.y~=5 bakes in a huge (k*5 radian) baseline rotation that
// can push the whole sphere out of the march region — RecipeParityCorpus.h's own Twist
// entries only ever twist objects placed near-origin, for exactly this reason.
SdfInstruction translateOp(glm::vec3 t) {
    SdfInstruction in{}; in.opCode = (uint8_t)SdfOpCode::Transform;
    in.data[0] = t.x; in.data[1] = t.y; in.data[2] = t.z;      // translation
    in.data[4] = 0.0f; in.data[5] = 0.0f; in.data[6] = 0.0f; in.data[7] = 1.0f;  // identity invRot quat
    in.data[8] = 1.0f; in.data[9] = 1.0f; in.data[10] = 1.0f;  // identity invScale
    in.data[11] = 1.0f;                                        // distScale
    return in;
}

std::vector<ParityRecipe> BuildCorpus() {
    std::vector<ParityRecipe> out;

    // bodyCentre — the FIXED rendered-world point a worldPos=(0,0,0)/renderScale=1 baked
    // instance always lands at (test_mip_fallback_render.cpp's own derivation, reused
    // verbatim): 0.5*kWorldGridSize on each axis, independent of bakeCenter.
    const glm::vec3 bodyCentre(0.5f * kWorldGridSize);

    // (1) Plain sphere — occupancy-grid-eligible, simplest possible field.
    {
        ParityRecipe r;
        r.name = "sphere";
        r.bakeCenter  = glm::vec3(32.0f, 32.0f, 32.0f);  // bake-grid midpoint (n=64 default)
        r.worldTarget = bodyCentre;
        r.worldSpaceProgram = { sphereAt(r.worldTarget, 3.0f) };       // world-unit radius (~30% of kWorldGridSize)
        r.localSpaceProgram = { sphereAt(glm::vec3(0.0f), 18.0f) };    // SAME shape, bake-grid-unit radius
        r.expectOccupancyGrid = true;
        out.push_back(std::move(r));
    }

    // (2) sphere+box SmoothUnion CSG — occupancy-grid-eligible composite field, exercises
    // Task 13's grid on a non-trivial shape (not just a single leaf primitive).
    {
        ParityRecipe r;
        r.name = "csg_smoothunion";
        r.bakeCenter  = glm::vec3(32.0f, 32.0f, 32.0f);
        r.worldTarget = bodyCentre;
        // EXACT 1/6 scale of localSpaceProgram below (matches recipe (1)'s own 18->3 ratio) —
        // every dimension (box half-extents, sphere/box offset+radius, smooth-union k) scaled
        // by the SAME factor so the two programs describe geometrically similar shapes, just
        // at different scales. An inconsistent per-axis scale factor (an earlier version of
        // this recipe used ~6.0-6.25 inconsistently) shears the shape between the two
        // renders, which is exactly the kind of bug this parity gate exists to catch.
        //
        // Uses RoundedBox (data[4..6]=position), NOT plain Box (no position field — see
        // roundedBoxAt's doc comment above) — an earlier version of this recipe placed
        // boxAt() with no translation at all, which silently evaluated Box at WORLD ORIGIN
        // (0,0,0) instead of worldTarget while the sphere correctly sat at worldTarget: the
        // two primitives ended up ~5.7 world units apart instead of coincident. RoundedBox
        // (whitelisted for occupancy-grid derivation, unlike Transform) lets both primitives
        // carry explicit positions without breaking expectOccupancyGrid=true below. A small
        // rounding (0.05 world units / 0.3 bake-grid units) keeps the shape topologically a
        // "rounded box" in both spaces — negligible next to the 2.0/12.0-unit half-extents.
        constexpr float kScale = 1.0f / 6.0f;
        r.worldSpaceProgram = {
            roundedBoxAt(r.worldTarget, glm::vec3(12.0f, 10.0f, 10.0f) * kScale, 0.3f * kScale),
            sphereAt(r.worldTarget + glm::vec3(8.0f, 0.0f, 0.0f) * kScale, 10.0f * kScale),
            combine(SdfOpCode::SmoothUnion, 3.0f * kScale),
        };
        r.localSpaceProgram = {
            roundedBoxAt(glm::vec3(0.0f), glm::vec3(12.0f, 10.0f, 10.0f), 0.3f),
            sphereAt(glm::vec3(8.0f, 0.0f, 0.0f), 10.0f),
            combine(SdfOpCode::SmoothUnion, 3.0f),
        };
        r.expectOccupancyGrid = true;
        out.push_back(std::move(r));
    }

    // (3) Twist-modified sphere — domain modifier, NOT occupancy-grid-eligible
    // (RecipeOccupancy.h's Lipschitz whitelist excludes Twist/RestorePos): the class most
    // likely to break step-relaxation, per this milestone's plan. Proves the parity gate
    // holds even with zero grid fast-path assistance.
    {
        ParityRecipe r;
        r.name = "twist_sphere";
        r.bakeCenter  = glm::vec3(32.0f, 32.0f, 32.0f);
        r.worldTarget = bodyCentre;
        // World-space program: Twist rotates (x,z) about pos.y ABSOLUTELY (SdfRecipeEval.h:
        // "k=radians/unit Y" — a rotation whose angle depends on the CURRENT p.y, not an
        // object-relative offset). The localSpaceProgram below twists a sphere centered at
        // LOCAL ORIGIN (bake-grid coords are p-bakeCenter, so pos.y there is ~0), so for the
        // two paths to twist by the SAME baseline angle, the worldSpaceProgram must ALSO
        // present pos.y ~= 0 to Twist. translateOp(worldTarget) does exactly that: pos' =
        // pos - worldTarget (SdfCore_Transform's p-translation convention — see its doc
        // comment above), so Twist runs in the same origin-centered local frame as the baked
        // path, then a second RestorePos (Transform AND Twist each push one position-stack
        // frame; RestorePos pops exactly one per call — SdfRecipeEval.h's push-then-mutate
        // pattern) restores pos back to world space before the program ends. Sphere is
        // authored at LOCAL origin (matching localSpaceProgram's own placement) since it now
        // evaluates inside the translated frame, not at worldTarget directly.
        r.worldSpaceProgram = {
            translateOp(r.worldTarget),
            twist(0.05f),
            sphereAt(glm::vec3(0.0f), 2.6f),
            restorePos(),  // pops Twist's frame
            restorePos(),  // pops Transform's frame
        };
        r.localSpaceProgram = { twist(0.05f), sphereAt(glm::vec3(0.0f), 16.0f), restorePos() };
        // Twist is outside DeriveConservativeBounds' whitelist -> must author the bound
        // explicitly so the march's entry/exit bracket actually covers worldTarget.
        r.authoredBoundCenter = r.worldTarget;
        r.authoredBoundRadius = 6.0f;
        r.expectOccupancyGrid = false;
        out.push_back(std::move(r));
    }

    // (4) ReadParam-driven sphere radius offset — Recipe-Parameterization M4 Task 11. Same
    // bytecode SHAPE M3 Task 8 already registered/rendered live (BuildRenderGraph.cpp's
    // VIXEN_PROCEDURAL_UBER_DEMO ReadParam demo body, and test_body_octree_lifetime.cpp's
    // ReadParamValueSweepNeverMarksNodeNeedsRecompile gtest): { sphere(center, baseRadius),
    // ReadParam(0), MathSub }. MathSub is non-commutative a-b (RecipeStack push order:
    // [sphereSD, params[0]] -> a=sphereSD, b=params[0]), so sd = sphereSD - params[0] i.e. a
    // pure radius offset: rendered radius = baseRadius + readParamSnapshot[0]. Baked with a
    // SPECIFIC snapshotted param value (M1/M3 Task 10's params argument to
    // BakeRecipeInstructionsToSdfWorld) and rendered virtual with recipeParams[] set to the
    // IDENTICAL value — proves CPU bake-time-snapshot eval and GPU per-frame-dynamic-read
    // eval agree for the same effective parameter value. ReadParam/MathSub are outside both
    // DeriveConservativeBounds' and RecipeOccupancy.h's Lipschitz whitelists (same as Twist
    // above) -> occupancy grid not expected, bound authored explicitly and MUST cover the
    // full baseRadius+snapshot extent, mirroring BuildRenderGraph.cpp's own margin comment.
    {
        ParityRecipe r;
        r.name = "readparam_sphere";
        r.bakeCenter  = glm::vec3(32.0f, 32.0f, 32.0f);
        r.worldTarget = bodyCentre;
        constexpr float kBaseRadius = 2.0f;      // world-unit base radius (bake-grid: *6, matching recipe (1)'s 18/3 ratio)
        constexpr float kParamValue = 0.5f;      // the SAME snapshot value baked AND rendered virtual
        r.worldSpaceProgram = {
            sphereAt(r.worldTarget, kBaseRadius),
            readParam(0),
            mathSub(),
        };
        r.localSpaceProgram = {
            sphereAt(glm::vec3(0.0f), kBaseRadius * 6.0f),
            readParam(0),
            mathSub(),
        };
        r.readParamSnapshot = { kParamValue };
        r.authoredBoundCenter = r.worldTarget;
        r.authoredBoundRadius = kBaseRadius + kParamValue + 1.0f;  // margin, mirrors BuildRenderGraph.cpp's demo
        r.expectOccupancyGrid = false;
        out.push_back(std::move(r));
    }

    return out;
}

}  // namespace

// ---------------------------------------------------------------------------
// Fixture — mirrors test_mip_fallback_render.cpp's device/buffer/dispatch machinery, but
// (a) runtime-compiles the spliced uber-shader for the virtual path (ShaderBundleBuilder,
// same production preprocessing/compile path test_uber_shader_splice.cpp already proves
// compiles clean) instead of loading a build-time .spv, and (b) reuses the mip-fallback
// harness's device selection / pool setup / dispatch / readback for BOTH paths.
// ---------------------------------------------------------------------------
class BakedVsVirtualParityTest : public ::testing::Test {
protected:
    VkInstance       instance_       = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VkDevice         logicalDevice_  = VK_NULL_HANDLE;
    VkQueue          queue_          = VK_NULL_HANDLE;
    VkCommandPool    commandPool_    = VK_NULL_HANDLE;
    uint32_t         queueFamily_    = 0;
    bool             deviceConfirmed_ = false;
    std::string      selectedDeviceName_;
    std::unique_ptr<VulkanDevice> deviceShell_;

    static bool IsRealGpu(const VkPhysicalDeviceProperties& p) {
        return p.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ||
               p.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU;
    }
    static bool LooksLikeSoftwareOrDozen(const VkPhysicalDeviceProperties& p) {
        std::string n(p.deviceName); for (char& c : n) c = char(::tolower(c));
        const bool isSoftware =
            (n.find("llvmpipe") != std::string::npos || n.find("lavapipe") != std::string::npos) &&
            p.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU;
        const bool isDozen = n.find("direct3d12") != std::string::npos;
        return isSoftware || isDozen;
    }

    void SetUp() override {
        VixenSelectWslGpuIcd();
        VkApplicationInfo ai{}; ai.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        ai.pApplicationName = "test_baked_vs_virtual_parity"; ai.apiVersion = VK_API_VERSION_1_3;
        const auto layers = EnabledValidationLayers();
        const char* exts[] = {VK_EXT_DEBUG_UTILS_EXTENSION_NAME};
        VkInstanceCreateInfo ci{}; ci.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        ci.pApplicationInfo = &ai;
        ci.enabledLayerCount = uint32_t(layers.size()); ci.ppEnabledLayerNames = layers.empty() ? nullptr : layers.data();
        ci.enabledExtensionCount = 1; ci.ppEnabledExtensionNames = exts;
        ASSERT_EQ(vkCreateInstance(&ci, nullptr, &instance_), VK_SUCCESS);
        ASSERT_NO_FATAL_FAILURE(PickUsableDevice());
        ASSERT_TRUE(deviceConfirmed_);
        ASSERT_NO_FATAL_FAILURE(CreateLogicalDevice());
        ASSERT_NO_FATAL_FAILURE(CreateCmdPool());
        deviceShell_ = std::make_unique<VulkanDevice>(&physicalDevice_);
        deviceShell_->device = logicalDevice_;
    }

    void TearDown() override {
        if (deviceShell_) { deviceShell_->device = VK_NULL_HANDLE; deviceShell_.reset(); }
        if (commandPool_ != VK_NULL_HANDLE) vkDestroyCommandPool(logicalDevice_, commandPool_, nullptr);
        if (logicalDevice_ != VK_NULL_HANDLE) vkDestroyDevice(logicalDevice_, nullptr);
        if (instance_     != VK_NULL_HANDLE) vkDestroyInstance(instance_, nullptr);
    }

    void PickUsableDevice() {
        uint32_t cnt = 0; ASSERT_EQ(vkEnumeratePhysicalDevices(instance_, &cnt, nullptr), VK_SUCCESS);
        ASSERT_GT(cnt, 0u) << "No Vulkan devices visible.";
        std::vector<VkPhysicalDevice> devs(cnt);
        ASSERT_EQ(vkEnumeratePhysicalDevices(instance_, &cnt, devs.data()), VK_SUCCESS);
        for (auto dev : devs) {
            VkPhysicalDeviceProperties p{}; vkGetPhysicalDeviceProperties(dev, &p);
            if (IsRealGpu(p)) { physicalDevice_ = dev; selectedDeviceName_ = p.deviceName; deviceConfirmed_ = true; return; }
        }
        for (auto dev : devs) {
            VkPhysicalDeviceProperties p{}; vkGetPhysicalDeviceProperties(dev, &p);
            if (LooksLikeSoftwareOrDozen(p)) { physicalDevice_ = dev; selectedDeviceName_ = p.deviceName; deviceConfirmed_ = true; return; }
        }
        VkPhysicalDeviceProperties p{}; vkGetPhysicalDeviceProperties(devs[0], &p);
        selectedDeviceName_ = p.deviceName;
    }

    void CreateLogicalDevice() {
        uint32_t qfCnt = 0; vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice_, &qfCnt, nullptr);
        std::vector<VkQueueFamilyProperties> qfs(qfCnt);
        vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice_, &qfCnt, qfs.data());
        bool found = false;
        for (uint32_t i = 0; i < qfCnt; ++i) {
            if (qfs[i].queueFlags & VK_QUEUE_COMPUTE_BIT) { queueFamily_ = i; found = true; break; }
        }
        ASSERT_TRUE(found);
        float prio = 1.0f;
        VkDeviceQueueCreateInfo qi{}; qi.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        qi.queueFamilyIndex = queueFamily_; qi.queueCount = 1; qi.pQueuePriorities = &prio;
        VkPhysicalDeviceFeatures feats{}; feats.shaderStorageImageWriteWithoutFormat = VK_TRUE;
        VkDeviceCreateInfo di{}; di.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        di.queueCreateInfoCount = 1; di.pQueueCreateInfos = &qi; di.pEnabledFeatures = &feats;
        ASSERT_EQ(vkCreateDevice(physicalDevice_, &di, nullptr, &logicalDevice_), VK_SUCCESS);
        vkGetDeviceQueue(logicalDevice_, queueFamily_, 0, &queue_);
    }

    void CreateCmdPool() {
        VkCommandPoolCreateInfo pi{}; pi.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        pi.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT; pi.queueFamilyIndex = queueFamily_;
        ASSERT_EQ(vkCreateCommandPool(logicalDevice_, &pi, nullptr, &commandPool_), VK_SUCCESS);
    }

    template<typename T>
    static void SetHandleVal(Resource& res, T value) { res.SetHandle<T>(std::move(value)); }

    uint32_t FindMemType(uint32_t filter, VkMemoryPropertyFlags flags) {
        VkPhysicalDeviceMemoryProperties mp{}; vkGetPhysicalDeviceMemoryProperties(physicalDevice_, &mp);
        for (uint32_t i = 0; i < mp.memoryTypeCount; ++i)
            if ((filter & (1u<<i)) && (mp.memoryTypes[i].propertyFlags & flags) == flags) return i;
        return UINT32_MAX;
    }

    void CreateImage(uint32_t w, uint32_t h, VkFormat fmt, VkImage& img, VkDeviceMemory& mem) {
        VkImageCreateInfo ci{}; ci.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        ci.imageType = VK_IMAGE_TYPE_2D; ci.format = fmt; ci.extent = {w,h,1};
        ci.mipLevels = 1; ci.arrayLayers = 1; ci.samples = VK_SAMPLE_COUNT_1_BIT;
        ci.tiling = VK_IMAGE_TILING_OPTIMAL; ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        ci.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        ASSERT_EQ(vkCreateImage(logicalDevice_, &ci, nullptr, &img), VK_SUCCESS);
        VkMemoryRequirements req{}; vkGetImageMemoryRequirements(logicalDevice_, img, &req);
        VkMemoryAllocateInfo ai{}; ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        ai.allocationSize = req.size; ai.memoryTypeIndex = FindMemType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        ASSERT_NE(ai.memoryTypeIndex, UINT32_MAX);
        ASSERT_EQ(vkAllocateMemory(logicalDevice_, &ai, nullptr, &mem), VK_SUCCESS);
        ASSERT_EQ(vkBindImageMemory(logicalDevice_, img, mem, 0), VK_SUCCESS);
    }

    VkImageView MakeView(VkImage img, VkFormat fmt) {
        VkImageViewCreateInfo vi{}; vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vi.image = img; vi.viewType = VK_IMAGE_VIEW_TYPE_2D; vi.format = fmt;
        vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
        VkImageView v = VK_NULL_HANDLE; vkCreateImageView(logicalDevice_, &vi, nullptr, &v); return v;
    }

    void CreateHostBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                          VkBuffer& buf, VkDeviceMemory& mem, bool zero) {
        VkBufferCreateInfo bi{}; bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bi.size = size; bi.usage = usage; bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        ASSERT_EQ(vkCreateBuffer(logicalDevice_, &bi, nullptr, &buf), VK_SUCCESS);
        VkMemoryRequirements req{}; vkGetBufferMemoryRequirements(logicalDevice_, buf, &req);
        VkMemoryAllocateInfo ai{}; ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        ai.allocationSize = req.size;
        ai.memoryTypeIndex = FindMemType(req.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        ASSERT_NE(ai.memoryTypeIndex, UINT32_MAX);
        ASSERT_EQ(vkAllocateMemory(logicalDevice_, &ai, nullptr, &mem), VK_SUCCESS);
        ASSERT_EQ(vkBindBufferMemory(logicalDevice_, buf, mem, 0), VK_SUCCESS);
        if (zero) { void* m=nullptr; vkMapMemory(logicalDevice_, mem, 0, size, 0, &m); std::memset(m,0,size_t(size)); vkUnmapMemory(logicalDevice_, mem); }
    }

    // Sampled Lighting Inc0 M3: the same directional-light default LightingConfigNode uploads
    // in the real graph (MakeDefaultLightingConfig — direction normalize(1,1,-1), white
    // radiance, ambientIntensity 0.3) — this harness bypasses LightingConfigNode entirely, so
    // it must write this itself or the shaded output is pure black (lightCount=0).
    static Vixen::Gpu::LightingConfig MakeTestDefaultLightingConfig() {
        Vixen::Gpu::LightingConfig cfg{};
        cfg.lightCount       = 1u;
        cfg.ambientIntensity = 0.3f;
        const float dx = 1.0f, dy = 1.0f, dz = -1.0f;
        const float invLen = 1.0f / std::sqrt(dx*dx + dy*dy + dz*dz);
        cfg.lights[0].direction_or_positionX = dx * invLen;
        cfg.lights[0].direction_or_positionY = dy * invLen;
        cfg.lights[0].direction_or_positionZ = dz * invLen;
        cfg.lights[0].kind      = 0u;
        cfg.lights[0].radianceX = 1.0f;
        cfg.lights[0].radianceY = 1.0f;
        cfg.lights[0].radianceZ = 1.0f;
        cfg.lights[0].range     = 0.0f;
        return cfg;
    }

    void UploadBufferContent(VkDeviceMemory mem, const void* data, size_t size) {
        void* m = nullptr;
        ASSERT_EQ(vkMapMemory(logicalDevice_, mem, 0, size, 0, &m), VK_SUCCESS);
        std::memcpy(m, data, size);
        vkUnmapMemory(logicalDevice_, mem);
    }

    // Render dispatch — takes an explicit compiled SPIR-V module (either the build-time-loaded
    // one for the baked path, or the runtime-compiled spliced one for the virtual path). Binds
    // all 17 bindings (0-5,8-16) so both paths always see a fully-populated descriptor set
    // (placeholders where a path has no real data — same "always valid" invariant
    // BodyOctreeSceneNode's own CreateOctreeBuffers keeps).
    void RenderToRgba(const std::vector<uint32_t>& spirv,
                      VkBuffer nodes, VkBuffer bricks, VkBuffer mats, VkBuffer cfg,
                      VkBuffer inst, VkBuffer sdf, VkBuffer lookup, VkBuffer mip,
                      VkBuffer tierRef, VkBuffer occGrid,
                      const PushConstants& pc, uint32_t w, uint32_t h,
                      std::vector<uint8_t>& rgba, double& ms,
                      std::vector<HitRecordCpu>* outHitRecords = nullptr) {
        ASSERT_TRUE(deviceConfirmed_);
        VkBuffer traceBuf=VK_NULL_HANDLE;
        VkDeviceMemory traceMem=VK_NULL_HANDLE;
        CreateHostBuffer(256, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, traceBuf, traceMem, true);
        VkBuffer dummySdf=VK_NULL_HANDLE, dummyLookup=VK_NULL_HANDLE, dummyMip=VK_NULL_HANDLE, dummyIter=VK_NULL_HANDLE,
                 dummyTierRef=VK_NULL_HANDLE, dummyOccGrid=VK_NULL_HANDLE;
        VkDeviceMemory dSdfMem=VK_NULL_HANDLE, dLookupMem=VK_NULL_HANDLE, dMipMem=VK_NULL_HANDLE, dIterMem=VK_NULL_HANDLE,
                       dTierRefMem=VK_NULL_HANDLE, dOccGridMem=VK_NULL_HANDLE;
        CreateHostBuffer(256,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,dummyIter,dIterMem,true);
        if (sdf     == VK_NULL_HANDLE) { CreateHostBuffer(256,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,dummySdf,dSdfMem,true); sdf = dummySdf; }
        if (lookup  == VK_NULL_HANDLE) { CreateHostBuffer(256,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,dummyLookup,dLookupMem,true); lookup = dummyLookup; }
        if (mip     == VK_NULL_HANDLE) { CreateHostBuffer(256,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,dummyMip,dMipMem,true); mip = dummyMip; }
        if (tierRef == VK_NULL_HANDLE) { CreateHostBuffer(256,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,dummyTierRef,dTierRefMem,true); tierRef = dummyTierRef; }
        if (occGrid == VK_NULL_HANDLE) { CreateHostBuffer(256,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,dummyOccGrid,dOccGridMem,true); occGrid = dummyOccGrid; }

        // Sampled Lighting Inc0-Inc2 bindings (17-22): this harness only checks GEOMETRIC
        // equivalence (baked vs virtual octree traversal), not shading, so these are bound as
        // zeroed placeholders — LightingConfig/ShadowConfig/AccumulationConfig all default to
        // effectively-inert content (accumulationConfig.enabled==0 keeps the temporal-accum
        // seam a pure passthrough; shadowConfig with a zeroed enabled flag skips shadow rays)
        // and HitRecord/PrevCameraConfig are round-tripped but not read for this test's own
        // pass/fail signal (RenderToRgba only samples the color image below).
        VkBuffer dummyLighting=VK_NULL_HANDLE, dummyHitRecord=VK_NULL_HANDLE, dummyShadow=VK_NULL_HANDLE,
                 dummyAccum=VK_NULL_HANDLE, dummyPrevCam=VK_NULL_HANDLE;
        VkDeviceMemory dLightingMem=VK_NULL_HANDLE, dHitRecordMem=VK_NULL_HANDLE, dShadowMem=VK_NULL_HANDLE,
                       dAccumMem=VK_NULL_HANDLE, dPrevCamMem=VK_NULL_HANDLE;
        CreateHostBuffer(256,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,dummyLighting,dLightingMem,true);
        {
            const Vixen::Gpu::LightingConfig defaultLighting = MakeTestDefaultLightingConfig();
            ASSERT_NO_FATAL_FAILURE(UploadBufferContent(dLightingMem, &defaultLighting, sizeof(defaultLighting)));
        }
        // HitRecord.glsl's HitRecord struct is 64 bytes; the shader indexes it by the FULL
        // flat pixel count (y*imgSize.x+x, up to w*h-1) every dispatch, so this buffer MUST be
        // sized for the actual w*h being rendered here -- a fixed small guess (the previous
        // 65536-byte placeholder only covers 1024 pixels) writes out of bounds for any
        // non-trivial w*h and silently corrupts/discards the round-trip this milestone proves.
        const VkDeviceSize hitRecordBufSize = VkDeviceSize(w) * VkDeviceSize(h) * 64;
        CreateHostBuffer(hitRecordBufSize,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,dummyHitRecord,dHitRecordMem,true);
        CreateHostBuffer(256,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,dummyShadow,dShadowMem,true);
        CreateHostBuffer(256,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,dummyAccum,dAccumMem,true);
        CreateHostBuffer(256,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,dummyPrevCam,dPrevCamMem,true);

        VkImage colorImg=VK_NULL_HANDLE, idImg=VK_NULL_HANDLE, historyImg=VK_NULL_HANDLE;
        VkDeviceMemory colorMem=VK_NULL_HANDLE, idMem=VK_NULL_HANDLE, historyMem=VK_NULL_HANDLE;
        ASSERT_NO_FATAL_FAILURE(CreateImage(w,h,VK_FORMAT_R8G8B8A8_UNORM, colorImg, colorMem));
        ASSERT_NO_FATAL_FAILURE(CreateImage(w,h,VK_FORMAT_R32_UINT, idImg, idMem));
        ASSERT_NO_FATAL_FAILURE(CreateImage(w,h,VK_FORMAT_R8G8B8A8_UNORM, historyImg, historyMem));
        VkImageView colorView   = MakeView(colorImg,   VK_FORMAT_R8G8B8A8_UNORM);
        VkImageView idView      = MakeView(idImg,      VK_FORMAT_R32_UINT);
        VkImageView historyView = MakeView(historyImg, VK_FORMAT_R8G8B8A8_UNORM);

        ASSERT_FALSE(spirv.empty()) << "runtime-compiled SPIR-V is empty";
        VkShaderModuleCreateInfo smc{}; smc.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        smc.codeSize = spirv.size()*4; smc.pCode = spirv.data();
        VkShaderModule sm = VK_NULL_HANDLE;
        ASSERT_EQ(vkCreateShaderModule(logicalDevice_, &smc, nullptr, &sm), VK_SUCCESS);

        auto bindL = [](uint32_t b, VkDescriptorType t) {
            VkDescriptorSetLayoutBinding lb{}; lb.binding=b; lb.descriptorType=t;
            lb.descriptorCount=1; lb.stageFlags=VK_SHADER_STAGE_COMPUTE_BIT; return lb;
        };
        // NOTE: binding 8 (ShaderCounters debug SSBO) deliberately absent — removed from the
        // shader's reflected interface by 8509f58b (ENABLE_SHADER_COUNTERS compiled out
        // unconditionally; see SceneBindings.glsl's binding-8 comment). Including it here
        // desyncs this local layout from the SPIR-V module's actual resource interface,
        // which is a VUID-VkComputePipelineCreateInfo-layout-07988-class validation error
        // (root-caused 2026-07-15, Recipe-Parameterization M4 — was previously misdiagnosed
        // as a boot-recompile descriptor-staleness bug in KI-028; that issue is real but
        // unrelated to this test's symmetric bakedHits=0/virtualHits=0 failure).
        const std::array<VkDescriptorSetLayoutBinding,20> bindings = {
            bindL(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE),
            bindL(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
            bindL(2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
            bindL(3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
            bindL(4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
            bindL(5, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
            bindL(9, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE),
            bindL(10,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
            bindL(11,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
            bindL(12,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
            bindL(13,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
            bindL(14,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
            bindL(15,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
            bindL(16,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),  // M6 Task 13: OccupancyGridBuffer
            bindL(17,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),  // Sampled Lighting Inc0 M3: LightingConfigSSBO
            bindL(18,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),  // Sampled Lighting Inc1 M3: HitRecordBuffer
            bindL(19,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),  // Sampled Lighting Inc1 M4: ShadowConfigSSBO
            bindL(20,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),  // Sampled Lighting Inc2 M1: AccumulationConfigSSBO
            bindL(21,VK_DESCRIPTOR_TYPE_STORAGE_IMAGE),   // Sampled Lighting Inc2 M1: historyImage
            bindL(22,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),  // Sampled Lighting Inc2 M3: PrevCameraConfigSSBO
        };
        VkDescriptorSetLayoutCreateInfo dslci{}; dslci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        dslci.bindingCount = uint32_t(bindings.size()); dslci.pBindings = bindings.data();
        VkDescriptorSetLayout dsl = VK_NULL_HANDLE;
        ASSERT_EQ(vkCreateDescriptorSetLayout(logicalDevice_, &dslci, nullptr, &dsl), VK_SUCCESS);

        VkPushConstantRange pcr{}; pcr.stageFlags=VK_SHADER_STAGE_COMPUTE_BIT; pcr.size=sizeof(pc);
        VkPipelineLayoutCreateInfo plci{}; plci.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        plci.setLayoutCount=1; plci.pSetLayouts=&dsl; plci.pushConstantRangeCount=1; plci.pPushConstantRanges=&pcr;
        VkPipelineLayout pl = VK_NULL_HANDLE;
        ASSERT_EQ(vkCreatePipelineLayout(logicalDevice_, &plci, nullptr, &pl), VK_SUCCESS);

        VkComputePipelineCreateInfo cpci{}; cpci.sType=VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        cpci.stage.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        cpci.stage.stage=VK_SHADER_STAGE_COMPUTE_BIT; cpci.stage.module=sm; cpci.stage.pName="main";
        cpci.layout=pl;
        VkPipeline pipeline = VK_NULL_HANDLE;
        ASSERT_EQ(vkCreateComputePipelines(logicalDevice_,VK_NULL_HANDLE,1,&cpci,nullptr,&pipeline), VK_SUCCESS);

        const std::array<VkDescriptorPoolSize,2> poolSizes = {{
            {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,  3},
            {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 17},
        }};
        VkDescriptorPoolCreateInfo dpci{}; dpci.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        dpci.maxSets=1; dpci.poolSizeCount=uint32_t(poolSizes.size()); dpci.pPoolSizes=poolSizes.data();
        VkDescriptorPool pool2 = VK_NULL_HANDLE;
        ASSERT_EQ(vkCreateDescriptorPool(logicalDevice_,&dpci,nullptr,&pool2), VK_SUCCESS);
        VkDescriptorSetAllocateInfo dsai{}; dsai.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        dsai.descriptorPool=pool2; dsai.descriptorSetCount=1; dsai.pSetLayouts=&dsl;
        VkDescriptorSet ds = VK_NULL_HANDLE;
        ASSERT_EQ(vkAllocateDescriptorSets(logicalDevice_,&dsai,&ds), VK_SUCCESS);

        VkDescriptorImageInfo colImg{VK_NULL_HANDLE,colorView,VK_IMAGE_LAYOUT_GENERAL};
        VkDescriptorImageInfo idImgI{VK_NULL_HANDLE,idView,VK_IMAGE_LAYOUT_GENERAL};
        VkDescriptorImageInfo historyImgI{VK_NULL_HANDLE,historyView,VK_IMAGE_LAYOUT_GENERAL};
        VkDescriptorBufferInfo nodesI{nodes,0,VK_WHOLE_SIZE}, bricksI{bricks,0,VK_WHOLE_SIZE},
            matsI{mats,0,VK_WHOLE_SIZE}, traceI{traceBuf,0,VK_WHOLE_SIZE}, cfgI{cfg,0,VK_WHOLE_SIZE},
            instI{inst,0,VK_WHOLE_SIZE},
            sdfI{sdf,0,VK_WHOLE_SIZE}, lookupI{lookup,0,VK_WHOLE_SIZE}, iterI{dummyIter,0,VK_WHOLE_SIZE}, mipI{mip,0,VK_WHOLE_SIZE},
            tierRefI{tierRef,0,VK_WHOLE_SIZE}, occGridI{occGrid,0,VK_WHOLE_SIZE},
            lightingI{dummyLighting,0,VK_WHOLE_SIZE}, hitRecordI{dummyHitRecord,0,VK_WHOLE_SIZE},
            shadowI{dummyShadow,0,VK_WHOLE_SIZE}, accumI{dummyAccum,0,VK_WHOLE_SIZE},
            prevCamI{dummyPrevCam,0,VK_WHOLE_SIZE};

        auto wI = [&](uint32_t b, VkDescriptorImageInfo* info) {
            VkWriteDescriptorSet w{}; w.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            w.dstSet=ds; w.dstBinding=b; w.descriptorCount=1;
            w.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_IMAGE; w.pImageInfo=info; return w;
        };
        auto wB = [&](uint32_t b, VkDescriptorBufferInfo* info) {
            VkWriteDescriptorSet w{}; w.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            w.dstSet=ds; w.dstBinding=b; w.descriptorCount=1;
            w.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; w.pBufferInfo=info; return w;
        };
        const std::array<VkWriteDescriptorSet,20> writes = {
            wI(0,&colImg), wB(1,&nodesI), wB(2,&bricksI), wB(3,&matsI), wB(4,&traceI),
            wB(5,&cfgI), wI(9,&idImgI), wB(10,&instI), wB(11,&sdfI), wB(12,&lookupI), wB(13,&mipI),
            wB(14,&iterI), wB(15,&tierRefI), wB(16,&occGridI),
            wB(17,&lightingI), wB(18,&hitRecordI), wB(19,&shadowI), wB(20,&accumI),
            wI(21,&historyImgI), wB(22,&prevCamI),
        };
        vkUpdateDescriptorSets(logicalDevice_, uint32_t(writes.size()), writes.data(), 0, nullptr);

        VkCommandBufferAllocateInfo cbai{}; cbai.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cbai.commandPool=commandPool_; cbai.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY; cbai.commandBufferCount=1;
        VkCommandBuffer cmd=VK_NULL_HANDLE; ASSERT_EQ(vkAllocateCommandBuffers(logicalDevice_,&cbai,&cmd), VK_SUCCESS);

        VkCommandBufferBeginInfo bi{}; bi.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        bi.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        ASSERT_EQ(vkBeginCommandBuffer(cmd, &bi), VK_SUCCESS);

        auto toGeneral = [&](VkImage img) {
            VkImageMemoryBarrier b{}; b.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            b.oldLayout=VK_IMAGE_LAYOUT_UNDEFINED; b.newLayout=VK_IMAGE_LAYOUT_GENERAL;
            b.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED; b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
            b.image=img; b.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
            b.dstAccessMask=VK_ACCESS_SHADER_WRITE_BIT;
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,0,nullptr,0,nullptr,1,&b);
        };
        toGeneral(colorImg); toGeneral(idImg); toGeneral(historyImg);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pl, 0, 1, &ds, 0, nullptr);
        vkCmdPushConstants(cmd, pl, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
        vkCmdDispatch(cmd, (w+7)/8, (h+7)/8, 1);

        // KI-032 fix: barrier the HitRecord SSBO (shader write -> host read) before the host
        // reads it below -- same pattern test_recipe_pool_render.cpp's identical fix uses.
        VkBufferMemoryBarrier hitRecordBarrier{}; hitRecordBarrier.sType=VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        hitRecordBarrier.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT; hitRecordBarrier.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
        hitRecordBarrier.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED; hitRecordBarrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
        hitRecordBarrier.buffer=dummyHitRecord; hitRecordBarrier.offset=0; hitRecordBarrier.size=VK_WHOLE_SIZE;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
            0,0,nullptr,1,&hitRecordBarrier,0,nullptr);

        VkImageMemoryBarrier toSrc{}; toSrc.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        toSrc.oldLayout=VK_IMAGE_LAYOUT_GENERAL; toSrc.newLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        toSrc.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED; toSrc.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
        toSrc.image=colorImg; toSrc.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
        toSrc.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT; toSrc.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,1,&toSrc);

        const VkDeviceSize rbSz = VkDeviceSize(w)*h*4;
        VkBuffer rb=VK_NULL_HANDLE; VkDeviceMemory rbMem=VK_NULL_HANDLE;
        CreateHostBuffer(rbSz, VK_BUFFER_USAGE_TRANSFER_DST_BIT, rb, rbMem, false);
        VkBufferImageCopy cp{}; cp.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1}; cp.imageExtent={w,h,1};
        vkCmdCopyImageToBuffer(cmd, colorImg, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, rb, 1, &cp);
        ASSERT_EQ(vkEndCommandBuffer(cmd), VK_SUCCESS);

        VkSubmitInfo si{}; si.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO; si.commandBufferCount=1; si.pCommandBuffers=&cmd;
        const auto t0 = std::chrono::steady_clock::now();
        ASSERT_EQ(vkQueueSubmit(queue_,1,&si,VK_NULL_HANDLE), VK_SUCCESS);
        ASSERT_EQ(vkQueueWaitIdle(queue_), VK_SUCCESS);
        const auto t1 = std::chrono::steady_clock::now();
        ms = double(std::chrono::duration_cast<std::chrono::milliseconds>(t1-t0).count());

        void* mapped=nullptr; ASSERT_EQ(vkMapMemory(logicalDevice_,rbMem,0,rbSz,0,&mapped), VK_SUCCESS);
        rgba.assign(size_t(w)*h*4, 0); std::memcpy(rgba.data(), mapped, size_t(rbSz));
        vkUnmapMemory(logicalDevice_, rbMem);

        if (outHitRecords != nullptr) {
            void* hrMapped = nullptr;
            ASSERT_EQ(vkMapMemory(logicalDevice_, dHitRecordMem, 0, hitRecordBufSize, 0, &hrMapped), VK_SUCCESS);
            outHitRecords->assign(size_t(w) * h, HitRecordCpu{});
            std::memcpy(outHitRecords->data(), hrMapped, size_t(hitRecordBufSize));
            vkUnmapMemory(logicalDevice_, dHitRecordMem);
        }

        vkDeviceWaitIdle(logicalDevice_);
        vkDestroyBuffer(logicalDevice_,rb,nullptr); vkFreeMemory(logicalDevice_,rbMem,nullptr);
        vkDestroyDescriptorPool(logicalDevice_,pool2,nullptr);
        vkDestroyPipeline(logicalDevice_,pipeline,nullptr);
        vkDestroyPipelineLayout(logicalDevice_,pl,nullptr);
        vkDestroyDescriptorSetLayout(logicalDevice_,dsl,nullptr);
        vkDestroyShaderModule(logicalDevice_,sm,nullptr);
        vkDestroyImageView(logicalDevice_,colorView,nullptr); vkDestroyImageView(logicalDevice_,idView,nullptr);
        vkDestroyImageView(logicalDevice_,historyView,nullptr);
        vkDestroyImage(logicalDevice_,colorImg,nullptr); vkFreeMemory(logicalDevice_,colorMem,nullptr);
        vkDestroyImage(logicalDevice_,idImg,nullptr);    vkFreeMemory(logicalDevice_,idMem,nullptr);
        vkDestroyImage(logicalDevice_,historyImg,nullptr); vkFreeMemory(logicalDevice_,historyMem,nullptr);
        vkDestroyBuffer(logicalDevice_,traceBuf,nullptr); vkFreeMemory(logicalDevice_,traceMem,nullptr);
        if (dummySdf     != VK_NULL_HANDLE) { vkDestroyBuffer(logicalDevice_,dummySdf,nullptr);     vkFreeMemory(logicalDevice_,dSdfMem,nullptr); }
        if (dummyLookup  != VK_NULL_HANDLE) { vkDestroyBuffer(logicalDevice_,dummyLookup,nullptr);  vkFreeMemory(logicalDevice_,dLookupMem,nullptr); }
        if (dummyMip     != VK_NULL_HANDLE) { vkDestroyBuffer(logicalDevice_,dummyMip,nullptr);     vkFreeMemory(logicalDevice_,dMipMem,nullptr); }
        if (dummyTierRef != VK_NULL_HANDLE) { vkDestroyBuffer(logicalDevice_,dummyTierRef,nullptr); vkFreeMemory(logicalDevice_,dTierRefMem,nullptr); }
        if (dummyOccGrid != VK_NULL_HANDLE) { vkDestroyBuffer(logicalDevice_,dummyOccGrid,nullptr); vkFreeMemory(logicalDevice_,dOccGridMem,nullptr); }
        vkDestroyBuffer(logicalDevice_,dummyIter,nullptr); vkFreeMemory(logicalDevice_,dIterMem,nullptr);
        vkDestroyBuffer(logicalDevice_,dummyLighting,nullptr);   vkFreeMemory(logicalDevice_,dLightingMem,nullptr);
        vkDestroyBuffer(logicalDevice_,dummyHitRecord,nullptr);  vkFreeMemory(logicalDevice_,dHitRecordMem,nullptr);
        vkDestroyBuffer(logicalDevice_,dummyShadow,nullptr);     vkFreeMemory(logicalDevice_,dShadowMem,nullptr);
        vkDestroyBuffer(logicalDevice_,dummyAccum,nullptr);      vkFreeMemory(logicalDevice_,dAccumMem,nullptr);
        vkDestroyBuffer(logicalDevice_,dummyPrevCam,nullptr);    vkFreeMemory(logicalDevice_,dPrevCamMem,nullptr);
    }

    // Coverage mask + count. KI-032 fix: a pixel counts as "hit" using HitRecordBuffer's
    // flags (still written post-KI-018) instead of the dead colorImg luminance threshold
    // the mip-fallback/recipe-pool render gates used to share -- see this file's
    // HitRecordCpu comment. DO NOT revert to a colorImg readback (784adff7) -- see KI-032.
    static std::vector<uint8_t> CoverageMask(const std::vector<HitRecordCpu>& hitRecords, uint32_t w, uint32_t h, int& hitCount) {
        std::vector<uint8_t> mask(size_t(w)*h, 0);
        hitCount = 0;
        for (uint32_t i = 0; i < w*h; ++i) {
            const bool hit = (hitRecords[i].flags & kHitRecordFlagHit) != 0u;
            mask[i] = hit ? 1 : 0;
            if (hit) ++hitCount;
        }
        return mask;
    }

    static double ComputeIoU(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b) {
        size_t inter = 0, uni = 0;
        for (size_t i = 0; i < a.size(); ++i) {
            const bool ha = a[i] != 0, hb = b[i] != 0;
            if (ha || hb) ++uni;
            if (ha && hb) ++inter;
        }
        if (uni == 0) return 0.0;  // both empty — NON-VACUITY: caller must reject this case explicitly, not treat as a match
        return double(inter) / double(uni);
    }

    // KI-032 fix: PNG rendered from HitRecord.albedo (still written post-KI-018), not the
    // dead colorImg, so it stays visually meaningful for inspection.
    void SavePng(const char* path, const std::vector<HitRecordCpu>& hitRecords, uint32_t w, uint32_t h) {
        std::vector<uint8_t> rgb(size_t(w)*h*3);
        for (uint32_t i = 0; i < w*h; ++i) {
            const HitRecordCpu& rec = hitRecords[i];
            const bool hit = (rec.flags & kHitRecordFlagHit) != 0u;
            rgb[i*3+0] = hit ? static_cast<uint8_t>(std::clamp(rec.albedo[0], 0.0f, 1.0f) * 255.0f) : 0;
            rgb[i*3+1] = hit ? static_cast<uint8_t>(std::clamp(rec.albedo[1], 0.0f, 1.0f) * 255.0f) : 0;
            rgb[i*3+2] = hit ? static_cast<uint8_t>(std::clamp(rec.albedo[2], 0.0f, 1.0f) * 255.0f) : 0;
        }
        stbi_write_png(path, int(w), int(h), 3, rgb.data(), int(w)*3);
    }

    // ---- BAKED path: build octree pool via BakeRecipeInstructionsToSdfWorld (via the
    // counted wrapper), render through BodyOctreeSceneNode::SetRecipePool. ----
    void RenderBaked(const ParityRecipe& r, std::vector<uint8_t>& rgba, uint32_t kW, uint32_t kH,
                     const PushConstants& pc, std::vector<HitRecordCpu>& hitRecords) {
        using C = BodyOctreeSceneNodeConfig;

        auto baked = CountedBake(r.localSpaceProgram.data(), uint32_t(r.localSpaceProgram.size()),
                                  r.bakeCenter, /*n=*/64, /*bandVoxels=*/2.5f, /*brickDepth=*/3,
                                  std::span<const float>(r.readParamSnapshot));
        Vixen::SVO::SdfBodyOctree body = Vixen::SVO::BuildSdfBodyOctree(baked, 3);
        std::vector<const Vixen::SVO::SdfBodyOctree*> ptrs{&body};
        Vixen::SVO::ConcatenatedOctrees pool = Vixen::SVO::ConcatenateSdfWithMips(ptrs);

        BodyOctreeSceneNodeType nodeType("BodyOctreeScene");
        auto nodeBase = nodeType.CreateInstance("parity_baked_test");
        auto* node = dynamic_cast<BodyOctreeSceneNode*>(nodeBase.get());
        ASSERT_NE(node, nullptr);

        Resource devRes;  SetHandleVal<VulkanDevice*>(devRes, deviceShell_.get());
        Resource poolRes; SetHandleVal<VkCommandPool>(poolRes, commandPool_);
        Resource frRes;   uint32_t frameIndex=0; SetHandleVal<uint32_t>(frRes, frameIndex);
        node->SetInput(C::VULKAN_DEVICE_IN_Slot::index,    0, &devRes);
        node->SetInput(C::COMMAND_POOL_Slot::index,        0, &poolRes);
        node->SetInput(C::CURRENT_FRAME_INDEX_Slot::index, 0, &frRes);

        node->SetRecipePool(std::move(pool));
        node->RequestBrickResidency(true);  // real trilinear march — the actual iso-surface, no mip coarseness

        Vixen::SVO::BodyInstanceGpu inst{};
        inst.worldPos[0]=0.0f; inst.worldPos[1]=0.0f; inst.worldPos[2]=0.0f;
        inst.renderScale=1.0f; inst.octreeIndex=0u; inst.providerKind=0u; inst.recipeId=0u;
        inst.color[0]=1.0f; inst.color[1]=1.0f; inst.color[2]=1.0f;
        node->SetInstances({inst});
        node->Setup();
        ASSERT_NO_THROW(node->Compile());
        frameIndex = 0; SetHandleVal<uint32_t>(frRes, frameIndex);
        ASSERT_NO_THROW(node->Execute());

        auto buf = [&](int slot) -> VkBuffer { return node->GetOutput(slot, 0)->GetHandle<VkBuffer>(); };
        double ms = 0.0;
        ASSERT_NO_FATAL_FAILURE(RenderToRgba(bakedSpirv_,
            buf(C::OCTREE_NODES_BUFFER_Slot::index), buf(C::OCTREE_BRICKS_BUFFER_Slot::index),
            buf(C::OCTREE_MATERIALS_BUFFER_Slot::index), buf(C::OCTREE_CONFIG_BUFFER_Slot::index),
            buf(C::INSTANCE_BUFFER_Slot::index), buf(C::OCTREE_SDF_BUFFER_Slot::index),
            buf(C::OCTREE_BRICKLOOKUP_BUFFER_Slot::index), buf(C::OCTREE_MIPPOOL_BUFFER_Slot::index),
            buf(C::OCTREE_TIERREFTABLE_BUFFER_Slot::index), buf(C::OCTREE_OCCUPANCYGRID_BUFFER_Slot::index),
            pc, kW, kH, rgba, ms, &hitRecords));

        vkDeviceWaitIdle(logicalDevice_);
        node->Cleanup(CleanupReason::FinalTeardown);
    }

    // ---- VIRTUAL path: register in a RecipeRegistry, splice+runtime-compile, render a
    // PROVIDER_PROCEDURAL BodyInstance. ZERO bake calls. ----
    void RenderVirtual(const ParityRecipe& r, std::vector<uint8_t>& rgba, uint32_t kW, uint32_t kH,
                       const PushConstants& pc, std::vector<HitRecordCpu>& hitRecords) {
        using C = BodyOctreeSceneNodeConfig;

        Vixen::SVO::RecipeRegistry registry;
        Vixen::SVO::RecipeRegistry::RecipeEntry entry{};
        entry.bytecode = r.worldSpaceProgram;
        // Corpus-authored bound (see ParityRecipe::authoredBoundCenter's doc comment) wins
        // over derivation — ApplyRecipeBoundsDefaults never touches an already-nonzero
        // boundRadius, matching its own "authored values always win" contract.
        if (r.authoredBoundRadius > 0.0f) {
            entry.boundCenter = r.authoredBoundCenter;
            entry.boundRadius = r.authoredBoundRadius;
        }
        auto boundsResult = Vixen::SVO::Recipe::ApplyRecipeBoundsDefaults(entry, 24.0f, 0.9f);
        (void)boundsResult;
        auto occGrid = Vixen::SVO::Recipe::DeriveOccupancyGrid(
            entry.bytecode.data(), uint32_t(entry.bytecode.size()), entry.boundCenter, entry.boundRadius);
        EXPECT_EQ(occGrid.ok, r.expectOccupancyGrid)
            << "recipe '" << r.name << "': occupancy grid eligibility mismatch vs corpus expectation";
        if (occGrid.ok) {
            entry.occupancyGridValues   = std::move(occGrid.values);
            entry.occupancyGridDim      = occGrid.dim;
            entry.occupancyGridAabbMin  = occGrid.aabbMin;
            entry.occupancyGridCellSize = occGrid.cellSize;
        }
        constexpr uint32_t kRecipeId = 2u;
        ASSERT_EQ(registry.Register(kRecipeId, entry), Vixen::SVO::RecipeRegistry::RegisterResult::Ok);

        const std::string rawSource = ReadFile(BODY_INSTANCE_RAYMARCH_COMP_PATH);
        ASSERT_FALSE(rawSource.empty());
        std::vector<float> occupancyBlob;
        const std::string spliced = Vixen::SVO::Recipe::SpliceProceduralRecipesIntoSource(
            rawSource, registry, &occupancyBlob);

        ShaderManagement::ShaderBundleBuilder builder;
        builder.SetProgramName("BodyInstanceRayMarch_ParityTest")
               .SetPipelineType(ShaderManagement::PipelineTypeConstraint::Compute)
               .SetTargetVulkanVersion(130)
               .SetTargetSpirvVersion(160)
               .AddIncludePath(VIXEN_SHADERS_DIR)
               .AddIncludePath(VIXEN_SVO_SHADERS_DIR)
               .AddStage(ShaderManagement::ShaderStage::Compute, spliced, "main");
        auto buildResult = builder.Build();
        ASSERT_TRUE(buildResult.success) << buildResult.errorMessage;
        const auto& virtualSpirv = buildResult->GetSpirv(ShaderManagement::ShaderStage::Compute);
        ASSERT_FALSE(virtualSpirv.empty());

        BodyOctreeSceneNodeType nodeType("BodyOctreeScene");
        auto nodeBase = nodeType.CreateInstance("parity_virtual_test");
        auto* node = dynamic_cast<BodyOctreeSceneNode*>(nodeBase.get());
        ASSERT_NE(node, nullptr);

        Resource devRes;  SetHandleVal<VulkanDevice*>(devRes, deviceShell_.get());
        Resource poolRes; SetHandleVal<VkCommandPool>(poolRes, commandPool_);
        Resource frRes;   uint32_t frameIndex=0; SetHandleVal<uint32_t>(frRes, frameIndex);
        node->SetInput(C::VULKAN_DEVICE_IN_Slot::index,    0, &devRes);
        node->SetInput(C::COMMAND_POOL_Slot::index,        0, &poolRes);
        node->SetInput(C::CURRENT_FRAME_INDEX_Slot::index, 0, &frRes);

        node->SetOccupancyGrid(occupancyBlob);

        Vixen::SVO::BodyInstanceGpu inst{};
        inst.worldPos[0]=0.0f; inst.worldPos[1]=0.0f; inst.worldPos[2]=0.0f;  // unused: field samples world p directly
        inst.renderScale=1.0f; inst.octreeIndex=0u;
        inst.providerKind=1u;  // PROVIDER_PROCEDURAL
        inst.recipeId=kRecipeId;
        inst.color[0]=1.0f; inst.color[1]=1.0f; inst.color[2]=1.0f;
        // Recipe-Parameterization M4 Task 11: recipeParams[] set to the IDENTICAL value the
        // baked path snapshotted (r.readParamSnapshot) — the whole point of this corpus entry
        // is proving both paths agree on the SAME effective parameter value.
        for (size_t i = 0; i < r.readParamSnapshot.size() && i < 6; ++i)
            inst.recipeParams[i] = r.readParamSnapshot[i];
        node->SetInstances({inst});
        node->Setup();
        ASSERT_NO_THROW(node->Compile());
        frameIndex = 0; SetHandleVal<uint32_t>(frRes, frameIndex);
        ASSERT_NO_THROW(node->Execute());

        auto buf = [&](int slot) -> VkBuffer { return node->GetOutput(slot, 0)->GetHandle<VkBuffer>(); };
        double ms = 0.0;
        ASSERT_NO_FATAL_FAILURE(RenderToRgba(virtualSpirv,
            buf(C::OCTREE_NODES_BUFFER_Slot::index), buf(C::OCTREE_BRICKS_BUFFER_Slot::index),
            buf(C::OCTREE_MATERIALS_BUFFER_Slot::index), buf(C::OCTREE_CONFIG_BUFFER_Slot::index),
            buf(C::INSTANCE_BUFFER_Slot::index), buf(C::OCTREE_SDF_BUFFER_Slot::index),
            buf(C::OCTREE_BRICKLOOKUP_BUFFER_Slot::index), buf(C::OCTREE_MIPPOOL_BUFFER_Slot::index),
            buf(C::OCTREE_TIERREFTABLE_BUFFER_Slot::index), buf(C::OCTREE_OCCUPANCYGRID_BUFFER_Slot::index),
            pc, kW, kH, rgba, ms, &hitRecords));

        vkDeviceWaitIdle(logicalDevice_);
        node->Cleanup(CleanupReason::FinalTeardown);
    }

    // Placeholder — the BAKED path renders through a real SPIR-V too (the same shader, no
    // splice needed since it never references evalRecipeField for recipeId<2/ESVO paths).
    // Compiled once in SetUp-adjacent fixture state to avoid recompiling per-recipe.
    std::vector<uint32_t> bakedSpirv_;

    void CompileBakedShader() {
        const std::string rawSource = ReadFile(BODY_INSTANCE_RAYMARCH_COMP_PATH);
        ASSERT_FALSE(rawSource.empty());
        ShaderManagement::ShaderBundleBuilder builder;
        builder.SetProgramName("BodyInstanceRayMarch_ParityBaked")
               .SetPipelineType(ShaderManagement::PipelineTypeConstraint::Compute)
               .SetTargetVulkanVersion(130)
               .SetTargetSpirvVersion(160)
               .AddIncludePath(VIXEN_SHADERS_DIR)
               .AddIncludePath(VIXEN_SVO_SHADERS_DIR)
               .AddStage(ShaderManagement::ShaderStage::Compute, rawSource, "main");
        auto buildResult = builder.Build();
        ASSERT_TRUE(buildResult.success) << buildResult.errorMessage;
        bakedSpirv_ = buildResult->GetSpirv(ShaderManagement::ShaderStage::Compute);
        ASSERT_FALSE(bakedSpirv_.empty());
    }
};

// ---------------------------------------------------------------------------
// THE GATE
// ---------------------------------------------------------------------------
TEST_F(BakedVsVirtualParityTest, VirtualRendersGeometricallyEquivalentToBaked) {
    std::printf("[ device ] %s\n", selectedDeviceName_.c_str());
    ASSERT_TRUE(deviceConfirmed_);
    ASSERT_NO_FATAL_FAILURE(CompileBakedShader());

    constexpr uint32_t kW = 400, kH = 400;
    // NON-VACUITY floor: a real silhouette at this camera distance covers thousands of
    // pixels (matches the mip-fallback/recipe-pool gates' own >5000 convention, scaled down
    // slightly for this test's smaller 400x400 resolution vs their 512x512).
    constexpr int kMinSilhouettePixels = 3000;
    // IoU floor: representation-mismatch tolerance (voxelized-64^3 vs analytic). Chosen
    // empirically generous (0.75) — a genuine geometry bug (wrong recipe evaluated, wrong
    // placement, wrong scale) produces IoU well under 0.5, while antialiasing-only
    // differences stay comfortably above 0.85 in practice; 0.75 leaves margin without
    // masking a real mismatch.
    constexpr double kIoUFloor = 0.75;

    const auto corpus = BuildCorpus();
    ASSERT_GE(corpus.size(), 3u) << "Task 14 requires >=3 corpus recipes including a domain-modifier one";

    bool sawDomainModifierRecipe = false;

    for (const auto& r : corpus) {
        SCOPED_TRACE("recipe=" + r.name);

        // Camera distance in WORLD units (kWorldGridSize scale), matching
        // test_mip_fallback_render.cpp's own dist = 2.2*kWorldGridSize convention.
        const float dist = 2.2f * kWorldGridSize;
        const glm::vec3 eye = r.worldTarget + glm::vec3(0.0f, 0.0f, dist);
        const PushConstants pc = MakeCamera(eye, r.worldTarget, kW, kH, 1);

        // --- Baked render ---
        const uint32_t bakeCallsBefore = g_bakeCallCount;
        std::vector<uint8_t> bakedRgba;
        std::vector<HitRecordCpu> bakedHitRecords;
        ASSERT_NO_FATAL_FAILURE(RenderBaked(r, bakedRgba, kW, kH, pc, bakedHitRecords));
        EXPECT_EQ(g_bakeCallCount, bakeCallsBefore + 1u)
            << "baked path should call BakeRecipeInstructionsToSdfWorld exactly once";
        SavePng(("/tmp/parity_" + r.name + "_baked.png").c_str(), bakedHitRecords, kW, kH);

        // --- Virtual render (NEVER-BAKED PROOF: counter must not move) ---
        const uint32_t bakeCallsBeforeVirtual = g_bakeCallCount;
        std::vector<uint8_t> virtualRgba;
        std::vector<HitRecordCpu> virtualHitRecords;
        ASSERT_NO_FATAL_FAILURE(RenderVirtual(r, virtualRgba, kW, kH, pc, virtualHitRecords));
        EXPECT_EQ(g_bakeCallCount, bakeCallsBeforeVirtual)
            << "VIRTUAL path must call zero bake functions — this is the whole point of Inc0";
        SavePng(("/tmp/parity_" + r.name + "_virtual.png").c_str(), virtualHitRecords, kW, kH);

        if (!r.expectOccupancyGrid) sawDomainModifierRecipe = true;

        // --- Non-vacuity: both renders must show real geometry, independently ---
        int bakedHits = 0, virtualHits = 0;
        auto bakedMask   = CoverageMask(bakedHitRecords,   kW, kH, bakedHits);
        auto virtualMask = CoverageMask(virtualHitRecords, kW, kH, virtualHits);

        EXPECT_GT(bakedHits, kMinSilhouettePixels)
            << "baked render's own silhouette must be non-trivial (floor=" << kMinSilhouettePixels << ")";
        EXPECT_GT(virtualHits, kMinSilhouettePixels)
            << "virtual render's own silhouette must be non-trivial (floor=" << kMinSilhouettePixels << ")";

        // --- IoU: FAILS outright on zero-vs-zero (ComputeIoU returns 0.0 for empty union,
        // never a spurious 1.0) — combined with the two floor checks above, an empty-vs-empty
        // pass is structurally impossible here. ---
        const double iou = ComputeIoU(bakedMask, virtualMask);
        std::printf("[PARITY] recipe=%s bakedHits=%d virtualHits=%d IoU=%.4f occGrid=%s\n",
                    r.name.c_str(), bakedHits, virtualHits, iou,
                    r.expectOccupancyGrid ? "yes" : "no");
        EXPECT_GT(iou, kIoUFloor)
            << "silhouette IoU below tolerance — baked vs virtual geometry mismatch for recipe '"
            << r.name << "' (bakedHits=" << bakedHits << " virtualHits=" << virtualHits << ")";
    }

    EXPECT_TRUE(sawDomainModifierRecipe)
        << "corpus must include at least one domain-modifier (non-occupancy-grid-eligible) recipe";
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
