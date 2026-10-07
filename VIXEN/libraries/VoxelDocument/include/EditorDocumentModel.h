#pragma once
// EditorDocumentModel — Inc1 editor state: a loaded VoxelDocument (VDC1) plus the
// flatten/save seam. Inc-2: the per-layer enabled state moved out to LayerController (the
// app owns it as the source of truth, dispatched through AppFlowRuntime) — the model is now
// a pure function of a caller-supplied enabledMask bitmask and holds no mutable edit state
// of its own. This library keeps the same operation surface available without the editor's
// graph, window, or rendering dependencies.
#include <cstdint>
#include <string>
#include <vector>

#include "Recipe/generated/VoxelDocument.g.h"
#include "Recipe/RecipeRegistry.h"

namespace Vixen::Editor {

// Loads a .vxd file and keeps the raw bytes alive (VoxelDocumentView holds pointers into
// them). Enabled/disabled per layer is supplied by the caller at flatten/save time as an
// enabledMask bitmask (bit i = layer i), not tracked here.
class EditorDocumentModel {
public:
    // Reads the file at path into rawBytes_ and parses it via ReadVoxelDocument. Returns
    // false (err set) on I/O failure or a malformed document.
    bool Load(const std::string& path, std::string& err);

    const Yeroket::Sdf::Generated::VoxelDocumentView& View() const;
    const std::string& SourcePath() const;
    uint32_t LayerCount() const;

    std::string LayerName(uint32_t i) const;
    static const char* OpName(uint8_t op);

    // Flattens the document (with the caller-supplied enabledMask, bit i = layer i) into a
    // VRC1 blob.
    bool Flatten(uint32_t enabledMask, std::vector<uint8_t>& outVrc1Blob, std::string& err) const;

    // Flattens directly into a RecipeRegistry::RecipeEntry ready to Register()/bake. VRC1 remains
    // the persistence/export format through Flatten(); this preview-only seam no longer allocates,
    // serializes, parses, and copies a container round-trip.
    bool FlattenToRecipeEntry(uint32_t enabledMask, Vixen::SVO::RecipeRegistry::RecipeEntry& outEntry,
                              std::string& err) const;

    // Reconstructs the document bytes with enabled replaced by the caller-supplied
    // enabledMask (everything else unchanged) and writes them to outPath. Uses
    // WriteVoxelDocument with layer headers copied from the original view (only .enabled
    // patched).
    bool Save(uint32_t enabledMask, const std::string& outPath, std::string& err) const;

private:
    std::vector<uint8_t> rawBytes_;
    Yeroket::Sdf::Generated::VoxelDocumentView view_{};
    std::string sourcePath_;
};

}  // namespace Vixen::Editor
