// test_instance_occlusion_cull_mirror.cpp — Raster-proxy B1 M3 (CPU mirror, no device).
//
// 1:1 CPU mirror tests for shaders/InstanceOcclusionCull.comp per the
// gpu-shader-debug discipline. The mirror (Nodes/InstanceOcclusionCullMirror.h)
// carries the exact per-word cull a shader thread performs:
//   - world AABB = inst.worldPos + inst.renderScale * (config.localToWorld *
//     traceBounds corner)  — TraceWorld.glsl's inverse, corners min/maxed
//   - project the 8 corners by prevViewProj; uv = ndc*0.5+0.5, texel = uv*dims
//     (SpatialReuseShade.comp's reprojection convention, w>1e-5 validity)
//   - tile rect (/16), dilated one tile, clamped; occluded iff EVERY consulted
//     tile's max ray distance < the AABB's nearest distance from prevCamPos
//   - conservatism escapes → VISIBLE: procedural provider, invalid traceBounds,
//     any corner w<=1e-5, undilated rect fully offscreen, >64 covered tiles,
//     camera inside the AABB (distance 0), any covered miss tile (1e30)
//   - one thread owns one 32-bit mask word; bit set = SKIP; OR-composed over
//     the word's existing content (the bucketed-dispatch CPU writer's bits)
// Any fix here MUST be applied to the .comp and vice versa.

#include <gtest/gtest.h>
#include "Nodes/InstanceOcclusionCullMirror.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <cstdint>
#include <vector>

using namespace Vixen::RenderGraph::Mirror;

namespace {

// Ortho world→clip over x,y ∈ [-1,1] with w == 1: projection is LINEAR and the
// pixel math below is exact (uv = world*0.5+0.5). Depth range is irrelevant to
// the cull (only w-validity and the xy rect are consumed).
glm::mat4 UnitOrthoViewProj() {
    return glm::ortho(-1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 100.0f);
}

// A 256x256 depth target: 16x16 tiles of 16px (HiZTileCount(256) == 16).
constexpr uint32_t kW = 256, kH = 256, kTiles = 16;

std::vector<float> Tiles(float v) {
    return std::vector<float>(kTiles * kTiles, v);
}

// The canonical test instance: axis-aligned template box, identity localToWorld,
// traceBounds spanning [0, 0.125] in x/y and [5, 5.5] in z (via bounds+scale
// composed below), placed so the projected uv rect is exactly [0.5, 0.5625]
// (pixels [128,144] → tiles 8..9, dilated 7..10) and the nearest distance from
// the origin camera is exactly 5.0 (dx=dy=0 inside the xy footprint, dz=5).
CullInstance BoxInstance() {
    CullInstance inst{};
    inst.worldPos = glm::vec3(0.0f, 0.0f, 5.0f);
    inst.renderScale = 1.0f;
    inst.octreeIndex = 0u;
    inst.providerKind = 0u;  // Stored/ESVO
    return inst;
}

CullOctreeConfig BoxConfig() {
    CullOctreeConfig cfg{};
    cfg.traceBoundsMin = glm::vec3(0.0f, 0.0f, 0.0f);
    cfg.traceBoundsMax = glm::vec3(0.125f, 0.125f, 0.5f);
    cfg.localToWorld = glm::mat4(1.0f);
    return cfg;
}

CullParams BoxParams() {
    CullParams p{};
    p.prevViewProj = UnitOrthoViewProj();
    p.prevCamPos = glm::vec3(0.0f);
    p.srcWidth = kW;
    p.srcHeight = kH;
    p.instanceCount = 1u;
    return p;
}

}  // namespace

// === World-AABB composition ==============================================

TEST(InstanceOcclusionCullMirror, WorldAabbComposesScaleTranslate) {
    CullInstance inst{};
    inst.worldPos = glm::vec3(3.0f, 0.0f, 5.0f);
    inst.renderScale = 2.0f;
    CullOctreeConfig cfg{};
    cfg.traceBoundsMin = glm::vec3(0.25f);
    cfg.traceBoundsMax = glm::vec3(0.75f);
    // localToWorld = translate(1,0,0) * scale(2): local corner l -> 2*l + (1,0,0)
    cfg.localToWorld = glm::translate(glm::mat4(1.0f), glm::vec3(1.0f, 0.0f, 0.0f)) *
                       glm::scale(glm::mat4(1.0f), glm::vec3(2.0f));
    glm::vec3 mn, mx;
    CullWorldAabb(inst, cfg, mn, mx);
    // base = 2*[0.25,0.75] + 1 = [1.5, 2.5] (x), [0.5, 1.5] (y,z)
    // world = worldPos + 2*base
    EXPECT_FLOAT_EQ(mn.x, 3.0f + 2.0f * 1.5f);   // 6.0
    EXPECT_FLOAT_EQ(mx.x, 3.0f + 2.0f * 2.5f);   // 8.0
    EXPECT_FLOAT_EQ(mn.y, 0.0f + 2.0f * 0.5f);   // 1.0
    EXPECT_FLOAT_EQ(mx.y, 0.0f + 2.0f * 1.5f);   // 3.0
    EXPECT_FLOAT_EQ(mn.z, 5.0f + 2.0f * 0.5f);   // 6.0
    EXPECT_FLOAT_EQ(mx.z, 5.0f + 2.0f * 1.5f);   // 8.0
}

