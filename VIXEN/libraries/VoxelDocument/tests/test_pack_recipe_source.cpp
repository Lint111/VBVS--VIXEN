#include <gtest/gtest.h>

#include "EditorDocumentModel.h"
#include "PackRecipeSource.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#ifndef VXD_GOLDEN_PATH
#error "VXD_GOLDEN_PATH must be defined by CMake"
#endif
#ifndef VXD_TEST_TEMP_DIR
#error "VXD_TEST_TEMP_DIR must be defined by CMake"
#endif

namespace {

using Vixen::Editor::DocumentDiagnostic;
using Vixen::Editor::EditorDocumentModel;
using Vixen::Editor::PackRecipeSource;

constexpr const char* kSchema = R"({
  "//": "test projection of the generated schemas.json",
  "render_recipe": { "fields": [], "assetFiles": { "directory": "recipes", "extension": ".vxd" } }
})";

constexpr const char* kManifest = "UTDL\n// a comment that mentions id: wrong\n{\n  type: manifest,\n  id: probe,\n  version: \"1.0.0\",\n}\n";

void WriteText(const std::filesystem::path& path, const std::string& text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary) << text;
}

struct ScopedPack {
    std::filesystem::path root;
    ScopedPack() {
        static int sequence = 0;
        root = std::filesystem::path(VXD_TEST_TEMP_DIR) /
               ("pack-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
                "-" + std::to_string(sequence++));
        WriteText(root / "manifest.utdl", kManifest);
        WriteText(root / "schemas.json", kSchema);
        std::filesystem::create_directories(root / "recipes");
        std::filesystem::copy_file(VXD_GOLDEN_PATH, root / "recipes" / "boulder.vxd");
        std::filesystem::copy_file(VXD_GOLDEN_PATH, root / "recipes" / "anvil.vxd");
        WriteText(root / "recipes" / "recipes.utdl", "// production recipes live beside the documents\n");
    }
    ~ScopedPack() {
        std::error_code ignored;
        std::filesystem::remove_all(root, ignored);
    }
};

TEST(PackRecipeSource, ReadsTheAssetFacetFromTheSchemaAndThePackIdFromTheManifest) {
    std::string directory, extension, packId, error;
    ASSERT_TRUE(PackRecipeSource::ParseAssetFacet(kSchema, directory, extension, error)) << error;
    EXPECT_EQ(directory, "recipes");
    EXPECT_EQ(extension, ".vxd");
    ASSERT_TRUE(PackRecipeSource::ParseManifestId(kManifest, packId, error)) << error;
    EXPECT_EQ(packId, "probe");

    EXPECT_FALSE(PackRecipeSource::ParseAssetFacet(R"({"render_recipe": {"fields": []}})", directory, extension, error));
    EXPECT_FALSE(PackRecipeSource::ParseManifestId("{ type: manifest }", packId, error));
}

TEST(PackRecipeSource, ListsThePacksDocumentsByContentIdInOrdinalOrder) {
    ScopedPack pack;
    PackRecipeSource source;
    std::string error;
    ASSERT_TRUE(source.Configure(pack.root.string(), (pack.root / "schemas.json").string(), error)) << error;

    ASSERT_EQ(source.Recipes().size(), 2u);
    EXPECT_EQ(source.Recipes()[0].id, "probe:anvil");
    EXPECT_EQ(source.Recipes()[1].id, "probe:boulder");
    std::string path;
    ASSERT_TRUE(source.TryResolve("probe:boulder", path));
    EXPECT_EQ(std::filesystem::path(path).filename(), "boulder.vxd");
    EXPECT_FALSE(source.TryResolve("probe:recipes", path));
    EXPECT_FALSE(source.TryResolve("core:boulder", path));
}

// The R765 round trip at the document level: open a pack recipe, toggle a layer and change a parameter,
// save it back into the pack, reopen it -- every layer is still there with its edited state.
TEST(PackRecipeSource, SavedDocumentReopensWithItsLayersAndEdits) {
    ScopedPack pack;
    PackRecipeSource source;
    std::string error;
    ASSERT_TRUE(source.Configure(pack.root.string(), (pack.root / "schemas.json").string(), error)) << error;
    std::string path;
    ASSERT_TRUE(source.TryResolve("probe:boulder", path));

    EditorDocumentModel model;
    DocumentDiagnostic diagnostic;
    ASSERT_TRUE(model.Load(path, diagnostic)) << diagnostic.message;
    const uint32_t layerCount = model.LayerCount();
    ASSERT_GE(layerCount, 2u);
    ASSERT_GE(model.ParameterCount(), 1u);
    const float edited = model.ParameterMax(0);
    ASSERT_TRUE(model.SetLayerEnabled(layerCount - 1u, false, diagnostic)) << diagnostic.message;
    ASSERT_TRUE(model.SetParameterValue(0, edited, diagnostic)) << diagnostic.message;

    std::vector<uint8_t> bytes;
    ASSERT_TRUE(model.Serialize(bytes, diagnostic)) << diagnostic.message;
    ASSERT_TRUE(PackRecipeSource::WriteAtomically(path, bytes, error)) << error;
    EXPECT_FALSE(std::filesystem::exists(path + ".saving"));

    EditorDocumentModel reopened;
    ASSERT_TRUE(reopened.Load(path, diagnostic)) << diagnostic.message;
    EXPECT_EQ(reopened.LayerCount(), layerCount);
    EXPECT_EQ(reopened.EnabledMask(), model.EnabledMask());
    EXPECT_EQ(reopened.ParameterValue(0), edited);
    for (uint32_t i = 0; i < layerCount; ++i) EXPECT_EQ(reopened.LayerName(i), model.LayerName(i));

    // Saving never drops a flattened container into the pack.
    for (const auto& entry : std::filesystem::directory_iterator(pack.root / "recipes"))
        EXPECT_NE(entry.path().extension(), ".vrc");
}

}  // namespace
