#include <gtest/gtest.h>
#include "MainCacher.h"
#include "RecipeContentCacher.h"

using Vixen::SVO::Recipe::SdfInstruction;
using Vixen::SVO::Recipe::SdfOpCode;

namespace {

SdfInstruction Sphere(float r) {
    SdfInstruction in{};
    in.opCode = (uint8_t)SdfOpCode::Sphere;
    in.data[3] = r;
    return in;
}

SdfInstruction Box(float half) {
    SdfInstruction in{};
    in.opCode = (uint8_t)SdfOpCode::Box;
    in.data[3] = half;
    return in;
}

SdfInstruction ReadParam(uint8_t paramMask, float slotIdx) {
    SdfInstruction in{};
    in.opCode = (uint8_t)SdfOpCode::ReadParam;
    in.paramMask = paramMask;
    in.data[0] = slotIdx;
    return in;
}

} // namespace

// Test fixture — mirrors test_registration_api.cpp's "MainCacher is not a singleton, each test
// owns a fresh instance" pattern (AR#8).
class RecipeContentCacherTest : public ::testing::Test {
protected:
    void SetUp() override {
        mainCacher_.ClearAll();
        cacherType_ = std::type_index(typeid(CashSystem::RecipeFamilyRecord));
        mainCacher_.RegisterCacher<
            CashSystem::RecipeContentCacher,
            CashSystem::RecipeFamilyRecord,
            CashSystem::RecipeContentCacheCreateInfo
        >(cacherType_, "RecipeContent", /*isDeviceDependent=*/false);
        cacher_ = mainCacher_.GetCacher<
            CashSystem::RecipeContentCacher,
            CashSystem::RecipeFamilyRecord,
            CashSystem::RecipeContentCacheCreateInfo
        >(cacherType_);
    }

    void TearDown() override {
        mainCacher_.ClearAll();
    }

    CashSystem::MainCacher mainCacher_;
    std::type_index cacherType_ = std::type_index(typeid(void));
    CashSystem::RecipeContentCacher* cacher_ = nullptr;
};

TEST_F(RecipeContentCacherTest, RegistersAsDeviceIndependent) {
    ASSERT_NE(cacher_, nullptr);
    EXPECT_TRUE(mainCacher_.IsRegistered(cacherType_));
    EXPECT_FALSE(mainCacher_.IsDeviceDependent(cacherType_));
}

// (a) Two recipes with byte-identical bytecode registered under different recipeIds collapse to
// one cache entry — the "exact-dup family sharing" proof, exercised through the cacher itself
// (not just the hash function, which M1 already covers).
TEST_F(RecipeContentCacherTest, IdenticalBytecodeDistinctRecipeIdsCollapseToOneEntry) {
    ASSERT_NE(cacher_, nullptr);
    std::vector<SdfInstruction> bytecodeA = { Sphere(26.0f) };
    std::vector<SdfInstruction> bytecodeB = { Sphere(26.0f) };

    auto recordA = cacher_->RegisterRecipe(/*recipeId=*/1, bytecodeA);
    auto recordB = cacher_->RegisterRecipe(/*recipeId=*/2, bytecodeB);

    ASSERT_NE(recordA, nullptr);
    ASSERT_NE(recordB, nullptr);
    EXPECT_EQ(recordA, recordB) << "same shared_ptr identity — TypedCacher::GetOrCreate returns "
                                    "the SAME cached entry on a content-hash hit";
    EXPECT_EQ(recordA->contentHash, recordB->contentHash);
    EXPECT_EQ(recordA->firstRecipeId, 1u);
    ASSERT_EQ(recordA->memberRecipeIds.size(), 2u);
    EXPECT_EQ(recordA->memberRecipeIds[0], 1u);
    EXPECT_EQ(recordA->memberRecipeIds[1], 2u);
}

