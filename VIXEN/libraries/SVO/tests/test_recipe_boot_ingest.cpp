#include <gtest/gtest.h>
#include <cstring>
#include <vector>
#include "Recipe/RecipeBootIngest.h"
#include "Recipe/generated/RecipeContainer.g.h"
using namespace Vixen::SVO;
using namespace Yeroket::Sdf::Generated;

// Build a minimal valid VRC1 blob: header + one Sphere instruction (mirrors test_recipe_ingest.cpp).
static std::vector<uint8_t> MakeSphereBlob(float radius) {
    RecipeContainerHeader h{};
    h.magic            = 0x31435256u; // 'VRC1'
    h.formatVersion    = 1u;
    h.instructionCount = 1u;
    h.bakeResolution   = 64u;
    h.bandVoxels       = 2.5f;
    h.brickDepth       = 3u;

    SdfInstruction instr{};
    instr.opCode  = (uint8_t)Vixen::SVO::Recipe::SdfOpCode::Sphere;
    instr.data[0] = 32.0f; instr.data[1] = 32.0f; instr.data[2] = 32.0f; instr.data[3] = radius;

    std::vector<uint8_t> blob(sizeof(h) + sizeof(instr));
    std::memcpy(blob.data(), &h, sizeof(h));
    std::memcpy(blob.data() + sizeof(h), &instr, sizeof(instr));
    return blob;
}

// Pack N (recipeId, blob) pairs into the host wire shape: [count][ (id,len,bytes) ... ].
static std::vector<uint8_t> PackBuffer(const std::vector<std::pair<uint32_t, std::vector<uint8_t>>>& entries) {
    std::vector<uint8_t> buf;
    uint32_t count = static_cast<uint32_t>(entries.size());
    buf.resize(sizeof(count));
    std::memcpy(buf.data(), &count, sizeof(count));
    for (const auto& [id, bytes] : entries) {
        size_t at = buf.size();
        uint32_t len = static_cast<uint32_t>(bytes.size());
        buf.resize(at + 8 + bytes.size());
        std::memcpy(buf.data() + at, &id, 4);
        std::memcpy(buf.data() + at + 4, &len, 4);
        std::memcpy(buf.data() + at + 8, bytes.data(), bytes.size());
    }
    return buf;
}

TEST(RecipeBootIngest, EmptyBuffer_OkNoRecipes) {
    auto buf = PackBuffer({});
    auto res = ParseAndBakeRecipeBlobBuffer(buf.data(), static_cast<int32_t>(buf.size()));
    ASSERT_TRUE(res.ok) << res.err;
    EXPECT_EQ(res.registry.Ids().size(), 0u);
}

TEST(RecipeBootIngest, RegistersAndBakesMultipleBlobsInAscendingOrder) {
    // 5 distinct blobs (N>3 per spec §8) at out-of-order recipeIds — registry/bake must still
    // resolve ascending id order (RecipeRegistry::Ids() / BakeRegistryToPool contract).
    auto buf = PackBuffer({
        {12u, MakeSphereBlob(20.0f)},
        {2u,  MakeSphereBlob(21.0f)},
        {7u,  MakeSphereBlob(22.0f)},
        {2u + 1u, MakeSphereBlob(23.0f)}, // 3u
        {20u, MakeSphereBlob(24.0f)},
    });
    auto res = ParseAndBakeRecipeBlobBuffer(buf.data(), static_cast<int32_t>(buf.size()));
    ASSERT_TRUE(res.ok) << res.err;
    ASSERT_TRUE(res.bake.ok) << res.bake.err;

    auto ids = res.registry.Ids();
    ASSERT_EQ(ids.size(), 5u);
    EXPECT_TRUE(std::is_sorted(ids.begin(), ids.end()));

    // Every registered id gets a distinct octree slot, ascending with id.
    for (size_t i = 0; i + 1 < ids.size(); ++i) {
        EXPECT_LT(res.registry.Get(ids[i])->octreeSlot, res.registry.Get(ids[i + 1])->octreeSlot);
    }
    EXPECT_EQ(res.bake.pool.count, 5u);
}

TEST(RecipeBootIngest, TruncatedBuffer_FailsLoudWithoutCrash) {
    auto full = PackBuffer({ {1u, MakeSphereBlob(10.0f)} });
    full.resize(full.size() - 5); // chop into the blob bytes
    auto res = ParseAndBakeRecipeBlobBuffer(full.data(), static_cast<int32_t>(full.size()));
    EXPECT_FALSE(res.ok);
    EXPECT_NE(res.err.find("truncated"), std::string::npos) << "err was: " << res.err;
}

TEST(RecipeBootIngest, BadContainerInBuffer_FailsLoudNamesRecipeId) {
    auto badBlob = MakeSphereBlob(10.0f);
    badBlob[0] = 0xFF; // corrupt VRC1 magic
    auto buf = PackBuffer({ {99u, badBlob} });
    auto res = ParseAndBakeRecipeBlobBuffer(buf.data(), static_cast<int32_t>(buf.size()));
    EXPECT_FALSE(res.ok);
    EXPECT_NE(res.err.find("99"), std::string::npos) << "err was: " << res.err;
}

TEST(RecipeBootIngest, DuplicateRecipeId_FailsLoud) {
    auto buf = PackBuffer({ {5u, MakeSphereBlob(10.0f)}, {5u, MakeSphereBlob(11.0f)} });
    auto res = ParseAndBakeRecipeBlobBuffer(buf.data(), static_cast<int32_t>(buf.size()));
    EXPECT_FALSE(res.ok);
    EXPECT_NE(res.err.find("registry rejected"), std::string::npos) << "err was: " << res.err;
}
