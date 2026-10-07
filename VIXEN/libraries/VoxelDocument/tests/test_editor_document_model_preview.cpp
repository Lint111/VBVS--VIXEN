#include <gtest/gtest.h>

#include "ActionStack.h"
#include "EditorDocumentModel.h"
#include "Recipe/generated/RecipeContainer.g.h"
#include "Recipe/generated/RecipeSimd.g.hpp"

#include <chrono>
#include <cstdint>
#include <cstring>
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
using Vixen::Editor::DocumentDiagnosticCode;
using Vixen::Editor::EditorDocumentModel;

std::vector<std::uint8_t> ReadFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) return {};
    const std::streamsize size = file.tellg();
    if (size <= 0) return {};
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    file.seekg(0);
    if (!file.read(reinterpret_cast<char*>(bytes.data()), size)) return {};
    return bytes;
}

struct ScopedDocumentFile {
    std::string path;
    ~ScopedDocumentFile() {
        std::error_code ignored;
        std::filesystem::remove(path, ignored);
    }
};

ScopedDocumentFile MakeTempDocumentPath(const std::string& suffix) {
    static std::uint64_t sequence = 0;
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::string path = std::string(VXD_TEST_TEMP_DIR) + "/vxd-test-" +
                             std::to_string(stamp) + "-" +
                             std::to_string(sequence++) + "-" + suffix + ".vxd";
    return {path};
}

bool WriteDocument(const Yeroket::Sdf::Generated::VoxelDocumentView& source,
                   uint32_t layerCount, uint32_t enabledMask, const std::string& path) {
    using namespace Yeroket::Sdf::Generated;
    if (source.header.layerCount == 0u) return false;

    std::vector<VoxelDocLayerHeader> headers(layerCount);
    std::vector<VoxelDocLayerWrite> layers(layerCount);
    for (uint32_t i = 0; i < layerCount; ++i) {
        headers[i] = *source.layers[0].header;
        headers[i].enabled = (i < 32u && ((enabledMask >> i) & 1u) != 0u) ? 1u : 0u;
        layers[i].header = headers[i];
        layers[i].instructions = source.layers[0].instructions;
    }

    size_t required = 0;
    WriteVoxelDocument(source.channels, source.header.channelCount,
                       layers.data(), layerCount, nullptr, 0, required);
    std::vector<std::uint8_t> bytes(required);
    size_t written = 0;
    if (!WriteVoxelDocument(source.channels, source.header.channelCount,
                            layers.data(), layerCount,
                            bytes.data(), bytes.size(), written)) {
        return false;
    }
    bytes.resize(written);

    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) return false;
    file.write(reinterpret_cast<const char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
    return file.good();
}

TEST(EditorDocumentModelPreview, DirectEntryMatchesVrc1FlattenAndExportBytes) {
    const auto golden = ReadFile(VXD_GOLDEN_PATH);
    ASSERT_FALSE(golden.empty());

    EditorDocumentModel model;
    DocumentDiagnostic diagnostic;
    ASSERT_TRUE(model.Load(VXD_GOLDEN_PATH, diagnostic)) << diagnostic.message;
    const uint32_t allLayers = (1u << model.LayerCount()) - 1u;
    ASSERT_TRUE(model.SetEnabledMask(allLayers, diagnostic)) << diagnostic.message;

    std::vector<std::uint8_t> exported;
    ASSERT_TRUE(model.Flatten(exported, diagnostic)) << diagnostic.message;

    std::vector<std::uint8_t> allEnabledOverride(model.LayerCount(), 1u);
    std::vector<std::uint8_t> canonical;
    std::string error;
    ASSERT_TRUE(Vixen::SVO::FlattenVoxelDocument(
        model.View(), &allEnabledOverride, canonical, error)) << error;
    ASSERT_EQ(exported.size(), canonical.size());
    EXPECT_EQ(std::memcmp(exported.data(), canonical.data(), canonical.size()), 0)
        << "VRC1 export path changed while preview bypass was introduced";

    Yeroket::Sdf::Generated::RecipeContainerView oldView{};
    ASSERT_TRUE(Yeroket::Sdf::Generated::ReadRecipeContainer(
        exported.data(), exported.size(), oldView));

    Vixen::SVO::RecipeRegistry::RecipeEntry previewEntry;
    ASSERT_TRUE(model.FlattenToRecipeEntry(previewEntry, diagnostic)) << diagnostic.message;
    ASSERT_EQ(previewEntry.bytecode.size(), oldView.header.instructionCount);
    EXPECT_EQ(std::memcmp(previewEntry.bytecode.data(), oldView.instructions,
                          previewEntry.bytecode.size() * sizeof(previewEntry.bytecode[0])), 0);
    EXPECT_EQ(previewEntry.bakeResolution, oldView.header.bakeResolution);
    EXPECT_FLOAT_EQ(previewEntry.bandVoxels, oldView.header.bandVoxels);
    EXPECT_EQ(previewEntry.brickDepth, oldView.header.brickDepth);
}

