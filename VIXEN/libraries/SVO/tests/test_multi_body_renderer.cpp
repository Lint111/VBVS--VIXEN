#include <limits>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "Recipe/MultiBodyRenderer.h"
#include "Recipe/RecipeRegistry.h"

using namespace Vixen::SVO;

namespace {

RecipeRegistry::RecipeEntry MakeRecipeEntry(std::vector<LodBand> ladder) {
    RecipeRegistry::RecipeEntry entry{};
    Recipe::SdfInstruction sphere{};
    sphere.opCode = static_cast<uint8_t>(Recipe::SdfOpCode::Sphere);
    sphere.data[3] = 1.0f;
    entry.bytecode = {sphere};
    entry.lodLadder = std::move(ladder);
    return entry;
}

std::vector<LodBand> FourBandLadder() {
    return {
        LodBand{1.0f, static_cast<uint8_t>(LodStrategy::MarchFull)},
        LodBand{2.0f, static_cast<uint8_t>(LodStrategy::MarchFull)},
        LodBand{3.0f, static_cast<uint8_t>(LodStrategy::Impostor)},
        LodBand{std::numeric_limits<float>::infinity(),
                static_cast<uint8_t>(LodStrategy::FarField)},
    };
}

} // namespace

TEST(CelestialBodyRegistry, UpsertSnapshotAndRemoveAreDeterministic) {
    CelestialBodyRegistry registry;
    EXPECT_EQ(registry.Upsert(CelestialBody{20, glm::dvec3(20.0, 0.0, 0.0), 2.0f, 1}),
              CelestialBodyUpdateResult::Added);
    EXPECT_EQ(registry.Upsert(CelestialBody{10, glm::dvec3(10.0, 0.0, 0.0), 1.0f, 1}),
              CelestialBodyUpdateResult::Added);
    EXPECT_EQ(registry.Upsert(CelestialBody{20, glm::dvec3(21.0, 0.0, 0.0), 2.0f, 2}),
              CelestialBodyUpdateResult::Updated);

    const auto snapshot = registry.Snapshot();
    ASSERT_EQ(snapshot.size(), 2u);
    EXPECT_EQ(snapshot[0].id, 10u);
    EXPECT_EQ(snapshot[1].id, 20u);
    EXPECT_DOUBLE_EQ(snapshot[1].position.x, 21.0);
    EXPECT_TRUE(registry.Remove(10));
    EXPECT_FALSE(registry.Remove(10));
    EXPECT_EQ(registry.Size(), 1u);
}

TEST(CelestialBodyRegistry, RejectsInvalidPlacement) {
    CelestialBodyRegistry registry;
    EXPECT_EQ(registry.Upsert(CelestialBody{1, {}, 0.0f, 1}),
              CelestialBodyUpdateResult::InvalidRadius);
    EXPECT_EQ(registry.Upsert(CelestialBody{1, glm::dvec3(std::numeric_limits<double>::infinity(), 0.0, 0.0), 1.0f, 1}),
              CelestialBodyUpdateResult::NonFinitePosition);
}

TEST(CelestialRenderListBuilder, BuildsPerBodyLodAndSortsNearToFar) {
    RecipeRegistry recipes;
    ASSERT_EQ(recipes.Register(1u, MakeRecipeEntry(FourBandLadder())), RecipeRegistry::RegisterResult::Ok);
    ASSERT_EQ(recipes.Register(2u, MakeRecipeEntry(FourBandLadder())), RecipeRegistry::RegisterResult::Ok);

    CelestialBodyRegistry bodies;
    ASSERT_EQ(bodies.Upsert(CelestialBody{20, glm::dvec3(100.0, 0.0, 0.0), 10.0f, 1}),
              CelestialBodyUpdateResult::Added);
    ASSERT_EQ(bodies.Upsert(CelestialBody{10, glm::dvec3(64.0, 0.0, 0.0), 1.0f, 2}),
              CelestialBodyUpdateResult::Added);

    CelestialRenderListBuilder builder(recipes);
    const CelestialRenderList list = builder.Build(
        bodies, glm::dvec3(0.0), LODParameters(0.0f, 0.1f));
    ASSERT_EQ(list.items.size(), 2u);
    EXPECT_EQ(list.items[0].bodyId, 10u);
    EXPECT_EQ(list.items[1].bodyId, 20u);
    EXPECT_LT(list.items[0].surfaceDistance, list.items[1].surfaceDistance);
    EXPECT_EQ(list.items[0].regime, BodyFootprintRegime::System);
    EXPECT_EQ(list.items[1].regime, BodyFootprintRegime::Orbital);
    EXPECT_NE(list.items[0].bandIndex, list.items[1].bandIndex);
    EXPECT_FLOAT_EQ(list.items[0].cameraRelativePosition.x, 64.0f);
}

