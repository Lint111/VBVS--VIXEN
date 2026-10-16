#include <gtest/gtest.h>
#include "Data/ReconstructionDepth.h"

using namespace Vixen::RenderGraph;

TEST(ReconstructionDepth, ConsumerConventionsDeclareNearFarAndMissDepth) {
    struct Expected {
        ReconstructionDepthConvention convention;
        float nearDepth;
        float farDepth;
    };
    const Expected cases[] = {
        {ReconstructionDepthConvention::ForwardZeroToOne, 0.0f, 1.0f},
        {ReconstructionDepthConvention::ReversedZeroToOne, 1.0f, 0.0f},
        {ReconstructionDepthConvention::ForwardMinusOneToOne, -1.0f, 1.0f},
        {ReconstructionDepthConvention::ReversedMinusOneToOne, 1.0f, -1.0f},
    };
    for (const auto& expected : cases) {
        const auto encoding = ReconstructionDepthEncoding(expected.convention);
        EXPECT_FLOAT_EQ(encoding.y, expected.nearDepth);
        EXPECT_FLOAT_EQ(encoding.x + encoding.y, expected.farDepth);
        EXPECT_FLOAT_EQ(encoding.z, expected.farDepth); // Misses use the consumer's far plane.
        EXPECT_FLOAT_EQ(encoding.w, 1.0f); // Engine view depth is positive camera-forward.
    }
}
