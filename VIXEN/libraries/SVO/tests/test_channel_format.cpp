#include <gtest/gtest.h>

// MSVC's <windows.h> (pulled in transitively via ShellOctreeGpu.h -> SdfBake.h
// -> GaiaVoxelWorld.h) defines min/max as macros, colliding with std::max in
// SdfRecipes.h — same class of pre-existing gap fixed in SdfRecipeEval.h and
// every other SVO test file's own top-of-file #undef guard.
#undef far
#undef near
#undef min
#undef max

#include "VoxelChannelFormat.h"
#include "ShellOctreeGpu.h"
using namespace Vixen::SVO;
TEST(ChannelFormat, EnumValuesAreStable) {
    EXPECT_EQ(SEM_SDF,0u); EXPECT_EQ(SEM_COLOR,1u); EXPECT_EQ(SEM_ROUGHNESS,2u);
    EXPECT_EQ(SEM_DENSITY,6u);
    EXPECT_EQ(FK_DISTANCE,1u); EXPECT_EQ(FK_DENSITY,2u);
    EXPECT_EQ(SemanticElemCount(SEM_COLOR),3u); EXPECT_EQ(SemanticElemCount(SEM_SDF),1u);
    EXPECT_EQ(SEM_EMISSION,5u);
    // Sampled Lighting Inc3 M3: emissive intensity is SCALAR (RGB tint deferred to spectral).
    EXPECT_EQ(SemanticElemCount(SEM_EMISSION),1u);
}
TEST(ChannelFormat, OctreeConfigStillMatchesShaderStride) {
    EXPECT_EQ(sizeof(OctreeConfig), 432u);
}
