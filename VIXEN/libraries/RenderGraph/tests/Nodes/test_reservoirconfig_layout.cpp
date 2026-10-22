#include <gtest/gtest.h>

#include "Generated/ReservoirConfig.g.h"

#include <cstddef>

using Vixen::Gpu::ReservoirSettings;

TEST(ReservoirConfigLayout, StructSizeAndOffsetsMatchGeneratedSettings) {
    EXPECT_EQ(sizeof(ReservoirSettings), 28u);
    EXPECT_EQ(offsetof(ReservoirSettings, reservoirEnabled), 0u);
    EXPECT_EQ(offsetof(ReservoirSettings, candidateCount), 4u);
    EXPECT_EQ(offsetof(ReservoirSettings, spatialRadius), 8u);
    EXPECT_EQ(offsetof(ReservoirSettings, spatialCount), 12u);
    EXPECT_EQ(offsetof(ReservoirSettings, temporalCap), 16u);
    EXPECT_EQ(offsetof(ReservoirSettings, biasedModeEnabled), 20u);
    EXPECT_EQ(offsetof(ReservoirSettings, lightTreeCutThreshold), 24u);
}

TEST(ReservoirConfigLayout, ValueInitializedIsAllZero) {
    const ReservoirSettings settings{};
    const auto* bytes = reinterpret_cast<const unsigned char*>(&settings);
    for (std::size_t i = 0; i < sizeof(settings); ++i) EXPECT_EQ(bytes[i], 0u);
}
