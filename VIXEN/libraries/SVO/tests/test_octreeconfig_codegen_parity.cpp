#include <gtest/gtest.h>
#include <cstddef>
#include "ShellOctreeGpu.h"            // Vixen::SVO::OctreeConfig (current hand-written)
#include "Generated/OctreeConfig.g.h"  // Vixen::Gpu::OctreeConfig (generated from canonical)

// Proves the canonical [GpuStruct] regenerates today's exact 432 B layout.
namespace {

TEST(OctreeConfigCodegenParity, SizeMatches) {
    EXPECT_EQ(sizeof(Vixen::Gpu::OctreeConfig), sizeof(Vixen::SVO::OctreeConfig));
    EXPECT_EQ(sizeof(Vixen::Gpu::OctreeConfig), static_cast<size_t>(432));
}

#define PARITY_FIELD(field) \
    EXPECT_EQ(offsetof(Vixen::Gpu::OctreeConfig, field), offsetof(Vixen::SVO::OctreeConfig, field)) \
        << "offset drift for " #field

TEST(OctreeConfigCodegenParity, FieldOffsetsMatch) {
    PARITY_FIELD(esvoMaxScale);   PARITY_FIELD(userMaxLevels);
    PARITY_FIELD(brickDepthLevels); PARITY_FIELD(brickSize);
    PARITY_FIELD(minESVOScale);   PARITY_FIELD(brickESVOScale);
    PARITY_FIELD(bricksPerAxis);  PARITY_FIELD(_padding1);
    PARITY_FIELD(gridMinX); PARITY_FIELD(gridMinY); PARITY_FIELD(gridMinZ);
    PARITY_FIELD(gridMaxX); PARITY_FIELD(gridMaxY); PARITY_FIELD(gridMaxZ);
    PARITY_FIELD(localToWorld);   PARITY_FIELD(worldToLocal);
    PARITY_FIELD(nodeArrayBase);  PARITY_FIELD(brickArrayBase);
    PARITY_FIELD(formatId);       PARITY_FIELD(bricksPerAxisSdf);
    PARITY_FIELD(poolBrickBase);  PARITY_FIELD(channelCount);
    PARITY_FIELD(brickStrideFloats); PARITY_FIELD(_padChannels);
    PARITY_FIELD(channels);       PARITY_FIELD(_tailPad);
}

TEST(OctreeConfigCodegenParity, KeyAbsoluteOffsets) {
    EXPECT_EQ(offsetof(Vixen::Gpu::OctreeConfig, localToWorld),  static_cast<size_t>(64));
    EXPECT_EQ(offsetof(Vixen::Gpu::OctreeConfig, worldToLocal),  static_cast<size_t>(128));
    EXPECT_EQ(offsetof(Vixen::Gpu::OctreeConfig, nodeArrayBase), static_cast<size_t>(192));
    EXPECT_EQ(offsetof(Vixen::Gpu::OctreeConfig, formatId),      static_cast<size_t>(200));
    EXPECT_EQ(offsetof(Vixen::Gpu::OctreeConfig, brickStrideFloats), static_cast<size_t>(216));
    EXPECT_EQ(offsetof(Vixen::Gpu::OctreeConfig, channels),      static_cast<size_t>(224));
}

} // namespace
