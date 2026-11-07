// test_sdf_trace_probe.cpp - T-2895: CPU decoder for the shared SDF trace probe contract,
// plus a text check that shaders/SdfTraceProbe.glsl and SdfTraceProbe.h agree on the layout.
#include <gtest/gtest.h>
#include <bit>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>
#include "SdfTraceProbe.h"

#ifndef VIXEN_SHADERS_DIR
#  error "VIXEN_SHADERS_DIR must be defined via CMake compile_definitions"
#endif

using namespace Vixen::SVO;

namespace {
uint32_t Bits(float v) { return std::bit_cast<uint32_t>(v); }

std::string ReadShader(const char* name) {
    std::ifstream in(std::string(VIXEN_SHADERS_DIR) + "/" + name);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}
}  // namespace

TEST(SdfTraceProbe, DecodesEveryWordOfARecord) {
    uint32_t words[kSdfTraceProbeWords] = {
        37u, uint32_t(SdfTraceTermination::Hit), 5u, 8u, Bits(1.5f), Bits(-0.25f),
        Bits(1.f), Bits(2.f), Bits(3.f), Bits(0.f), Bits(1.f), Bits(0.f),
        Bits(0.75f), Bits(0.5f), Bits(4.f), Bits(0.125f)};
    const SdfTraceRecord r = DecodeSdfTraceRecord(words);
    EXPECT_EQ(r.steps, 37u);
    EXPECT_TRUE(r.Hit());
    EXPECT_EQ(r.recipeId, 5u);
    EXPECT_EQ(r.gridDim, 8u);
    EXPECT_FLOAT_EQ(r.t, 1.5f);
    EXPECT_FLOAT_EQ(r.sdf, -0.25f);
    EXPECT_FLOAT_EQ(r.position[2], 3.f);
    EXPECT_FLOAT_EQ(r.gradient[1], 1.f);
    EXPECT_FLOAT_EQ(r.gridBound, 0.75f);
    EXPECT_FLOAT_EQ(r.tNear, 0.5f);
    EXPECT_FLOAT_EQ(r.tFar, 4.f);
    EXPECT_FLOAT_EQ(r.lastStep, 0.125f);
}

TEST(SdfTraceProbe, DecodesRowMajorProbeBufferAndSummarizesSteps) {
    std::vector<uint32_t> words(4 * kSdfTraceProbeWords, 0u);
    const uint32_t steps[4] = {0u, 10u, 20u, 30u};
    for (size_t i = 0; i < 4; ++i) words[i * kSdfTraceProbeWords] = steps[i];
    const auto traces = DecodeSdfTraceProbe(words.data(), 4);
    ASSERT_EQ(traces.size(), 4u);
    const SdfStepDistribution stats = SummarizeSdfSteps(traces);
    EXPECT_EQ(stats.activePixels, 3u);
    EXPECT_EQ(stats.maximum, 30u);
    EXPECT_DOUBLE_EQ(stats.mean, 15.0);
    EXPECT_EQ(SdfTraceProbeBufferBytes(2, 2), 4u * 64u);
}

TEST(SdfTraceProbe, ShaderIncludeMatchesCppLayout) {
    const std::string glsl = ReadShader("SdfTraceProbe.glsl");
    ASSERT_FALSE(glsl.empty());
    EXPECT_NE(glsl.find("#define SDF_TRACE_PROBE_BINDING " + std::to_string(kSdfTraceProbeBinding)),
              std::string::npos);
    EXPECT_NE(glsl.find("#define SDF_TRACE_PROBE_WORDS   " + std::to_string(kSdfTraceProbeWords) + "u"),
              std::string::npos);
    EXPECT_NE(glsl.find("SDF_TRACE_TERM_HIT        " + std::to_string(uint32_t(SdfTraceTermination::Hit)) + "u"),
              std::string::npos);
    EXPECT_NE(glsl.find("SDF_TRACE_TERM_EXIT       " + std::to_string(uint32_t(SdfTraceTermination::Exit)) + "u"),
              std::string::npos);
    // Every word offset 0..15 is written exactly once.
    for (uint32_t w = 0; w < kSdfTraceProbeWords; ++w) {
        const std::string needle = "sdfTraceProbeWords[base + " + std::to_string(w) + "u] =";
        const size_t first = glsl.find(needle);
        ASSERT_NE(first, std::string::npos) << "word " << w;
        EXPECT_EQ(glsl.find(needle, first + 1), std::string::npos) << "word " << w;
    }
}

TEST(SdfTraceProbe, RecipeLibraryIncludesProbeAndUsesNamedTerminations) {
    const std::string recipes = ReadShader("SdfRecipes.glsl");
    EXPECT_NE(recipes.find("#include \"SdfTraceProbe.glsl\""), std::string::npos);
    EXPECT_EQ(recipes.find("VIXEN_RIMLEAK_TRACE"), std::string::npos);
    EXPECT_EQ(recipes.find("rimleakRecordSdfTrace"), std::string::npos);
}
