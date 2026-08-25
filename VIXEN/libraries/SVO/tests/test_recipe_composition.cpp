#include <limits>

#include <gtest/gtest.h>

#include "Recipe/RecipeComposition.h"
#include "Recipe/RecipeRegistry.h"

using namespace Vixen::SVO;
using Recipe::SdfInstruction;
using Recipe::SdfOpCode;

namespace {

SdfInstruction Sphere() {
    SdfInstruction instruction{};
    instruction.opCode = static_cast<uint8_t>(SdfOpCode::Sphere);
    instruction.data[3] = 1.0f;
    return instruction;
}

RecipeRegistry::RecipeEntry PlainEntry() {
    RecipeRegistry::RecipeEntry entry{};
    entry.bytecode = {Sphere()};
    return entry;
}

} // namespace

TEST(RecipeComposition, ResolvesVariantAndTwoMaskBlockSelectionByBand) {
    RecipeRegistry registry;
    ASSERT_EQ(registry.Register(10u, PlainEntry()), RecipeRegistry::RegisterResult::Ok);
    ASSERT_EQ(registry.Register(11u, PlainEntry()), RecipeRegistry::RegisterResult::Ok);
    ASSERT_EQ(registry.Register(12u, PlainEntry()), RecipeRegistry::RegisterResult::Ok);

    RecipeRegistry::RecipeEntry parent = PlainEntry();
    parent.lodLadder = {
        LodBand{0.5f, static_cast<uint8_t>(LodStrategy::MarchFull), 0u, 0x7u,
                static_cast<uint8_t>(LodParamTier::Full), kAllLodUploadUnits},
        LodBand{std::numeric_limits<float>::infinity(),
                static_cast<uint8_t>(LodStrategy::MarchVariant), 11u, 0x5u,
                static_cast<uint8_t>(LodParamTier::Full), kAllLodUploadUnits},
    };
    parent.blocks = {
        BlockRef{static_cast<uint8_t>(SdfCompositeRole::Emissive), 10u, 0x3u, 0.0f},
        BlockRef{static_cast<uint8_t>(SdfCompositeRole::Interval), 11u, 0x1u, 4.0f},
        BlockRef{static_cast<uint8_t>(SdfCompositeRole::SurfaceMod), 12u, 0x3u, 0.0f},
    };
    ASSERT_EQ(registry.Register(20u, parent), RecipeRegistry::RegisterResult::Ok);

    const auto near = registry.ResolveSdfComposite(20u, 0u);
    ASSERT_TRUE(near.has_value());
    EXPECT_TRUE(near->HasField());
    EXPECT_EQ(near->fieldRecipeId, 20u);
    ASSERT_EQ(near->blocks.size(), 3u);
    EXPECT_EQ(near->blocks[0]->recipeId, 10u);
    EXPECT_EQ(near->blocks[1]->recipeId, 11u);
    EXPECT_EQ(near->blocks[2]->recipeId, 12u);
    EXPECT_EQ(near->activeBlockMask, 0x7u);

    // q selects the coarser band, which redirects the field and drops the
    // interval block through the intersection of the two authoring masks.
    const auto far = registry.ResolveSdfCompositeAtQ(20u, 0.75f);
    ASSERT_TRUE(far.has_value());
    EXPECT_EQ(far->bandIndex, 1u);
    EXPECT_EQ(far->fieldRecipeId, 11u);
    ASSERT_EQ(far->blocks.size(), 2u);
    EXPECT_EQ(far->blocks[0]->recipeId, 10u);
    EXPECT_EQ(far->blocks[1]->recipeId, 12u);
    EXPECT_EQ(far->activeBlockMask, 0x5u);
}

TEST(RecipeComposition, SkipBandHasNoFieldOrBlocks) {
    RecipeRegistry registry;
    ASSERT_EQ(registry.Register(30u, PlainEntry()), RecipeRegistry::RegisterResult::Ok);

    RecipeRegistry::RecipeEntry entry = PlainEntry();
    entry.lodLadder = {
        LodBand{1.0f, static_cast<uint8_t>(LodStrategy::MarchFull), 0u, 0x1u,
                static_cast<uint8_t>(LodParamTier::Full), kAllLodUploadUnits},
        LodBand{std::numeric_limits<float>::infinity(), static_cast<uint8_t>(LodStrategy::Skip),
                0u, 0x1u, static_cast<uint8_t>(LodParamTier::Full), 0u},
    };
    entry.blocks = {BlockRef{static_cast<uint8_t>(SdfCompositeRole::Emissive), 30u, 0x3u, 0.0f}};
    ASSERT_EQ(registry.Register(31u, entry), RecipeRegistry::RegisterResult::Ok);

    const auto resolved = registry.ResolveSdfCompositeAtQ(31u, 2.0f);
    ASSERT_TRUE(resolved.has_value());
    EXPECT_FALSE(resolved->HasField());
    EXPECT_TRUE(resolved->blocks.empty());
    EXPECT_EQ(resolved->activeBlockMask, 0u);
}

TEST(RecipeComposition, BlocksParticipateInRegistryDependencyGuards) {
    RecipeRegistry registry;
    RecipeRegistry::RecipeEntry unknown = PlainEntry();
    unknown.blocks = {BlockRef{static_cast<uint8_t>(SdfCompositeRole::Field), 999u, 0x1u, 0.0f}};
    EXPECT_EQ(registry.Register(40u, unknown), RecipeRegistry::RegisterResult::UnknownCalleeRecipe);

    RecipeRegistry::RecipeEntry recursive = PlainEntry();
    recursive.blocks = {BlockRef{static_cast<uint8_t>(SdfCompositeRole::Field), 41u, 0x1u, 0.0f}};
    EXPECT_EQ(registry.Register(41u, recursive), RecipeRegistry::RegisterResult::RecursiveInvocation);
}

TEST(RecipeComposition, RejectsMalformedBlockDeclarations) {
    RecipeRegistry registry;

    RecipeRegistry::RecipeEntry badRole = PlainEntry();
    badRole.blocks = {BlockRef{255u, 50u, 0x1u, 0.0f}};
    EXPECT_EQ(registry.Register(50u, badRole), RecipeRegistry::RegisterResult::BadCompositionRole);

    RecipeRegistry::RecipeEntry badRadius = PlainEntry();
    badRadius.blocks = {BlockRef{static_cast<uint8_t>(SdfCompositeRole::Emissive), 50u, 0x1u, 1.0f}};
    EXPECT_EQ(registry.Register(51u, badRadius), RecipeRegistry::RegisterResult::BadCompositionShellRadius);

    RecipeRegistry::RecipeEntry tooMany = PlainEntry();
    tooMany.blocks.resize(kMaxCompositionBlocks + 1);
    EXPECT_EQ(registry.Register(52u, tooMany), RecipeRegistry::RegisterResult::BadCompositionBlockCount);
}