TEST(EditorDocumentModelPreview, DirectEntryMatchesVrc1ForLayerOverride) {
    EditorDocumentModel model;
    DocumentDiagnostic diagnostic;
    ASSERT_TRUE(model.Load(VXD_GOLDEN_PATH, diagnostic)) << diagnostic.message;

    constexpr std::uint32_t kBaseAndCut = (1u << 0) | (1u << 2);
    ASSERT_TRUE(model.SetEnabledMask(kBaseAndCut, diagnostic)) << diagnostic.message;
    std::vector<std::uint8_t> exported;
    ASSERT_TRUE(model.Flatten(exported, diagnostic)) << diagnostic.message;
    Yeroket::Sdf::Generated::RecipeContainerView oldView{};
    ASSERT_TRUE(Yeroket::Sdf::Generated::ReadRecipeContainer(
        exported.data(), exported.size(), oldView));

    Vixen::SVO::RecipeRegistry::RecipeEntry previewEntry;
    ASSERT_TRUE(model.FlattenToRecipeEntry(previewEntry, diagnostic)) << diagnostic.message;
    ASSERT_EQ(previewEntry.bytecode.size(), oldView.header.instructionCount);
    EXPECT_EQ(std::memcmp(previewEntry.bytecode.data(), oldView.instructions,
                          previewEntry.bytecode.size() * sizeof(previewEntry.bytecode[0])), 0);
}

TEST(EditorDocumentModelAdmission, Accepts31And32LayersRejects33And256WithTypedDiagnostic) {
    EditorDocumentModel source;
    DocumentDiagnostic diagnostic;
    ASSERT_TRUE(source.Load(VXD_GOLDEN_PATH, diagnostic)) << diagnostic.message;

    for (const uint32_t layerCount : {31u, 32u}) {
        auto path = MakeTempDocumentPath("boundary-" + std::to_string(layerCount));
        ASSERT_TRUE(WriteDocument(source.View(), layerCount, 0xFFFFFFFFu, path.path));

        EditorDocumentModel model;
        ASSERT_TRUE(model.Load(path.path, diagnostic)) << diagnostic.message;
        EXPECT_EQ(model.LayerCount(), layerCount);
        EXPECT_EQ(model.EnabledMask(), 0xFFFFFFFFu >> (32u - layerCount));
        if (layerCount == 32u) {
            EXPECT_TRUE(model.SetLayerEnabled(31u, false, diagnostic)) << diagnostic.message;
            EXPECT_EQ(model.EnabledMask(), 0x7FFFFFFFu);
        }
    }

    for (const uint32_t layerCount : {33u, 256u}) {
        auto path = MakeTempDocumentPath("boundary-" + std::to_string(layerCount));
        ASSERT_TRUE(WriteDocument(source.View(), layerCount, 0xFFFFFFFFu, path.path));

        EditorDocumentModel model;
        EXPECT_FALSE(model.Load(path.path, diagnostic));
        EXPECT_EQ(diagnostic.code, DocumentDiagnosticCode::TooManyLayers);
        EXPECT_NE(diagnostic.message.find(std::to_string(layerCount)), std::string::npos);
    }
}

