#include <gtest/gtest.h>

#include "Generated/AccumulationConfig.g.h"

#include <cstddef>

TEST(AccumulationConfigSdiParity, GeneratedSettingsRegionsMatchHostRecord) {
    using Vixen::Gpu::AccumulationSettings;
    using Vixen::Gpu::AccumulationSettingsComponentRegions;

    EXPECT_EQ(sizeof(AccumulationSettings), 20u);
    ASSERT_EQ(AccumulationSettingsComponentRegions.size(), 5u);
    for (std::size_t i = 0; i < AccumulationSettingsComponentRegions.size(); ++i) {
        EXPECT_EQ(AccumulationSettingsComponentRegions[i].offset, i * sizeof(uint32_t));
        EXPECT_EQ(AccumulationSettingsComponentRegions[i].sourceOffset, i * sizeof(uint32_t));
    }
}