// (b) Bytecode differing in one opcode -> distinct cache entries.
TEST_F(RecipeContentCacherTest, DiffersByOpcodeDistinctEntries) {
    ASSERT_NE(cacher_, nullptr);
    std::vector<SdfInstruction> sphereBytecode = { Sphere(1.0f) };
    std::vector<SdfInstruction> boxBytecode = { Box(1.0f) };

    auto recordA = cacher_->RegisterRecipe(1, sphereBytecode);
    auto recordB = cacher_->RegisterRecipe(2, boxBytecode);

    ASSERT_NE(recordA, nullptr);
    ASSERT_NE(recordB, nullptr);
    EXPECT_NE(recordA, recordB);
    EXPECT_NE(recordA->contentHash, recordB->contentHash);
    EXPECT_EQ(recordA->memberRecipeIds.size(), 1u);
    EXPECT_EQ(recordB->memberRecipeIds.size(), 1u);
}

// (b) Bytecode differing in one literal -> distinct cache entries.
TEST_F(RecipeContentCacherTest, DiffersByOneLiteralDistinctEntries) {
    ASSERT_NE(cacher_, nullptr);
    std::vector<SdfInstruction> a = { Sphere(26.0f) };
    std::vector<SdfInstruction> b = { Sphere(26.5f) };

    auto recordA = cacher_->RegisterRecipe(1, a);
    auto recordB = cacher_->RegisterRecipe(2, b);

    EXPECT_NE(recordA, recordB);
    EXPECT_NE(recordA->contentHash, recordB->contentHash);
}

// (c) ReadParam slot-index distinction re-proven through the CACHER (not just the hash function):
// two recipes, otherwise-identical bytecode, differing ONLY in which param slot they read, must
// NOT collapse to one family — this would silently share a "pipeline family" between recipes that
// need different param semantics (plan Task 4 (c), the real correctness-bug scenario).
TEST_F(RecipeContentCacherTest, ReadParamSlotIndexDoesNotCollapseAcrossCacher) {
    ASSERT_NE(cacher_, nullptr);
    std::vector<SdfInstruction> readsSlot0 = { ReadParam(1, 0.0f) };
    std::vector<SdfInstruction> readsSlot1 = { ReadParam(1, 1.0f) };

    auto recordSlot0 = cacher_->RegisterRecipe(10, readsSlot0);
    auto recordSlot1 = cacher_->RegisterRecipe(11, readsSlot1);

    ASSERT_NE(recordSlot0, nullptr);
    ASSERT_NE(recordSlot1, nullptr);
    EXPECT_NE(recordSlot0, recordSlot1);
    EXPECT_NE(recordSlot0->contentHash, recordSlot1->contentHash);
    EXPECT_EQ(recordSlot0->memberRecipeIds.size(), 1u);
    EXPECT_EQ(recordSlot1->memberRecipeIds.size(), 1u);

    // A THIRD recipe with the exact same slot-0 read as the first must still join that family,
    // proving the distinction above isn't accidentally over-splitting either.
    std::vector<SdfInstruction> alsoReadsSlot0 = { ReadParam(1, 0.0f) };
    auto recordSlot0Again = cacher_->RegisterRecipe(12, alsoReadsSlot0);
    EXPECT_EQ(recordSlot0Again, recordSlot0);
    ASSERT_EQ(recordSlot0->memberRecipeIds.size(), 2u);
    EXPECT_EQ(recordSlot0->memberRecipeIds[1], 12u);
}

// Re-registering the SAME recipeId (e.g. a caller retrying) must not duplicate membership.
TEST_F(RecipeContentCacherTest, ReRegisteringSameRecipeIdDoesNotDuplicateMembership) {
    ASSERT_NE(cacher_, nullptr);
    std::vector<SdfInstruction> bytecode = { Sphere(5.0f) };

    auto first = cacher_->RegisterRecipe(7, bytecode);
    auto second = cacher_->RegisterRecipe(7, bytecode);

    EXPECT_EQ(first, second);
    ASSERT_EQ(first->memberRecipeIds.size(), 1u);
    EXPECT_EQ(first->memberRecipeIds[0], 7u);
}
