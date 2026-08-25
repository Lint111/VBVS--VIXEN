#include <limits>
#include <vector>

#include <gtest/gtest.h>

#include "Recipe/FootprintRegime.h"
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

TEST(FootprintRegime, TransitionsAtDistanceToRadiusBoundaries) {
    EXPECT_EQ(ClassifyFootprintRegime(0.0f, 1.0f), FootprintRegime::SurfaceDetail);
    EXPECT_EQ(ClassifyFootprintRegime(8.0f, 1.0f), FootprintRegime::Orbital);
    EXPECT_EQ(ClassifyFootprintRegime(64.0f, 1.0f), FootprintRegime::System);
    EXPECT_EQ(ClassifyFootprintRegime(512.0f, 1.0f), FootprintRegime::DeepField);
}

TEST(FootprintRegime, ScaleInvariantAcrossBodySizes) {
    EXPECT_EQ(ClassifyFootprintRegime(80.0f, 10.0f), FootprintRegime::Orbital);
    EXPECT_EQ(ClassifyFootprintRegime(640.0f, 10.0f), FootprintRegime::System);
    EXPECT_EQ(ClassifyFootprintRegime(5120.0f, 10.0f), FootprintRegime::DeepField);
}

TEST(FootprintRegime, InvalidRadiusFailsClosedToDeepField) {
    EXPECT_EQ(ClassifyFootprintRegime(1.0f, 0.0f), FootprintRegime::DeepField);
    EXPECT_EQ(ClassifyFootprintRegime(1.0f, -1.0f), FootprintRegime::DeepField);
    EXPECT_EQ(ClassifyFootprintRegime(1.0f, std::numeric_limits<float>::infinity()),
              FootprintRegime::DeepField);
    EXPECT_EQ(ClassifyFootprintRegime(std::numeric_limits<float>::infinity(), 1.0f),
              FootprintRegime::DeepField);
}

TEST(FootprintRegime, DistanceNeverSelectsFinerRegime) {
    const float distances[] = {0.0f, 7.9f, 8.0f, 63.9f, 64.0f, 511.9f, 512.0f, 10000.0f};
    unsigned int previous = static_cast<unsigned int>(FootprintRegime::SurfaceDetail);
    for (float distance : distances) {
        const auto current = static_cast<unsigned int>(
            ClassifyFootprintRegime(distance, 1.0f));
        EXPECT_GE(current, previous) << "distance=" << distance;
        previous = current;
    }
}

TEST(FootprintRegime, RegimeProvidesConservativeLodBandFloor) {
    const auto ladder = FourBandLadder();
    EXPECT_EQ(SelectLodBandForRegime(ladder, 0.0f, FootprintRegime::SurfaceDetail), 0u);
    EXPECT_EQ(SelectLodBandForRegime(ladder, 0.0f, FootprintRegime::Orbital), 1u);
    EXPECT_EQ(SelectLodBandForRegime(ladder, 0.0f, FootprintRegime::System), 2u);
    EXPECT_EQ(SelectLodBandForRegime(ladder, 0.0f, FootprintRegime::DeepField), 3u);

    // q still selects a coarser band inside the permitted regime range.
    EXPECT_EQ(SelectLodBandForRegime(ladder, 2.0f, FootprintRegime::Orbital), 2u);
    EXPECT_EQ(SelectLodBand(ladder, 0.0f, 64.0f, 1.0f), 2u);
}

TEST(FootprintRegime, ShortLaddersClampTheRegimeFloor) {
    const std::vector<LodBand> ladder = {
        LodBand{1.0f, static_cast<uint8_t>(LodStrategy::MarchFull)},
        LodBand{std::numeric_limits<float>::infinity(),
                static_cast<uint8_t>(LodStrategy::FarField)},
    };
    EXPECT_EQ(SelectLodBandForRegime(ladder, 0.0f, FootprintRegime::System), 1u);
    EXPECT_EQ(SelectLodBandForRegime(ladder, 0.0f, FootprintRegime::DeepField), 1u);
}

TEST(FootprintRegime, TransitionCannotHystereseBelowRegimeFloor) {
    const auto ladder = FourBandLadder();
    LodTransition transition;

    EXPECT_EQ(transition.Update(ladder, 0.0f).bandIndex, 0u);
    const auto deepField = transition.Update(ladder, 0.0f, 512.0f, 1.0f);
    EXPECT_EQ(deepField.bandIndex, 3u);
    EXPECT_TRUE(deepField.changed);
}
