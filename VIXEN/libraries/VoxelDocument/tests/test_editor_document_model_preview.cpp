#include <gtest/gtest.h>
#include "Recipe/RecipeBaker.h"

#include "ActionStack.h"
#include "EditorDocumentFraming.h"
#include "EditorDocumentModel.h"
#include "VoxelChannelFormat.h"
#include "Recipe/generated/RecipeContainer.g.h"
#include "Recipe/generated/RecipeSimd.g.hpp"
#include "Recipe/RecipeBounds.h"
#include "Recipe/SdfRecipeEval.h"
#include "Recipe/RecipeServices.h"

#include <chrono>
#include <algorithm>
#include <array>
#include <cstdlib>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <initializer_list>
#include <iostream>
#include <limits>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#ifndef VXD_GOLDEN_PATH
#error "VXD_GOLDEN_PATH must be defined by CMake"
#endif
#ifndef VXD_ASTEROID_PATH
#error "VXD_ASTEROID_PATH must be defined by CMake"
#endif
#ifndef VXD_BOULDER_PATH
#error "VXD_BOULDER_PATH must be defined by CMake"
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

bool SameBytes(const std::vector<Yeroket::Sdf::Generated::SdfInstruction>& a,
               const std::vector<Yeroket::Sdf::Generated::SdfInstruction>& b) {
    return a.size() == b.size() &&
           (a.empty() || std::memcmp(a.data(), b.data(), a.size() * sizeof(a.front())) == 0);
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
                       source.parameters, source.header.parameterCount,
                       layers.data(), layerCount, nullptr, 0, required);
    std::vector<std::uint8_t> bytes(required);
    size_t written = 0;
    if (!WriteVoxelDocument(source.channels, source.header.channelCount,
                            source.parameters, source.header.parameterCount,
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

TEST(EditorDocumentModelBake, ExportExternalVxdWhenRequested) {
    const char* input = std::getenv("EDMAT11_PARITY_VXD");
    const char* output = std::getenv("EDMAT11_PARITY_VRC1");
    if (input == nullptr || output == nullptr || *input == '\0' || *output == '\0') {
        GTEST_SKIP() << "set EDMAT11_PARITY_VXD and EDMAT11_PARITY_VRC1 for the external bake-parity witness";
    }

    EditorDocumentModel model;
    DocumentDiagnostic diagnostic;
    ASSERT_TRUE(model.Load(input, diagnostic)) << diagnostic.message;
    std::vector<std::uint8_t> flattened;
    ASSERT_TRUE(model.Flatten(flattened, diagnostic)) << diagnostic.message;
    std::ofstream file(output, std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(file.good()) << output;
    file.write(reinterpret_cast<const char*>(flattened.data()),
               static_cast<std::streamsize>(flattened.size()));
    ASSERT_TRUE(file.good()) << output;
}

TEST(EditorDocumentModelBake, ExportParityFixtureWhenRequested) {
    const char* input = std::getenv("EDMAT12_PARITY_INPUT");
    const char* output = std::getenv("EDMAT12_PARITY_OUTPUT");
    const char* mode = std::getenv("EDMAT12_PARITY_MODE");
    if (input == nullptr || output == nullptr || mode == nullptr || *input == '\0' ||
        *output == '\0' || *mode == '\0') {
        GTEST_SKIP() << "set EDMAT12_PARITY_INPUT, EDMAT12_PARITY_OUTPUT, and "
                        "EDMAT12_PARITY_MODE for the editor-save parity fixture";
    }

    EditorDocumentModel model;
    DocumentDiagnostic diagnostic;
    ASSERT_TRUE(model.Load(input, diagnostic)) << diagnostic.message;

    for (uint32_t layer = 0u; layer < model.LayerCount(); ++layer) {
        ASSERT_TRUE(model.ClearLayerMaterial(model.LayerId(layer), diagnostic))
            << diagnostic.message;
    }
    if (std::string(mode) == "two-material") {
        ASSERT_TRUE(model.SetDocumentDefaultMaterial({}, diagnostic)) << diagnostic.message;
        ASSERT_TRUE(model.SetLayerMaterial(
            model.LayerId(0u), "core:hull_metal_major", diagnostic))
            << diagnostic.message;
        ASSERT_TRUE(model.SetLayerMaterial(
            model.LayerId(1u), "core:emissive_trim", diagnostic))
            << diagnostic.message;
    } else if (std::string(mode) == "default-only") {
        ASSERT_TRUE(model.SetDocumentDefaultMaterial("core:emissive_trim", diagnostic))
            << diagnostic.message;
    } else if (std::string(mode) == "neutral") {
        ASSERT_TRUE(model.SetDocumentDefaultMaterial({}, diagnostic)) << diagnostic.message;
    } else {
        FAIL() << "unknown EDMAT12_PARITY_MODE: " << mode;
    }

    ASSERT_TRUE(model.Save(output, diagnostic)) << diagnostic.message;
    std::cout << "[bake-parity] saved mode=" << mode << " path=" << output << "\n";
}

TEST(EditorDocumentModelBake, FlattenVrc1MatchesAuthorBakeOutputs) {
    const char* manifestPath = std::getenv("EDMAT12_PARITY_MANIFEST");
    if (manifestPath == nullptr || *manifestPath == '\0') {
        GTEST_SKIP() << "set EDMAT12_PARITY_MANIFEST for the three-case bake parity witness";
    }

    std::ifstream manifest(manifestPath);
    ASSERT_TRUE(manifest.good()) << manifestPath;
    std::string line;
    std::size_t caseCount = 0u;
    while (std::getline(manifest, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream fields(line);
        std::string name;
        std::string vxdPath;
        std::string authorPackPath;
        ASSERT_TRUE(std::getline(fields, name, '\t')) << line;
        ASSERT_TRUE(std::getline(fields, vxdPath, '\t')) << line;
        ASSERT_TRUE(std::getline(fields, authorPackPath, '\t')) << line;
        ASSERT_FALSE(name.empty()) << line;
        ASSERT_FALSE(vxdPath.empty()) << line;
        ASSERT_FALSE(authorPackPath.empty()) << line;

        EditorDocumentModel model;
        DocumentDiagnostic diagnostic;
        ASSERT_TRUE(model.Load(vxdPath, diagnostic)) << name << ": " << diagnostic.message;
        std::vector<std::uint8_t> editorVrc1;
        // The author VRC1 writer preserves the VDC1 reference-table index in MaterialRef. The
        // runtime CPU witness separately resolves these names to palette slots (5 and 8); parity
        // here intentionally compares the serialized editor/author container contract.
        const auto materialResolver = [&model](const std::string& id, uint32_t& materialId) {
            const auto& references = model.References();
            const auto found = std::find(references.begin(), references.end(), id);
            if (found == references.end()) return false;
            materialId = static_cast<uint32_t>(found - references.begin());
            return true;
        };
        ASSERT_TRUE(model.Flatten(editorVrc1, materialResolver, diagnostic))
            << name << ": " << diagnostic.message;

        const auto authorPack = ReadFile(authorPackPath);
        ASSERT_FALSE(authorPack.empty()) << name << ": " << authorPackPath;
        std::vector<std::uint8_t> authorVrc1;
        std::size_t authorOffset = std::string::npos;
        for (std::size_t offset = 0u;
             offset + sizeof(Yeroket::Sdf::Generated::RecipeContainerHeader) <= authorPack.size();
             ++offset) {
            Yeroket::Sdf::Generated::RecipeContainerHeader header{};
            std::memcpy(&header, authorPack.data() + offset, sizeof(header));
            if (header.magic != Yeroket::Sdf::Generated::kRecipeContainerMagic ||
                (header.formatVersion != 1u && header.formatVersion != 2u)) {
                continue;
            }
            const std::size_t containerSize =
                sizeof(header) + static_cast<std::size_t>(header.instructionCount) *
                                     sizeof(Yeroket::Sdf::Generated::SdfInstruction);
            if (containerSize > authorPack.size() - offset) continue;
            Yeroket::Sdf::Generated::RecipeContainerView view{};
            if (!Yeroket::Sdf::Generated::ReadRecipeContainer(
                    authorPack.data() + offset, containerSize, view)) {
                continue;
            }
            if (containerSize != editorVrc1.size()) continue;
            authorVrc1.assign(authorPack.begin() + static_cast<std::ptrdiff_t>(offset),
                              authorPack.begin() + static_cast<std::ptrdiff_t>(offset + containerSize));
            authorOffset = offset;
            break;
        }
        ASSERT_NE(authorOffset, std::string::npos)
            << name << ": no VRC1 container of editor size " << editorVrc1.size()
            << " found in " << authorPackPath;

        std::size_t firstDifference = 0u;
        const std::size_t commonSize = std::min(editorVrc1.size(), authorVrc1.size());
        while (firstDifference < commonSize &&
               editorVrc1[firstDifference] == authorVrc1[firstDifference]) {
            ++firstDifference;
        }
        if (firstDifference == commonSize && editorVrc1.size() == authorVrc1.size()) {
            std::cout << "[bake-parity] case=" << name << " equal bytes=" << editorVrc1.size()
                      << " author-offset=" << authorOffset << "\n";
        } else {
            const auto byteAt = [](const std::vector<std::uint8_t>& bytes, std::size_t offset) {
                return offset < bytes.size() ? static_cast<unsigned>(bytes[offset]) : 256u;
            };
            std::cout << "[bake-parity] case=" << name << " first-difference="
                      << firstDifference << " editor=" << byteAt(editorVrc1, firstDifference)
                      << " author=" << byteAt(authorVrc1, firstDifference)
                      << " editor-size=" << editorVrc1.size()
                      << " author-size=" << authorVrc1.size() << "\n";
            ADD_FAILURE() << name << ": editor Flatten VRC1 differs from author bake at offset "
                          << firstDifference;
        }
        ++caseCount;
    }
    EXPECT_EQ(caseCount, 3u);
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

TEST(EditorDocumentModelParameters, SnapshotValuesAndMetadataSurviveSaveAndRejectStaleUse) {
    EditorDocumentModel model;
    DocumentDiagnostic diagnostic;
    ASSERT_TRUE(model.Load(VXD_GOLDEN_PATH, diagnostic)) << diagnostic.message;

    ASSERT_EQ(model.ParameterCount(), 1u);
    EXPECT_EQ(model.ParameterName(0), "bulgeRadius");
    EXPECT_EQ(model.ParameterUnit(0), "voxels");
    EXPECT_EQ(model.ParameterSlot(0), 0u);
    EXPECT_FLOAT_EQ(model.ParameterDefault(0), 0.6f);
    EXPECT_FLOAT_EQ(model.ParameterMin(0), 0.25f);
    EXPECT_FLOAT_EQ(model.ParameterMax(0), 1.5f);

    const auto original = model.CaptureBakeSnapshot();
    EXPECT_FLOAT_EQ(original.parameterValues[0], 0.6f);
    Vixen::SVO::RecipeRegistry::RecipeEntry originalEntry;
    ASSERT_TRUE(model.FlattenToRecipeEntry(originalEntry, original, diagnostic)) << diagnostic.message;
    ASSERT_EQ(originalEntry.parameterValues.size(), 6u);
    EXPECT_FLOAT_EQ(originalEntry.parameterValues[0], original.parameterValues[0]);

    ASSERT_TRUE(model.SetParameterValue(0u, 0.9f, diagnostic)) << diagnostic.message;
    EXPECT_GT(model.Revision(), original.revision);
    Vixen::SVO::RecipeRegistry::RecipeEntry staleEntry;
    EXPECT_FALSE(model.FlattenToRecipeEntry(staleEntry, original, diagnostic));
    EXPECT_EQ(diagnostic.code, DocumentDiagnosticCode::StaleBakeSnapshot);

    const auto current = model.CaptureBakeSnapshot();
    Vixen::SVO::RecipeRegistry::RecipeEntry currentEntry;
    ASSERT_TRUE(model.FlattenToRecipeEntry(currentEntry, current, diagnostic)) << diagnostic.message;
    EXPECT_FLOAT_EQ(currentEntry.parameterValues[0], 0.9f);

    auto savedPath = MakeTempDocumentPath("parameter-save-reopen");
    ASSERT_TRUE(model.Save(savedPath.path, diagnostic)) << diagnostic.message;
    EditorDocumentModel reopened;
    ASSERT_TRUE(reopened.Load(savedPath.path, diagnostic)) << diagnostic.message;
    EXPECT_EQ(reopened.ParameterName(0), "bulgeRadius");
    EXPECT_EQ(reopened.ParameterUnit(0), "voxels");
    EXPECT_FLOAT_EQ(reopened.ParameterValue(0), 0.9f);
    EXPECT_EQ(reopened.CaptureBakeSnapshot().parameterValues, current.parameterValues);

    Vixen::SVO::RecipeRegistry::RecipeEntry reopenedEntry;
    ASSERT_TRUE(reopened.FlattenToRecipeEntry(reopenedEntry, diagnostic)) << diagnostic.message;
    ASSERT_EQ(reopenedEntry.bytecode.size(), currentEntry.bytecode.size());
    EXPECT_EQ(std::memcmp(reopenedEntry.bytecode.data(), currentEntry.bytecode.data(),
                          currentEntry.bytecode.size() * sizeof(currentEntry.bytecode[0])), 0);
    EXPECT_EQ(reopenedEntry.parameterValues, currentEntry.parameterValues);
}

TEST(EditorDocumentModelOperations, LayerInsertDeleteMoveAndProgramFieldEditPersist) {
    EditorDocumentModel model;
    DocumentDiagnostic diagnostic;
    ASSERT_TRUE(model.Load(VXD_GOLDEN_PATH, diagnostic)) << diagnostic.message;

    Vixen::Editor::EditableDocumentLayer inserted;
    inserted.type = 0u;
    inserted.op = 0u;
    inserted.enabled = false;
    inserted.name = "guide";
    Yeroket::Sdf::Generated::SdfInstruction guideSphere{};
    guideSphere.opCode = 0u;
    guideSphere.data[3] = 0.25f;
    inserted.program.push_back(guideSphere);
    ASSERT_TRUE(model.InsertLayer(1u, inserted, diagnostic)) << diagnostic.message;
    ASSERT_EQ(model.LayerCount(), 4u);
    EXPECT_EQ(model.LayerName(1u), "guide");
    EXPECT_EQ(model.EnabledMask(), 0xDu);

    float oldField = 0.0f;
    ASSERT_TRUE(model.GetLayerProgramField(0u, 0u, 3u, oldField, diagnostic)) << diagnostic.message;
    ASSERT_TRUE(model.SetLayerProgramField(0u, 0u, 3u, oldField + 0.5f, diagnostic)) << diagnostic.message;
    float editedField = 0.0f;
    ASSERT_TRUE(model.GetLayerProgramField(0u, 0u, 3u, editedField, diagnostic)) << diagnostic.message;
    EXPECT_FLOAT_EQ(editedField, oldField + 0.5f);

    ASSERT_TRUE(model.MoveLayer(0u, 2u, diagnostic)) << diagnostic.message;
    EXPECT_EQ(model.LayerName(2u), "base");
    ASSERT_TRUE(model.MoveLayer(2u, 0u, diagnostic)) << diagnostic.message;
    EXPECT_EQ(model.LayerName(0u), "base");

    Vixen::Editor::EditableDocumentLayer removed;
    ASSERT_TRUE(model.DeleteLayer(1u, &removed, diagnostic)) << diagnostic.message;
    EXPECT_EQ(removed.name, "guide");
    ASSERT_EQ(model.LayerCount(), 3u);

    auto savedPath = MakeTempDocumentPath("layer-operations-save-reopen");
    ASSERT_TRUE(model.Save(savedPath.path, diagnostic)) << diagnostic.message;
    EditorDocumentModel reopened;
    ASSERT_TRUE(reopened.Load(savedPath.path, diagnostic)) << diagnostic.message;
    ASSERT_EQ(reopened.LayerCount(), model.LayerCount());
    EXPECT_EQ(reopened.EnabledMask(), model.EnabledMask());
    EXPECT_EQ(reopened.LayerName(0u), "base");
    ASSERT_TRUE(reopened.GetLayerProgramField(0u, 0u, 3u, editedField, diagnostic)) << diagnostic.message;
    EXPECT_FLOAT_EQ(editedField, oldField + 0.5f);
}

// ---- Layer blend, name and placement (E3 catalogue-free half) -------------------------------

TEST(EditorDocumentModelShape, BlendRadiusAndNameRoundTripAndInvalidValuesLeaveTheDocumentUntouched) {
    EditorDocumentModel model;
    DocumentDiagnostic diagnostic;
    ASSERT_TRUE(model.Load(VXD_GOLDEN_PATH, diagnostic)) << diagnostic.message;
    const uint64_t revision = model.Revision();

    ASSERT_TRUE(model.SetLayerBlendRadius(1u, 0.5f, diagnostic)) << diagnostic.message;
    EXPECT_FLOAT_EQ(model.View().layers[1].header->blendRadius, 0.5f);
    ASSERT_TRUE(model.SetLayerName(1u, "notch", diagnostic)) << diagnostic.message;
    EXPECT_EQ(model.LayerName(1u), "notch");
    const std::string longest(EditorDocumentModel::kMaximumLayerNameBytes, 'n');
    ASSERT_TRUE(model.SetLayerName(0u, longest, diagnostic)) << diagnostic.message;
    EXPECT_EQ(model.LayerName(0u), longest);
    const uint64_t accepted = model.Revision();
    EXPECT_GT(accepted, revision);

    EXPECT_FALSE(model.SetLayerBlendRadius(model.LayerCount(), 0.5f, diagnostic));
    EXPECT_EQ(diagnostic.code, DocumentDiagnosticCode::InvalidLayerIndex);
    EXPECT_FALSE(model.SetLayerBlendRadius(1u, -0.25f, diagnostic));
    EXPECT_EQ(diagnostic.code, DocumentDiagnosticCode::InvalidBlendRadius);
    EXPECT_FALSE(model.SetLayerBlendRadius(1u, std::numeric_limits<float>::quiet_NaN(), diagnostic));
    EXPECT_EQ(diagnostic.code, DocumentDiagnosticCode::InvalidBlendRadius);
    EXPECT_FALSE(model.SetLayerName(model.LayerCount(), "x", diagnostic));
    EXPECT_EQ(diagnostic.code, DocumentDiagnosticCode::InvalidLayerIndex);
    EXPECT_FALSE(model.SetLayerName(1u, std::string(32u, 'n'), diagnostic));
    EXPECT_EQ(diagnostic.code, DocumentDiagnosticCode::InvalidLayerName);
    EXPECT_FALSE(model.SetLayerName(1u, "", diagnostic));
    EXPECT_EQ(diagnostic.code, DocumentDiagnosticCode::InvalidLayerName);
    EXPECT_FALSE(model.SetLayerName(1u, std::string("a\0b", 3u), diagnostic));
    EXPECT_EQ(diagnostic.code, DocumentDiagnosticCode::InvalidLayerName);
    EXPECT_EQ(model.Revision(), accepted);
    EXPECT_FLOAT_EQ(model.View().layers[1].header->blendRadius, 0.5f);
    EXPECT_EQ(model.LayerName(1u), "notch");

    auto saved = MakeTempDocumentPath("shape-blend-name");
    ASSERT_TRUE(model.Save(saved.path, diagnostic)) << diagnostic.message;
    EditorDocumentModel reopened;
    ASSERT_TRUE(reopened.Load(saved.path, diagnostic)) << diagnostic.message;
    EXPECT_FLOAT_EQ(reopened.View().layers[1].header->blendRadius, 0.5f);
    EXPECT_EQ(reopened.LayerName(1u), "notch");
    EXPECT_EQ(reopened.LayerName(0u), longest);
}

TEST(EditorDocumentModelShape, TransformInsertsUpdatesAndRemovesTheWrapperAndFlattensToTheHandBuiltProgram) {
    using Yeroket::Sdf::Generated::SdfInstruction;
    using Vixen::SVO::Recipe::SdfOpCode;
    EditorDocumentModel model;
    DocumentDiagnostic diagnostic;
    ASSERT_TRUE(model.Load(VXD_GOLDEN_PATH, diagnostic)) << diagnostic.message;
    ASSERT_TRUE(model.SetEnabledMask(1u, diagnostic)) << diagnostic.message;

    Vixen::Editor::EditableDocumentLayer original;
    ASSERT_TRUE(model.GetLayer(0u, original, diagnostic)) << diagnostic.message;
    Vixen::Editor::LayerTransform identity;
    Vixen::Editor::LayerTransform placed;
    ASSERT_TRUE(model.GetLayerTransform(0u, placed, diagnostic)) << diagnostic.message;
    EXPECT_TRUE(placed.IsIdentity());

    placed.position = {1.0f, 2.0f, 3.0f};
    placed.scale = {2.0f, 2.0f, 2.0f};
    ASSERT_TRUE(model.SetLayerTransform(0u, placed, diagnostic)) << diagnostic.message;

    std::vector<SdfInstruction> expected;
    SdfInstruction xform{};
    xform.opCode = static_cast<uint8_t>(SdfOpCode::Transform);
    xform.data[0] = 1.0f; xform.data[1] = 2.0f; xform.data[2] = 3.0f;
    xform.data[7] = 1.0f;
    xform.data[8] = 0.5f; xform.data[9] = 0.5f; xform.data[10] = 0.5f;
    xform.data[11] = 2.0f;
    SdfInstruction restore{};
    restore.opCode = static_cast<uint8_t>(SdfOpCode::RestorePos);
    expected.push_back(xform);
    expected.insert(expected.end(), original.program.begin(), original.program.end());
    expected.push_back(restore);

    Vixen::SVO::RecipeRegistry::RecipeEntry entry;
    ASSERT_TRUE(model.FlattenToRecipeEntry(entry, diagnostic)) << diagnostic.message;
    ASSERT_EQ(entry.bytecode.size(), expected.size());
    EXPECT_EQ(std::memcmp(entry.bytecode.data(), expected.data(),
                          expected.size() * sizeof(expected[0])), 0)
        << "flattened transformed layer differs from the hand-built Transform..RestorePos program";

    // Updating replaces the wrapper (no nesting) and the euler angles round-trip.
    placed.position = {0.0f, -1.5f, 0.25f};
    placed.rotationDegrees = {10.0f, -35.0f, 80.0f};
    placed.scale = {1.5f, 1.0f, 0.75f};
    ASSERT_TRUE(model.SetLayerTransform(0u, placed, diagnostic)) << diagnostic.message;
    Vixen::Editor::EditableDocumentLayer updated;
    ASSERT_TRUE(model.GetLayer(0u, updated, diagnostic)) << diagnostic.message;
    EXPECT_EQ(updated.program.size(), original.program.size() + 2u);
    Vixen::Editor::LayerTransform read;
    ASSERT_TRUE(model.GetLayerTransform(0u, read, diagnostic)) << diagnostic.message;
    for (int i = 0; i < 3; ++i) {
        EXPECT_NEAR(read.position[i], placed.position[i], 1e-5f);
        EXPECT_NEAR(read.rotationDegrees[i], placed.rotationDegrees[i], 1e-3f);
        EXPECT_NEAR(read.scale[i], placed.scale[i], 1e-5f);
    }

    // An untouched axis never reads back as -0 (the inspector would print it).
    Vixen::Editor::LayerTransform moved;
    moved.position = {1.0f, 0.0f, 0.0f};
    ASSERT_TRUE(model.SetLayerTransform(0u, moved, diagnostic)) << diagnostic.message;
    ASSERT_TRUE(model.GetLayerTransform(0u, read, diagnostic)) << diagnostic.message;
    for (const float degrees : read.rotationDegrees) {
        EXPECT_EQ(degrees, 0.0f);
        EXPECT_FALSE(std::signbit(degrees));
    }

    // A quarter turn about z: inverse rotation quaternion is (0, 0, -sin45, cos45).
    Vixen::Editor::LayerTransform quarter;
    quarter.rotationDegrees = {0.0f, 0.0f, 90.0f};
    ASSERT_TRUE(model.SetLayerTransform(0u, quarter, diagnostic)) << diagnostic.message;
    ASSERT_TRUE(model.GetLayer(0u, updated, diagnostic)) << diagnostic.message;
    EXPECT_NEAR(updated.program.front().data[6], -0.70710678f, 1e-6f);
    EXPECT_NEAR(updated.program.front().data[7], 0.70710678f, 1e-6f);

    // Serialize -> Load keeps the placement, and the identity removes the wrapper again.
    auto saved = MakeTempDocumentPath("shape-transform");
    ASSERT_TRUE(model.Save(saved.path, diagnostic)) << diagnostic.message;
    EditorDocumentModel reopened;
    ASSERT_TRUE(reopened.Load(saved.path, diagnostic)) << diagnostic.message;
    ASSERT_TRUE(reopened.GetLayerTransform(0u, read, diagnostic)) << diagnostic.message;
    EXPECT_NEAR(read.rotationDegrees[2], 90.0f, 1e-3f);
    ASSERT_TRUE(model.SetLayerTransform(0u, identity, diagnostic)) << diagnostic.message;
    ASSERT_TRUE(model.GetLayer(0u, updated, diagnostic)) << diagnostic.message;
    EXPECT_TRUE(SameBytes(updated.program, original.program));

    // SetLayerProgram is the exact inverse of a placement edit.
    ASSERT_TRUE(model.SetLayerTransform(0u, placed, diagnostic)) << diagnostic.message;
    ASSERT_TRUE(model.SetLayerProgram(0u, original.program, diagnostic)) << diagnostic.message;
    ASSERT_TRUE(model.GetLayer(0u, updated, diagnostic)) << diagnostic.message;
    EXPECT_TRUE(SameBytes(updated.program, original.program));
}

TEST(EditorDocumentModelShape, InvalidTransformAndProgramAreRejectedWithoutChangingTheRevision) {
    EditorDocumentModel model;
    DocumentDiagnostic diagnostic;
    ASSERT_TRUE(model.Load(VXD_GOLDEN_PATH, diagnostic)) << diagnostic.message;
    const uint64_t revision = model.Revision();
    Vixen::Editor::LayerTransform bad;
    EXPECT_FALSE(model.SetLayerTransform(model.LayerCount(), bad, diagnostic));
    EXPECT_EQ(diagnostic.code, DocumentDiagnosticCode::InvalidLayerIndex);
    bad.scale = {1.0f, 0.0f, 1.0f};
    EXPECT_FALSE(model.SetLayerTransform(0u, bad, diagnostic));
    EXPECT_EQ(diagnostic.code, DocumentDiagnosticCode::InvalidTransform);
    bad.scale = {1.0f, -2.0f, 1.0f};
    EXPECT_FALSE(model.SetLayerTransform(0u, bad, diagnostic));
    bad.scale = {1.0f, 1.0f, 1.0f};
    bad.position = {std::numeric_limits<float>::infinity(), 0.0f, 0.0f};
    EXPECT_FALSE(model.SetLayerTransform(0u, bad, diagnostic));
    EXPECT_EQ(diagnostic.code, DocumentDiagnosticCode::InvalidTransform);
    std::vector<Yeroket::Sdf::Generated::SdfInstruction> program(1u);
    program[0].data[0] = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(model.SetLayerProgram(0u, program, diagnostic));
    EXPECT_EQ(diagnostic.code, DocumentDiagnosticCode::InvalidProgramField);
    EXPECT_FALSE(model.SetLayerProgram(model.LayerCount(), program, diagnostic));
    EXPECT_EQ(model.Revision(), revision);
}

TEST(EditorDocumentFraming, FramesThreeDifferentlyBoundedDocumentsAndParameterizedRecipe) {
    using Vixen::SVO::Recipe::SdfInstruction;
    using Vixen::SVO::Recipe::SdfOpCode;

    const SdfInstruction sphere = [] {
        SdfInstruction instruction{};
        instruction.opCode = static_cast<uint8_t>(SdfOpCode::Sphere);
        instruction.data[3] = 0.5f;
        return instruction;
    }();
    const SdfInstruction box = [] {
        SdfInstruction instruction{};
        instruction.opCode = static_cast<uint8_t>(SdfOpCode::Box);
        instruction.data[0] = 1.0f;
        instruction.data[1] = 2.0f;
        instruction.data[2] = 0.75f;
        return instruction;
    }();
    const SdfInstruction cylinder = [] {
        SdfInstruction instruction{};
        instruction.opCode = static_cast<uint8_t>(SdfOpCode::Cylinder);
        instruction.data[0] = 3.0f;
        instruction.data[1] = 1.0f;
        return instruction;
    }();
    const std::vector<std::vector<SdfInstruction>> programs = {
        {sphere}, {box}, {cylinder},
    };

    std::vector<Vixen::Editor::EditorCameraFrame> frames;
    for (const auto& program : programs) {
        Vixen::SVO::RecipeRegistry::RecipeEntry entry;
        entry.bytecode = program;
        const auto analysis = Vixen::SVO::Recipe::AnalyzeRecipeRegistration(entry, 24.0f, 0.9f);
        ASSERT_EQ(analysis.boundSource,
                  Vixen::SVO::Recipe::RecipeRegistrationAnalysis::BoundSource::Derived);
        frames.push_back(Vixen::Editor::FitEditorCameraToBounds(
            entry.boundCenter, entry.boundRadius, glm::vec3(32.0f), 64));
    }
    ASSERT_EQ(frames.size(), 3u);
    EXPECT_NE(frames[0].distance, frames[1].distance);
    EXPECT_NE(frames[1].distance, frames[2].distance);
    EXPECT_LT(frames[0].distance, frames[1].distance);
    EXPECT_LT(frames[0].distance, frames[2].distance);
    for (const auto& frame : frames) {
        EXPECT_GT(frame.distance, 0.0f);
        EXPECT_NEAR(frame.center.x, 25.0f, 1e-5f);
        EXPECT_NEAR(frame.center.y, 25.0f, 1e-5f);
        EXPECT_NEAR(frame.center.z, 25.0f, 1e-5f);
        EXPECT_GE(frame.distance * std::sin(45.0f * 3.14159265358979323846f / 360.0f),
                  frame.worldRadius);
    }

    const SdfInstruction parameterized[] = {
        sphere,
        [] {
            SdfInstruction instruction{};
            instruction.opCode = static_cast<uint8_t>(SdfOpCode::ReadParam);
            instruction.paramMask = 1u;
            instruction.data[0] = 0.0f;
            return instruction;
        }(),
        [] {
            SdfInstruction instruction{};
            instruction.opCode = static_cast<uint8_t>(SdfOpCode::MathSub);
            return instruction;
        }(),
    };
    const float parameterValues[] = {1.25f};
    Vixen::SVO::RecipeRegistry::RecipeEntry parameterizedEntry;
    parameterizedEntry.bytecode.assign(parameterized, parameterized + 3u);
    parameterizedEntry.parameterValues.assign(parameterValues, parameterValues + 1u);
    const auto parameterizedAnalysis = Vixen::SVO::Recipe::AnalyzeRecipeRegistration(
        parameterizedEntry, 24.0f, 0.9f);
    ASSERT_EQ(parameterizedAnalysis.boundSource,
              Vixen::SVO::Recipe::RecipeRegistrationAnalysis::BoundSource::Derived);
    EXPECT_GE(parameterizedEntry.boundRadius, 1.75f);
}

TEST(EditorDocumentFraming, StoredAndProceduralPlacementShareTheCameraBoundsSource) {
    const glm::vec3 recipeCenter(2.0f, -1.0f, 0.5f);
    constexpr float recipeRadius = 12.0f;
    const auto procedural = Vixen::Editor::PlaceEditorBody(recipeCenter, recipeRadius, false);
    const auto stored = Vixen::Editor::PlaceEditorBody(recipeCenter, recipeRadius, true);
    const auto proceduralFrame = Vixen::Editor::FitEditorCameraToWorldBounds(
        procedural.worldCenter, procedural.worldRadius, 60.0f, 1.15f);
    const auto storedFrame = Vixen::Editor::FitEditorCameraToWorldBounds(
        stored.worldCenter, stored.worldRadius, 60.0f, 1.15f);

    EXPECT_NEAR(stored.worldCenter.x, procedural.worldCenter.x, 1e-5f);
    EXPECT_NEAR(stored.worldCenter.y, procedural.worldCenter.y, 1e-5f);
    EXPECT_NEAR(stored.worldCenter.z, procedural.worldCenter.z, 1e-5f);
    EXPECT_NEAR(stored.worldRadius, procedural.worldRadius, 1e-5f);
    EXPECT_NEAR(storedFrame.center.x, proceduralFrame.center.x, 1e-5f);
    EXPECT_NEAR(storedFrame.center.y, proceduralFrame.center.y, 1e-5f);
    EXPECT_NEAR(storedFrame.center.z, proceduralFrame.center.z, 1e-5f);
    EXPECT_NEAR(storedFrame.worldRadius, proceduralFrame.worldRadius, 1e-5f);
    EXPECT_NEAR(storedFrame.distance, proceduralFrame.distance, 1e-5f);
}

// ---- R872.1 catalogue consumer -------------------------------------------------------------

// Samples a flattened recipe on a coarse grid and reports whether it has a surface (both signs).
bool HasSurface(const Vixen::SVO::RecipeRegistry::RecipeEntry& entry) {
    float lowest = std::numeric_limits<float>::infinity();
    float highest = -std::numeric_limits<float>::infinity();
    for (int x = 0; x <= 20; ++x) for (int y = 0; y <= 20; ++y) for (int z = 0; z <= 20; ++z) {
        const glm::vec3 p(-2.0f + 0.2f * x, -2.0f + 0.2f * y, -2.0f + 0.2f * z);
        const float d = Vixen::SVO::Recipe::evalRecipe(entry.bytecode.data(),
                                                       static_cast<uint32_t>(entry.bytecode.size()), p);
        lowest = std::min(lowest, d);
        highest = std::max(highest, d);
    }
    return lowest < 0.0f && highest > 0.0f;
}

TEST(EditorDocumentModelCatalogue, EveryCreatablePrimitiveMakesALayerThatSerializesLoadsFlattensAndHasASurface) {
    using Vixen::SVO::Recipe::SdfOpCategory;
    using Vixen::SVO::Recipe::SdfOpCode;
    EditorDocumentModel model;
    DocumentDiagnostic diagnostic;
    ASSERT_TRUE(model.Load(VXD_GOLDEN_PATH, diagnostic)) << diagnostic.message;
    const uint32_t original = model.LayerCount();

    size_t primitives = 0;
    size_t refused = 0;
    for (const auto& info : Vixen::SVO::Recipe::SdfOpInfoTable) {
        if (info.category != SdfOpCategory::Primitive) continue;
        const auto opcode = static_cast<SdfOpCode>(info.opcode);
        ++primitives;
        std::string reason;
        if (!EditorDocumentModel::CanMakePrimitive(opcode, &reason)) {
            // Instance-typed primitives (no declared default for the instance reference) are refused by name.
            EXPECT_NE(reason.find("declares no default"), std::string::npos) << info.name << ": " << reason;
            ++refused;
            continue;
        }
        Vixen::Editor::EditableDocumentLayer layer;
        ASSERT_TRUE(EditorDocumentModel::MakePrimitiveLayer(opcode, info.name, layer, diagnostic))
            << info.name << ": " << diagnostic.message;
        ASSERT_EQ(layer.program.size(), 1u);
        ASSERT_EQ(layer.program[0].opCode, info.opcode);
        // Every InstructionData argument holds exactly its declared default.
        const auto* arguments = Vixen::SVO::Recipe::SdfOpArguments(info);
        for (uint32_t i = 0; i < info.argumentCount; ++i) {
            if (arguments[i].source != Vixen::SVO::Recipe::SdfArgumentSource::InstructionData) continue;
            for (uint32_t c = 0; c < arguments[i].width; ++c) {
                EXPECT_EQ(layer.program[0].data[arguments[i].offset + c], arguments[i].defaultValue[c])
                    << info.name << "." << arguments[i].name;
            }
        }
        const uint32_t index = model.LayerCount();
        ASSERT_TRUE(model.InsertLayer(index, layer, diagnostic)) << info.name << ": " << diagnostic.message;
        ASSERT_TRUE(model.SetEnabledMask(1u << index, diagnostic)) << diagnostic.message;

        // Serialize -> Load keeps the layer, and flatten + CPU evaluation finds a surface.
        auto saved = MakeTempDocumentPath(std::string("catalogue-") + info.name);
        ASSERT_TRUE(model.Save(saved.path, diagnostic)) << info.name << ": " << diagnostic.message;
        EditorDocumentModel reopened;
        ASSERT_TRUE(reopened.Load(saved.path, diagnostic)) << info.name << ": " << diagnostic.message;
        Vixen::Editor::EditableDocumentLayer reloaded;
        ASSERT_TRUE(reopened.GetLayer(index, reloaded, diagnostic)) << diagnostic.message;
        EXPECT_TRUE(SameBytes(reloaded.program, layer.program)) << info.name;
        Vixen::SVO::RecipeRegistry::RecipeEntry entry;
        ASSERT_TRUE(reopened.FlattenToRecipeEntry(entry, diagnostic)) << info.name << ": " << diagnostic.message;
        EXPECT_TRUE(HasSurface(entry)) << info.name << " at its declared defaults has no surface on [-2,2]^3";

        ASSERT_TRUE(model.DeleteLayer(index, nullptr, diagnostic)) << diagnostic.message;
        ASSERT_TRUE(model.SetEnabledMask(0xFFFFFFFFu & ((1u << original) - 1u), diagnostic)) << diagnostic.message;
    }
    EXPECT_EQ(primitives, 24u);
    EXPECT_EQ(refused, 2u) << "GlyphOutline and PathDistance take typed instance references";
    EXPECT_EQ(model.LayerCount(), original);

    // A non-Primitive opcode is refused.
    Vixen::Editor::EditableDocumentLayer none;
    EXPECT_FALSE(EditorDocumentModel::MakePrimitiveLayer(SdfOpCode::Union, "x", none, diagnostic));
    EXPECT_EQ(diagnostic.code, DocumentDiagnosticCode::InvalidPrimitive);
    EXPECT_FALSE(EditorDocumentModel::MakePrimitiveLayer(SdfOpCode::GlyphOutline, "x", none, diagnostic));
    EXPECT_NE(diagnostic.message.find("glyph"), std::string::npos) << diagnostic.message;
}

TEST(EditorDocumentModelCatalogue, SetLayerOpCoversTheLayerOpTableAndRefusesUnknownValues) {
    EditorDocumentModel model;
    DocumentDiagnostic diagnostic;
    ASSERT_TRUE(model.Load(VXD_GOLDEN_PATH, diagnostic)) << diagnostic.message;
    for (const auto& entry : Vixen::SVO::Recipe::SdfLayerOpTable) {
        ASSERT_TRUE(model.SetLayerOp(1u, entry.layerOp, diagnostic)) << entry.name << ": " << diagnostic.message;
        EXPECT_EQ(model.View().layers[1].header->op, entry.layerOp);
        EXPECT_STREQ(EditorDocumentModel::OpName(entry.layerOp), entry.name);
        auto saved = MakeTempDocumentPath(std::string("layerop-") + entry.name);
        ASSERT_TRUE(model.Save(saved.path, diagnostic)) << diagnostic.message;
        EditorDocumentModel reopened;
        ASSERT_TRUE(reopened.Load(saved.path, diagnostic)) << diagnostic.message;
        EXPECT_EQ(reopened.View().layers[1].header->op, entry.layerOp);
    }
    const uint64_t revision = model.Revision();
    EXPECT_FALSE(model.SetLayerOp(1u, 9u, diagnostic));
    EXPECT_EQ(diagnostic.code, DocumentDiagnosticCode::InvalidLayerOp);
    EXPECT_FALSE(model.SetLayerOp(99u, 0u, diagnostic));
    EXPECT_EQ(diagnostic.code, DocumentDiagnosticCode::InvalidLayerIndex);
    EXPECT_EQ(model.Revision(), revision);
    EXPECT_STREQ(EditorDocumentModel::OpName(9u), "unknown");
}

TEST(EditorDocumentModelCatalogue, PrimitiveFieldsRoundTripAreRangeCheckedAndNonEditableLayersReportIt) {
    using Vixen::SVO::Recipe::SdfOpCode;
    EditorDocumentModel model;
    DocumentDiagnostic diagnostic;
    ASSERT_TRUE(model.Load(VXD_GOLDEN_PATH, diagnostic)) << diagnostic.message;

    Vixen::Editor::EditableDocumentLayer box;
    ASSERT_TRUE(EditorDocumentModel::MakePrimitiveLayer(SdfOpCode::Box, "boxed", box, diagnostic)) << diagnostic.message;
    const uint32_t index = model.LayerCount();
    ASSERT_TRUE(model.InsertLayer(index, box, diagnostic)) << diagnostic.message;

    std::vector<Vixen::Editor::PrimitiveFieldInfo> fields;
    ASSERT_TRUE(model.GetLayerPrimitiveFields(index, fields, diagnostic)) << diagnostic.message;
    ASSERT_EQ(fields.size(), 1u);
    EXPECT_STREQ(fields[0].name, "b");
    EXPECT_EQ(fields[0].width, 3u);
    EXPECT_TRUE(fields[0].hasRange);
    EXPECT_FLOAT_EQ(fields[0].value[0], 0.5f);

    // Set -> get round trip, then Serialize -> Load keeps it.
    ASSERT_TRUE(model.SetLayerPrimitiveField(index, "b", {1.0f, 2.0f, 3.0f, 9.0f}, diagnostic)) << diagnostic.message;
    std::array<float, 4> read{};
    ASSERT_TRUE(model.GetLayerPrimitiveField(index, "b", read, diagnostic)) << diagnostic.message;
    EXPECT_EQ(read[0], 1.0f); EXPECT_EQ(read[1], 2.0f); EXPECT_EQ(read[2], 3.0f);
    Vixen::Editor::EditableDocumentLayer stored;
    ASSERT_TRUE(model.GetLayer(index, stored, diagnostic)) << diagnostic.message;
    EXPECT_EQ(stored.program[0].data[3], 0.0f) << "components beyond the argument width are ignored";
    auto saved = MakeTempDocumentPath("primitive-field");
    ASSERT_TRUE(model.Save(saved.path, diagnostic)) << diagnostic.message;
    EditorDocumentModel reopened;
    ASSERT_TRUE(reopened.Load(saved.path, diagnostic)) << diagnostic.message;
    ASSERT_TRUE(reopened.GetLayerPrimitiveField(index, "b", read, diagnostic)) << diagnostic.message;
    EXPECT_EQ(read[2], 3.0f);

    // A placement wrapper keeps the layer editable (the primitive sits between Transform and RestorePos).
    Vixen::Editor::LayerTransform placed;
    placed.position = {1.0f, 0.0f, 0.0f};
    ASSERT_TRUE(model.SetLayerTransform(index, placed, diagnostic)) << diagnostic.message;
    ASSERT_TRUE(model.SetLayerPrimitiveField(index, "b", {0.25f, 0.25f, 0.25f, 0.0f}, diagnostic)) << diagnostic.message;
    ASSERT_TRUE(model.GetLayerTransform(index, placed, diagnostic)) << diagnostic.message;
    EXPECT_FLOAT_EQ(placed.position[0], 1.0f);

    // Range refusal (the declared range of b is non-negative), non-finite refusal, unknown field: the
    // document and its revision stay untouched.
    const uint64_t revision = model.Revision();
    EXPECT_FALSE(model.SetLayerPrimitiveField(index, "b", {-1.0f, 1.0f, 1.0f, 0.0f}, diagnostic));
    EXPECT_EQ(diagnostic.code, DocumentDiagnosticCode::InvalidPrimitiveField);
    EXPECT_NE(diagnostic.message.find("declared range"), std::string::npos) << diagnostic.message;
    EXPECT_FALSE(model.SetLayerPrimitiveField(index, "b", {std::numeric_limits<float>::quiet_NaN(), 1.0f, 1.0f, 0.0f}, diagnostic));
    EXPECT_FALSE(model.SetLayerPrimitiveField(index, "nope", {1.0f, 1.0f, 1.0f, 0.0f}, diagnostic));
    EXPECT_EQ(model.Revision(), revision);

    // A program that is not Transform + one primitive (+RestorePos) reports its fields as not editable.
    std::vector<Yeroket::Sdf::Generated::SdfInstruction> two = box.program;
    two.push_back(box.program[0]);
    Yeroket::Sdf::Generated::SdfInstruction combine{};
    combine.opCode = static_cast<uint8_t>(SdfOpCode::Union);
    two.push_back(combine);
    ASSERT_TRUE(model.SetLayerProgram(index, two, diagnostic)) << diagnostic.message;
    EXPECT_FALSE(model.GetLayerPrimitiveFields(index, fields, diagnostic));
    EXPECT_EQ(diagnostic.code, DocumentDiagnosticCode::FieldNotEditable);
    EXPECT_FALSE(model.SetLayerPrimitiveField(index, "b", {1.0f, 1.0f, 1.0f, 0.0f}, diagnostic));
    EXPECT_EQ(diagnostic.code, DocumentDiagnosticCode::FieldNotEditable);
}

// sn-editorpicker1: labels come from the generated catalogue, never from a hand list.
TEST(EditorDocumentModelCatalogue, PrimitiveFieldsCarryTheCataloguesLabelAndDefault) {
    using Vixen::SVO::Recipe::SdfOpCategory;
    using Vixen::SVO::Recipe::SdfOpCode;
    EditorDocumentModel model;
    DocumentDiagnostic diagnostic;
    ASSERT_TRUE(model.Load(VXD_GOLDEN_PATH, diagnostic)) << diagnostic.message;
    size_t fieldsChecked = 0;
    for (const auto& info : Vixen::SVO::Recipe::SdfOpInfoTable) {
        if (info.category != SdfOpCategory::Primitive) continue;
        const auto opcode = static_cast<SdfOpCode>(info.opcode);
        if (!EditorDocumentModel::CanMakePrimitive(opcode)) continue;
        Vixen::Editor::EditableDocumentLayer layer;
        ASSERT_TRUE(EditorDocumentModel::MakePrimitiveLayer(opcode, "probe", layer, diagnostic)) << diagnostic.message;
        const uint32_t index = model.LayerCount();
        ASSERT_TRUE(model.InsertLayer(index, layer, diagnostic)) << info.name << ": " << diagnostic.message;
        std::vector<Vixen::Editor::PrimitiveFieldInfo> fields;
        ASSERT_TRUE(model.GetLayerPrimitiveFields(index, fields, diagnostic)) << info.name << ": " << diagnostic.message;
        const auto* arguments = Vixen::SVO::Recipe::SdfOpArguments(info);
        size_t next = 0;
        for (uint32_t i = 0; i < info.argumentCount; ++i) {
            if (arguments[i].source != Vixen::SVO::Recipe::SdfArgumentSource::InstructionData) continue;
            ASSERT_LT(next, fields.size());
            EXPECT_STREQ(fields[next].label, arguments[i].label) << info.name << "." << arguments[i].name;
            EXPECT_NE(std::string(fields[next].label), "") << info.name << "." << arguments[i].name;
            EXPECT_TRUE(fields[next].hasDefault);
            // A freshly made layer holds exactly the declared defaults, which the reset button writes back.
            for (uint32_t c = 0; c < arguments[i].width; ++c) {
                EXPECT_EQ(fields[next].defaultValue[c], arguments[i].defaultValue[c]);
                EXPECT_EQ(fields[next].value[c], fields[next].defaultValue[c]);
                ++fieldsChecked;
            }
            ++next;
        }
        EXPECT_EQ(next, fields.size()) << info.name;
        ASSERT_TRUE(model.DeleteLayer(index, nullptr, diagnostic)) << diagnostic.message;
    }
    EXPECT_GT(fieldsChecked, 20u);
}

TEST(EditorDocumentModelGraph, DerivesNodesAndRoutesArgumentsAndRewritesThroughGeneratedGraph) {
    using Vixen::SVO::Recipe::SdfGraphDiagnosticCode;
    using Vixen::SVO::Recipe::SdfOpCode;
    EditorDocumentModel model;
    DocumentDiagnostic diagnostic;
    ASSERT_TRUE(model.Load(VXD_GOLDEN_PATH, diagnostic)) << diagnostic.message;

    Vixen::Editor::EditableDocumentLayer sphere;
    ASSERT_TRUE(EditorDocumentModel::MakePrimitiveLayer(SdfOpCode::Sphere, "graph", sphere, diagnostic));
    const uint32_t index = model.LayerCount();
    ASSERT_TRUE(model.InsertLayer(index, sphere, diagnostic)) << diagnostic.message;

    const auto* graph = model.GetLayerGraph(index, diagnostic);
    ASSERT_NE(graph, nullptr);
    ASSERT_FALSE(graph->raw);
    ASSERT_EQ(graph->view.nodeCount, 1u);
    ASSERT_STREQ(graph->view.nodes[0].opcodeInfo->name, "Sphere");

    ASSERT_TRUE(model.SetNodeArgument(index, 0u, "radius", {2.5f, 0.0f, 0.0f, 0.0f}, diagnostic))
        << diagnostic.message;
    std::array<float, 4> radius{};
    ASSERT_TRUE(model.GetLayerPrimitiveField(index, "radius", radius, diagnostic)) << diagnostic.message;
    EXPECT_FLOAT_EQ(radius[0], 2.5f);
    const auto original = [&]() {
        Vixen::Editor::EditableDocumentLayer layer;
        EXPECT_TRUE(model.GetLayer(index, layer, diagnostic));
        return layer.program;
    }();

    ASSERT_TRUE(model.InsertNodeAfter(index, 0u, "Round", diagnostic)) << diagnostic.message;
    graph = model.GetLayerGraph(index, diagnostic);
    ASSERT_FALSE(graph->raw);
    ASSERT_EQ(graph->view.nodeCount, 2u);
    EXPECT_STREQ(graph->view.nodes[1].opcodeInfo->name, "Round");
    ASSERT_TRUE(model.RemoveNode(index, 1u, diagnostic)) << diagnostic.message;
    Vixen::Editor::EditableDocumentLayer restored;
    ASSERT_TRUE(model.GetLayer(index, restored, diagnostic)) << diagnostic.message;
    EXPECT_TRUE(SameBytes(restored.program, original));

    ASSERT_TRUE(model.WrapNodes(index, 0u, 0u, "Transform", diagnostic)) << diagnostic.message;
    graph = model.GetLayerGraph(index, diagnostic);
    ASSERT_FALSE(graph->raw);
    ASSERT_EQ(graph->view.nodeCount, 3u);
    EXPECT_STREQ(graph->view.nodes[0].opcodeInfo->name, "Transform");
    EXPECT_STREQ(graph->view.nodes[2].opcodeInfo->name, "RestorePos");
    ASSERT_TRUE(model.SetLayerProgram(index, original, diagnostic)) << diagnostic.message;

    auto invalid = original;
    invalid.front().opCode = 0xFFu;
    ASSERT_TRUE(model.SetLayerProgram(index, invalid, diagnostic)) << diagnostic.message;
    graph = model.GetLayerGraph(index, diagnostic);
    ASSERT_TRUE(graph->raw);
    EXPECT_EQ(graph->diagnostic.code, SdfGraphDiagnosticCode::UnknownOpcode);
    ASSERT_FALSE(model.RemoveNode(index, 0u, diagnostic));
    EXPECT_EQ(diagnostic.code, DocumentDiagnosticCode::InvalidGraphEdit);
    EXPECT_NE(diagnostic.message.find("generated graph diagnostic"), std::string::npos);
}

TEST(EditorDocumentModelGraph, RealUndertowLayersRoundTripArgumentsAndGeneratedRewrites) {
    using Vixen::SVO::Recipe::SdfArgumentSource;

    struct LayerSpec {
        const char* path;
        std::initializer_list<const char*> names;
    };
    const LayerSpec documents[] = {
        {VXD_ASTEROID_PATH, {"body", "ring"}},
        {VXD_BOULDER_PATH, {"body", "lump", "chip"}},
    };

    for (const auto& document : documents) {
        EditorDocumentModel model;
        DocumentDiagnostic diagnostic;
        ASSERT_TRUE(model.Load(document.path, diagnostic)) << document.path << ": " << diagnostic.message;

        for (const char* expectedName : document.names) {
            uint32_t layerIndex = model.LayerCount();
            for (uint32_t i = 0; i < model.LayerCount(); ++i) {
                if (model.LayerName(i) == expectedName) {
                    layerIndex = i;
                    break;
                }
            }
            ASSERT_LT(layerIndex, model.LayerCount()) << document.path << ": missing layer " << expectedName;

            const auto* graph = model.GetLayerGraph(layerIndex, diagnostic);
            ASSERT_NE(graph, nullptr) << diagnostic.message;
            ASSERT_FALSE(graph->raw) << expectedName << ": "
                                      << EditorDocumentModel::GraphDiagnosticMessage(graph->diagnostic);
            ASSERT_GT(graph->view.nodeCount, 0u) << expectedName;

            std::vector<Yeroket::Sdf::Generated::SdfInstruction> before;
            const auto layer = model.View().layers[layerIndex];
            before.assign(layer.instructions, layer.instructions + layer.header->instructionCount);
            struct ArgumentEdit {
                uint32_t nodeIndex;
                std::string name;
                std::array<float, 4> value{};
            };
            std::vector<ArgumentEdit> edits;
            for (uint32_t nodeIndex = 0; nodeIndex < graph->view.nodeCount; ++nodeIndex) {
                const auto& node = graph->view.nodes[nodeIndex];
                const auto& instruction = layer.instructions[node.instructionIndex];
                const auto* arguments = Vixen::SVO::Recipe::SdfOpArguments(*node.opcodeInfo);
                for (uint32_t i = 0; i < node.opcodeInfo->argumentCount; ++i) {
                    const auto& argument = arguments[i];
                    if (argument.source != SdfArgumentSource::InstructionData) continue;
                    ArgumentEdit edit{nodeIndex, argument.name};
                    for (uint32_t component = 0; component < argument.width; ++component)
                        edit.value[component] = instruction.data[argument.offset + static_cast<int16_t>(component)];
                    edits.push_back(std::move(edit));
                }
            }
            ASSERT_GT(edits.size(), 0u) << expectedName;
            for (const auto& edit : edits) {
                ASSERT_TRUE(model.SetNodeArgument(layerIndex, edit.nodeIndex, edit.name, edit.value, diagnostic))
                    << expectedName << ".node" << edit.nodeIndex << "." << edit.name << ": "
                    << diagnostic.message;
            }
            const auto after = model.View().layers[layerIndex];
            ASSERT_EQ(after.header->instructionCount, before.size());
            EXPECT_TRUE(std::equal(before.begin(), before.end(), after.instructions,
                                   [](const auto& lhs, const auto& rhs) {
                                       return std::memcmp(&lhs, &rhs, sizeof(lhs)) == 0;
                                   }))
                << expectedName << ": setting current catalogue values changed bytes";
        }
    }

    EditorDocumentModel model;
    DocumentDiagnostic diagnostic;
    ASSERT_TRUE(model.Load(VXD_BOULDER_PATH, diagnostic)) << diagnostic.message;
    uint32_t bodyIndex = model.LayerCount();
    for (uint32_t i = 0; i < model.LayerCount(); ++i)
        if (model.LayerName(i) == "body") bodyIndex = i;
    ASSERT_LT(bodyIndex, model.LayerCount());

    const auto* graph = model.GetLayerGraph(bodyIndex, diagnostic);
    ASSERT_NE(graph, nullptr);
    ASSERT_FALSE(graph->raw);
    uint32_t sphereNode = graph->view.nodeCount;
    for (uint32_t i = 0; i < graph->view.nodeCount; ++i)
        if (std::string_view(graph->view.nodes[i].opcodeInfo->name) == "Sphere") sphereNode = i;
    ASSERT_LT(sphereNode, graph->view.nodeCount);

    const auto original = [&]() {
        const auto layer = model.View().layers[bodyIndex];
        return std::vector<Yeroket::Sdf::Generated::SdfInstruction>(
            layer.instructions, layer.instructions + layer.header->instructionCount);
    }();

    ASSERT_FALSE(model.RemoveNode(bodyIndex, sphereNode, diagnostic));
    EXPECT_EQ(diagnostic.code, DocumentDiagnosticCode::InvalidGraphEdit);
    EXPECT_NE(diagnostic.message.find("generated graph diagnostic"), std::string::npos);

    ASSERT_TRUE(model.InsertNodeAfter(bodyIndex, sphereNode, "Round", diagnostic)) << diagnostic.message;
    graph = model.GetLayerGraph(bodyIndex, diagnostic);
    ASSERT_FALSE(graph->raw);
    uint32_t roundNode = graph->view.nodeCount;
    for (uint32_t i = 0; i < graph->view.nodeCount; ++i)
        if (std::string_view(graph->view.nodes[i].opcodeInfo->name) == "Round") roundNode = i;
    ASSERT_LT(roundNode, graph->view.nodeCount);
    ASSERT_TRUE(model.RemoveNode(bodyIndex, roundNode, diagnostic)) << diagnostic.message;
    auto restored = model.View().layers[bodyIndex];
    ASSERT_EQ(restored.header->instructionCount, original.size());
    EXPECT_TRUE(std::equal(original.begin(), original.end(), restored.instructions,
                           [](const auto& lhs, const auto& rhs) {
                               return std::memcmp(&lhs, &rhs, sizeof(lhs)) == 0;
                           }));

    graph = model.GetLayerGraph(bodyIndex, diagnostic);
    ASSERT_FALSE(graph->raw);
    ASSERT_TRUE(model.WrapNodes(bodyIndex, 0u, graph->view.nodeCount - 1u, "Transform", diagnostic))
        << diagnostic.message;
    graph = model.GetLayerGraph(bodyIndex, diagnostic);
    ASSERT_FALSE(graph->raw);
    ASSERT_GT(graph->view.nodeCount, 2u);
    ASSERT_TRUE(model.SetLayerProgram(bodyIndex, original, diagnostic)) << diagnostic.message;
    graph = model.GetLayerGraph(bodyIndex, diagnostic);
    ASSERT_FALSE(graph->raw);
    EXPECT_EQ(graph->view.nodeCount, original.size());
}

TEST(EditorDocumentModelCatalogue, DisplayNamesSplitAtCamelCaseBoundariesAndIdsKeepTheOpcodeName) {
    EXPECT_EQ(EditorDocumentModel::DisplayName("Torus"), "Torus");
    EXPECT_EQ(EditorDocumentModel::DisplayName("CappedTorus"), "Capped Torus");
    EXPECT_EQ(EditorDocumentModel::DisplayName("BoxRounded"), "Box Rounded");
    EXPECT_EQ(EditorDocumentModel::DisplayName("SDFBox"), "SDF Box");
    EXPECT_EQ(EditorDocumentModel::DisplayName(""), "");
    // Every creatable primitive's default layer name ("<display> <n>", n up to 32) must fit a layer name.
    for (const auto& choice : EditorDocumentModel::CreatablePrimitives()) {
        EXPECT_EQ(choice.name.find(' '), std::string::npos) << "the exact opcode name is the id";
        EXPECT_LE(choice.display.size() + 3u, EditorDocumentModel::kMaximumLayerNameBytes) << choice.display;
    }
}

TEST(EditorDocumentModelCatalogue, FlyoutRowsAreTheCreatablePrimitivesAndTheFilterRanksPrefixThenWordThenSubstring) {
    using Vixen::SVO::Recipe::SdfOpCategory;
    size_t creatable = 0;
    for (const auto& info : Vixen::SVO::Recipe::SdfOpInfoTable) {
        if (info.category == SdfOpCategory::Primitive &&
            EditorDocumentModel::CanMakePrimitive(static_cast<Vixen::SVO::Recipe::SdfOpCode>(info.opcode))) ++creatable;
    }
    const auto all = EditorDocumentModel::CreatablePrimitives();
    EXPECT_EQ(all.size(), creatable);
    EXPECT_GT(creatable, 0u);
    for (size_t i = 1; i < all.size(); ++i) EXPECT_LT(all[i - 1].name, all[i].name) << "no filter: ordered by name";

    const auto names = [](const std::vector<EditorDocumentModel::PrimitiveChoice>& rows) {
        std::vector<std::string> out;
        for (const auto& row : rows) out.push_back(row.name);
        return out;
    };
    // "tor": Torus (prefix) before CappedTorus (a display word starts with it); nothing else in the catalogue matches.
    EXPECT_EQ(names(EditorDocumentModel::CreatablePrimitives("tor")), (std::vector<std::string>{"Torus", "CappedTorus"}));
    EXPECT_EQ(names(EditorDocumentModel::CreatablePrimitives("TOR")), (std::vector<std::string>{"Torus", "CappedTorus"}));
    EXPECT_EQ(names(EditorDocumentModel::CreatablePrimitives("capped t")), (std::vector<std::string>{"CappedTorus"}));
    EXPECT_TRUE(EditorDocumentModel::CreatablePrimitives("zzzz").empty());
    EXPECT_EQ(EditorDocumentModel::FilterRank("CappedTorus", "orus"), 2) << "a substring inside a word";
    EXPECT_EQ(EditorDocumentModel::FilterRank("CappedTorus", "cap"), 0);
    EXPECT_EQ(EditorDocumentModel::FilterRank("CappedTorus", "x"), -1);
    EXPECT_EQ(EditorDocumentModel::FilterRank("Box", ""), 0);
}

// ---- R872.9 Q1: a new primitive lands visible --------------------------------------------------

namespace {
Vixen::SVO::RecipeRegistry::RecipeEntry FlattenWithMask(EditorDocumentModel& model, uint32_t mask) {
    DocumentDiagnostic diagnostic;
    EXPECT_TRUE(model.SetEnabledMask(mask, diagnostic)) << diagnostic.message;
    Vixen::SVO::RecipeRegistry::RecipeEntry entry;
    EXPECT_TRUE(model.FlattenToRecipeEntry(entry, diagnostic)) << diagnostic.message;
    return entry;
}
float EvalAt(const Vixen::SVO::RecipeRegistry::RecipeEntry& entry, const glm::vec3& p) {
    return Vixen::SVO::Recipe::evalRecipe(entry.bytecode.data(), static_cast<uint32_t>(entry.bytecode.size()), p);
}
}  // namespace

TEST(EditorDocumentModelPlacement, EveryCreatablePrimitiveLandsWithShapeOutsideTheDocumentsSurface) {
    EditorDocumentModel model;
    DocumentDiagnostic diagnostic;
    ASSERT_TRUE(model.Load(VXD_GOLDEN_PATH, diagnostic)) << diagnostic.message;
    const uint32_t original = model.LayerCount();
    const uint32_t originalMask = model.EnabledMask();
    std::array<float, 3> docCenter{};
    float docRadius = 0.0f;
    model.DerivePreviewBounds(docCenter, docRadius);
    ASSERT_GT(docRadius, 1.0f);
    const auto oldEntry = FlattenWithMask(model, originalMask);

    size_t checked = 0;
    for (const auto& choice : EditorDocumentModel::CreatablePrimitives()) {
        Vixen::SVO::Recipe::SdfOpCode opcode{};
        ASSERT_TRUE(Vixen::SVO::Recipe::FindSdfOpCode(choice.name, opcode));
        Vixen::Editor::EditableDocumentLayer layer;
        ASSERT_TRUE(EditorDocumentModel::MakePrimitiveLayer(opcode, choice.name, layer, diagnostic)) << diagnostic.message;
        const uint64_t revision = model.Revision();
        // Placing is pure: it reads the document and changes only the layer it is handed.
        auto again = layer;
        ASSERT_TRUE(model.PlaceNewPrimitive(layer, 0u, diagnostic)) << choice.name << ": " << diagnostic.message;
        ASSERT_TRUE(model.PlaceNewPrimitive(again, 0u, diagnostic)) << diagnostic.message;
        EXPECT_EQ(model.Revision(), revision) << choice.name;
        ASSERT_EQ(layer.program.size(), again.program.size());
        EXPECT_EQ(0, std::memcmp(layer.program.data(), again.program.data(),
                                 layer.program.size() * sizeof(layer.program.front()))) << "deterministic: " << choice.name;
        ASSERT_EQ(layer.program.size(), 3u) << "Transform + primitive + RestorePos: " << choice.name;

        const uint32_t index = model.LayerCount();
        ASSERT_TRUE(model.InsertLayer(index, layer, diagnostic)) << choice.name << ": " << diagnostic.message;
        const auto newEntry = FlattenWithMask(model, 1u << index);

        // Sample inside the new layer's own placement sphere: some point is inside the new shape (d < 0)
        // while the previous union has it outside (d > 0): the shape is visible in principle.
        Vixen::Editor::LayerTransform placed;
        ASSERT_TRUE(model.GetLayerTransform(index, placed, diagnostic)) << diagnostic.message;
        const float reach = EditorDocumentModel::kNewPrimitiveBoundsFraction * docRadius;
        EXPECT_NEAR(placed.scale[0], placed.scale[1], 1e-6f) << "uniform scale";
        const glm::vec3 centre(placed.position[0], placed.position[1], placed.position[2]);
        const float step = reach / 24.0f;
        bool visible = false;
        for (int x = -28; x <= 28 && !visible; ++x) for (int y = -28; y <= 28 && !visible; ++y) for (int z = -28; z <= 28; ++z) {
            const glm::vec3 p = centre + step * glm::vec3(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z));
            if (EvalAt(newEntry, p) < 0.0f && EvalAt(oldEntry, p) > 0.0f) { visible = true; break; }
        }
        EXPECT_TRUE(visible) << choice.name << " has no point outside the previous union's surface (scale " << placed.scale[0] << ")";

        ASSERT_TRUE(model.DeleteLayer(index, nullptr, diagnostic)) << diagnostic.message;
        ASSERT_TRUE(model.SetEnabledMask(originalMask, diagnostic)) << diagnostic.message;
        ++checked;
    }
    EXPECT_EQ(checked, 22u);
    EXPECT_EQ(model.LayerCount(), original);
}

TEST(EditorDocumentModelPlacement, SizeIsTheDeclaredFractionOfTheBoundsAndPositionFollowsTheSelectedLayer) {
    EditorDocumentModel model;
    DocumentDiagnostic diagnostic;
    ASSERT_TRUE(model.Load(VXD_GOLDEN_PATH, diagnostic)) << diagnostic.message;
    std::array<float, 3> docCenter{};
    float docRadius = 0.0f;
    model.DerivePreviewBounds(docCenter, docRadius);

    Vixen::Editor::EditableDocumentLayer sphere;
    ASSERT_TRUE(EditorDocumentModel::MakePrimitiveLayer(Vixen::SVO::Recipe::SdfOpCode::Sphere, "s", sphere, diagnostic));
    const auto own = Vixen::SVO::Recipe::DeriveConservativeBounds(sphere.program.data(), 1u);
    ASSERT_TRUE(own.ok);
    auto placed = sphere;
    ASSERT_TRUE(model.PlaceNewPrimitive(placed, 0u, diagnostic)) << diagnostic.message;
    // Round-trip through the model to read the placement back (no private decoding in the test).
    ASSERT_TRUE(model.InsertLayer(model.LayerCount(), placed, diagnostic));
    const uint32_t index = model.LayerCount() - 1u;
    Vixen::Editor::LayerTransform t0;
    ASSERT_TRUE(model.GetLayerTransform(index, t0, diagnostic));
    EXPECT_NEAR(t0.scale[0] * own.radius, EditorDocumentModel::kNewPrimitiveBoundsFraction * docRadius, 1e-3f * docRadius)
        << "the new shape's bound radius is the declared fraction of the document's";
    // Moving the selected layer moves the landing spot by exactly the same offset.
    Vixen::Editor::LayerTransform base;
    ASSERT_TRUE(model.GetLayerTransform(0u, base, diagnostic));
    auto moved = base;
    moved.position = {base.position[0] + 7.0f, base.position[1] - 3.0f, base.position[2] + 1.0f};
    ASSERT_TRUE(model.SetLayerTransform(0u, moved, diagnostic)) << diagnostic.message;
    auto placedMoved = sphere;
    ASSERT_TRUE(model.PlaceNewPrimitive(placedMoved, 0u, diagnostic)) << diagnostic.message;
    ASSERT_TRUE(model.InsertLayer(model.LayerCount(), placedMoved, diagnostic));
    Vixen::Editor::LayerTransform t1;
    ASSERT_TRUE(model.GetLayerTransform(model.LayerCount() - 1u, t1, diagnostic));
    // The first layer's scale is unchanged by moving it, so its bound radius (and the +Y offset) is the same;
    // only the 'bounds' of the document may differ, which moves the size: compare positions only.
    EXPECT_NEAR(t1.position[0] - t0.position[0], 7.0f, 1e-3f);
    EXPECT_NEAR(t1.position[2] - t0.position[2], 1.0f, 1e-3f);
}

TEST(EditorDocumentModelPlacement, WithNoSelectedLayerTheShapeSitsAtTheDocumentBoundsCentre) {
    EditorDocumentModel model;
    DocumentDiagnostic diagnostic;
    ASSERT_TRUE(model.Load(VXD_GOLDEN_PATH, diagnostic)) << diagnostic.message;
    std::array<float, 3> docCenter{};
    float docRadius = 0.0f;
    model.DerivePreviewBounds(docCenter, docRadius);
    Vixen::Editor::EditableDocumentLayer layer;
    ASSERT_TRUE(EditorDocumentModel::MakePrimitiveLayer(Vixen::SVO::Recipe::SdfOpCode::Sphere, "s", layer, diagnostic));
    ASSERT_TRUE(model.PlaceNewPrimitive(layer, std::nullopt, diagnostic)) << diagnostic.message;
    const uint32_t index = model.LayerCount();
    ASSERT_TRUE(model.InsertLayer(index, layer, diagnostic));
    Vixen::Editor::LayerTransform placed;
    ASSERT_TRUE(model.GetLayerTransform(index, placed, diagnostic));
    for (size_t axis = 0; axis < 3; ++axis) EXPECT_NEAR(placed.position[axis], docCenter[axis], 1e-3f);
}

TEST(EditorDocumentModelPlacement, MaterialSuffixDoesNotChangePreviewBounds) {
    EditorDocumentModel model;
    DocumentDiagnostic diagnostic;
    ASSERT_TRUE(model.Load(VXD_GOLDEN_PATH, diagnostic)) << diagnostic.message;
    std::array<float, 3> neutralCenter{};
    float neutralRadius = 0.0f;
    model.DerivePreviewBounds(neutralCenter, neutralRadius);

    ASSERT_TRUE(model.SetLayerMaterial(model.LayerId(0u), "core:hull_metal_major", diagnostic))
        << diagnostic.message;
    std::array<float, 3> materialCenter{};
    float materialRadius = 0.0f;
    model.DerivePreviewBounds(materialCenter, materialRadius);

    for (size_t axis = 0; axis < 3; ++axis)
        EXPECT_NEAR(materialCenter[axis], neutralCenter[axis], 1e-5f);
    EXPECT_NEAR(materialRadius, neutralRadius, 1e-5f);
}

TEST(EditorDocumentModelVdc1, MaterialsAndRecipeReferencesRoundTripWithValidation) {
    const std::string recipeId = std::string("pack") + ":" + "recipe";
    const auto original = ReadFile(VXD_GOLDEN_PATH);
    ASSERT_FALSE(original.empty());

    EditorDocumentModel model;
    DocumentDiagnostic diagnostic;
    ASSERT_TRUE(model.Load(VXD_GOLDEN_PATH, diagnostic)) << diagnostic.message;
    ASSERT_EQ(model.FormatVersion(), 2u);

    std::vector<std::uint8_t> untouched;
    ASSERT_TRUE(model.Serialize(untouched, diagnostic)) << diagnostic.message;
    EXPECT_EQ(untouched, original) << "an untouched v2 document must remain byte-identical";
    ASSERT_TRUE(model.SetLayerEnabled(0u, false, diagnostic)) << diagnostic.message;
    std::vector<std::uint8_t> toggled;
    ASSERT_TRUE(model.Serialize(toggled, diagnostic)) << diagnostic.message;
    Yeroket::Sdf::Generated::VoxelDocumentView toggledView{};
    ASSERT_TRUE(Yeroket::Sdf::Generated::ReadVoxelDocument(
        toggled.data(), toggled.size(), toggledView));
    EXPECT_EQ(toggledView.header.formatVersion, 2u)
        << "a v2 layer-state edit must not silently upgrade the authored format";

    EXPECT_FALSE(model.SetDocumentMaterials(7u, 0u, 50u, diagnostic));
    EXPECT_EQ(diagnostic.code, DocumentDiagnosticCode::InvalidDocumentMaterial);
    ASSERT_TRUE(model.SetDocumentMaterials(7u, 9u, 75u, diagnostic)) << diagnostic.message;
    EXPECT_EQ(model.FormatVersion(), 3u);
    EXPECT_EQ(model.MajorMaterialId(), 7u);
    EXPECT_EQ(model.MinorMaterialId(), 9u);
    EXPECT_EQ(model.MajorMaterialPercent(), 75u);

    Vixen::SVO::RecipeRegistry::RecipeEntry materialEntry;
    ASSERT_TRUE(model.FlattenToRecipeEntry(materialEntry, model.CaptureBakeSnapshot(), diagnostic))
        << diagnostic.message;
    // Legacy v3 fields remain document metadata. They do not add a typed output instruction;
    // authored v4 MaterialRef/Attach suffixes are the only layer material lowering seam.
    ASSERT_GE(materialEntry.bytecode.size(), 5u);
    EXPECT_NE(materialEntry.bytecode.back().opCode,
              static_cast<uint8_t>(Vixen::SVO::Recipe::SdfOpCode::Output));

    EXPECT_FALSE(model.AddReference("invalid", diagnostic));
    EXPECT_EQ(diagnostic.code, DocumentDiagnosticCode::InvalidReferenceId);
    EXPECT_FALSE(model.AddReference("test-pack:test:too-many-colons", diagnostic));
    EXPECT_EQ(diagnostic.code, DocumentDiagnosticCode::InvalidReferenceId);
    ASSERT_TRUE(model.AddReference(recipeId, diagnostic)) << diagnostic.message;
    EXPECT_FALSE(model.AddReference(recipeId, diagnostic));
    EXPECT_EQ(diagnostic.code, DocumentDiagnosticCode::DuplicateReference);

    Vixen::Editor::EditableDocumentLayer layer;
    ASSERT_TRUE(model.MakeRecipeReferenceLayer(recipeId, "Recipe test", layer, diagnostic))
        << diagnostic.message;
    ASSERT_EQ(layer.program.size(), 1u);
    EXPECT_EQ(layer.program.front().opCode,
              static_cast<uint8_t>(Vixen::SVO::Recipe::SdfOpCode::InvokeRecipe));
    EXPECT_FLOAT_EQ(layer.program.front().data[0], 0.0f);
    ASSERT_TRUE(model.PlaceNewPrimitive(layer, std::nullopt, diagnostic)) << diagnostic.message;
    ASSERT_TRUE(model.InsertLayer(model.LayerCount(), layer, diagnostic)) << diagnostic.message;
    const uint32_t referenceLayer = model.LayerCount() - 1u;
    std::string identified;
    ASSERT_TRUE(model.IsRecipeReferenceLayer(referenceLayer, &identified, diagnostic))
        << diagnostic.message;
    EXPECT_EQ(identified, recipeId);
    EXPECT_EQ(model.FormatVersion(), 4u);
    EXPECT_EQ(model.View().referenceCount, 1u);
    ASSERT_FALSE(model.RemoveReference(recipeId, diagnostic));
    EXPECT_EQ(diagnostic.code, DocumentDiagnosticCode::ReferenceInUse);

    Vixen::SVO::RecipeRegistry::RecipeEntry entry;
    ASSERT_TRUE(model.FlattenToRecipeEntry(
        entry, model.CaptureBakeSnapshot(),
        [&recipeId](const std::string& id, uint32_t& resolved) {
            if (id != recipeId) return false;
            resolved = 77u;
            return true;
        }, false, diagnostic)) << diagnostic.message;
    bool foundResolvedInvoke = false;
    for (const auto& instruction : entry.bytecode) {
        if (instruction.opCode ==
            static_cast<uint8_t>(Vixen::SVO::Recipe::SdfOpCode::InvokeRecipe) &&
            instruction.data[0] == 77.0f) {
            foundResolvedInvoke = true;
            break;
        }
    }
    EXPECT_TRUE(foundResolvedInvoke);

    ASSERT_TRUE(model.DeleteLayer(referenceLayer, nullptr, diagnostic)) << diagnostic.message;
    ASSERT_TRUE(model.RemoveReference(recipeId, diagnostic)) << diagnostic.message;
    EXPECT_TRUE(model.References().empty());
    EXPECT_EQ(model.FormatVersion(), 3u) << "material fields keep the authored feature version current";

    auto saved = MakeTempDocumentPath("vdc1-v4");
    ASSERT_TRUE(model.Save(saved.path, diagnostic)) << diagnostic.message;
    EditorDocumentModel reopened;
    ASSERT_TRUE(reopened.Load(saved.path, diagnostic)) << diagnostic.message;
    EXPECT_EQ(reopened.MajorMaterialId(), 7u);
    EXPECT_EQ(reopened.MinorMaterialId(), 9u);
    EXPECT_EQ(reopened.MajorMaterialPercent(), 75u);
    EXPECT_EQ(reopened.FormatVersion(), 3u);
}

TEST(EditorDocumentModelVdc1, LayerMaterialRoundTripsThroughTypedReferenceAndRestoresBytes) {
    const auto original = ReadFile(VXD_GOLDEN_PATH);
    ASSERT_FALSE(original.empty());

    EditorDocumentModel model;
    DocumentDiagnostic diagnostic;
    ASSERT_TRUE(model.Load(VXD_GOLDEN_PATH, diagnostic)) << diagnostic.message;
    std::vector<uint8_t> before;
    ASSERT_TRUE(model.Serialize(before, diagnostic)) << diagnostic.message;
    ASSERT_EQ(before, original);

    const uint32_t layerId = model.LayerId(0u);
    std::string material;
    ASSERT_TRUE(model.GetLayerMaterial(layerId, material, diagnostic)) << diagnostic.message;
    EXPECT_TRUE(material.empty());
    ASSERT_TRUE(model.SetLayerMaterial(layerId, "core:hull_metal_major", diagnostic))
        << diagnostic.message;
    ASSERT_TRUE(model.GetLayerMaterial(layerId, material, diagnostic)) << diagnostic.message;
    EXPECT_EQ(material, "core:hull_metal_major");

    Vixen::SVO::RecipeRegistry::RecipeEntry entry;
    ASSERT_TRUE(model.FlattenToRecipeEntry(
        entry, model.CaptureBakeSnapshot(), {},
        [](const std::string& id, uint32_t& resolved) {
            if (id != "core:hull_metal_major") return false;
            resolved = 5u;
            return true;
        }, false, diagnostic)) << diagnostic.message;
    bool foundMaterial = false;
    for (const auto& instruction : entry.bytecode) {
        if (instruction.opCode ==
                static_cast<uint8_t>(Vixen::SVO::Recipe::SdfOpCode::MaterialRef) &&
            instruction.data[0] == 5.0f) {
            foundMaterial = true;
            break;
        }
    }
    EXPECT_TRUE(foundMaterial);

    ASSERT_TRUE(model.ClearLayerMaterial(layerId, diagnostic)) << diagnostic.message;
    ASSERT_TRUE(model.Serialize(before, diagnostic)) << diagnostic.message;
    EXPECT_EQ(before, original) << "clearing the only material reference restores the exact document bytes";
}

TEST(EditorDocumentModelVdc1, DocumentDefaultMaterialPersistsAndFeedsFlattenOutput) {
    EditorDocumentModel model;
    DocumentDiagnostic diagnostic;
    ASSERT_TRUE(model.Load(VXD_GOLDEN_PATH, diagnostic)) << diagnostic.message;

    ASSERT_TRUE(model.SetDocumentDefaultMaterial("core:emissive_trim", diagnostic))
        << diagnostic.message;
    EXPECT_EQ(model.DocumentDefaultMaterial(), "core:emissive_trim");
    EXPECT_EQ(model.FormatVersion(), 5u);

    std::vector<uint8_t> savedBytes;
    ASSERT_TRUE(model.Serialize(savedBytes, diagnostic)) << diagnostic.message;
    Yeroket::Sdf::Generated::VoxelDocumentView savedView{};
    ASSERT_TRUE(Yeroket::Sdf::Generated::ReadVoxelDocument(
        savedBytes.data(), savedBytes.size(), savedView));
    EXPECT_EQ(savedView.header.formatVersion, 5u);
    EXPECT_EQ(savedView.defaultMaterialReference, 1u);
    EXPECT_EQ(savedView.header.minorMaterialId, 0u);
    EXPECT_EQ(savedView.header.majorWeightPercent, 0u);

    const auto path = MakeTempDocumentPath("document-default");
    ASSERT_TRUE(model.Save(path.path, diagnostic)) << diagnostic.message;
    EditorDocumentModel reopened;
    ASSERT_TRUE(reopened.Load(path.path, diagnostic)) << diagnostic.message;
    EXPECT_EQ(reopened.DocumentDefaultMaterial(), "core:emissive_trim");
    EXPECT_TRUE(reopened.IsMaterialReference("core:emissive_trim"));
    std::vector<uint8_t> reopenedBytes;
    ASSERT_TRUE(reopened.Serialize(reopenedBytes, diagnostic)) << diagnostic.message;
    EXPECT_EQ(reopenedBytes, savedBytes);

    uint32_t resolvedDefault = 0u;
    Vixen::SVO::RecipeRegistry::RecipeEntry entry;
    const auto materialResolver = [](const std::string& reference, uint32_t& materialId) {
        if (reference != "core:emissive_trim") return false;
        materialId = 8u;
        return true;
    };
    ASSERT_TRUE(reopened.FlattenToRecipeEntry(
        entry, reopened.CaptureBakeSnapshot(), EditorDocumentModel::RecipeReferenceResolver{},
        materialResolver, true, resolvedDefault, diagnostic)) << diagnostic.message;
    EXPECT_EQ(resolvedDefault, 8u);
    EXPECT_EQ(entry.defaultMaterialId, 8u);

    ASSERT_TRUE(reopened.SetDocumentDefaultMaterial({}, diagnostic)) << diagnostic.message;
    EXPECT_TRUE(reopened.DocumentDefaultMaterial().empty());
    EXPECT_FALSE(reopened.IsMaterialReference("core:emissive_trim"));
    std::vector<uint8_t> clearedBytes;
    ASSERT_TRUE(reopened.Serialize(clearedBytes, diagnostic)) << diagnostic.message;
    Yeroket::Sdf::Generated::VoxelDocumentView clearedView{};
    ASSERT_TRUE(Yeroket::Sdf::Generated::ReadVoxelDocument(
        clearedBytes.data(), clearedBytes.size(), clearedView));
    EXPECT_EQ(clearedView.header.formatVersion, 5u);
    EXPECT_EQ(clearedView.defaultMaterialReference, 0u);
}

TEST(EditorDocumentModelMaterialCarry, FlattenAndSimdUseTransparentAndDocumentDefault) {
    constexpr uint32_t kMaterialA = 5u;
    constexpr uint32_t kMaterialB = 8u;
    constexpr std::array<glm::vec3, 4> kFixedPoints = {
        glm::vec3{0.0f, 0.0f, 0.0f},
        glm::vec3{1.0f, 0.9f, 0.0f},
        glm::vec3{0.0f, 0.0f, 1.2f},
        glm::vec3{0.35f, 0.0f, 0.0f},
    };
    const auto materialResolver = [](const std::string& id, uint32_t& materialId) {
        if (id == "core:hull_metal_major") {
            materialId = kMaterialA;
            return true;
        }
        if (id == "core:emissive_trim") {
            materialId = kMaterialB;
            return true;
        }
        return false;
    };

    auto expectMaterialCase = [&](const char* name, EditorDocumentModel& model,
                                  uint32_t expectedDefault, uint32_t expectedMaterial) {
        Vixen::SVO::RecipeRegistry::RecipeEntry entry;
        uint32_t resolvedDefault = 0u;
        DocumentDiagnostic diagnostic;
        ASSERT_TRUE(model.FlattenToRecipeEntry(
            entry, model.CaptureBakeSnapshot(), {}, materialResolver, false,
            resolvedDefault, diagnostic))
            << name << ": " << diagnostic.message;
        ASSERT_EQ(resolvedDefault, expectedDefault) << name;
        ASSERT_EQ(entry.defaultMaterialId, expectedDefault) << name;

        Vixen::SVO::Recipe::LoweredRecipeProgram simd;
        std::string lowerError;
        ASSERT_TRUE(simd.Lower(
            entry.bytecode.data(), static_cast<uint32_t>(entry.bytecode.size()),
            entry.parameterValues, true, entry.defaultMaterialId, lowerError))
            << name << ": " << lowerError;
        ASSERT_EQ(simd.OutputCount(), 2u) << name;
        const auto& distanceOutput = simd.OutputAt(0u);
        const auto& materialOutput = simd.OutputAt(1u);
        ASSERT_EQ(distanceOutput.channel, static_cast<uint32_t>(Vixen::SVO::SEM_SDF)) << name;
        ASSERT_EQ(materialOutput.channel, static_cast<uint32_t>(Vixen::SVO::SEM_MATERIAL_ID)) << name;
        ASSERT_TRUE(materialOutput.materialSidecar) << name;

        glm::vec3 positions[4] = {
            kFixedPoints[0], kFixedPoints[1], kFixedPoints[2], kFixedPoints[3],
        };
        float output[8]{};
        std::string evaluateError;
        ASSERT_TRUE(simd.EvaluateOutputSet4(
            positions, output, std::size(output), nullptr, &evaluateError))
            << name << ": " << evaluateError;
        for (std::size_t lane = 0; lane < kFixedPoints.size(); ++lane) {
            EXPECT_FLOAT_EQ(output[materialOutput.valueOffset * 4u + lane],
                            static_cast<float>(expectedMaterial))
                << name << " fixed point " << lane;
        }
    };

    EditorDocumentModel allUnmaterialedDefaultB;
    DocumentDiagnostic diagnostic;
    ASSERT_TRUE(allUnmaterialedDefaultB.Load(VXD_GOLDEN_PATH, diagnostic)) << diagnostic.message;
    for (uint32_t layer = 0u; layer < allUnmaterialedDefaultB.LayerCount(); ++layer) {
        ASSERT_TRUE(allUnmaterialedDefaultB.ClearLayerMaterial(
            allUnmaterialedDefaultB.LayerId(layer), diagnostic)) << diagnostic.message;
    }
    ASSERT_TRUE(allUnmaterialedDefaultB.SetDocumentDefaultMaterial(
        "core:emissive_trim", diagnostic)) << diagnostic.message;
    expectMaterialCase("a: no layer material, default B", allUnmaterialedDefaultB,
                       kMaterialB, kMaterialB);

    EditorDocumentModel allUnmaterialedNoDefault;
    ASSERT_TRUE(allUnmaterialedNoDefault.Load(VXD_GOLDEN_PATH, diagnostic)) << diagnostic.message;
    for (uint32_t layer = 0u; layer < allUnmaterialedNoDefault.LayerCount(); ++layer) {
        ASSERT_TRUE(allUnmaterialedNoDefault.ClearLayerMaterial(
            allUnmaterialedNoDefault.LayerId(layer), diagnostic)) << diagnostic.message;
    }
    expectMaterialCase("b: no layer material, no default", allUnmaterialedNoDefault,
                       0u, 0u);

    EditorDocumentModel baseAWithDefaultB;
    ASSERT_TRUE(baseAWithDefaultB.Load(VXD_GOLDEN_PATH, diagnostic)) << diagnostic.message;
    for (uint32_t layer = 0u; layer < baseAWithDefaultB.LayerCount(); ++layer) {
        ASSERT_TRUE(baseAWithDefaultB.ClearLayerMaterial(
            baseAWithDefaultB.LayerId(layer), diagnostic)) << diagnostic.message;
    }
    ASSERT_TRUE(baseAWithDefaultB.SetLayerMaterial(
        baseAWithDefaultB.LayerId(0u), "core:hull_metal_major", diagnostic))
        << diagnostic.message;
    ASSERT_TRUE(baseAWithDefaultB.SetDocumentDefaultMaterial(
        "core:emissive_trim", diagnostic)) << diagnostic.message;
    expectMaterialCase("c: base A, transparent bulge, default B", baseAWithDefaultB,
                       kMaterialB, kMaterialA);
}

TEST(EditorDocumentModelVdc1, UnresolvedReferencesAreLocatedAndCanBeSkippedForPreview) {
    const std::string recipeId = std::string("missing") + ":" + "recipe";
    EditorDocumentModel model;
    DocumentDiagnostic diagnostic;
    ASSERT_TRUE(model.Load(VXD_GOLDEN_PATH, diagnostic)) << diagnostic.message;
    ASSERT_TRUE(model.AddReference(recipeId, diagnostic)) << diagnostic.message;
    Vixen::Editor::EditableDocumentLayer layer;
    ASSERT_TRUE(model.MakeRecipeReferenceLayer(recipeId, "Missing recipe", layer, diagnostic));
    ASSERT_TRUE(model.InsertLayer(model.LayerCount(), layer, diagnostic));

    Vixen::SVO::RecipeRegistry::RecipeEntry entry;
    ASSERT_FALSE(model.FlattenToRecipeEntry(
        entry, model.CaptureBakeSnapshot(), {}, false, diagnostic));
    EXPECT_EQ(diagnostic.code, DocumentDiagnosticCode::FlattenError);
    EXPECT_NE(diagnostic.message.find("layer"), std::string::npos);

    ASSERT_TRUE(model.FlattenToRecipeEntry(
        entry, model.CaptureBakeSnapshot(), {}, true, diagnostic)) << diagnostic.message;
    EXPECT_EQ(diagnostic.code, DocumentDiagnosticCode::FlattenError);
    EXPECT_NE(diagnostic.message.find(recipeId), std::string::npos);
}

TEST(EditorDocumentModelLayerPorts, LegacyBytesStayStableUntilAFeatureIsUsed) {
    const auto original = ReadFile(VXD_GOLDEN_PATH);
    ASSERT_FALSE(original.empty());
    EditorDocumentModel model;
    DocumentDiagnostic diagnostic;
    ASSERT_TRUE(model.Load(VXD_GOLDEN_PATH, diagnostic)) << diagnostic.message;
    ASSERT_FALSE(model.UsesLayerPorts());
    std::vector<std::uint8_t> saved;
    ASSERT_TRUE(model.Serialize(saved, diagnostic)) << diagnostic.message;
    EXPECT_EQ(saved, original);
    ASSERT_GE(model.LayerCount(), 2u);
    EXPECT_EQ(model.LayerId(0u), 1u);
    EXPECT_EQ(model.LayerId(1u), 2u);
}

TEST(EditorDocumentModelLayerPorts, WiresUseStableIdsAndRefuseUpwardReorder) {
    EditorDocumentModel model;
    DocumentDiagnostic diagnostic;
    ASSERT_TRUE(model.Load(VXD_GOLDEN_PATH, diagnostic)) << diagnostic.message;
    ASSERT_GE(model.LayerCount(), 2u);
    const uint32_t sourceId = model.LayerId(0u);
    const uint32_t readerId = model.LayerId(1u);
    const uint8_t ordinaryOp = model.View().layers[0].header->op;
    ASSERT_TRUE(model.SetLayerProvide(sourceId, true, diagnostic)) << diagnostic.message;
    ASSERT_TRUE(model.SetLayerProvide(sourceId, false, diagnostic)) << diagnostic.message;
    EXPECT_EQ(model.View().layers[0].header->op, ordinaryOp);
    ASSERT_TRUE(model.SetLayerProvide(sourceId, true, diagnostic)) << diagnostic.message;
    ASSERT_TRUE(model.AddWire(readerId, "field", sourceId, "field", diagnostic)) << diagnostic.message;
    ASSERT_EQ(model.FormatVersion(), 5u);
    ASSERT_EQ(model.Wires().size(), 1u);
    EXPECT_EQ(model.Wires().front().readerLayerId, readerId);
    EXPECT_EQ(model.Wires().front().sourceLayerId, sourceId);

    const uint64_t revision = model.Revision();
    ASSERT_FALSE(model.MoveLayer(1u, 0u, diagnostic));
    EXPECT_EQ(diagnostic.code, DocumentDiagnosticCode::LayerDependencyOrder);
    EXPECT_EQ(model.Revision(), revision);

    ASSERT_TRUE(model.SetLayerEnabled(0u, false, diagnostic)) << diagnostic.message;
    const auto warnings = model.LayerWarnings(readerId);
    ASSERT_EQ(warnings.size(), 1u);
    EXPECT_NE(warnings.front().find("using default"), std::string::npos);
    std::vector<std::uint8_t> fallbackFlatten;
    ASSERT_TRUE(model.Flatten(fallbackFlatten, diagnostic)) << diagnostic.message;

    const auto path = MakeTempDocumentPath("vdc1-v5-wires");
    ASSERT_TRUE(model.Save(path.path, diagnostic)) << diagnostic.message;
    EditorDocumentModel reopened;
    ASSERT_TRUE(reopened.Load(path.path, diagnostic)) << diagnostic.message;
    EXPECT_EQ(reopened.FormatVersion(), 5u);
    EXPECT_EQ(reopened.LayerId(0u), sourceId);
    EXPECT_EQ(reopened.LayerId(1u), readerId);
    ASSERT_EQ(reopened.Wires().size(), 1u);
    EXPECT_EQ(reopened.Wires().front().readerLayerId, readerId);
    ASSERT_TRUE(reopened.RemoveWire(readerId, "field", diagnostic)) << diagnostic.message;
    EXPECT_TRUE(reopened.Wires().empty());
    ASSERT_TRUE(reopened.AddWire(readerId, "field", sourceId, "field", diagnostic)) << diagnostic.message;

    const uint64_t soloRevision = reopened.Revision();
    ASSERT_TRUE(reopened.SetSoloLayer(readerId, diagnostic)) << diagnostic.message;
    EXPECT_EQ(reopened.Revision(), soloRevision);
    EXPECT_TRUE(reopened.SoloLayerId().has_value());
    reopened.ClearSoloLayer();
    EXPECT_FALSE(reopened.SoloLayerId().has_value());

    Vixen::Editor::EditableDocumentLayer removed;
    ASSERT_TRUE(reopened.DeleteLayer(0u, &removed, diagnostic)) << diagnostic.message;
    EXPECT_EQ(removed.id, sourceId);
    EXPECT_TRUE(reopened.Wires().empty());
}

TEST(EditorDocumentModelLayerPorts, TypedAndCapacityDiagnosticsAreGeneratedAndLocated) {
    EditorDocumentModel model;
    DocumentDiagnostic diagnostic;
    ASSERT_TRUE(model.Load(VXD_GOLDEN_PATH, diagnostic)) << diagnostic.message;
    ASSERT_GE(model.LayerCount(), 2u);
    const uint32_t sourceId = model.LayerId(0u);
    const uint32_t readerId = model.LayerId(1u);

    EXPECT_FALSE(model.AddWire(readerId, "radius", sourceId, "field", diagnostic));
    EXPECT_EQ(diagnostic.code, DocumentDiagnosticCode::WireTypeMismatch);
    EXPECT_NE(diagnostic.message.find("radius"), std::string::npos);
    EXPECT_FALSE(model.RemoveWire(readerId, "missing", diagnostic));
    EXPECT_EQ(diagnostic.code, DocumentDiagnosticCode::WireNotFound);

    for (uint32_t i = 0; i < 64u; ++i) {
        ASSERT_TRUE(model.AddWire(readerId, "input" + std::to_string(i), sourceId,
                                  "port" + std::to_string(i), diagnostic)) << diagnostic.message;
    }
    EXPECT_EQ(model.Wires().size(), 64u);
    EXPECT_FALSE(model.AddWire(readerId, "input64", sourceId, "port64", diagnostic));
    EXPECT_EQ(diagnostic.code, DocumentDiagnosticCode::WireCapacityExceeded);
}

TEST(EditorDocumentModelLayerPorts, RemovingAReadPortNodeRemovesItsWire) {
    EditorDocumentModel model;
    DocumentDiagnostic diagnostic;
    ASSERT_TRUE(model.Load(VXD_GOLDEN_PATH, diagnostic)) << diagnostic.message;
    ASSERT_GE(model.LayerCount(), 2u);
    const uint32_t sourceId = model.LayerId(0u);
    const uint32_t readerId = model.LayerId(1u);
    ASSERT_TRUE(model.SetLayerProvide(sourceId, true, diagnostic)) << diagnostic.message;

    ASSERT_TRUE(model.InsertPortRead(readerId, 0u, model.LayerName(0u) + ".field", diagnostic))
        << diagnostic.message;
    ASSERT_EQ(model.Wires().size(), 1u);
    const auto* graph = model.GetLayerGraph(1u, diagnostic);
    ASSERT_NE(graph, nullptr);
    ASSERT_FALSE(graph->raw);
    ASSERT_GE(graph->view.nodeCount, 1u);
    uint32_t readNode = graph->view.nodeCount;
    for (uint32_t i = 0u; i < graph->view.nodeCount; ++i) {
        if (graph->view.nodes[i].opcodeInfo->opcode ==
            static_cast<uint8_t>(Vixen::SVO::Recipe::SdfOpCode::ReadPort)) {
            readNode = i;
            break;
        }
    }
    ASSERT_LT(readNode, graph->view.nodeCount);
    Vixen::SVO::RecipeRegistry::RecipeEntry flattened;
    ASSERT_TRUE(model.FlattenToRecipeEntry(flattened, diagnostic)) << diagnostic.message;
    ASSERT_TRUE(model.RemoveNode(1u, readNode, diagnostic)) << diagnostic.message;
    EXPECT_TRUE(model.Wires().empty());
    flattened = {};
    ASSERT_TRUE(model.FlattenToRecipeEntry(flattened, diagnostic)) << diagnostic.message;
}

TEST(EditorDocumentModelMaterialDiagnosis, HullMaterialDistanceAndDisplayedBakeSlotGuard) {
    constexpr const char* kHullPath =
        "/home/liory/codeman-cases/undertow/reports/cx-edmat5-evidence/"
        "pack-hullproof-final9-1280/recipes/hull.vxd";
    EditorDocumentModel materialModel;
    DocumentDiagnostic diagnostic;
    ASSERT_TRUE(materialModel.Load(kHullPath, diagnostic)) << diagnostic.message;

    EditorDocumentModel neutralModel;
    ASSERT_TRUE(neutralModel.Load(kHullPath, diagnostic)) << diagnostic.message;
    // The shipped fixture defaults the bulge radius to 0.6, fully inside the base box. Raise it
    // for this material witness so both authored layer materials reach the stored surface.
    ASSERT_TRUE(materialModel.SetParameterValue(0u, 1.2f, diagnostic)) << diagnostic.message;
    ASSERT_TRUE(neutralModel.SetParameterValue(0u, 1.2f, diagnostic)) << diagnostic.message;
    for (uint32_t i = 0; i < neutralModel.LayerCount(); ++i)
        ASSERT_TRUE(neutralModel.ClearLayerMaterial(neutralModel.LayerId(i), diagnostic))
            << diagnostic.message;

    const auto materialResolver = [](const std::string& id, uint32_t& slot) {
        if (id == "core:hull_metal_major") { slot = 5u; return true; }
        if (id == "core:emissive_trim") { slot = 8u; return true; }
        return false;
    };
    Vixen::SVO::RecipeRegistry::RecipeEntry materialEntry;
    Vixen::SVO::RecipeRegistry::RecipeEntry neutralEntry;
    ASSERT_TRUE(materialModel.FlattenToRecipeEntry(
        materialEntry, materialModel.CaptureBakeSnapshot(), {}, materialResolver, false, diagnostic))
        << diagnostic.message;
    ASSERT_TRUE(neutralModel.FlattenToRecipeEntry(neutralEntry, neutralModel.CaptureBakeSnapshot(), diagnostic))
        << diagnostic.message;

    std::array<float, 3> boundsCenter{};
    float boundsRadius = 0.0f;
    neutralModel.DerivePreviewBounds(boundsCenter, boundsRadius);
    Vixen::SVO::Recipe::LoweredRecipeProgram materialSimd;
    std::string lowerError;
    ASSERT_TRUE(materialSimd.Lower(materialEntry.bytecode.data(),
                                   static_cast<uint32_t>(materialEntry.bytecode.size()),
                                   materialEntry.parameterValues, true,
                                   Vixen::SVO::kMaterialRefNone, lowerError)) << lowerError;
    Vixen::SVO::RecipeRegistry registry;
    ASSERT_EQ(registry.Register(1u, materialEntry),
              Vixen::SVO::RecipeRegistry::RegisterResult::Ok);
    Vixen::SVO::RecipeBakeConfig config{};
    constexpr uint32_t kBakeResolution = 64u;
    constexpr float kRecipeUnitsPerVoxel = 0.25f;
    config.center = glm::vec3(static_cast<float>(kBakeResolution) * 0.5f);
    config.defaultResolution = kBakeResolution;
    config.defaultBand = 2.5f;
    config.recipeUnitsPerVoxel = kRecipeUnitsPerVoxel;
    config.defaultMaterialId = Vixen::SVO::kMaterialRefNone;
    const auto bake = Vixen::SVO::BakeRegistryToPool(registry, config);
    ASSERT_TRUE(bake.ok) << bake.err;
    ASSERT_EQ(bake.owned.size(), 1u);
    ASSERT_EQ(registry.Get(1u)->octreeSlot, 0u);
    Vixen::SVO::RecipeRegistry neutralRegistry;
    ASSERT_EQ(neutralRegistry.Register(2u, neutralEntry),
              Vixen::SVO::RecipeRegistry::RegisterResult::Ok);
    const auto neutralBake = Vixen::SVO::BakeRegistryToPool(neutralRegistry, config);
    ASSERT_TRUE(neutralBake.ok) << neutralBake.err;
    ASSERT_EQ(neutralBake.owned.size(), 1u);
    const auto materialSerialized = Vixen::SVO::SerializeSdf(bake.owned[0]);
    const auto neutralSerialized = Vixen::SVO::SerializeSdf(neutralBake.owned[0]);
    EXPECT_GT(materialSerialized.channelCount, neutralSerialized.channelCount);
    EXPECT_NE(materialSerialized.channelPool, neutralSerialized.channelPool);
    EXPECT_EQ(materialSerialized.brickGridLookup, neutralSerialized.brickGridLookup);

    const auto& materialOutput = bake.owned[0].recipeOutputSet;
    const Vixen::SVO::Recipe::RecipeOutputDescriptor* distanceOutput = nullptr;
    const Vixen::SVO::Recipe::RecipeOutputDescriptor* materialOutputDescriptor = nullptr;
    for (const auto& descriptor : materialOutput.outputs) {
        if (descriptor.channel == static_cast<uint32_t>(Vixen::SVO::SEM_SDF))
            distanceOutput = &descriptor;
        if (descriptor.channel == static_cast<uint32_t>(Vixen::SVO::SEM_MATERIAL_ID))
            materialOutputDescriptor = &descriptor;
    }
    ASSERT_NE(distanceOutput, nullptr);
    ASSERT_NE(materialOutputDescriptor, nullptr);
    EXPECT_TRUE(materialOutputDescriptor->materialSidecar);
    EXPECT_TRUE(Vixen::SVO::ChannelIsCategorical(Vixen::SVO::SEM_MATERIAL_ID));
    std::map<uint32_t, uint64_t> materialSamplesById;
    uint64_t totalMaterialSurfaceSamples = 0u;
    for (int z = 0; z < materialOutput.resolution; ++z)
    for (int y = 0; y < materialOutput.resolution; ++y)
    for (int x = 0; x < materialOutput.resolution; ++x) {
        const float* values = materialOutput.sample({x, y, z});
        if (values == nullptr) continue;
        if (!std::isfinite(values[distanceOutput->valueOffset]) ||
            std::abs(values[distanceOutput->valueOffset]) > 0.5f) continue;
        const float rawId = values[materialOutputDescriptor->valueOffset];
        ASSERT_TRUE(std::isfinite(rawId));
        ASSERT_EQ(std::floor(rawId), rawId);
        ++materialSamplesById[static_cast<uint32_t>(rawId)];
        ++totalMaterialSurfaceSamples;
    }
    EXPECT_GT(totalMaterialSurfaceSamples, 0u);
    EXPECT_EQ(materialSamplesById.size(), 2u);
    EXPECT_EQ(materialSamplesById[5u] + materialSamplesById[8u], totalMaterialSurfaceSamples);
    EXPECT_GT(materialSamplesById[5u], 0u);
    EXPECT_GT(materialSamplesById[8u], 0u);
    const auto readSerializedVoxel = [](const Vixen::SVO::SerializedOctree& serialized,
                                        const glm::ivec3& grid) -> std::optional<float> {
        const int n = static_cast<int>(serialized.config.bricksPerAxis) * 8;
        if (glm::any(glm::lessThan(grid, glm::ivec3(0))) ||
            glm::any(glm::greaterThanEqual(grid, glm::ivec3(n))))
            return std::nullopt;
        const glm::ivec3 brick = grid / 8;
        const uint32_t flat = static_cast<uint32_t>(brick.x +
            brick.y * static_cast<int>(serialized.config.bricksPerAxis) +
            brick.z * static_cast<int>(serialized.config.bricksPerAxis) *
                static_cast<int>(serialized.config.bricksPerAxis));
        if (static_cast<size_t>(flat + 1u) * sizeof(uint32_t) > serialized.brickGridLookup.size())
            return std::nullopt;
        uint32_t brickIndex = 0xFFFFFFFFu;
        std::memcpy(&brickIndex, serialized.brickGridLookup.data() + flat * sizeof(uint32_t), sizeof(uint32_t));
        if (brickIndex == 0xFFFFFFFFu || brickIndex >= serialized.brickCount) return std::nullopt;
        const glm::ivec3 local = grid - brick * 8;
        const uint32_t voxel = static_cast<uint32_t>(local.z * 64 + local.y * 8 + local.x);
        return serialized.readPoolVoxel(Vixen::SVO::SEM_SDF, brickIndex, voxel, 0u);
    };
    float maxStoredVoxelVsReference = 0.0f;
    float maxSerializedVsWorld = 0.0f;
    glm::ivec3 storedVoxelVsReferenceAt{};
    glm::ivec3 serializedVsWorldAt{};
    float storedAtReferenceMax = 0.0f;
    float referenceAtStoredMax = 0.0f;
    uint32_t storedNegative = 0u;
    uint32_t referenceNegative = 0u;
    uint32_t storedReferenceSignMismatch = 0u;
    const int bakeCenter = static_cast<int>(kBakeResolution / 2u);
    const auto centerEntity = bake.owned[0].world->getEntityByWorldSpace(
        glm::vec3(static_cast<float>(bakeCenter)));
    if (bake.owned[0].world->exists(centerEntity)) {
        const auto centerStored = bake.owned[0].world->getComponentValue<Vixen::GaiaVoxel::Density>(centerEntity);
        ASSERT_TRUE(centerStored.has_value());
        EXPECT_GT(centerStored.value(), 0.0f)
            << "stored bake must keep the authored round cut open at document center";
    }
    for (int z = 0; z < static_cast<int>(kBakeResolution); ++z)
    for (int y = 0; y < static_cast<int>(kBakeResolution); ++y)
    for (int x = 0; x < static_cast<int>(kBakeResolution); ++x) {
        const glm::ivec3 grid(x, y, z);
        const auto entity = bake.owned[0].world->getEntityByWorldSpace(glm::vec3(grid));
        if (!bake.owned[0].world->exists(entity)) continue;
        const auto stored = bake.owned[0].world->getComponentValue<Vixen::GaiaVoxel::Density>(entity);
        if (!stored.has_value()) continue;
        const float reference = Vixen::SVO::Recipe::evalRecipe(
            neutralEntry.bytecode.data(), static_cast<uint32_t>(neutralEntry.bytecode.size()),
            (glm::vec3(grid) - glm::vec3(static_cast<float>(bakeCenter))) * config.recipeUnitsPerVoxel,
            neutralEntry.parameterValues) / config.recipeUnitsPerVoxel;
        if (stored.value() < 0.0f) ++storedNegative;
        if (reference < 0.0f) ++referenceNegative;
        if ((stored.value() < 0.0f) != (reference < 0.0f)) ++storedReferenceSignMismatch;
        if (std::abs(stored.value() - reference) > maxStoredVoxelVsReference) {
            maxStoredVoxelVsReference = std::abs(stored.value() - reference);
            storedVoxelVsReferenceAt = grid;
            storedAtReferenceMax = stored.value();
            referenceAtStoredMax = reference;
        }
        const auto serialized = readSerializedVoxel(materialSerialized, grid);
        if (serialized.has_value() && std::abs(serialized.value() - stored.value()) > maxSerializedVsWorld) {
            maxSerializedVsWorld = std::abs(serialized.value() - stored.value());
            serializedVsWorldAt = grid;
        }
    }

    float maxMaterialVsNeutral = 0.0f;
    float maxStoredVsNeutral = 0.0f;
    glm::vec3 materialVsNeutralAt{};
    glm::vec3 storedVsNeutralAt{};
    uint32_t storedSamples = 0u;
    float neutralAtStoredMax = 0.0f;
    float storedAtMax = 0.0f;
    const auto sampleStoredTrilinear = [&](const Vixen::SVO::RecipeBakeResult& bakeResult,
                                           const glm::vec3& grid) -> std::optional<float> {
        const glm::vec3 base = glm::floor(grid);
        const glm::vec3 f = grid - base;
        const glm::ivec3 i(base);
        float sum = 0.0f;
        for (int dz = 0; dz <= 1; ++dz)
            for (int dy = 0; dy <= 1; ++dy)
                for (int dx = 0; dx <= 1; ++dx) {
                    const glm::ivec3 p = i + glm::ivec3(dx, dy, dz);
                    const auto entity = bakeResult.owned[0].world->getEntityByWorldSpace(glm::vec3(p));
                    if (!bakeResult.owned[0].world->exists(entity)) return std::nullopt;
                    const auto value = bakeResult.owned[0].world->getComponentValue<Vixen::GaiaVoxel::Density>(entity);
                    if (!value.has_value()) return std::nullopt;
                    const float wx = dx ? f.x : 1.0f - f.x;
                    const float wy = dy ? f.y : 1.0f - f.y;
                    const float wz = dz ? f.z : 1.0f - f.z;
                    sum += value.value() * wx * wy * wz;
                }
        return sum;
    };
    float maxMaterialBakeVsNeutralBake = 0.0f;
    glm::vec3 materialBakeVsNeutralBakeAt{};
    uint32_t neutralStoredSamples = 0u;
    for (int z = 0; z < 17; ++z) {
        for (int y = 0; y < 17; ++y) {
            for (int x = 0; x < 17; ++x) {
                const glm::vec3 local(
                    boundsCenter[0] + (static_cast<float>(x) / 16.0f * 2.0f - 1.0f) * boundsRadius,
                    boundsCenter[1] + (static_cast<float>(y) / 16.0f * 2.0f - 1.0f) * boundsRadius,
                    boundsCenter[2] + (static_cast<float>(z) / 16.0f * 2.0f - 1.0f) * boundsRadius);
                const float neutral = Vixen::SVO::Recipe::evalRecipe(
                    neutralEntry.bytecode.data(), static_cast<uint32_t>(neutralEntry.bytecode.size()),
                    local, neutralEntry.parameterValues);
                const float material = Vixen::SVO::Recipe::evalRecipe(
                    materialEntry.bytecode.data(), static_cast<uint32_t>(materialEntry.bytecode.size()),
                    local, materialEntry.parameterValues);
                const float materialDelta = std::abs(material - neutral);
                if (materialDelta > maxMaterialVsNeutral) {
                    maxMaterialVsNeutral = materialDelta;
                    materialVsNeutralAt = local;
                }
                const glm::vec3 grid = glm::vec3(static_cast<float>(bakeCenter)) +
                                       local / config.recipeUnitsPerVoxel;
                const auto stored = sampleStoredTrilinear(bake, grid);
                if (!stored.has_value()) continue;
                ++storedSamples;
                const float storedInRecipeUnits = stored.value() * config.recipeUnitsPerVoxel;
                const float storedDelta = std::abs(storedInRecipeUnits - neutral);
                if (storedDelta > maxStoredVsNeutral) {
                    maxStoredVsNeutral = storedDelta;
                    storedVsNeutralAt = local;
                    neutralAtStoredMax = neutral;
                    storedAtMax = storedInRecipeUnits;
                }
                const auto neutralStored = sampleStoredTrilinear(neutralBake, grid);
                if (neutralStored.has_value()) {
                    ++neutralStoredSamples;
                    const float bakeDelta = std::abs(stored.value() - neutralStored.value());
                    if (bakeDelta > maxMaterialBakeVsNeutralBake) {
                        maxMaterialBakeVsNeutralBake = bakeDelta;
                        materialBakeVsNeutralBakeAt = local;
                    }
                }
            }
        }
    }
    std::cout << "[material-diagnosis] program=" << materialEntry.bytecode.size()
              << " simdInstructions=" << materialSimd.InstructionCount()
              << " outputs=" << materialSimd.OutputCount()
              << " outputValues=" << materialSimd.OutputValueCount()
              << " bakePool=" << bake.pool.count
              << " previewSlot=" << registry.Get(1u)->octreeSlot
              << " owned=" << bake.owned.size()
              << " storedGridSamples=" << storedSamples
              << " neutralStoredGridSamples=" << neutralStoredSamples
              << " maxMaterialVsNeutral=" << maxMaterialVsNeutral
              << " at=" << materialVsNeutralAt.x << "," << materialVsNeutralAt.y << "," << materialVsNeutralAt.z
              << " maxStoredVsNeutral=" << maxStoredVsNeutral
              << " at=" << storedVsNeutralAt.x << "," << storedVsNeutralAt.y << "," << storedVsNeutralAt.z
              << " neutralAtStoredMax=" << neutralAtStoredMax
              << " storedAtMax=" << storedAtMax
              << " maxMaterialBakeVsNeutralBake=" << maxMaterialBakeVsNeutralBake
              << " at=" << materialBakeVsNeutralBakeAt.x << "," << materialBakeVsNeutralBakeAt.y << "," << materialBakeVsNeutralBakeAt.z
              << " serializedChannelCount=" << materialSerialized.channelCount
              << " serializedBrickStrideFloats=" << materialSerialized.brickStrideFloats
              << " maxStoredVoxelVsReference=" << maxStoredVoxelVsReference
              << " at=" << storedVoxelVsReferenceAt.x << "," << storedVoxelVsReferenceAt.y << "," << storedVoxelVsReferenceAt.z
              << " storedAtReferenceMax=" << storedAtReferenceMax
              << " referenceAtStoredMax=" << referenceAtStoredMax
              << " storedNegative=" << storedNegative
              << " referenceNegative=" << referenceNegative
              << " storedReferenceSignMismatch=" << storedReferenceSignMismatch
              << " maxSerializedVsWorld=" << maxSerializedVsWorld
              << " at=" << serializedVsWorldAt.x << "," << serializedVsWorldAt.y << "," << serializedVsWorldAt.z
              << " boundsCenter=" << boundsCenter[0] << "," << boundsCenter[1] << "," << boundsCenter[2]
              << " boundsRadius=" << boundsRadius
              << " outputSetResolution=" << bake.owned[0].recipeOutputSet.resolution
              << " outputSetValues=" << bake.owned[0].recipeOutputSet.values.size()
              << " samplesById=";
    for (const auto& [id, count] : materialSamplesById)
        std::cout << id << ":" << count << ",";
    std::cout
              << " totalMaterialSurfaceSamples=" << totalMaterialSurfaceSamples
              << "\n";
    std::cout << "[material-diagnosis] flattened-bytes=";
    for (const auto& instruction : materialEntry.bytecode) {
        const auto* bytes = reinterpret_cast<const std::uint8_t*>(&instruction);
        for (std::size_t byte = 0; byte < sizeof(instruction); ++byte)
            std::cout << std::hex << std::setw(2) << std::setfill('0')
                      << static_cast<unsigned>(bytes[byte]);
        std::cout << ' ';
    }
    std::cout << std::dec << "\n";
    std::cout << "[material-diagnosis] neutral-bytes=";
    for (const auto& instruction : neutralEntry.bytecode) {
        const auto* bytes = reinterpret_cast<const std::uint8_t*>(&instruction);
        for (std::size_t byte = 0; byte < sizeof(instruction); ++byte)
            std::cout << std::hex << std::setw(2) << std::setfill('0')
                      << static_cast<unsigned>(bytes[byte]);
        std::cout << ' ';
    }
    std::cout << std::dec << "\n";

    struct MaterialWitnessPoint {
        const char* name;
        glm::vec3 position;
    };
    // These are document-space probes chosen on the box/sphere silhouette, the smooth-union
    // transition, and the cylindrical cut wall. Keep the coordinates fixed so the CPU table is
    // comparable with any future procedural-frame witness.
    constexpr MaterialWitnessPoint kWitnessPoints[] = {
        {"box-face-centre", {1.0f, 0.9f, 0.0f}},
        {"box-face-lower", {1.0f, -0.9f, 0.0f}},
        {"box-corner-region", {1.0f, 1.0f, 0.0f}},
        {"box-corner-3d", {1.0f, 1.0f, 1.0f}},
        {"bulge-crest", {0.0f, 0.0f, 1.2f}},
        {"bulge-crest-x", {1.2f, 0.0f, 0.0f}},
        {"smooth-union-seam", {1.03f, 0.7f, 0.0f}},
        {"cut-wall-front", {0.35f, 0.0f, 0.0f}},
        {"cut-wall-side", {0.0f, 0.0f, 0.35f}},
    };
    ASSERT_EQ(std::size(kWitnessPoints), 9u);
    ASSERT_EQ(materialSimd.OutputCount(), 2u);
    const auto& materialDistanceOutput = materialSimd.OutputAt(0u);
    const auto& materialIdOutput = materialSimd.OutputAt(1u);
    ASSERT_EQ(materialDistanceOutput.channel, static_cast<uint32_t>(Vixen::SVO::SEM_SDF));
    ASSERT_EQ(materialIdOutput.channel, static_cast<uint32_t>(Vixen::SVO::SEM_MATERIAL_ID));
    std::cout << "[material-diagnosis] simd-points=";
    for (std::size_t first = 0; first < std::size(kWitnessPoints); first += 4u) {
        glm::vec3 positions[4]{};
        const std::size_t count = std::min<std::size_t>(4u, std::size(kWitnessPoints) - first);
        for (std::size_t lane = 0; lane < count; ++lane)
            positions[lane] = kWitnessPoints[first + lane].position;
        for (std::size_t lane = count; lane < 4u; ++lane)
            positions[lane] = positions[count - 1u];
        float output[2u * 4u]{};
        std::string evaluateError;
        ASSERT_TRUE(materialSimd.EvaluateOutputSet4(
            positions, output, std::size(output), nullptr, &evaluateError)) << evaluateError;
        for (std::size_t lane = 0; lane < count; ++lane) {
            const float distance = output[materialDistanceOutput.valueOffset * 4u + lane];
            const float materialId = output[materialIdOutput.valueOffset * 4u + lane];
            std::cout << kWitnessPoints[first + lane].name << "@"
                      << positions[lane].x << "," << positions[lane].y << "," << positions[lane].z
                      << "=distance:" << distance << ":id:" << materialId << ";";
        }
    }
    std::cout << "\n";
}

}  // namespace
