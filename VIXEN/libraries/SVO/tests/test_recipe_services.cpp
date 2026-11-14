#include "Recipe/RecipeServices.h"

#include <gtest/gtest.h>

#include <bit>

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

} // namespace

TEST(RecipeServices, ProviderExposesAllThreeFixedAbis) {
    const auto& services = GetRecipeServiceApis();
    ASSERT_TRUE(services.available);

    YkRecipeServiceInfo cpuInfo{};
    cpuInfo.structSize = sizeof(cpuInfo);
    EXPECT_EQ(services.cpu.describe(&cpuInfo), YK_RECIPE_OK);
    EXPECT_EQ(cpuInfo.laneWidth, YK_RECIPE_RUNTIME_LANES);

    YkRecipeGpuServiceInfo gpuInfo{};
    gpuInfo.structSize = sizeof(gpuInfo);
    EXPECT_EQ(services.gpu.describe(&gpuInfo), YK_RECIPE_GPU_OK);
    EXPECT_NE(gpuInfo.backendMask & (1u << YK_RECIPE_GPU_BACKEND_GLSL_FIELD), 0u);

    YkRecipeAnalysisServiceInfo analysisInfo{};
    analysisInfo.structSize = sizeof(analysisInfo);
    EXPECT_EQ(services.analysis.describe(&analysisInfo), YK_RECIPE_ANALYSIS_OK);
    EXPECT_NE(analysisInfo.backendMask & (1u << YK_RECIPE_ANALYSIS_BACKEND_GPU_DZN_PRECISE), 0u);
}

TEST(RecipeServices, GlslFieldMatchesRecordedOldOracleForSphere) {
    const std::array<SdfInstruction, 1> program = {Sphere(1.0f, -2.0f, 3.0f, 4.5f)};
    const std::string serviceField = RecipeGpuField(program, 17u, false);

    ASSERT_FALSE(serviceField.empty());
    EXPECT_EQ(serviceField.size(), 227u);
    EXPECT_EQ(HashBytes(serviceField.data(), serviceField.size()), 9945844444134442959ull);
}

TEST(RecipeServices, RegistrationAnalysisMatchesRecordedOldBoundAndOccupancyOracle) {
    RecipeRegistry::RecipeEntry entry;
    entry.bytecode = {Sphere(1.0f, -2.0f, 3.0f, 4.5f)};

    const auto analysis = AnalyzeRecipeRegistration(entry, 24.0f, 0.9f);

    ASSERT_EQ(analysis.status, YK_RECIPE_ANALYSIS_OK);
    ASSERT_EQ(analysis.boundSource, RecipeRegistrationAnalysis::BoundSource::Derived);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(entry.boundCenter.x), 1065353216u);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(entry.boundCenter.y), 3221225472u);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(entry.boundCenter.z), 1077936128u);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(entry.boundRadius), 1083179008u);
    ASSERT_TRUE(analysis.occupancyAvailable);
    EXPECT_EQ(entry.occupancyGridDim, 16u);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(entry.occupancyGridCellSize), 1058013184u);
    EXPECT_EQ(entry.occupancyGridValues.size(), 4096u);
    EXPECT_EQ(HashVector(entry.occupancyGridValues), 677295323107829139ull);
}

TEST(RecipeServices, AuthoredBoundWinsAnalysis) {
    RecipeRegistry::RecipeEntry entry;
    entry.bytecode = {Sphere(1.0f, -2.0f, 3.0f, 4.5f)};
    entry.boundCenter = glm::vec3(8.0f, 9.0f, 10.0f);
    entry.boundRadius = 11.0f;

    const auto analysis = AnalyzeRecipeRegistration(entry, 24.0f, 0.9f);
    EXPECT_EQ(analysis.boundSource, RecipeRegistrationAnalysis::BoundSource::Authored);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(entry.boundCenter.x), std::bit_cast<std::uint32_t>(8.0f));
    EXPECT_EQ(std::bit_cast<std::uint32_t>(entry.boundCenter.y), std::bit_cast<std::uint32_t>(9.0f));
    EXPECT_EQ(std::bit_cast<std::uint32_t>(entry.boundCenter.z), std::bit_cast<std::uint32_t>(10.0f));
    EXPECT_EQ(std::bit_cast<std::uint32_t>(entry.boundRadius), std::bit_cast<std::uint32_t>(11.0f));
}

TEST(RecipeServices, SceneVariantsMatchRecordedOldTileAndCompactionOracles) {
    const std::vector<SdfInstruction> source = {
        Sphere(0.0f, 0.0f, 0.0f, 1.0f),
        Sphere(80.0f, 0.0f, 0.0f, 2.0f),
        Combine(SdfOpCode::Union),
    };
    const glm::vec3 domainMinimum(-2.0f);
    const glm::vec3 domainMaximum(2.0f);

    std::array<std::uint8_t, YK_RECIPE_ANALYSIS_MAX_CHANNELS> dependencies{};
    dependencies[0] = YK_RECIPE_ANALYSIS_CHANNEL_GEOMETRY_WINNER;
    for (std::size_t index = 1; index < 5; ++index)
        dependencies[index] = YK_RECIPE_ANALYSIS_CHANNEL_INDEPENDENT;
    const auto service = AnalyzeRecipeScene(
        source, {}, domainMinimum, domainMaximum, YK_RECIPE_ANALYSIS_BACKEND_CPU_STRICT,
        true, (1u << 5u) - 1u, dependencies, false, 41u);

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
