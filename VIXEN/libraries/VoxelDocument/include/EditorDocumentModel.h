#pragma once
// Inc1 editor state: a loaded VoxelDocument, its enabled-layer operations, and the
// flatten/save seam. The application owns input history, while each mutation is applied here so
// the headless module remains the single authority for accepted document state.

#include <cstdint>
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

    // All document layer-state mutations pass through these validated operations. The mask
    // accepts only bits corresponding to layers admitted by Load().
    bool SetLayerEnabled(uint32_t index, bool enabled, DocumentDiagnostic& diagnostic);
    bool SetEnabledMask(uint32_t mask, DocumentDiagnostic& diagnostic);

    std::string LayerName(uint32_t i) const;
    static const char* OpName(uint8_t op);

    bool Flatten(std::vector<uint8_t>& outVrc1Blob, DocumentDiagnostic& diagnostic) const;

    // Flattens directly into a RecipeRegistry::RecipeEntry ready to Register()/bake. VRC1 remains
    // the persistence/export format through Flatten(); this preview-only seam avoids a temporary
    // serialize/parse/copy round-trip.
    bool FlattenToRecipeEntry(Vixen::SVO::RecipeRegistry::RecipeEntry& outEntry,
                              DocumentDiagnostic& diagnostic) const;

    // Writes the accepted document with its current enabled-layer state.
    bool Save(const std::string& outPath, DocumentDiagnostic& diagnostic) const;

private:
    static uint32_t ValidMaskForLayerCount(uint32_t count);

    std::vector<uint8_t> rawBytes_;
    Yeroket::Sdf::Generated::VoxelDocumentView view_{};
    std::string sourcePath_;
    uint32_t enabledMask_ = 0;
};

}  // namespace Vixen::Editor
