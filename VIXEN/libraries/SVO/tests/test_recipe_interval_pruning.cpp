#include <gtest/gtest.h>

#include "Recipe/RecipeServices.h"

#include <array>
#include <bit>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

using namespace Vixen::SVO;
using namespace Vixen::SVO::Recipe;

namespace {

SdfInstruction Sphere(float x, float y, float z, float radius) {
    SdfInstruction instruction{};
    instruction.opCode = static_cast<std::uint8_t>(SdfOpCode::Sphere);
    instruction.data[0] = x;
    instruction.data[1] = y;
    instruction.data[2] = z;
    instruction.data[3] = radius;
    return instruction;
}

SdfInstruction Combine(SdfOpCode opcode) {
    SdfInstruction instruction{};
    instruction.opCode = static_cast<std::uint8_t>(opcode);
    return instruction;
}

std::uint64_t HashBytes(const void* data, std::size_t size) {
    auto hash = std::uint64_t{1469598103934665603ull};
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    for (std::size_t index = 0; index < size; ++index) {
        hash ^= bytes[index];
        hash *= std::uint64_t{1099511628211ull};
    }
    return hash;
}

template <typename T>
std::uint64_t HashVector(const std::vector<T>& values) {
    return HashBytes(values.data(), values.size() * sizeof(T));
}

void ExpectSameInstructions(const std::vector<SdfInstruction>& expected,
                            const std::vector<SdfInstruction>& actual) {
    ASSERT_EQ(expected.size(), actual.size());
    ASSERT_EQ(std::memcmp(expected.data(), actual.data(), expected.size() * sizeof(SdfInstruction)), 0);
}

RecipeSceneAnalysis Analyze(const std::vector<SdfInstruction>& source,
                            glm::vec3 minimum = glm::vec3(-2.0f),
                            glm::vec3 maximum = glm::vec3(2.0f),
                            bool domainEnforced = true,
                            std::uint32_t backend = YK_RECIPE_ANALYSIS_BACKEND_CPU_STRICT,
                            std::uint32_t channels = (1u << 5u) - 1u,
                            std::array<std::uint8_t, YK_RECIPE_ANALYSIS_MAX_CHANNELS> dependencies = {},
                            bool duplicateEquivalent = false,
                            std::uint64_t sourceRevision = 7u) {
    if (dependencies[0] == YK_RECIPE_ANALYSIS_CHANNEL_UNDECLARED) {
        dependencies[0] = YK_RECIPE_ANALYSIS_CHANNEL_GEOMETRY_WINNER;
        for (std::size_t index = 1; index < 5u; ++index)
            dependencies[index] = YK_RECIPE_ANALYSIS_CHANNEL_INDEPENDENT;
    }
    return AnalyzeRecipeScene(source, {}, minimum, maximum, backend, domainEnforced,
                              channels, dependencies, duplicateEquivalent, sourceRevision);
}

} // namespace

TEST(RecipeServiceAnalysis, TileAndCompactedVariantsMatchRecordedSlice6Golden) {
    const std::vector<SdfInstruction> source = {
        Sphere(0.0f, 0.0f, 0.0f, 1.0f),
        Sphere(80.0f, 0.0f, 0.0f, 2.0f),
        Combine(SdfOpCode::Union),
    };
    const auto service = Analyze(source, glm::vec3(-2.0f), glm::vec3(2.0f), true,
                                 YK_RECIPE_ANALYSIS_BACKEND_CPU_STRICT, (1u << 5u) - 1u,
                                 {}, false, 41u);

    ASSERT_EQ(service.status, YK_RECIPE_ANALYSIS_OK);
    ASSERT_EQ(service.tile.status, YK_RECIPE_ANALYSIS_OK);
    ASSERT_EQ(service.compacted.status, YK_RECIPE_ANALYSIS_OK);
    EXPECT_EQ(service.tile.stats.sourceInstructions, 3u);
    EXPECT_EQ(service.tile.stats.finalInstructions, 1u);
    EXPECT_EQ(service.compacted.stats.sourceRevision, 41u);
    EXPECT_EQ(service.compacted.stats.finalInstructions, 1u);
    EXPECT_EQ(HashVector(service.tile.instructions), 14988329129560913562ull);
    EXPECT_EQ(HashVector(service.compacted.instructions), 14988329129560913562ull);
}

