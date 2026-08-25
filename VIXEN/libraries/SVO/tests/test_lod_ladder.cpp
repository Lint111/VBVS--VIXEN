#include <limits>
#include <vector>

#include <gtest/gtest.h>

#include "Recipe/LodSelection.h"
#include "Recipe/RecipeRegistry.h"

using namespace Vixen::SVO;

namespace {

std::vector<LodBand> TwoBandLadder() {
    return {
        LodBand{0.5f, static_cast<uint8_t>(LodStrategy::MarchFull), 0, 0xFFFFu,
                static_cast<uint8_t>(LodParamTier::Full), kAllLodUploadUnits},
        LodBand{std::numeric_limits<float>::infinity(), static_cast<uint8_t>(LodStrategy::Skip),
                0, 0, static_cast<uint8_t>(LodParamTier::Half), 0},
    };
}

} // namespace

TEST(LodLadder, SelectsByNormalizedFootprint) {
    const auto ladder = TwoBandLadder();
    EXPECT_EQ(SelectLodBand(ladder, 0.49f), 0u);
    EXPECT_EQ(SelectLodBand(ladder, 0.5f), 1u);
    EXPECT_EQ(SelectLodBand(ladder, 100.0f), 1u);
}

TEST(LodLadder, ComputesConservativeDistanceFootprint) {
    const LODParameters camera(0.0f, 0.1f);
    EXPECT_FLOAT_EQ(ComputeLodQ(10.0f, 2.0f, camera), 0.25f);
    EXPECT_FLOAT_EQ(
        ComputeLodQ(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 12.0f), 2.0f, camera),
        0.25f);
}

TEST(LodLadder, ValidatesFinalInfinityAndMonotoneUploads) {
    auto ladder = TwoBandLadder();
    EXPECT_EQ(ValidateLodLadder(ladder), LodValidationError::None);

    ladder.back().maxQ = 2.0f;
    EXPECT_EQ(ValidateLodLadder(ladder), LodValidationError::FinalBandMustBeInfinite);

    ladder.back().maxQ = std::numeric_limits<float>::infinity();
    ladder.front().uploadSet = LodUploadBit(LodUploadUnit::Instance);
    ladder.back().uploadSet = LodUploadBit(LodUploadUnit::Instance);
    EXPECT_EQ(ValidateLodLadder(ladder), LodValidationError::None);
    ladder.front().uploadSet = LodUploadBit(LodUploadUnit::Instance);
    ladder.back().uploadSet = LodUploadBit(LodUploadUnit::Instance) |
                              LodUploadBit(LodUploadUnit::Bricks);
    EXPECT_EQ(ValidateLodLadder(ladder), LodValidationError::UploadSetNotMonotone);
}

TEST(LodLadder, RegistryStoresImplicitOneBandAndNormalizesLegacyThresholds) {
    RecipeRegistry reg;
    RecipeRegistry::RecipeEntry entry{};
    Recipe::SdfInstruction sphere{};
    sphere.opCode = static_cast<uint8_t>(Recipe::SdfOpCode::Sphere);
    sphere.data[3] = 1.0f;
    entry.bytecode = {sphere};

    ASSERT_EQ(reg.Register(1u, entry), RecipeRegistry::RegisterResult::Ok);
    ASSERT_NE(reg.Get(1u), nullptr);
    ASSERT_EQ(reg.Get(1u)->lodLadder.size(), 1u);
    EXPECT_EQ(reg.Get(1u)->lodLadder.front().maxQ, std::numeric_limits<float>::infinity());

    entry.boundRadius = 2.0f;
    entry.gateFootprintThreshold = 4.0f;
    ASSERT_EQ(reg.Register(2u, entry), RecipeRegistry::RegisterResult::Ok);
    ASSERT_EQ(reg.Get(2u)->lodLadder.size(), 2u);
    EXPECT_FLOAT_EQ(reg.Get(2u)->lodLadder.front().maxQ, 1.0f);
    EXPECT_EQ(reg.Get(2u)->lodLadder.back().strategy,
              static_cast<uint8_t>(LodStrategy::Skip));
}

TEST(LodLadder, RejectsInvalidExplicitLadders) {
    RecipeRegistry reg;
    RecipeRegistry::RecipeEntry entry{};
    Recipe::SdfInstruction sphere{};
    sphere.opCode = static_cast<uint8_t>(Recipe::SdfOpCode::Sphere);
    entry.bytecode = {sphere};

    entry.lodLadder = {
        LodBand{1.0f, static_cast<uint8_t>(LodStrategy::MarchFull)},
        LodBand{0.5f, static_cast<uint8_t>(LodStrategy::Skip)},
    };
    EXPECT_EQ(reg.Register(3u, entry), RecipeRegistry::RegisterResult::BadLodLadderOrdering);
}

TEST(LodTransition, UsesDeadZoneAndThreeFrameDwell) {
    const auto ladder = TwoBandLadder();
    LodTransition transition;

    EXPECT_EQ(transition.Update(ladder, 0.25f).bandIndex, 0u);
    EXPECT_EQ(transition.Update(ladder, 0.52f).bandIndex, 0u); // inside +10% dead zone
    EXPECT_EQ(transition.Update(ladder, 0.56f).bandIndex, 0u);
    EXPECT_EQ(transition.Update(ladder, 0.56f).bandIndex, 0u);
    EXPECT_EQ(transition.Update(ladder, 0.56f).bandIndex, 1u);

    EXPECT_EQ(transition.Update(ladder, 0.49f).bandIndex, 1u); // finer edge dead zone
    EXPECT_EQ(transition.Update(ladder, 0.43f).bandIndex, 1u);
    EXPECT_EQ(transition.Update(ladder, 0.43f).bandIndex, 1u);
    EXPECT_EQ(transition.Update(ladder, 0.43f).bandIndex, 0u);
}

TEST(LodTransition, BlendWindowProducesCoarserWeight) {
    const auto ladder = TwoBandLadder();
    const auto below = ComputeLodBlend(ladder, 0.4f);
    EXPECT_FALSE(below.active);

    const auto atEdge = ComputeLodBlend(ladder, 0.5f);
    EXPECT_TRUE(atEdge.active);
    EXPECT_EQ(atEdge.finerBand, 0u);
    EXPECT_EQ(atEdge.coarserBand, 1u);
    EXPECT_NEAR(atEdge.coarserWeight, 0.5f, 0.01f);
}

TEST(LodTransition, UploadGateHonorsSkipAndReentry) {
    LodBand skip{};
    skip.strategy = static_cast<uint8_t>(LodStrategy::Skip);
    skip.uploadSet = 0;
    skip.blockMask = 0;
    EXPECT_FALSE(GateLodUploads(skip).emitInstance);
    EXPECT_TRUE(GateLodUploads(skip, true).emitInstance);

    LodBand visible{};
    visible.strategy = static_cast<uint8_t>(LodStrategy::Impostor);
    visible.uploadSet = 0;
    EXPECT_NE(GateLodUploads(visible, false, true).uploadSet &
                  LodUploadBit(LodUploadUnit::Instance),
              0u);
}
