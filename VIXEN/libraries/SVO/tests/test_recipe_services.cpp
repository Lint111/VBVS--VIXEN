#include "Recipe/RecipeBounds.h"
#include "Recipe/RecipeOccupancy.h"
#include "Recipe/RecipeServices.h"
#include "Recipe/RecipeTileSpecialization.h"
#include "Recipe/RecipeWholeDomainCompaction.h"
#include "Recipe/SdfRecipeCodegenGlsl.h"

#include <gtest/gtest.h>

#include <bit>
#include <cstring>

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

void ExpectSameInstructionTape(const std::vector<SdfInstruction>& expected,
                               const std::vector<SdfInstruction>& actual) {
    ASSERT_EQ(expected.size(), actual.size());
    ASSERT_EQ(std::memcmp(expected.data(), actual.data(), expected.size() * sizeof(SdfInstruction)), 0);
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

TEST(RecipeServices, GlslFieldMatchesOldOracleForSphere) {
    const std::array<SdfInstruction, 1> program = {Sphere(1.0f, -2.0f, 3.0f, 4.5f)};
    const std::string oldField = EmitProceduralFieldFunctionGlsl(program.data(), 1u, 17u);
    const std::string serviceField = RecipeGpuField(program, 17u, false);

    ASSERT_FALSE(serviceField.empty());
    EXPECT_EQ(serviceField, oldField);
}

TEST(RecipeServices, RegistrationAnalysisMatchesOldBoundAndOccupancyOracle) {
    RecipeRegistry::RecipeEntry entry;
    entry.bytecode = {Sphere(1.0f, -2.0f, 3.0f, 4.5f)};

    const auto oldBound = DeriveConservativeBounds(entry.bytecode.data(), 1u);
    const auto oldGrid = DeriveOccupancyGrid(
        entry.bytecode.data(), 1u, oldBound.center, oldBound.radius);
    const auto analysis = AnalyzeRecipeRegistration(entry, 24.0f, 0.9f);

    ASSERT_EQ(analysis.status, YK_RECIPE_ANALYSIS_OK);
    ASSERT_EQ(analysis.boundSource, RecipeRegistrationAnalysis::BoundSource::Derived);
    ASSERT_TRUE(oldBound.ok);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(entry.boundCenter.x), std::bit_cast<std::uint32_t>(oldBound.center.x));
    EXPECT_EQ(std::bit_cast<std::uint32_t>(entry.boundCenter.y), std::bit_cast<std::uint32_t>(oldBound.center.y));
    EXPECT_EQ(std::bit_cast<std::uint32_t>(entry.boundCenter.z), std::bit_cast<std::uint32_t>(oldBound.center.z));
    EXPECT_EQ(std::bit_cast<std::uint32_t>(entry.boundRadius), std::bit_cast<std::uint32_t>(oldBound.radius));
    ASSERT_EQ(analysis.occupancyAvailable, oldGrid.ok);
    if (oldGrid.ok) {
        EXPECT_EQ(entry.occupancyGridDim, oldGrid.dim);
        EXPECT_EQ(std::bit_cast<std::uint32_t>(entry.occupancyGridCellSize),
                  std::bit_cast<std::uint32_t>(oldGrid.cellSize));
        ASSERT_EQ(entry.occupancyGridValues.size(), oldGrid.values.size());
        for (std::size_t index = 0; index < oldGrid.values.size(); ++index)
            EXPECT_EQ(std::bit_cast<std::uint32_t>(entry.occupancyGridValues[index]),
                      std::bit_cast<std::uint32_t>(oldGrid.values[index])) << "cell " << index;
    }
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

TEST(RecipeServices, SceneVariantsMatchOldTileAndCompactionOracles) {
    const std::vector<SdfInstruction> source = {
        Sphere(0.0f, 0.0f, 0.0f, 1.0f),
        Sphere(80.0f, 0.0f, 0.0f, 2.0f),
        Combine(SdfOpCode::Union),
    };
    const RecipeTileDomain domain{{-2.0f, -2.0f, -2.0f}, {2.0f, 2.0f, 2.0f}};
    const auto oldTile = SpecializeRecipeTapeForTile(source, domain);

    RecipeWholeDomainCompactionRequest oldRequest;
    oldRequest.sourceRevision = 41u;
    oldRequest.domain = domain;
    oldRequest.domainIsEnforced = true;
    oldRequest.requiredOutputChannels = kAllRecipeOutputChannels;
    oldRequest.channelDependencies = {
        RecipeChannelDependency::GeometryWinner,
        RecipeChannelDependency::Independent,
        RecipeChannelDependency::Independent,
        RecipeChannelDependency::Independent,
        RecipeChannelDependency::Independent,
    };
    const auto oldCompacted = CompactRecipeOverWholeDomain(source, oldRequest);

    std::array<std::uint8_t, YK_RECIPE_ANALYSIS_MAX_CHANNELS> dependencies{};
    dependencies[0] = YK_RECIPE_ANALYSIS_CHANNEL_GEOMETRY_WINNER;
    for (std::size_t index = 1; index < 5; ++index)
        dependencies[index] = YK_RECIPE_ANALYSIS_CHANNEL_INDEPENDENT;
    const auto service = AnalyzeRecipeScene(
        source, {}, domain.minimum, domain.maximum, YK_RECIPE_ANALYSIS_BACKEND_CPU_STRICT,
        true, kAllRecipeOutputChannels, dependencies, false, 41u);

    ASSERT_EQ(service.status, YK_RECIPE_ANALYSIS_OK);
    ASSERT_EQ(service.tile.status, YK_RECIPE_ANALYSIS_OK);
    ASSERT_EQ(service.compacted.status, YK_RECIPE_ANALYSIS_OK);
    EXPECT_EQ(service.tile.stats.sourceInstructions, oldTile.stats.sourceInstructions);
    EXPECT_EQ(service.tile.stats.finalInstructions, oldTile.stats.retainedInstructions);
    EXPECT_EQ(service.compacted.stats.sourceRevision, oldCompacted.sourceRevision);
    EXPECT_EQ(service.compacted.stats.finalInstructions, oldCompacted.stats.finalInstructions);
    ExpectSameInstructionTape(oldTile.instructions, service.tile.instructions);
    ExpectSameInstructionTape(oldCompacted.instructions, service.compacted.instructions);
}
