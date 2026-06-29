#include <gtest/gtest.h>
#include <cstring>
#include <vector>
#include "Recipe/RecipeIngest.h"
#include "Recipe/generated/RecipeContainer.g.h"
using namespace Vixen::SVO;
using namespace Yeroket::Sdf::Generated;

// Build a minimal valid blob: header + one Sphere instruction.
static std::vector<uint8_t> MakeValidBlob(uint32_t bakeRes = 64,
                                          float    band    = 2.5f,
                                          uint32_t depth   = 3) {
    RecipeContainerHeader h{};
    h.magic            = 0x31435256u; // 'VRC1'
    h.formatVersion    = 1u;
    h.instructionCount = 1u;
    h.bakeResolution   = bakeRes;
    h.bandVoxels       = band;
    h.brickDepth       = depth;

    SdfInstruction instr{};
    instr.opCode  = (uint8_t)Vixen::SVO::Recipe::SdfOpCode::Sphere;
    instr.data[3] = 10.0f;

    std::vector<uint8_t> blob(sizeof(h) + sizeof(instr));
    std::memcpy(blob.data(), &h, sizeof(h));
    std::memcpy(blob.data() + sizeof(h), &instr, sizeof(instr));
    return blob;
}

TEST(RecipeIngest, ValidBlobRegistersOk) {
    RecipeRegistry reg;
    auto blob = MakeValidBlob(64, 2.5f, 3);
    ASSERT_EQ(IngestBlob(42u, blob.data(), blob.size(), reg), IngestResult::Ok);
    const auto* e = reg.Get(42u);
    ASSERT_NE(e, nullptr);
    EXPECT_EQ(e->bytecode.size(), 1u);
    EXPECT_EQ(e->bakeResolution, 64u);
    EXPECT_FLOAT_EQ(e->bandVoxels, 2.5f);
    EXPECT_EQ(e->brickDepth, 3u);
}

TEST(RecipeIngest, TruncatedBlobReturnsBadContainer) {
    RecipeRegistry reg;
    auto blob = MakeValidBlob();
    blob.resize(blob.size() / 2); // truncate
    EXPECT_EQ(IngestBlob(1u, blob.data(), blob.size(), reg), IngestResult::BadContainer);
    EXPECT_EQ(reg.Get(1u), nullptr);
}

TEST(RecipeIngest, WrongMagicReturnsBadContainer) {
    RecipeRegistry reg;
    auto blob = MakeValidBlob();
    blob[0] = 0xFF; // corrupt magic
    EXPECT_EQ(IngestBlob(1u, blob.data(), blob.size(), reg), IngestResult::BadContainer);
}

TEST(RecipeIngest, DuplicateIdReturnsRegistryRejected) {
    RecipeRegistry reg;
    auto blob = MakeValidBlob();
    ASSERT_EQ(IngestBlob(7u, blob.data(), blob.size(), reg), IngestResult::Ok);
    EXPECT_EQ(IngestBlob(7u, blob.data(), blob.size(), reg), IngestResult::RegistryRejected);
}