TEST(InstanceOcclusionCullMirror, WorldAabbHandlesRotation) {
    // 90 deg about +Z maps local +x -> world +y. Corner-based min/max must
    // stay a valid (min<max) box, not assume axis-aligned pass-through.
    CullInstance inst{};
    inst.worldPos = glm::vec3(0.0f);
    inst.renderScale = 1.0f;
    CullOctreeConfig cfg{};
    cfg.traceBoundsMin = glm::vec3(0.0f);
    cfg.traceBoundsMax = glm::vec3(1.0f, 0.5f, 0.25f);
    cfg.localToWorld = glm::rotate(glm::mat4(1.0f), glm::half_pi<float>(),
                                   glm::vec3(0.0f, 0.0f, 1.0f));
    glm::vec3 mn, mx;
    CullWorldAabb(inst, cfg, mn, mx);
    // x' = -y ∈ [-0.5, 0], y' = x ∈ [0, 1]
    EXPECT_NEAR(mn.x, -0.5f, 1e-5f);
    EXPECT_NEAR(mx.x, 0.0f, 1e-5f);
    EXPECT_NEAR(mn.y, 0.0f, 1e-5f);
    EXPECT_NEAR(mx.y, 1.0f, 1e-5f);
    EXPECT_NEAR(mn.z, 0.0f, 1e-5f);
    EXPECT_NEAR(mx.z, 0.25f, 1e-5f);
}

// === Core occlusion verdicts =============================================

TEST(InstanceOcclusionCullMirror, OccludedBehindUniformNearerWall) {
    // Wall at ray distance 4 in every tile; instance nearest distance 5.
    auto tiles = Tiles(4.0f);
    EXPECT_TRUE(InstanceOccluded(BoxInstance(), BoxConfig(), tiles.data(), BoxParams()));
}

TEST(InstanceOcclusionCullMirror, VisibleWhenWallIsFarther) {
    // Wall at 6 — some ray in every covered tile reached past the instance.
    auto tiles = Tiles(6.0f);
    EXPECT_FALSE(InstanceOccluded(BoxInstance(), BoxConfig(), tiles.data(), BoxParams()));
}

TEST(InstanceOcclusionCullMirror, AnyMissTileInDilatedRectMakesVisible) {
    // Undilated rect = tiles 8..9; dilated = 7..10. A sentinel ANYWHERE in the
    // dilated ring (7,7) must veto the cull — proves dilation is consulted.
    auto tiles = Tiles(4.0f);
    tiles[7u * kTiles + 7u] = kDepthMissSentinel;
    EXPECT_FALSE(InstanceOccluded(BoxInstance(), BoxConfig(), tiles.data(), BoxParams()));
}

TEST(InstanceOcclusionCullMirror, TilesOutsideDilatedRectAreIgnored) {
    // Everything OUTSIDE dilated 7..10 is sky; inside is a 4.0 wall. Still culled.
    auto tiles = Tiles(kDepthMissSentinel);
    for (uint32_t ty = 7; ty <= 10; ++ty)
        for (uint32_t tx = 7; tx <= 10; ++tx)
            tiles[ty * kTiles + tx] = 4.0f;
    EXPECT_TRUE(InstanceOccluded(BoxInstance(), BoxConfig(), tiles.data(), BoxParams()));
}

// === Conservatism escapes (all must report VISIBLE) ======================

TEST(InstanceOcclusionCullMirror, ProceduralProviderNeverCulled) {
    auto inst = BoxInstance();
    inst.providerKind = 1u;  // procedural — no octree config to bound it
    auto tiles = Tiles(4.0f);
    EXPECT_FALSE(InstanceOccluded(inst, BoxConfig(), tiles.data(), BoxParams()));
}

TEST(InstanceOcclusionCullMirror, InvalidTraceBoundsNeverCulled) {
    auto cfg = BoxConfig();
    cfg.traceBoundsMin = glm::vec3(0.0f);
    cfg.traceBoundsMax = glm::vec3(0.0f);  // the all-zero "no tighter bound" convention
    auto tiles = Tiles(4.0f);
    EXPECT_FALSE(InstanceOccluded(BoxInstance(), cfg, tiles.data(), BoxParams()));
}

