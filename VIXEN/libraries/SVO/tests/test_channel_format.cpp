#include <gtest/gtest.h>

// MSVC's <windows.h> (pulled in transitively via ShellOctreeGpu.h -> SdfBake.h
// -> GaiaVoxelWorld.h) defines min/max as macros, colliding with std::max in
// SdfRecipes.h — same class of pre-existing gap fixed in SdfRecipeEval.h and
// every other SVO test file's own top-of-file #undef guard.
#undef far
#undef near
#undef min
#undef max

#include "VoxelChannelFormat.h"
#include "RuntimeMaterialTable.h"
#include "ShellOctreeGpu.h"
#include <filesystem>
#include <fstream>
using namespace Vixen::SVO;
TEST(ChannelFormat, EnumValuesAreStable) {
    EXPECT_EQ(SEM_SDF,0u); EXPECT_EQ(SEM_COLOR,1u); EXPECT_EQ(SEM_ROUGHNESS,2u);
    EXPECT_EQ(SEM_DENSITY,6u);
    EXPECT_EQ(SEM_MATERIAL_ID,7u);
    EXPECT_EQ(SEM_EDGE_SMOOTHNESS,15u);
    EXPECT_EQ(SEM_COUNT,18u);
    EXPECT_EQ(FK_DISTANCE,1u); EXPECT_EQ(FK_DENSITY,2u);
    EXPECT_EQ(SemanticElemCount(SEM_COLOR),3u); EXPECT_EQ(SemanticElemCount(SEM_SDF),1u);
    EXPECT_EQ(SEM_EMISSION,5u);
    // Sampled Lighting Inc3 M3: emissive intensity is SCALAR (RGB tint deferred to spectral).
    EXPECT_EQ(SemanticElemCount(SEM_EMISSION),1u);
}

TEST(ChannelFormat, RuntimeMaterialVocabularyCarriesLayoutWithoutPackRows) {
    EXPECT_EQ(MAT_COUNT, 0u);
    ASSERT_EQ(kMaterialPackRowFieldCount, 4u);
    EXPECT_STREQ(kMaterialPackRowFieldNames[0], "id");
    EXPECT_STREQ(kMaterialPackRowFieldTypes[0], "NamespacedId");
    EXPECT_STREQ(kMaterialPackRowFieldNames[1], "color_hex");
    EXPECT_STREQ(kMaterialPackRowFieldTypes[1], "uint32");
    EXPECT_STREQ(kMaterialPackRowFieldNames[2], "default_edge_smoothness");
    EXPECT_STREQ(kMaterialPackRowFieldTypes[2], "uint8");
    EXPECT_STREQ(kMaterialPackRowFieldNames[3], "visibility");
    EXPECT_STREQ(kMaterialPackRowFieldTypes[3], "uint8");

    const MaterialPackRow row{"core.rock", 0xA5946Cu, 192u, 1u};
    const MaterialPaletteSpec spec = MaterialPaletteSpecFromPackRow(3u, row);
    EXPECT_EQ(spec.id, 3u);
    EXPECT_EQ(spec.srgbColor, 0xA5946Cu);
    EXPECT_EQ(spec.defaultEdgeSmoothness, 192u);
    EXPECT_EQ(spec.visibility, 1u);
    EXPECT_STREQ(spec.name, "core.rock");
    EXPECT_GT(spec.linearAlbedo[0], 0.0f);
}

TEST(ChannelFormat, RuntimeMaterialTableLoadsPackRowsInDeclarationOrder) {
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "vixen-edmat2-material-table-test";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root / "core", ec);
    ASSERT_FALSE(ec);
    {
    std::ofstream out(root / "core" / "materials.utdl");
        out << "namespace core { type: material\n"
               "{ id: rock, color_hex: \"#A5946C\", default_edge_smoothness: 192, visibility: 1 }\n"
               "{ id: ice, color_hex: \"#BBC6D9\", default_edge_smoothness: 240, visibility: 0 }\n}\n";
    }
    std::filesystem::create_directories(root / "content", ec);
    {
        std::ofstream out(root / "content" / "content-ids.g.json");
        out << R"({"ids":["0","core:rock","core:other","core:ice"]})";
    }
    const RuntimeMaterialTable table = RuntimeMaterialTable::LoadFromPackRoot(root.string());
    ASSERT_TRUE(table.Loaded()) << table.Diagnostic();
    ASSERT_EQ(table.Rows().size(), 2u);
    EXPECT_EQ(table.RefFor("core:rock"), 1u);
    EXPECT_EQ(table.RefFor("core:ice"), 2u);
    EXPECT_EQ(table.GlobalRefFor("core:rock"), 2u);
    EXPECT_EQ(table.GlobalRefFor("core:ice"), 4u);
    EXPECT_EQ(table.RuntimeRefForGlobal(2u), 1u);
    EXPECT_EQ(table.RuntimeRefForGlobal(4u), 2u);
    EXPECT_EQ(table.Rows()[0].defaultEdgeSmoothness, 192u);
    EXPECT_EQ(table.BuildGpuBuffer().size(), 64u * sizeof(GPUMaterial));
    std::filesystem::remove_all(root, ec);
}
TEST(ChannelFormat, OctreeConfigStillMatchesShaderStride) {
    EXPECT_EQ(sizeof(OctreeConfig), 432u);
}
