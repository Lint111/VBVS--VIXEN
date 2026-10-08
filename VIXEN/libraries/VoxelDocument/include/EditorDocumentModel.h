#pragma once
// Inc1 editor state: a loaded VoxelDocument, its enabled-layer operations, and the
// flatten/save seam. The application owns input history, while each mutation is applied here so
// the headless module remains the single authority for accepted document state.

#include <cstdint>
#include <array>
#include <string>
#include <vector>

#include "Recipe/generated/VoxelDocument.g.h"
#include "Recipe/RecipeRegistry.h"

namespace Vixen::Editor {

enum class DocumentDiagnosticCode : uint8_t {
    None,
    IoError,
    EmptyDocument,
    MalformedDocument,
    TooManyLayers,
    InvalidLayerIndex,
    InvalidLayerMask,
    InvalidProgramField,
    InvalidParameterValue,
    StaleBakeSnapshot,
    FlattenError,
    WriteError,
};

struct DocumentDiagnostic {
    DocumentDiagnosticCode code = DocumentDiagnosticCode::None;
    std::string message;

    void Clear() {
        code = DocumentDiagnosticCode::None;
        message.clear();
    }
};

struct EditableDocumentLayer {
    uint8_t type = 0;
    uint8_t op = 0;
    bool enabled = true;
    float blendRadius = 0.0f;
    std::string name;
    std::vector<Yeroket::Sdf::Generated::SdfInstruction> program;
};

struct DocumentBakeSnapshot {
    uint64_t revision = 0;
    std::array<float, 6> parameterValues{};
};

// Loads one VoxelDocument and owns the enabled-layer state used by flatten/save operations.
// VoxelDocumentView stores pointers into the owned byte buffer, so Load parses into a candidate
// buffer and replaces the accepted document only after all validation succeeds.
class EditorDocumentModel {
public:
    static constexpr uint32_t kMaximumLayerCount = 32;

    bool Load(const std::string& path, DocumentDiagnostic& diagnostic);

    const Yeroket::Sdf::Generated::VoxelDocumentView& View() const;
    const std::string& SourcePath() const;
    uint32_t LayerCount() const;
    uint32_t EnabledMask() const;
    uint64_t Revision() const;

    uint32_t ParameterCount() const;
    std::string ParameterName(uint32_t index) const;
    std::string ParameterUnit(uint32_t index) const;
    uint32_t ParameterSlot(uint32_t index) const;
    float ParameterValue(uint32_t index) const;
    float ParameterDefault(uint32_t index) const;
    float ParameterMin(uint32_t index) const;
    float ParameterMax(uint32_t index) const;
    std::array<float, 6> EffectiveParameterValues() const;
    DocumentBakeSnapshot CaptureBakeSnapshot() const;

    // All document layer-state mutations pass through these validated operations. The mask
    // accepts only bits corresponding to layers admitted by Load().
    bool SetLayerEnabled(uint32_t index, bool enabled, DocumentDiagnostic& diagnostic);
    bool SetEnabledMask(uint32_t mask, DocumentDiagnostic& diagnostic);
    bool GetLayer(uint32_t index, EditableDocumentLayer& out,
                  DocumentDiagnostic& diagnostic) const;
    bool InsertLayer(uint32_t index, const EditableDocumentLayer& layer,
                     DocumentDiagnostic& diagnostic);
    bool DeleteLayer(uint32_t index, EditableDocumentLayer* removed,
                     DocumentDiagnostic& diagnostic);
    bool MoveLayer(uint32_t from, uint32_t to, DocumentDiagnostic& diagnostic);
    bool GetLayerProgramField(uint32_t layerIndex, uint32_t instructionIndex,
                              uint32_t fieldIndex, float& out,
                              DocumentDiagnostic& diagnostic) const;
    bool SetLayerProgramField(uint32_t layerIndex, uint32_t instructionIndex,
                              uint32_t fieldIndex, float value,
                              DocumentDiagnostic& diagnostic);
    bool SetParameterValue(uint32_t index, float value,
                           DocumentDiagnostic& diagnostic);

    std::string LayerName(uint32_t i) const;
    static const char* OpName(uint8_t op);

    bool Flatten(std::vector<uint8_t>& outVrc1Blob, DocumentDiagnostic& diagnostic) const;

    // Flattens directly into a RecipeRegistry::RecipeEntry ready to Register()/bake. VRC1 remains
    // the persistence/export format through Flatten(); this preview-only seam avoids a temporary
    // serialize/parse/copy round-trip.
    bool FlattenToRecipeEntry(Vixen::SVO::RecipeRegistry::RecipeEntry& outEntry,
                              DocumentDiagnostic& diagnostic) const;
    bool FlattenToRecipeEntry(Vixen::SVO::RecipeRegistry::RecipeEntry& outEntry,
                              const DocumentBakeSnapshot& snapshot,
                              DocumentDiagnostic& diagnostic) const;

    // Writes the accepted document with its current enabled-layer state.
    bool Save(const std::string& outPath, DocumentDiagnostic& diagnostic) const;

private:
    static uint32_t ValidMaskForLayerCount(uint32_t count);
    std::vector<EditableDocumentLayer> CopyLayers() const;
    std::vector<Yeroket::Sdf::Generated::VoxelDocParameterHeader> CopyParameters() const;
    bool Rebuild(std::vector<EditableDocumentLayer> layers,
                 std::vector<Yeroket::Sdf::Generated::VoxelDocParameterHeader> parameters,
                 uint32_t enabledMask, DocumentDiagnostic& diagnostic);

    std::vector<uint8_t> rawBytes_;
    Yeroket::Sdf::Generated::VoxelDocumentView view_{};
    std::string sourcePath_;
    uint32_t enabledMask_ = 0;
    uint64_t revision_ = 0;
};

}  // namespace Vixen::Editor
