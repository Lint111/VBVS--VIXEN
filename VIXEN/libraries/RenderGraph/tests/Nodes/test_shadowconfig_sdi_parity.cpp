#include <gtest/gtest.h>

#include "Generated/ShadowConfig.g.h"

#include <cstddef>

TEST(ShadowConfigSdiParity, GeneratedSettingsRegionsMatchHostRecord) {
    using Vixen::Gpu::ShadowSettings;
    using Vixen::Gpu::ShadowSettingsComponentRegions;

    EXPECT_EQ(sizeof(ShadowSettings), 16u);
    ASSERT_EQ(ShadowSettingsComponentRegions.size(), 4u);
    for (std::size_t i = 0; i < ShadowSettingsComponentRegions.size(); ++i) {
        EXPECT_EQ(ShadowSettingsComponentRegions[i].offset, i * sizeof(uint32_t));
        EXPECT_EQ(ShadowSettingsComponentRegions[i].sourceOffset, i * sizeof(uint32_t));
    }
}