TEST(RecipeServiceAnalysis, DistantUnionUsesStrictTileProof) {
    const std::vector<SdfInstruction> source = {
        Sphere(0.0f, 0.0f, 0.0f, 8.0f),
        Sphere(80.0f, 0.0f, 0.0f, 2.0f),
        Combine(SdfOpCode::Union),
    };
    const auto service = Analyze(source, glm::vec3(-1.0f), glm::vec3(1.0f));
    ASSERT_EQ(service.status, YK_RECIPE_ANALYSIS_OK);
    ASSERT_EQ(service.tile.status, YK_RECIPE_ANALYSIS_OK);
    EXPECT_NE(service.tile.stats.specialized, 0u);
    ASSERT_EQ(service.tile.instructions.size(), 1u);
    EXPECT_EQ(service.tile.instructions.front().opCode,
              static_cast<std::uint8_t>(SdfOpCode::Sphere));
    EXPECT_EQ(service.tile.stats.prunedInstructions, 2u);
}

TEST(RecipeServiceAnalysis, DomainAndChannelFactsBlockUnprovedCompaction) {
    const std::vector<SdfInstruction> source = {
        Sphere(0.0f, 0.0f, 0.0f, 1.0f),
        Sphere(80.0f, 0.0f, 0.0f, 2.0f),
        Combine(SdfOpCode::Union),
    };
    auto dependencies = std::array<std::uint8_t, YK_RECIPE_ANALYSIS_MAX_CHANNELS>{};
    dependencies[0] = YK_RECIPE_ANALYSIS_CHANNEL_GEOMETRY_WINNER;
    dependencies[1] = YK_RECIPE_ANALYSIS_CHANNEL_UNDECLARED;
    const auto undeclared = Analyze(source, {}, {}, true,
                                    YK_RECIPE_ANALYSIS_BACKEND_CPU_STRICT,
                                    (1u << 5u) - 1u, dependencies);
    ASSERT_EQ(undeclared.status, YK_RECIPE_ANALYSIS_OK);
    EXPECT_EQ(undeclared.compacted.instructions.size(), source.size());
    EXPECT_EQ(undeclared.compacted.stats.removedByIntervalProof, 0u);

    const auto unenforced = Analyze(source, {}, {}, false);
    ASSERT_EQ(unenforced.status, YK_RECIPE_ANALYSIS_OK);
    EXPECT_EQ(unenforced.compacted.instructions.size(), source.size());
    EXPECT_NE(unenforced.compacted.stats.flags & YK_RECIPE_ANALYSIS_DOMAIN_UNENFORCED, 0u);
}

TEST(RecipeServiceAnalysis, DuplicateRemovalRequiresSelectionAndProvenanceEquivalence) {
    const std::vector<SdfInstruction> source = {
        Sphere(0.0f, 0.0f, 0.0f, 1.0f),
        Sphere(0.0f, 0.0f, 0.0f, 1.0f),
        Combine(SdfOpCode::Union),
    };
    auto channelSensitive = std::array<std::uint8_t, YK_RECIPE_ANALYSIS_MAX_CHANNELS>{};
    channelSensitive[0] = YK_RECIPE_ANALYSIS_CHANNEL_GEOMETRY_WINNER;
    channelSensitive[1] = YK_RECIPE_ANALYSIS_CHANNEL_GEOMETRY_WINNER;
    const auto sensitive = Analyze(source, {}, {}, true,
                                   YK_RECIPE_ANALYSIS_BACKEND_CPU_STRICT,
                                   (1u << 5u) - 1u, channelSensitive, false);
    ASSERT_EQ(sensitive.status, YK_RECIPE_ANALYSIS_OK);
    EXPECT_EQ(sensitive.compacted.stats.removedIdempotentDuplicateInstructions, 0u);
    EXPECT_EQ(sensitive.compacted.instructions.size(), source.size());

    const auto equivalent = Analyze(source, {}, {}, true,
                                    YK_RECIPE_ANALYSIS_BACKEND_CPU_STRICT,
                                    (1u << 5u) - 1u, {}, true);
    ASSERT_EQ(equivalent.status, YK_RECIPE_ANALYSIS_OK);
    EXPECT_EQ(equivalent.compacted.instructions.size(), 1u);
    EXPECT_EQ(equivalent.compacted.stats.removedIdempotentDuplicateInstructions, 2u);
}

TEST(RecipeServiceAnalysis, UncertifiedGpuBackendRetainsSourceTape) {
    const std::vector<SdfInstruction> source = {
        Sphere(0.0f, 0.0f, 0.0f, 8.0f),
        Sphere(80.0f, 0.0f, 0.0f, 2.0f),
        Combine(SdfOpCode::Union),
    };
    const auto service = Analyze(source, glm::vec3(-1.0f), glm::vec3(1.0f), true,
                                 YK_RECIPE_ANALYSIS_BACKEND_GPU_UNCERTIFIED);
    ASSERT_EQ(service.status, YK_RECIPE_ANALYSIS_OK);
    EXPECT_EQ(service.tile.instructions.size(), source.size());
    EXPECT_EQ(service.tile.stats.reason, YK_RECIPE_ANALYSIS_REASON_BACKEND_CONTRACT);
    ExpectSameInstructions(source, service.tile.instructions);
}
