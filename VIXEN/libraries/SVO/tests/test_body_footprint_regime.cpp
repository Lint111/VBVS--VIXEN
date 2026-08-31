// test_body_footprint_regime.cpp — body-scale rendering policy and LOD-floor proofs.

#include <limits>
#include <vector>

#include <gtest/gtest.h>

#include "Recipe/BodyFootprintRegime.h"
#include "Recipe/LodSelection.h"

using namespace Vixen::SVO;

namespace {

std::vector<LodBand> FourBandLadder() {
    return {
        LodBand{1.0f, static_cast<uint8_t>(LodStrategy::MarchFull)},
        LodBand{2.0f, static_cast<uint8_t>(LodStrategy::MarchVariant)},
        LodBand{3.0f, static_cast<uint8_t>(LodStrategy::Impostor)},
        LodBand{std::numeric_limits<float>::infinity(),
                static_cast<uint8_t>(LodStrategy::FarField)},
    };
}

} // namespace

TEST(BodyFootprintRegime, TransitionsAtDistanceToRadiusBoundaries) {
    EXPECT_EQ(ClassifyBodyFootprintRegime(0.0f, 1.0f), BodyFootprintRegime::SurfaceDetail);
    EXPECT_EQ(ClassifyBodyFootprintRegime(8.0f, 1.0f), BodyFootprintRegime::Orbital);
    EXPECT_EQ(ClassifyBodyFootprintRegime(64.0f, 1.0f), BodyFootprintRegime::System);
    EXPECT_EQ(ClassifyBodyFootprintRegime(512.0f, 1.0f), BodyFootprintRegime::DeepField);
}

TEST(BodyFootprintRegime, ScaleInvariantAcrossBodySizes) {
    EXPECT_EQ(ClassifyBodyFootprintRegime(80.0f, 10.0f), BodyFootprintRegime::Orbital);
    EXPECT_EQ(ClassifyBodyFootprintRegime(640.0f, 10.0f), BodyFootprintRegime::System);
    EXPECT_EQ(ClassifyBodyFootprintRegime(5120.0f, 10.0f), BodyFootprintRegime::DeepField);
}

TEST(BodyFootprintRegime, InvalidRadiusFailsClosedToDeepField) {
    EXPECT_EQ(ClassifyBodyFootprintRegime(1.0f, 0.0f), BodyFootprintRegime::DeepField);
    EXPECT_EQ(ClassifyBodyFootprintRegime(1.0f, -1.0f), BodyFootprintRegime::DeepField);
    EXPECT_EQ(ClassifyBodyFootprintRegime(1.0f, std::numeric_limits<float>::infinity()),
              BodyFootprintRegime::DeepField);
    EXPECT_EQ(ClassifyBodyFootprintRegime(std::numeric_limits<float>::infinity(), 1.0f),
              BodyFootprintRegime::DeepField);
}

TEST(BodyFootprintRegime, DistanceNeverSelectsFinerRegime) {
    const float distances[] = {0.0f, 7.9f, 8.0f, 63.9f, 64.0f, 511.9f, 512.0f, 10000.0f};
    unsigned int previous = static_cast<unsigned int>(BodyFootprintRegime::SurfaceDetail);
    for (float distance : distances) {
        const auto current = static_cast<unsigned int>(
            ClassifyBodyFootprintRegime(distance, 1.0f));
        EXPECT_GE(current, previous) << "distance=" << distance;
        previous = current;
    }
}

TEST(BodyFootprintRegime, RegimeProvidesConservativeLodBandFloor) {
    const auto ladder = FourBandLadder();
    EXPECT_EQ(SelectLodBandForRegime(ladder, 0.0f, BodyFootprintRegime::SurfaceDetail), 0u);
    EXPECT_EQ(SelectLodBandForRegime(ladder, 0.0f, BodyFootprintRegime::Orbital), 1u);
    EXPECT_EQ(SelectLodBandForRegime(ladder, 0.0f, BodyFootprintRegime::System), 2u);
    EXPECT_EQ(SelectLodBandForRegime(ladder, 0.0f, BodyFootprintRegime::DeepField), 3u);

    // q still selects a coarser band inside the permitted regime range.
    EXPECT_EQ(SelectLodBandForRegime(ladder, 2.0f, BodyFootprintRegime::Orbital), 2u);
    EXPECT_EQ(SelectLodBand(ladder, 0.0f, 64.0f, 1.0f), 2u);
}

TEST(BodyFootprintRegime, ShortLaddersClampTheRegimeFloor) {
    const std::vector<LodBand> ladder = {
        LodBand{1.0f, static_cast<uint8_t>(LodStrategy::MarchFull)},
        LodBand{std::numeric_limits<float>::infinity(),
                static_cast<uint8_t>(LodStrategy::FarField)},
    };
    EXPECT_EQ(SelectLodBandForRegime(ladder, 0.0f, BodyFootprintRegime::System), 1u);
    EXPECT_EQ(SelectLodBandForRegime(ladder, 0.0f, BodyFootprintRegime::DeepField), 1u);
}

TEST(BodyFootprintRegime, TransitionCannotHystereseBelowRegimeFloor) {
    const auto ladder = FourBandLadder();
    LodTransition transition;

    EXPECT_EQ(transition.Update(ladder, 0.0f).bandIndex, 0u);
    const auto deepField = transition.Update(ladder, 0.0f, 512.0f, 1.0f);
    EXPECT_EQ(deepField.bandIndex, 3u);
    EXPECT_TRUE(deepField.changed);
}
