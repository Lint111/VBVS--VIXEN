#include <gtest/gtest.h>

#include "Recipe/RecipeTileSpecialization.h"
#include "Recipe/SdfRecipeEval.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <vector>

namespace {

using namespace Vixen::SVO::Recipe;

SdfInstruction Sphere(float x, float y, float z, float radius) {
    SdfInstruction instruction{};
    instruction.opCode = static_cast<std::uint8_t>(SdfOpCode::Sphere);
    instruction.data[0] = x;
    instruction.data[1] = y;
    instruction.data[2] = z;
    instruction.data[3] = radius;
    return instruction;
}

SdfInstruction Box(float x, float y, float z) {
    SdfInstruction instruction{};
    instruction.opCode = static_cast<std::uint8_t>(SdfOpCode::Box);
    instruction.data[0] = x;
    instruction.data[1] = y;
    instruction.data[2] = z;
    return instruction;
}

SdfInstruction Combine(SdfOpCode opcode) {
    SdfInstruction instruction{};
    instruction.opCode = static_cast<std::uint8_t>(opcode);
    return instruction;
}

const RecipeTileDomain kNearOrigin{{-1.0f, -1.0f, -1.0f}, {1.0f, 1.0f, 1.0f}};

void ExpectSameField(const std::vector<SdfInstruction>& source,
    const std::vector<SdfInstruction>& specialized, const RecipeTileDomain& domain) {
    constexpr int kSteps = 9;
    for (int z = 0; z < kSteps; ++z) {
        for (int y = 0; y < kSteps; ++y) {
            for (int x = 0; x < kSteps; ++x) {
                const glm::vec3 point(
                    glm::mix(domain.minimum.x, domain.maximum.x, static_cast<float>(x) / (kSteps - 1)),
                    glm::mix(domain.minimum.y, domain.maximum.y, static_cast<float>(y) / (kSteps - 1)),
                    glm::mix(domain.minimum.z, domain.maximum.z, static_cast<float>(z) / (kSteps - 1)));
                EXPECT_FLOAT_EQ(evalRecipe(source.data(), static_cast<std::uint32_t>(source.size()), point),
                    evalRecipe(specialized.data(), static_cast<std::uint32_t>(specialized.size()), point))
                    << "at " << point.x << "," << point.y << "," << point.z;
            }
        }
    }
}

TEST(RecipeIntervalPruning, SphereAndBoxLeavesUseStrictHardUnionProofs) {
    const std::vector<SdfInstruction> sphereProgram = {
        Sphere(0.0f, 0.0f, 0.0f, 8.0f),
        Sphere(80.0f, 0.0f, 0.0f, 2.0f),
        Combine(SdfOpCode::Union),
    };
    const auto sphere = SpecializeRecipeTapeForTile(sphereProgram, kNearOrigin);
    ASSERT_TRUE(sphere.stats.specialized);
    ASSERT_EQ(sphere.instructions.size(), 1u);
    EXPECT_EQ(sphere.instructions.front().opCode, static_cast<std::uint8_t>(SdfOpCode::Sphere));
    EXPECT_EQ(sphere.stats.prunedInstructions, 2u);
    ExpectSameField(sphereProgram, sphere.instructions, kNearOrigin);

    const std::vector<SdfInstruction> boxProgram = {
        Box(8.0f, 8.0f, 8.0f),
        Sphere(80.0f, 0.0f, 0.0f, 2.0f),
        Combine(SdfOpCode::Union),
    };
    const auto box = SpecializeRecipeTapeForTile(boxProgram, kNearOrigin);
    ASSERT_TRUE(box.stats.specialized);
    ASSERT_EQ(box.instructions.size(), 1u);
    EXPECT_EQ(box.instructions.front().opCode, static_cast<std::uint8_t>(SdfOpCode::Box));
    ExpectSameField(boxProgram, box.instructions, kNearOrigin);
}

TEST(RecipeIntervalPruning, SubtractionOnlyDropsAProvenIrrelevantCutter) {
    const std::vector<SdfInstruction> distantCutter = {
        Sphere(0.0f, 0.0f, 0.0f, 8.0f),
        Sphere(80.0f, 0.0f, 0.0f, 2.0f),
        Combine(SdfOpCode::Subtract),
    };
    const auto result = SpecializeRecipeTapeForTile(distantCutter, kNearOrigin);
    ASSERT_TRUE(result.stats.specialized);
    ASSERT_EQ(result.instructions.size(), 1u);
    EXPECT_EQ(result.instructions.front().opCode, static_cast<std::uint8_t>(SdfOpCode::Sphere));
    ExpectSameField(distantCutter, result.instructions, kNearOrigin);

    const std::vector<SdfInstruction> dominantCutter = {
        Sphere(0.0f, 0.0f, 0.0f, 8.0f),
        Sphere(0.0f, 0.0f, 0.0f, 8.0f),
        Combine(SdfOpCode::Subtract),
    };
    const auto retained = SpecializeRecipeTapeForTile(dominantCutter, kNearOrigin);
    EXPECT_FALSE(retained.stats.specialized);
    EXPECT_EQ(retained.instructions.size(), dominantCutter.size());
    ExpectSameField(dominantCutter, retained.instructions, kNearOrigin);
}

TEST(RecipeIntervalPruning, EqualityAndUnsupportedOpcodesRetainTheOriginalTape) {
    const std::vector<SdfInstruction> equalOperands = {
        Sphere(0.0f, 0.0f, 0.0f, 3.0f),
        Sphere(0.0f, 0.0f, 0.0f, 3.0f),
        Combine(SdfOpCode::Union),
    };
    const auto equal = SpecializeRecipeTapeForTile(equalOperands, kNearOrigin);
    EXPECT_FALSE(equal.stats.specialized);
    EXPECT_EQ(equal.instructions.size(), equalOperands.size());

    std::vector<SdfInstruction> unsupported = equalOperands;
    unsupported.back() = Combine(SdfOpCode::SmoothUnion);
    unsupported.back().data[0] = 0.5f;
    const auto fallback = SpecializeRecipeTapeForTile(unsupported, kNearOrigin);
    EXPECT_EQ(fallback.stats.fallback, RecipeTileFallback::UnsupportedOpcode);
    ASSERT_EQ(fallback.instructions.size(), unsupported.size());
    EXPECT_EQ(std::memcmp(fallback.instructions.data(), unsupported.data(),
        unsupported.size() * sizeof(SdfInstruction)), 0);
}

TEST(RecipeIntervalPruning, EditHeavyBuriedBranchesReduceOnlyTheTileTape) {
    std::vector<SdfInstruction> source;
    source.reserve(700);
    source.push_back(Sphere(0.0f, 0.0f, 0.0f, 4.5f));

    // A buried patch subtree contains overlapping patches hundreds of units from this
    // object-local tile. The proof removes the whole subtree after its parent union.
    source.push_back(Sphere(100.0f, 0.0f, 0.0f, 12.0f));
    source.push_back(Sphere(102.0f, 0.0f, 0.0f, 12.0f));
    source.push_back(Combine(SdfOpCode::Union));
    source.push_back(Combine(SdfOpCode::Union));

    // The edit history stays in `source`; each overlapping distant cut or patch is
    // considered in accepted order and removed only from the disposable tile tape.
    for (int edit = 0; edit < 160; ++edit) {
        const float offset = static_cast<float>(edit % 11) * 0.75f;
        source.push_back(Sphere(100.0f + offset, 0.0f, 0.0f, 10.0f));
        source.push_back(Combine(edit < 80 ? SdfOpCode::Subtract : SdfOpCode::Union));
    }

    const auto result = SpecializeRecipeTapeForTile(source, kNearOrigin);
    ASSERT_TRUE(result.stats.specialized);
    ASSERT_EQ(result.instructions.size(), 1u);
    EXPECT_EQ(result.stats.sourceInstructions, source.size());
    EXPECT_LT(result.stats.retainedInstructions, result.stats.sourceInstructions / 4u);
    EXPECT_GT(result.stats.intervalEvaluations, 200u);
    ExpectSameField(source, result.instructions, kNearOrigin);
}

} // namespace