TEST(InstanceOcclusionCullMirror, BehindNearPlaneNeverCulled) {
    // Perspective camera at origin looking down -Z (GL convention); the box
    // sits at +Z = BEHIND the camera: corners project with w <= 1e-5.
    auto params = BoxParams();
    params.prevViewProj =
        glm::perspective(glm::radians(60.0f), 1.0f, 0.1f, 100.0f) *
        glm::lookAt(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, -1.0f),
                    glm::vec3(0.0f, 1.0f, 0.0f));
    auto tiles = Tiles(4.0f);
    EXPECT_FALSE(InstanceOccluded(BoxInstance(), BoxConfig(), tiles.data(), params));
}

TEST(InstanceOcclusionCullMirror, FullyOffscreenNeverCulled) {
    // Shift the instance so its uv rect lies entirely right of uv.x = 1.
    auto inst = BoxInstance();
    inst.worldPos.x = 1.5f;  // uv.x ∈ [1.25, 1.3125]
    auto tiles = Tiles(4.0f);
    EXPECT_FALSE(InstanceOccluded(inst, BoxConfig(), tiles.data(), BoxParams()));
}

TEST(InstanceOcclusionCullMirror, TileCapNeverCulled) {
    // Blow the instance up (renderScale, NOT traceBounds — those must stay in
    // [0,1] to pass validity) to cover the whole viewport: clamped 16x16 = 256
    // covered tiles > the 64-tile cap — big things are visible anyway.
    auto cfg = BoxConfig();
    cfg.traceBoundsMax = glm::vec3(1.0f, 1.0f, 0.125f);
    auto inst = BoxInstance();
    inst.worldPos = glm::vec3(-2.0f, -2.0f, 5.0f);
    inst.renderScale = 4.0f;  // world xy ∈ [-2, 2] → uv ∈ [-0.5, 1.5]
    auto tiles = Tiles(4.0f);
    EXPECT_FALSE(InstanceOccluded(inst, cfg, tiles.data(), BoxParams()));
}

TEST(InstanceOcclusionCullMirror, CameraInsideAabbNeverCulled) {
    // prevCamPos inside the box: nearest distance 0 can never exceed a tile max.
    auto params = BoxParams();
    params.prevCamPos = glm::vec3(0.0625f, 0.0625f, 5.25f);
    auto tiles = Tiles(4.0f);
    EXPECT_FALSE(InstanceOccluded(BoxInstance(), BoxConfig(), tiles.data(), params));
}

// === Word packing / OR-composition =======================================

TEST(InstanceOcclusionCullMirror, MaskWordSetsBitsOnlyForOccludedInstances) {
    // inst0 occluded (the canonical box), inst1 visible (in front of the wall:
    // z ∈ [2, 2.5], nearest 2.0 < wall 4.0), inst2 occluded (same as inst0 but
    // shifted +x one tile-width 0.125 — uv rect [0.5625, 0.625], still walled).
    std::vector<CullInstance> insts = {BoxInstance(), BoxInstance(), BoxInstance()};
    insts[1].worldPos.z = 2.0f;
    insts[2].worldPos.x = 0.125f;
    std::vector<CullOctreeConfig> cfgs = {BoxConfig()};
    auto params = BoxParams();
    params.instanceCount = 3u;
    auto tiles = Tiles(4.0f);
    const uint32_t word = CullMaskWord(0u, insts.data(), cfgs.data(),
                                       tiles.data(), params, 0u);
    EXPECT_EQ(word, (1u << 0) | (1u << 2));
}

TEST(InstanceOcclusionCullMirror, MaskWordOrComposesExistingBits) {
    // The bucketed-dispatch CPU writer already set bit 5; the cull's word must
    // preserve it while adding its own occlusion bits.
    std::vector<CullInstance> insts = {BoxInstance()};
    std::vector<CullOctreeConfig> cfgs = {BoxConfig()};
    auto tiles = Tiles(4.0f);
    const uint32_t word = CullMaskWord(0u, insts.data(), cfgs.data(),
                                       tiles.data(), BoxParams(), 1u << 5);
    EXPECT_EQ(word, (1u << 0) | (1u << 5));
}

TEST(InstanceOcclusionCullMirror, MaskWordIgnoresInstancesBeyondCount) {
    // instanceCount = 1: lanes 1..31 of word 0 must stay clear even though the
    // arrays hold a second occludable instance.
    std::vector<CullInstance> insts = {BoxInstance(), BoxInstance()};
    std::vector<CullOctreeConfig> cfgs = {BoxConfig()};
    auto params = BoxParams();
    params.instanceCount = 1u;
    auto tiles = Tiles(4.0f);
    const uint32_t word = CullMaskWord(0u, insts.data(), cfgs.data(),
                                       tiles.data(), params, 0u);
    EXPECT_EQ(word, 1u << 0);
}