TEST(EditorDocumentModelReload, InvalidReloadPreservesAcceptedRevisionThenValidReloadCommits) {
    EditorDocumentModel model;
    DocumentDiagnostic diagnostic;
    ASSERT_TRUE(model.Load(VXD_GOLDEN_PATH, diagnostic)) << diagnostic.message;
    ASSERT_TRUE(model.SetLayerEnabled(2u, false, diagnostic)) << diagnostic.message;

    const std::string acceptedPath = model.SourcePath();
    const uint32_t acceptedMask = model.EnabledMask();
    std::vector<std::uint8_t> acceptedFlatten;
    ASSERT_TRUE(model.Flatten(acceptedFlatten, diagnostic)) << diagnostic.message;

    auto invalidPath = MakeTempDocumentPath("malformed");
    const auto goldenBytes = ReadFile(VXD_GOLDEN_PATH);
    ASSERT_GT(goldenBytes.size(), 8u);
    {
        std::ofstream file(invalidPath.path, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(file.good());
        file.write(reinterpret_cast<const char*>(goldenBytes.data()), 8);
        ASSERT_TRUE(file.good());
    }

    EXPECT_FALSE(model.Load(invalidPath.path, diagnostic));
    EXPECT_EQ(diagnostic.code, DocumentDiagnosticCode::MalformedDocument);
    EXPECT_EQ(model.SourcePath(), acceptedPath);
    EXPECT_EQ(model.EnabledMask(), acceptedMask);
    std::vector<std::uint8_t> afterFailedReload;
    ASSERT_TRUE(model.Flatten(afterFailedReload, diagnostic)) << diagnostic.message;
    EXPECT_EQ(afterFailedReload, acceptedFlatten);

    auto validPath = MakeTempDocumentPath("valid-reload");
    const uint32_t replacementMask = acceptedMask ^ 1u;
    ASSERT_TRUE(WriteDocument(model.View(), model.LayerCount(), replacementMask, validPath.path));
    ASSERT_TRUE(model.Load(validPath.path, diagnostic)) << diagnostic.message;
    EXPECT_EQ(model.SourcePath(), validPath.path);
    EXPECT_EQ(model.EnabledMask(), replacementMask);
}

TEST(EditorDocumentModelOperations, ToggleUndoSaveAndReopenUseTheAcceptedDocumentState) {
    EditorDocumentModel model;
    DocumentDiagnostic diagnostic;
    ASSERT_TRUE(model.Load(VXD_GOLDEN_PATH, diagnostic)) << diagnostic.message;

    const uint32_t layerIndex = 2u;
    const uint32_t originalMask = model.EnabledMask();
    const bool wasEnabled = ((originalMask >> layerIndex) & 1u) != 0u;
    Vixen::AppFlow::ActionStack stack;
    stack.LoadActions(Vixen::AppFlow::Generated::kActionDecls,
                      sizeof(Vixen::AppFlow::Generated::kActionDecls) /
                          sizeof(Vixen::AppFlow::Generated::kActionDecls[0]));

    bool operationSucceeded = true;
    std::string operationError;
    const auto dispatch = stack.Dispatch(
        Vixen::AppFlow::Generated::FlowActionId::ToggleLayer,
        [&](bool forward) {
            DocumentDiagnostic operationDiagnostic;
            operationSucceeded = model.SetLayerEnabled(
                layerIndex, forward ? !wasEnabled : wasEnabled, operationDiagnostic);
            operationError = operationDiagnostic.message;
        });
    ASSERT_EQ(dispatch, Vixen::AppFlow::DispatchResult::Ok);
    ASSERT_TRUE(operationSucceeded) << operationError;
    EXPECT_NE(model.EnabledMask(), originalMask);

    EXPECT_EQ(stack.Undo(), Vixen::AppFlow::DispatchResult::Ok);
    ASSERT_TRUE(operationSucceeded) << operationError;
    EXPECT_EQ(model.EnabledMask(), originalMask);

    auto savedPath = MakeTempDocumentPath("undo-save-reopen");
    ASSERT_TRUE(model.Save(savedPath.path, diagnostic)) << diagnostic.message;
    EditorDocumentModel reopened;
    ASSERT_TRUE(reopened.Load(savedPath.path, diagnostic)) << diagnostic.message;
    EXPECT_EQ(reopened.EnabledMask(), originalMask);

    std::vector<std::uint8_t> acceptedCanonical;
    std::vector<std::uint8_t> reopenedCanonical;
    ASSERT_TRUE(model.Flatten(acceptedCanonical, diagnostic)) << diagnostic.message;
    ASSERT_TRUE(reopened.Flatten(reopenedCanonical, diagnostic)) << diagnostic.message;
    EXPECT_EQ(reopenedCanonical, acceptedCanonical);
    EXPECT_EQ(ReadFile(savedPath.path), ReadFile(VXD_GOLDEN_PATH));
}

}  // namespace
