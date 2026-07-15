#include <gtest/gtest.h>
#include "RecipeContentHash.h"

using Vixen::SVO::Recipe::SdfInstruction;
using Vixen::SVO::Recipe::SdfOpCode;

namespace {

SdfInstruction Sphere(float r) {
    SdfInstruction in{};
    in.opCode = (uint8_t)SdfOpCode::Sphere;
    in.data[3] = r;
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

TEST(RecipeContentHash, DeterministicAcrossCalls) {
    std::vector<SdfInstruction> bytecode = { Sphere(26.0f) };
    EXPECT_EQ(CashSystem::ComputeRecipeBytecodeHash(bytecode),
              CashSystem::ComputeRecipeBytecodeHash(bytecode));
}

TEST(RecipeContentHash, IdenticalBytecodeSameHashAcrossDistinctRecipeIds) {
    // Two logically-identical recipes (same bytecode) registered under different recipeIds
    // must still collapse to one hash — recipeId is per-registration identity, not bytecode
    // content, and is never part of the hash input.
    std::vector<SdfInstruction> bytecodeA = { Sphere(26.0f) };
    std::vector<SdfInstruction> bytecodeB = { Sphere(26.0f) };
    EXPECT_EQ(CashSystem::ComputeRecipeBytecodeHash(bytecodeA),
              CashSystem::ComputeRecipeBytecodeHash(bytecodeB));
}

TEST(RecipeContentHash, DiffersByOneLiteral) {
    std::vector<SdfInstruction> a = { Sphere(26.0f) };
    std::vector<SdfInstruction> b = { Sphere(26.5f) };
    EXPECT_NE(CashSystem::ComputeRecipeBytecodeHash(a), CashSystem::ComputeRecipeBytecodeHash(b));
}

TEST(RecipeContentHash, DiffersByOpcode) {
    std::vector<SdfInstruction> a = { Sphere(1.0f) };
    SdfInstruction box{};
    box.opCode = (uint8_t)SdfOpCode::Box;
    box.data[3] = 1.0f;
    std::vector<SdfInstruction> b = { box };
    EXPECT_NE(CashSystem::ComputeRecipeBytecodeHash(a), CashSystem::ComputeRecipeBytecodeHash(b));
}

// Critical correctness property (plan §Task 2): two recipes identical except for a ReadParam
// slot INDEX must NOT collapse to one hash — the slot index lives in data[0], which is part of
// the hashed bytecode bytes.
TEST(RecipeContentHash, DistinguishesReadParamSlotIndex) {
    std::vector<SdfInstruction> readsSlot0 = { ReadParam(1, 0.0f) };
    std::vector<SdfInstruction> readsSlot1 = { ReadParam(1, 1.0f) };
    EXPECT_NE(CashSystem::ComputeRecipeBytecodeHash(readsSlot0),
              CashSystem::ComputeRecipeBytecodeHash(readsSlot1));
}

TEST(RecipeContentHash, DiffersByInstructionOrder) {
    SdfInstruction sphere = Sphere(1.0f);
    SdfInstruction box{};
    box.opCode = (uint8_t)SdfOpCode::Box;
    box.data[3] = 1.0f;

    std::vector<SdfInstruction> a = { sphere, box };
    std::vector<SdfInstruction> b = { box, sphere };
    EXPECT_NE(CashSystem::ComputeRecipeBytecodeHash(a), CashSystem::ComputeRecipeBytecodeHash(b));
}