TEST(CelestialRenderListBuilder, RegimeFloorAppliesIndependentlyPerBody) {
    RecipeRegistry recipes;
    ASSERT_EQ(recipes.Register(1u, MakeRecipeEntry(FourBandLadder())), RecipeRegistry::RegisterResult::Ok);

    CelestialBodyRegistry bodies;
    ASSERT_EQ(bodies.Upsert(CelestialBody{1, glm::dvec3(1.0, 0.0, 0.0), 1.0f, 1}),
              CelestialBodyUpdateResult::Added);
    ASSERT_EQ(bodies.Upsert(CelestialBody{2, glm::dvec3(512.0, 0.0, 0.0), 1.0f, 1}),
              CelestialBodyUpdateResult::Added);

    CelestialRenderListBuilder builder(recipes);
    const auto list = builder.Build(bodies, glm::dvec3(0.0), LODParameters{});
    ASSERT_EQ(list.items.size(), 2u);
    const auto& nearBody = list.items[0].bodyId == 1 ? list.items[0] : list.items[1];
    const auto& farBody = list.items[0].bodyId == 2 ? list.items[0] : list.items[1];
    EXPECT_EQ(nearBody.regime, BodyFootprintRegime::SurfaceDetail);
    EXPECT_EQ(farBody.regime, BodyFootprintRegime::DeepField);
    EXPECT_EQ(nearBody.bandIndex, 0u);
    EXPECT_EQ(farBody.bandIndex, 3u);
}

TEST(CelestialRenderListBuilder, SkipsBodiesAndReportsMissingRecipes) {
    RecipeRegistry recipes;
    auto skipLadder = FourBandLadder();
    skipLadder[0].maxQ = 0.1f;
    skipLadder[1].maxQ = 0.2f;
    skipLadder[2].maxQ = 0.3f;
    skipLadder[3].strategy = static_cast<uint8_t>(LodStrategy::Skip);
    skipLadder[3].uploadSet = 0;
    skipLadder[3].blockMask = 0;
    ASSERT_EQ(recipes.Register(1u, MakeRecipeEntry(std::move(skipLadder))), RecipeRegistry::RegisterResult::Ok);

    CelestialBodyRegistry bodies;
    ASSERT_EQ(bodies.Upsert(CelestialBody{1, glm::dvec3(1000.0, 0.0, 0.0), 1.0f, 1}),
              CelestialBodyUpdateResult::Added);
    ASSERT_EQ(bodies.Upsert(CelestialBody{2, glm::dvec3(2.0, 0.0, 0.0), 1.0f, 99}),
              CelestialBodyUpdateResult::Added);

    CelestialRenderListBuilder builder(recipes);
    const auto list = builder.Build(bodies, glm::dvec3(0.0), LODParameters(0.0f, 0.1f));
    EXPECT_TRUE(list.items.empty());
    ASSERT_EQ(list.skippedBodyIds.size(), 1u);
    EXPECT_EQ(list.skippedBodyIds[0], 1u);
    ASSERT_EQ(list.missingRecipeBodyIds.size(), 1u);
    EXPECT_EQ(list.missingRecipeBodyIds[0], 2u);
}
