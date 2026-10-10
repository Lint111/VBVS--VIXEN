// test_emitter_sampling.cpp - R502 / R502.2 emitter table + sampler CPU mirror (S0).
//
// Gate: the table's power/CDF/totals follow the documented model; the mirror of
// shaders/EmitterSampling.glsl draws instances proportionally to power, lands on the
// emitting surface, reports a pdf whose reciprocal integrates to the emitter area, splits
// pipelines by power, and replays a seeded sequence bit-for-bit (pinned hash).

#include <gtest/gtest.h>
#include <glm/glm.hpp>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <map>
#include <vector>

#undef far
#undef near
#undef min
#undef max

#include "EmitterSampling.h"
#include "MipBake.h"
#include "Recipe/RecipeBounds.h"
#include "Recipe/RecipeOccupancy.h"
#include "Recipe/RecipeRegistry.h"
#include "SdfBake.h"
#include "SdfRecipes.h"
#include "ShellOctreeGpu.h"

using namespace Vixen::SVO;
using namespace Vixen::SVO::Recipe;  // SdfInstruction, evalRecipe

namespace {

constexpr uint32_t kGridRecipe = 1u;      // sphere r=4 with an occupancy grid
constexpr uint32_t kFallbackRecipe = 2u;  // sphere r=3, no grid (bound-sphere area)
constexpr float kGridRadius = 4.0f;
constexpr float kFallbackRadius = 3.0f;

SdfInstruction SphereInstr(float r) {
    SdfInstruction in{};
    in.opCode = static_cast<uint8_t>(SdfOpCode::Sphere);
    in.data[3] = r;
    return in;
}

SdfBodyOctree MakeBody(int n, float r, const std::function<float(const glm::vec3&)>& emission) {
    RecipeParams rp{r, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    const glm::vec3 c(n * 0.5f);
    SdfBakeResult baked = BakeRecipeToSdfWorldWithEmission(RECIPE_SPHERE, c, rp, n, 2.0f, emission);
    return BuildSdfBodyOctree(baked, 3);
}

float HalfEmitter(const glm::vec3& p) { return p.x > p.y ? 1.5f + 0.01f * p.z : 0.0f; }

BodyInstanceGpu MakeInstance(uint32_t provider, uint32_t octree, uint32_t recipe, const glm::vec3& origin,
                             float scale, const glm::vec3& color, float emission) {
    BodyInstanceGpu inst;
    SetInstanceTranslationScale(inst, origin, scale);
    inst.material.color[0] = color.x;
    inst.material.color[1] = color.y;
    inst.material.color[2] = color.z;
    inst.material.providerKind = provider;
    inst.material.octreeIndex = octree;
    inst.material.recipeId = recipe;
    inst.material.recipeParams[3] = emission;
    return inst;
}

RecipeRegistry MakeRegistry() {
    RecipeRegistry reg;
    {
        RecipeRegistry::RecipeEntry e;
        e.bytecode = {SphereInstr(kGridRadius)};
        const auto bounds = DeriveConservativeBounds(e.bytecode.data(), static_cast<uint32_t>(e.bytecode.size()));
        EXPECT_TRUE(bounds.ok);
        e.boundCenter = bounds.center;
        e.boundRadius = bounds.radius;
        const auto grid = DeriveOccupancyGrid(e.bytecode.data(), static_cast<uint32_t>(e.bytecode.size()),
                                              bounds.center, bounds.radius);
        EXPECT_TRUE(grid.ok);
        e.occupancyGridValues = grid.values;
        e.occupancyGridDim = grid.dim;
        e.occupancyGridAabbMin = grid.aabbMin;
        e.occupancyGridCellSize = grid.cellSize;
        EXPECT_EQ(reg.Register(kGridRecipe, e), RecipeRegistry::RegisterResult::Ok);
    }
    {
        RecipeRegistry::RecipeEntry e;
        e.bytecode = {SphereInstr(kFallbackRadius)};
        e.boundCenter = glm::vec3(0.0f);
        e.boundRadius = kFallbackRadius;
        EXPECT_EQ(reg.Register(kFallbackRecipe, e), RecipeRegistry::RegisterResult::Ok);
    }
    return reg;
}

// Mixed scene: 3 recipe emitters (one dark, filtered), 2 stored octrees (one dark).
struct Scene {
    RecipeRegistry registry = MakeRegistry();
    RecipeEmitterShapes shapes = BuildRecipeEmitterShapes(registry);
    SdfBodyOctree emissiveBody = MakeBody(32, 12.0f, HalfEmitter);
    SdfBodyOctree darkBody = MakeBody(32, 10.0f, [](const glm::vec3&) { return 0.0f; });
    ConcatenatedOctrees cat;
    std::vector<BodyInstanceGpu> instances;
    EmitterTable table;
    EmitterSceneCpu cpu;

    Scene(bool withRecipes = true, bool withStored = true) {
        const std::vector<const SdfBodyOctree*> bodies = {&emissiveBody, &darkBody};
        cat = ConcatenateSdfWithMips(bodies);
        if (withRecipes) {
            instances.push_back(MakeInstance(1, 0, kGridRecipe, {0, 0, 0}, 1.0f, {1.0f, 0.5f, 0.2f}, 2.0f));
            instances.push_back(MakeInstance(1, 0, kGridRecipe, {20, 0, 0}, 2.0f, {1.0f, 1.0f, 1.0f}, 1.0f));
            instances.push_back(MakeInstance(1, 0, kGridRecipe, {40, 0, 0}, 1.0f, {1.0f, 1.0f, 1.0f}, 0.0f));
            instances.push_back(MakeInstance(1, 0, kFallbackRecipe, {0, 30, 0}, 1.5f, {0.2f, 0.9f, 0.4f}, 1.0f));
        }
        if (withStored) {
            instances.push_back(MakeInstance(0, 0, 0, {-30, 0, 0}, 0.5f, {1, 1, 1}, 0.0f));
            instances.push_back(MakeInstance(0, 1, 0, {-60, 0, 0}, 1.0f, {1, 1, 1}, 0.0f));
            instances.push_back(MakeInstance(0, 0, 0, {-30, 20, 0}, 1.0f, {1, 1, 1}, 0.0f));
        }
        table = BuildEmitterTable(instances, shapes, EmitterRootPowers(cat));
        cpu.table = &table;
        cpu.shapes = &shapes;
        cpu.cat = &cat;
        cpu.instances = instances;
        cpu.recipeSd = [this](uint32_t inst, const glm::vec3& p) {
            const RecipeRegistry::RecipeEntry* e = registry.Get(instances[inst].material.recipeId);
            return evalRecipe(e->bytecode.data(), static_cast<uint32_t>(e->bytecode.size()), p);
        };
    }
};

uint64_t HashSamples(const Scene& scene, uint32_t seed, uint32_t count, bool withPositions) {
    uint64_t h = 1469598103934665603ull;
    auto mix = [&](uint32_t v) { h = (h ^ v) * 1099511628211ull; };
    uint32_t rng = seed;
    for (uint32_t i = 0; i < count; ++i) {
        const EmitterSampleCpu s = SampleEmitter(scene.cpu, rng);
        uint32_t bits;
        mix(s.key);
        std::memcpy(&bits, &s.pdf, 4); mix(bits);
        if (!withPositions) continue;
        std::memcpy(&bits, &s.position.x, 4); mix(bits);
        std::memcpy(&bits, &s.position.y, 4); mix(bits);
        std::memcpy(&bits, &s.position.z, 4); mix(bits);
    }
    return h;
}

}  // namespace

TEST(RecipeEmitterShapes, SurfaceCellAreaTracksSphereArea) {
    Scene scene(true, false);
    const RecipeEmitterShape* shape = scene.shapes.Find(kGridRecipe);
    ASSERT_NE(shape, nullptr);
    EXPECT_FALSE(shape->boundSphereFallback);
    ASSERT_GT(shape->cellCount, 0u);
    const float trueArea = 4.0f * kEmitterPi * kGridRadius * kGridRadius;
    const float ratio = shape->areaRecipe / trueArea;
    // The shell threshold over-counts (cells within half a diagonal of the surface); the
    // estimate must stay within a small constant of the true area.
    EXPECT_GT(ratio, 0.5f);
    EXPECT_LT(ratio, 4.0f);
    RecordProperty("surface_area_over_true", std::to_string(ratio));
}

TEST(RecipeEmitterShapes, NoGridFallsBackToBoundSphereArea) {
    Scene scene(true, false);
    const RecipeEmitterShape* shape = scene.shapes.Find(kFallbackRecipe);
    ASSERT_NE(shape, nullptr);
    EXPECT_TRUE(shape->boundSphereFallback);
    EXPECT_EQ(shape->cellCount, 0u);
    EXPECT_NEAR(shape->areaRecipe, 4.0f * kEmitterPi * kFallbackRadius * kFallbackRadius, 1e-3f);
    EXPECT_EQ(scene.shapes.Find(99u), nullptr);
}

TEST(EmitterTableBuild, OnlyEmissiveInstancesPowerScalesWithAreaAndCdfAccumulates) {
    Scene scene;
    const EmitterTable& t = scene.table;
    // recipe: instances 0, 1, 3 (2 has zero emission); stored: instance 4 (5 is dark; 6 shares
    // octree 0 with 4).
    ASSERT_EQ(t.recipe.size(), 3u);
    EXPECT_EQ(t.recipe[0].instIdx, 0u);
    EXPECT_EQ(t.recipe[1].instIdx, 1u);
    EXPECT_EQ(t.recipe[2].instIdx, 3u);
    ASSERT_EQ(t.sparse.size(), 2u);
    EXPECT_EQ(t.sparse[0].instIdx, 4u);
    EXPECT_EQ(t.sparse[1].instIdx, 6u);

    const RecipeEmitterShape* shape = scene.shapes.Find(kGridRecipe);
    const float lum0 = EmitterLuminance(glm::vec3(1.0f, 0.5f, 0.2f)) * 2.0f;
    EXPECT_NEAR(t.recipe[0].phi, kEmitterPi * lum0 * shape->areaRecipe, 1e-3f * t.recipe[0].phi);
    // Instance 1: scale 2 => area x4, luminance 1 (white x 1).
    EXPECT_NEAR(t.recipe[1].areaInst, shape->areaRecipe * 4.0f, 1e-3f * t.recipe[1].areaInst);
    EXPECT_NEAR(t.recipe[1].phi, kEmitterPi * 1.0f * t.recipe[1].areaInst, 1e-3f * t.recipe[1].phi);
    EXPECT_EQ(t.recipe[2].flags & kRecipeEmitterFlagBoundSphereArea, kRecipeEmitterFlagBoundSphereArea);
    EXPECT_EQ(t.recipe[0].flags, 0u);

    double run = 0.0;
    for (const RecipeEmitterEntry& e : t.recipe) {
        run += e.phi;
        EXPECT_NEAR(e.cdf, run, 1e-5 * run);
    }
    EXPECT_NEAR(t.totals.phiRecipe, run, 1e-5 * run);

    const std::vector<float> roots = EmitterRootPowers(scene.cat);
    ASSERT_EQ(roots.size(), 2u);
    EXPECT_GT(roots[0], 0.0f);
    EXPECT_EQ(roots[1], 0.0f);
    // s=0.5 => (10 * 0.5)^2 = 25 ; s=1 => 100.
    EXPECT_NEAR(t.sparse[0].phi, kEmitterPi * roots[0] * 25.0f, 1e-4f * t.sparse[0].phi);
    EXPECT_NEAR(t.sparse[1].phi, kEmitterPi * roots[0] * 100.0f, 1e-4f * t.sparse[1].phi);
    EXPECT_NEAR(t.totals.phiSparse, t.sparse[0].phi + t.sparse[1].phi, 1e-4f * t.totals.phiSparse);
    EXPECT_EQ(t.totals.recipeEntryCount, 3u);
    EXPECT_EQ(t.totals.sparseEntryCount, 2u);
}

TEST(EmitterTableBuild, EmptyWhenNothingEmits) {
    Scene scene(false, false);
    EXPECT_TRUE(scene.table.recipe.empty());
    EXPECT_TRUE(scene.table.sparse.empty());
    uint32_t rng = 7u;
    const EmitterSampleCpu s = SampleEmitter(scene.cpu, rng);
    EXPECT_EQ(s.pdf, 0.0f);
    EXPECT_EQ(EmitterTotalPower(scene.cpu), 0.0f);
}

TEST(EmitterSamplingRecipe, DrawsInstancesByPowerOnTheSurfaceWithUnitAreaPdf) {
    Scene scene(true, false);
    const EmitterTable& t = scene.table;
    ASSERT_EQ(t.recipe.size(), 3u);
    constexpr uint32_t kN = 200000;
    std::map<uint32_t, uint32_t> hits;
    double sumInvPdf = 0.0;
    uint32_t rng = 0xA11CE5u;
    uint32_t gridSamples = 0;
    for (uint32_t i = 0; i < kN; ++i) {
        const EmitterSampleCpu s = SampleEmitter(scene.cpu, rng);
        ASSERT_GT(s.pdf, 0.0f);
        ASSERT_EQ(s.key & 3u, 1u);
        const uint32_t inst = (s.key >> 2u) & 0xFFu;
        ++hits[inst];
        sumInvPdf += 1.0 / s.pdf;
        const BodyInstanceGpu& bi = scene.instances[inst];
        const glm::vec3 local = TransformPoint(bi.transform.worldToLocal, s.position);
        const float scale = EmitterInstanceScale(bi.transform.localToWorld);
        const float radius = bi.material.recipeId == kGridRecipe ? kGridRadius : kFallbackRadius;
        // Sphere SDF: every projected / bound-sphere sample lies on the surface.
        EXPECT_NEAR(glm::length(local), radius, 2e-3f * radius) << "inst " << inst << " scale " << scale;
        if (bi.material.recipeId == kGridRecipe) ++gridSamples;
    }
    EXPECT_GT(gridSamples, 0u);
    for (const RecipeEmitterEntry& e : t.recipe) {
        const double expected = static_cast<double>(e.phi) / t.totals.phiRecipe;
        const double got = static_cast<double>(hits[e.instIdx]) / kN;
        EXPECT_NEAR(got, expected, 0.01) << "instance " << e.instIdx;
    }
    EXPECT_EQ(hits.count(2u), 0u) << "zero-emission instance must never be drawn";
    // E[1 / pdf] == sum of instance areas for a pdf that is a density over area.
    double totalArea = 0.0;
    for (const RecipeEmitterEntry& e : t.recipe) totalArea += e.areaInst;
    EXPECT_NEAR(sumInvPdf / kN, totalArea, 0.02 * totalArea);
}

TEST(EmitterSamplingSparse, LandsInEmissiveOctantsAndPdfIntegratesToClusterArea) {
    Scene scene(false, true);
    const EmitterTable& t = scene.table;
    ASSERT_EQ(t.sparse.size(), 2u);
    const EmitterPowerLaneView lane = ViewEmitterPowerLaneOf(scene.cat, 0u);
    ASSERT_TRUE(lane.Valid());

    // Independent expectation: walk the tree for each emissive leaf's cell size, then sum
    // 3 * side^2 over octants that hold power, scaled per instance by areaScale.
    const Octree* oct = scene.emissiveBody.octree->getOctree();
    const auto& nodes = oct->root->childDescriptors;
    double clusterAreaNorm = 0.0;
    struct Item { uint32_t node; float cell; };
    std::vector<Item> stack = {{0u, 1.0f}};
    while (!stack.empty()) {
        const Item it = stack.back();
        stack.pop_back();
        const ChildDescriptor& d = nodes[it.node];
        const uint32_t totalNonLeaf = static_cast<uint32_t>(std::popcount(static_cast<uint8_t>(d.validMask & ~d.leafMask)));
        uint32_t nl = 0u, lf = 0u;
        for (int o = 0; o < 8; ++o) {
            if (!d.hasChild(o)) continue;
            const bool leaf = d.isLeaf(o);
            const uint32_t idx = leaf ? d.childPointer + totalNonLeaf + lf++ : d.childPointer + nl++;
            const float childCell = it.cell * 0.5f;
            if (!leaf) { stack.push_back({idx, childCell}); continue; }
            const uint32_t slot = lane.FindLeaf(idx);
            if (slot == 0xFFFFFFFFu) continue;
            for (uint32_t q = 0; q < 8; ++q)
                if (lane.LeafOctantPower(slot, q) > 0.0f)
                    clusterAreaNorm += 3.0 * (childCell * 0.5) * (childCell * 0.5);
        }
    }
    ASSERT_GT(clusterAreaNorm, 0.0);
    double expectedArea = 0.0;
    for (const SparseEmitterEntry& e : t.sparse) expectedArea += clusterAreaNorm * e.areaScale;

    constexpr uint32_t kN = 400000;
    std::map<uint32_t, uint32_t> hits;
    double sumInvPdf = 0.0;
    uint32_t rng = 0x5EED5u;
    const float octantSlack = 1.0f / 8.0f;  // one brick octant at 4 bricks/axis, n=32
    for (uint32_t i = 0; i < kN; ++i) {
        const EmitterSampleCpu s = SampleEmitter(scene.cpu, rng);
        ASSERT_GT(s.pdf, 0.0f);
        ASSERT_EQ(s.key & 3u, 2u);
        const uint32_t inst = (s.key >> 2u) & 0xFFu;
        ++hits[inst];
        sumInvPdf += 1.0 / s.pdf;
        const BodyInstanceGpu& bi = scene.instances[inst];
        const glm::vec3 pNorm = TransformPoint(bi.transform.worldToLocal, s.position) / kEmitterWorldGridSize;
        // HalfEmitter emits where x > y inside the sphere: the sampled cluster must overlap it.
        EXPECT_GT(pNorm.x, pNorm.y - octantSlack);
        // Bricks straddling the shell (plus the 2-voxel band) hold material (hence emission) beyond the surface band.
        EXPECT_LT(glm::length(pNorm - glm::vec3(0.5f)), 12.0f / 32.0f + 0.0625f + 0.25f * 1.7320508f);
        const uint32_t keyNode = s.key >> 13u;
        const uint32_t keyOctant = (s.key >> 10u) & 7u;
        const uint32_t slot = lane.FindLeaf(keyNode);
        ASSERT_NE(slot, 0xFFFFFFFFu);
        EXPECT_GT(lane.LeafOctantPower(slot, keyOctant), 0.0f);
        EXPECT_GT(s.radiance.x, 0.0f);
    }
    for (const SparseEmitterEntry& e : t.sparse) {
        const double expected = static_cast<double>(e.phi) / t.totals.phiSparse;
        EXPECT_NEAR(static_cast<double>(hits[e.instIdx]) / kN, expected, 0.01);
    }
    EXPECT_NEAR(sumInvPdf / kN, expectedArea, 0.03 * expectedArea);
}

TEST(EmitterSamplingSparse, PowerEstimatorReproducesTotalSparsePower) {
    // E[ pi * Y(radiance) / pdf ] is the sum of cluster powers: the lane's octant sums,
    // scaled per instance, must add back up to Phi_sparse.
    Scene scene(false, true);
    uint32_t rng = 99u;
    double acc = 0.0;
    constexpr uint32_t kN = 100000;
    for (uint32_t i = 0; i < kN; ++i) {
        const EmitterSampleCpu s = SampleEmitter(scene.cpu, rng);
        ASSERT_GT(s.pdf, 0.0f);
        acc += static_cast<double>(EmitterLuminance(s.radiance)) * kEmitterPi / s.pdf;
    }
    EXPECT_NEAR(acc / kN, scene.table.totals.phiSparse, 0.03 * scene.table.totals.phiSparse);
}

TEST(EmitterSamplingMixture, PipelineSplitFollowsPower) {
    Scene scene;
    const EmitterTotals& tot = scene.table.totals;
    ASSERT_GT(tot.phiRecipe, 0.0f);
    ASSERT_GT(tot.phiSparse, 0.0f);
    EXPECT_FLOAT_EQ(EmitterTotalPower(scene.cpu), tot.phiRecipe + tot.phiSparse);
    constexpr uint32_t kN = 200000;
    uint32_t recipeHits = 0u, sparseHits = 0u;
    double power = 0.0;
    uint32_t rng = 0xB0B5u;
    for (uint32_t i = 0; i < kN; ++i) {
        const EmitterSampleCpu s = SampleEmitter(scene.cpu, rng);
        ASSERT_GT(s.pdf, 0.0f);
        if ((s.key & 3u) == 1u) ++recipeHits; else ++sparseHits;
        power += static_cast<double>(EmitterLuminance(s.radiance)) * kEmitterPi / s.pdf;
    }
    const double expected = tot.phiRecipe / (tot.phiRecipe + tot.phiSparse);
    EXPECT_NEAR(static_cast<double>(recipeHits) / kN, expected, 0.01);
    EXPECT_EQ(recipeHits + sparseHits, kN);
    // Y*pi/pdf is an unbiased estimate of total emitted power (each emitter is a diffuse area).
    EXPECT_NEAR(power / kN, tot.phiRecipe + tot.phiSparse, 0.03 * (tot.phiRecipe + tot.phiSparse));
}

TEST(EmitterSamplingReplay, SeededSequenceIsDeterministicAndPinned) {
    Scene scene;
    EXPECT_EQ(HashSamples(scene, 0xC0FFEEu, 512u, true), HashSamples(scene, 0xC0FFEEu, 512u, true));
    EXPECT_NE(HashSamples(scene, 0xC0FFEEu, 512u, false), HashSamples(scene, 0xC0FFEFu, 512u, false));
    // Pinned over keys + pdf bits only (pure IEEE ops, no libm), so the value holds across
    // compilers. Guards the draw order / key layout shaders/EmitterSampling.glsl mirrors.
    const uint64_t keysAndPdf = HashSamples(scene, 0xC0FFEEu, 512u, false);
    constexpr uint64_t kPinned = 0x80e03dc3f6724b73ull;
    EXPECT_EQ(keysAndPdf, kPinned) << "pin value: 0x" << std::hex << keysAndPdf;
}

TEST(EmitterSamplingRng, NextFloatNeverReachesOne) {
    uint32_t rng = 1u;
    for (uint32_t i = 0; i < 2000000; ++i) {
        const float u = EmitterNext(rng);
        ASSERT_GE(u, 0.0f);
        ASSERT_LT(u, 1.0f);
    }
}

TEST(EmitterPHatMirror, MatchesClosedFormForAnalyticSphere) {
    Scene scene(true, false);
    uint32_t rng = 31337u;
    const glm::vec3 x(0.0f, 0.0f, 200.0f);
    const glm::vec3 n(0.0f, 0.0f, -1.0f);
    uint32_t checked = 0;
    for (uint32_t i = 0; i < 2000; ++i) {
        const EmitterSampleCpu s = SampleEmitter(scene.cpu, rng);
        const uint32_t inst = (s.key >> 2u) & 0xFFu;
        const BodyInstanceGpu& bi = scene.instances[inst];
        if (bi.material.recipeId != kGridRecipe) continue;
        const glm::vec3 centre(bi.transform.localToWorld.rows[0][3], bi.transform.localToWorld.rows[1][3],
                               bi.transform.localToWorld.rows[2][3]);
        const glm::vec3 d = s.position - x;
        const float dist2 = glm::dot(d, d);
        const glm::vec3 w = d / std::sqrt(dist2);
        const float cosR = std::max(glm::dot(n, w), 0.0f);
        const glm::vec3 en = glm::normalize(s.position - centre);
        const float cosE = std::max(glm::dot(en, -w), 0.0f);
        const float expected = EmitterLuminance(s.radiance) * cosR * cosE / dist2;
        EXPECT_NEAR(EmitterPHat(scene.cpu, s, x, n), expected, 2e-2f * expected + 1e-9f);
        ++checked;
    }
    EXPECT_GT(checked, 100u);
    EmitterSampleCpu none;
    EXPECT_EQ(EmitterPHat(scene.cpu, none, x, n), 0.0f);
}
