#include <gtest/gtest.h>

#include "Generated/ProbeGridConfig.g.h"

#include <cstddef>

using Vixen::Gpu::ProbeGridSettings;

TEST(ProbeGridConfigLayout, StructSizeAndOffsetsMatchGeneratedSettings) {
    EXPECT_EQ(sizeof(ProbeGridSettings), 52u);
    EXPECT_EQ(offsetof(ProbeGridSettings, probeGridEnabled), 0u);
    EXPECT_EQ(offsetof(ProbeGridSettings, originX), 4u);
    EXPECT_EQ(offsetof(ProbeGridSettings, originY), 8u);
    EXPECT_EQ(offsetof(ProbeGridSettings, originZ), 12u);
    EXPECT_EQ(offsetof(ProbeGridSettings, spacingX), 16u);
    EXPECT_EQ(offsetof(ProbeGridSettings, spacingY), 20u);
    EXPECT_EQ(offsetof(ProbeGridSettings, spacingZ), 24u);
    EXPECT_EQ(offsetof(ProbeGridSettings, countX), 28u);
    EXPECT_EQ(offsetof(ProbeGridSettings, countY), 32u);
    EXPECT_EQ(offsetof(ProbeGridSettings, countZ), 36u);
    EXPECT_EQ(offsetof(ProbeGridSettings, raysPerProbe), 40u);
    EXPECT_EQ(offsetof(ProbeGridSettings, hysteresisRate), 44u);
    EXPECT_EQ(offsetof(ProbeGridSettings, amortizationFactor), 48u);
}

TEST(ProbeGridConfigLayout, ValueInitializedIsAllZero) {
    const ProbeGridSettings settings{};
    const auto* bytes = reinterpret_cast<const unsigned char*>(&settings);
    for (std::size_t i = 0; i < sizeof(settings); ++i) EXPECT_EQ(bytes[i], 0u);
}
