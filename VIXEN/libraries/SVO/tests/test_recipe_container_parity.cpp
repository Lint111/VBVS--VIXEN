#include <gtest/gtest.h>
#include <fstream>
#include <vector>
#include <cstdint>
#include "Recipe/generated/RecipeContainer.g.h"  // Yeroket::Sdf::Generated::ReadRecipeContainer
#include "Recipe/generated/SdfOpCodes.g.h"       // Vixen::SVO::Recipe::SdfOpCode::Sphere

// RECIPE_CONTAINER_FIXTURE_PATH is injected by CMake (see CMakeLists.txt).
// It points to the byte blob produced by the canonical C# RecipeContainer.Serialize —
// the non-circular proof that writer (C#) and reader (generated C++) agree.
TEST(RecipeContainerParity, CSharpWriterBlobRoundTripsViaCppReader)
{
    // --- load the fixture ---
    std::ifstream f(RECIPE_CONTAINER_FIXTURE_PATH, std::ios::binary | std::ios::ate);
    ASSERT_TRUE(f.good()) << "Cannot open fixture: " RECIPE_CONTAINER_FIXTURE_PATH;
    auto size = static_cast<size_t>(f.tellg());
    f.seekg(0);
    std::vector<uint8_t> blob(size);
    f.read(reinterpret_cast<char*>(blob.data()), static_cast<std::streamsize>(size));

    // --- round-trip through the generated C++ reader ---
    Yeroket::Sdf::Generated::RecipeContainerView view{};
    ASSERT_TRUE(Yeroket::Sdf::Generated::ReadRecipeContainer(blob.data(), blob.size(), view));

    // --- header assertions ---
    EXPECT_EQ(view.header.magic,            0x31435256u);  // 'VRC1'
    EXPECT_EQ(view.header.formatVersion,    1u);
    EXPECT_EQ(view.header.instructionCount, 1u);

    // --- instruction assertions ---
    ASSERT_NE(view.instructions, nullptr);
    EXPECT_EQ(view.instructions[0].opCode,
              static_cast<uint8_t>(Vixen::SVO::Recipe::SdfOpCode::Sphere));
    EXPECT_FLOAT_EQ(view.instructions[0].data[3], 26.0f);  // Data0.w = radius
}
