#include <gtest/gtest.h>
#include "VoxelChannelFormat.h"
#include "ShellOctreeGpu.h"
using namespace Vixen::SVO;
TEST(ChannelFormat, EnumValuesAreStable) {
    EXPECT_EQ(SEM_SDF,0u); EXPECT_EQ(SEM_COLOR,1u); EXPECT_EQ(SEM_ROUGHNESS,2u);
    EXPECT_EQ(SEM_DENSITY,6u);
    EXPECT_EQ(FK_DISTANCE,1u); EXPECT_EQ(FK_DENSITY,2u);
    EXPECT_EQ(SemanticElemCount(SEM_COLOR),3u); EXPECT_EQ(SemanticElemCount(SEM_SDF),1u);
}
TEST(ChannelFormat, OctreeConfigStillMatchesShaderStride) {
    EXPECT_EQ(sizeof(OctreeConfig), 432u);
}
