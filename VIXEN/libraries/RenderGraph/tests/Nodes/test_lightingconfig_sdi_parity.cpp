#include <gtest/gtest.h>

#include "Generated/LightingConfig.g.h"

#include <array>
#include <cstddef>

TEST(LightingConfigSdiParity, GeneratedSettingsRegionsMatchHostRecord) {
    using Vixen::Gpu::LightingSettings;
    using Vixen::Gpu::LightingSettingsComponentRegions;

    EXPECT_EQ(sizeof(LightingSettings), 240u);
    ASSERT_EQ(LightingSettingsComponentRegions.size(), 18u);
    constexpr std::array<std::size_t, 18> expectedSourceOffsets{
        0u, 4u, 16u, 144u, 160u, 164u, 168u, 172u, 176u,
        180u, 184u, 188u, 192u, 196u, 200u, 208u, 220u, 224u};
    for (std::size_t i = 0; i < expectedSourceOffsets.size(); ++i) {
        EXPECT_EQ(LightingSettingsComponentRegions[i].offset, i * sizeof(uint32_t));
        EXPECT_EQ(LightingSettingsComponentRegions[i].sourceOffset, expectedSourceOffsets[i]);
    }
}
