#include <gtest/gtest.h>

#include "Recipe/RecipeParityCorpus.h"
#include "Recipe/SdfRecipeEval.h"
#include "Recipe/generated/RecipeSimd.g.hpp"

#include <array>
#include <bit>
#include <cstdint>
#include <string>

using namespace Vixen::SVO;

TEST(RecipeSimdParity, AllCorpusProgramsAreBitIdenticalAcrossFourLanes) {
    const std::array<glm::vec3, 4> points = {
        glm::vec3(-1.125f, 0.375f, 0.625f),
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(0.25f, -0.75f, 1.5f),
        glm::vec3(2.0f, 1.0f, -0.5f),
    };

    for (const auto& corpus : Recipe::ParityCorpus::GetAll()) {
        Recipe::LoweredRecipeProgram lowered;
        std::string error;
        ASSERT_TRUE(lowered.Lower(
            corpus.program.data(), static_cast<std::uint32_t>(corpus.program.size()),
            {}, error)) << corpus.name << ": " << error;

        float simd[4]{};
        lowered.Evaluate4(points.data(), simd);
        for (std::size_t lane = 0; lane < points.size(); ++lane) {
            const float scalar = Recipe::evalRecipe(
                corpus.program.data(), static_cast<std::uint32_t>(corpus.program.size()),
                points[lane]);
            EXPECT_EQ(std::bit_cast<std::uint32_t>(scalar),
                      std::bit_cast<std::uint32_t>(simd[lane]))
                << corpus.name << " lane " << lane
                << " scalar=" << scalar << " simd=" << simd[lane];
        }
    }
}

TEST(RecipeSimdParity, TypedRecipeValueCorpusProgramMatchesScalarAcrossFourLanes) {
    const std::vector<Recipe::SdfInstruction> program =
        Recipe::ParityCorpus::Make_TypedRecipeValues_OpcodeCoverage();
    Recipe::LoweredRecipeProgram lowered;
    std::string error;
    ASSERT_TRUE(lowered.Lower(
        program.data(), static_cast<std::uint32_t>(program.size()), {}, error)) << error;

    const std::array<glm::vec3, 4> points = {
        glm::vec3(-1.125f, 0.375f, 0.625f),
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(0.25f, -0.75f, 1.5f),
        glm::vec3(2.0f, 1.0f, -0.5f),
    };
    float simd[4]{};
    lowered.Evaluate4(points.data(), simd);
    for (std::size_t lane = 0; lane < points.size(); ++lane) {
        const float scalar = Recipe::evalRecipe(
            program.data(), static_cast<std::uint32_t>(program.size()), points[lane]);
        EXPECT_EQ(std::bit_cast<std::uint32_t>(scalar), std::bit_cast<std::uint32_t>(simd[lane]))
            << "typed recipe program lane " << lane;
        EXPECT_EQ(std::bit_cast<std::uint32_t>(scalar), std::bit_cast<std::uint32_t>(1.0f))
            << "typed recipe corpus probe lane " << lane;
    }
}
