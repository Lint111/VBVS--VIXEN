#include <gtest/gtest.h>
#include "Data/ReconstructionJitter.h"
#include <set>

using namespace Vixen::RenderGraph;

TEST(ReconstructionJitter, FixedSequenceInInputPixelUnitsRestartsWithHistory) {
    const auto first = ReconstructionJitter(0, ReconstructionSamplingPath::InteractiveVisual);
    EXPECT_FLOAT_EQ(first.x, 0.0f);
    EXPECT_NEAR(first.y, -1.0f / 6.0f, 1e-7f);
    std::set<std::pair<float, float>> samples;
    for (uint32_t frame = 0; frame < 16; ++frame) {
        const auto sample = ReconstructionJitter(frame, ReconstructionSamplingPath::InteractiveVisual);
        EXPECT_GE(sample.x, -0.5f); EXPECT_LT(sample.x, 0.5f);
        EXPECT_GE(sample.y, -0.5f); EXPECT_LT(sample.y, 0.5f);
        EXPECT_EQ(sample, ReconstructionJitter(frame + 16, ReconstructionSamplingPath::InteractiveVisual));
        samples.emplace(sample.x, sample.y);
    }
    EXPECT_EQ(samples.size(), 16u);
    EXPECT_EQ(first, ReconstructionJitter(0, ReconstructionSamplingPath::InteractiveVisual));
}

TEST(ReconstructionJitter, CanonicalParityAndPickingAreAlwaysUnjittered) {
    for (auto path : {ReconstructionSamplingPath::CanonicalCapture,
                      ReconstructionSamplingPath::Parity,
                      ReconstructionSamplingPath::AuthoritativePicking})
        for (uint32_t frame = 0; frame < 32; ++frame)
            EXPECT_EQ(ReconstructionJitter(frame, path), glm::vec2(0.0f));
}
