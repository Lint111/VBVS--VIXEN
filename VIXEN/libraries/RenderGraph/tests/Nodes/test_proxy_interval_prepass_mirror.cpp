// test_proxy_interval_prepass_mirror.cpp — Raster-proxy B2 CPU mirror.
//
// Pins the compact per-pixel contract shared by ProxyIntervalPrepass.frag and
// BodyInstanceRayMarch.comp: union [tEnter,tExit] plus a 192-bit candidate mask.
// The mask indexes the existing front-to-back BodyInstanceGpu order.

#include <gtest/gtest.h>
#include "Nodes/ProxyIntervalPrepassMirror.h"

#include <glm/glm.hpp>

#include <array>
#include <cstdint>

using namespace Vixen::RenderGraph::Mirror;

TEST(ProxyIntervalPrepassMirror, PixelRecordIsExactlyThirtyTwoBytes) {
    EXPECT_EQ(sizeof(ProxyIntervalPixel), 32u);
    EXPECT_EQ(kProxyCandidateWordCount, 6u);
    EXPECT_EQ(kProxyCandidateLimit, 192u);
}

TEST(ProxyIntervalPrepassMirror, ClearRecordHasNoCandidates) {
    const ProxyIntervalPixel pixel = ClearProxyIntervalPixel();
    EXPECT_FALSE(HasProxyCandidates(pixel));
    for (uint32_t word : pixel.candidateMask) EXPECT_EQ(word, 0u);
}

TEST(ProxyIntervalPrepassMirror, UnionKeepsNearestEntryAndFarthestExit) {
    ProxyIntervalPixel pixel = ClearProxyIntervalPixel();
    EXPECT_TRUE(AccumulateProxyInterval(pixel, 7u, 4.0f, 9.0f));
    EXPECT_TRUE(AccumulateProxyInterval(pixel, 2u, 1.5f, 3.0f));
    EXPECT_TRUE(AccumulateProxyInterval(pixel, 63u, 6.0f, 12.0f));

    EXPECT_FLOAT_EQ(DecodeProxyEnter(pixel), 1.5f);
    EXPECT_FLOAT_EQ(DecodeProxyExit(pixel), 12.0f);
    EXPECT_TRUE(IsProxyCandidate(pixel, 2u));
    EXPECT_TRUE(IsProxyCandidate(pixel, 7u));
    EXPECT_TRUE(IsProxyCandidate(pixel, 63u));
    EXPECT_FALSE(IsProxyCandidate(pixel, 3u));
}

TEST(ProxyIntervalPrepassMirror, CandidateBitsSpanAllSixWords) {
    ProxyIntervalPixel pixel = ClearProxyIntervalPixel();
    constexpr std::array<uint32_t, 12> kIndices = {
        0u, 31u, 32u, 63u, 64u, 95u, 96u, 127u, 128u, 159u, 160u, 191u};

    for (uint32_t index : kIndices) {
        ASSERT_TRUE(AccumulateProxyInterval(pixel, index, 1.0f, 2.0f));
    }
    for (uint32_t index : kIndices) EXPECT_TRUE(IsProxyCandidate(pixel, index));
    EXPECT_EQ(pixel.candidateMask[0], 0x80000001u);
    EXPECT_EQ(pixel.candidateMask[5], 0x80000001u);
}

TEST(ProxyIntervalPrepassMirror, CandidateLimitRejectsUnrepresentableInstance) {
    ProxyIntervalPixel pixel = ClearProxyIntervalPixel();
    EXPECT_FALSE(AccumulateProxyInterval(pixel, 192u, 1.0f, 2.0f));
    EXPECT_FALSE(HasProxyCandidates(pixel));
}

TEST(ProxyIntervalPrepassMirror, MissDoesNotMutateRecord) {
    ProxyIntervalPixel pixel = ClearProxyIntervalPixel();
    const ProxyIntervalPixel before = pixel;
    const ProxyRay ray{glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 1.0f)};

    EXPECT_FALSE(AccumulateProxyAabb(pixel, 4u, ray,
                                     glm::vec3(2.0f, 2.0f, -3.0f),
                                     glm::vec3(3.0f, 3.0f, -2.0f)));
    EXPECT_EQ(pixel.entryKey, before.entryKey);
    EXPECT_EQ(pixel.exitBits, before.exitBits);
    for (uint32_t word = 0; word < kProxyCandidateWordCount; ++word) {
        EXPECT_EQ(pixel.candidateMask[word], before.candidateMask[word]);
    }
}

TEST(ProxyIntervalPrepassMirror, ParallelRayHitsWhenOriginIsInsideSlab) {
    ProxyIntervalPixel pixel = ClearProxyIntervalPixel();
    const ProxyRay ray{glm::vec3(0.5f, 0.5f, -2.0f), glm::vec3(0.0f, 0.0f, 1.0f)};

    ASSERT_TRUE(AccumulateProxyAabb(pixel, 5u, ray,
                                    glm::vec3(0.0f), glm::vec3(1.0f)));
    EXPECT_FLOAT_EQ(DecodeProxyEnter(pixel), 2.0f);
    EXPECT_FLOAT_EQ(DecodeProxyExit(pixel), 3.0f);
}

TEST(ProxyIntervalPrepassMirror, ParallelRayMissesWhenOriginIsOutsideSlab) {
    ProxyIntervalPixel pixel = ClearProxyIntervalPixel();
    const ProxyRay ray{glm::vec3(1.5f, 0.5f, -2.0f), glm::vec3(0.0f, 0.0f, 1.0f)};

    EXPECT_FALSE(AccumulateProxyAabb(pixel, 5u, ray,
                                     glm::vec3(0.0f), glm::vec3(1.0f)));
    EXPECT_FALSE(HasProxyCandidates(pixel));
}

TEST(ProxyIntervalPrepassMirror, CameraInsideBoxClampsUnionEntryToZero) {
    ProxyIntervalPixel pixel = ClearProxyIntervalPixel();
    const ProxyRay ray{glm::vec3(0.5f), glm::normalize(glm::vec3(1.0f, 2.0f, 3.0f))};

    ASSERT_TRUE(AccumulateProxyAabb(pixel, 1u, ray,
                                    glm::vec3(0.0f), glm::vec3(1.0f)));
    EXPECT_FLOAT_EQ(DecodeProxyEnter(pixel), 0.0f);
    EXPECT_GT(DecodeProxyExit(pixel), 0.0f);
}

TEST(ProxyIntervalPrepassMirror, InvalidOrBehindIntervalsAreIgnored) {
    ProxyIntervalPixel pixel = ClearProxyIntervalPixel();
    EXPECT_FALSE(AccumulateProxyInterval(pixel, 1u, 4.0f, 3.0f));
    EXPECT_FALSE(AccumulateProxyInterval(pixel, 1u, -4.0f, -1.0f));
    EXPECT_FALSE(HasProxyCandidates(pixel));
}
