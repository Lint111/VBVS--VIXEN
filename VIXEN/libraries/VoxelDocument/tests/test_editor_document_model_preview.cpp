#include <gtest/gtest.h>

#include "ActionStack.h"
#include "EditorDocumentFraming.h"
#include "EditorDocumentModel.h"
#include "Recipe/generated/RecipeContainer.g.h"
#include "Recipe/generated/RecipeSimd.g.hpp"
#include "Recipe/RecipeBounds.h"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
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
                       source.parameters, source.header.reserved0,
                       layers.data(), layerCount, nullptr, 0, required);
    std::vector<std::uint8_t> bytes(required);
    size_t written = 0;
    if (!WriteVoxelDocument(source.channels, source.header.channelCount,
                            source.parameters, source.header.reserved0,
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
        const auto bounds = Vixen::SVO::Recipe::DeriveConservativeBounds(
            program.data(), static_cast<uint32_t>(program.size()));
        ASSERT_TRUE(bounds.ok);
        frames.push_back(Vixen::Editor::FitEditorCameraToBounds(
            bounds.center, bounds.radius, glm::vec3(32.0f), 64));
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
    const auto parameterizedBounds = Vixen::SVO::Recipe::DeriveConservativeBounds(
        parameterized, 3u, parameterValues);
    ASSERT_TRUE(parameterizedBounds.ok);
    EXPECT_GE(parameterizedBounds.radius, 1.75f);
}

}  // namespace
