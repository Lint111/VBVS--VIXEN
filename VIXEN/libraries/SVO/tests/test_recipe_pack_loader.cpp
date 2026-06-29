#include <gtest/gtest.h>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>
#include "Recipe/RecipePackLoader.h"
#include "Recipe/generated/RecipeContainer.g.h"
using namespace Vixen::SVO;
using namespace Yeroket::Sdf::Generated;

namespace fs = std::filesystem;

// Build a minimal valid blob (same helper as in test_recipe_ingest.cpp)
static std::vector<uint8_t> MakeBlob(uint8_t opcodeVal = (uint8_t)Recipe::SdfOpCode::Sphere) {
    RecipeContainerHeader h{};
    h.magic = 0x31435256u; h.formatVersion = 1u; h.instructionCount = 1u;
    h.bakeResolution = 64u; h.bandVoxels = 2.5f; h.brickDepth = 3u;
    SdfInstruction in{}; in.opCode = opcodeVal; in.data[3] = 5.0f;
    std::vector<uint8_t> b(sizeof(h) + sizeof(in));
    std::memcpy(b.data(), &h, sizeof(h));
    std::memcpy(b.data() + sizeof(h), &in, sizeof(in));
    return b;
}

// Write a simple pack (manifest + blobs) into a temp dir. Returns manifest path.
static fs::path WriteFixturePack(const fs::path& dir,
                                 bool corruptBlob2 = false)
{
    fs::create_directories(dir);

    // blob1.vrc — valid sphere
    auto b1 = MakeBlob();
    std::ofstream(dir / "blob1.vrc", std::ios::binary).write(
        reinterpret_cast<const char*>(b1.data()), static_cast<std::streamsize>(b1.size()));

    // blob2.vrc — valid box OR corrupt
    std::vector<uint8_t> b2;
    if (corruptBlob2) {
        b2 = {0xDE, 0xAD, 0xBE, 0xEF}; // junk
    } else {
        b2 = MakeBlob((uint8_t)Recipe::SdfOpCode::Box);
    }
    std::ofstream(dir / "blob2.vrc", std::ios::binary).write(
        reinterpret_cast<const char*>(b2.data()), static_cast<std::streamsize>(b2.size()));

    // manifest.txt
    const fs::path mp = dir / "manifest.txt";
    std::ofstream mf(mp);
    mf << "game.sphere 1 blob1.vrc\n";
    mf << "game.box    2 blob2.vrc\n";
    return mp;
}

TEST(RecipePackLoader, LoadsValidPack) {
    const fs::path dir = fs::temp_directory_path() / "vixen_test_pack_valid";
    fs::remove_all(dir);
    auto mp = WriteFixturePack(dir);

    RecipeRegistry reg;
    std::string err;
    ASSERT_TRUE(LoadRecipePack(mp, reg, err)) << err;
    EXPECT_NE(reg.Get(1u), nullptr);
    EXPECT_NE(reg.Get(2u), nullptr);
}

TEST(RecipePackLoader, FailsLoudOnCorruptBlob) {
    const fs::path dir = fs::temp_directory_path() / "vixen_test_pack_corrupt";
    fs::remove_all(dir);
    auto mp = WriteFixturePack(dir, /*corruptBlob2=*/true);

    RecipeRegistry reg;
    std::string err;
    EXPECT_FALSE(LoadRecipePack(mp, reg, err));
    EXPECT_FALSE(err.empty());
    // err should name the offending recipeId (2)
    EXPECT_NE(err.find("2"), std::string::npos) << "err: " << err;
}

TEST(RecipePackLoader, FailsOnMissingManifest) {
    RecipeRegistry reg;
    std::string err;
    EXPECT_FALSE(LoadRecipePack("/nonexistent/path/manifest.txt", reg, err));
    EXPECT_FALSE(err.empty());
}
