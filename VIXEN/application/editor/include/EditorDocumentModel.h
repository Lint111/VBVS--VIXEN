#pragma once
// EditorDocumentModel — Inc1 editor state: a loaded VoxelDocument (VDC1) plus the one
// mutable edit the app supports (per-layer enabled override) and the flatten/save seam.
// Kept Vulkan-free and app-free so the headless live-gate test can drive the same load/
// flatten/save code paths the real vixen_editor app uses, without pulling in the graph.
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

#include "Recipe/generated/VoxelDocument.g.h"
#include "Recipe/generated/RecipeContainer.g.h"
#include "Recipe/VoxelDocumentFlattener.h"
#include "Recipe/RecipeRegistry.h"

namespace Vixen::Editor {

// Loads a .vxd file, keeps the raw bytes alive (VoxelDocumentView holds pointers into
// them), and tracks the per-layer enabled override the UI toggle mutates.
class EditorDocumentModel {
public:
    // Reads the file at path into rawBytes_ and parses it via ReadVoxelDocument. Returns
    // false (err set) on I/O failure or a malformed document.
    bool Load(const std::string& path, std::string& err) {
        std::ifstream f(path, std::ios::binary | std::ios::ate);
        if (!f) { err = "cannot open document: " + path; return false; }
        const std::streamsize sz = f.tellg();
        if (sz <= 0) { err = "empty document: " + path; return false; }
        rawBytes_.resize(static_cast<size_t>(sz));
        f.seekg(0);
        f.read(reinterpret_cast<char*>(rawBytes_.data()), sz);

        if (!Yeroket::Sdf::Generated::ReadVoxelDocument(rawBytes_.data(), rawBytes_.size(), view_)) {
            err = "malformed VoxelDocument: " + path;
            return false;
        }
        sourcePath_ = path;
        enabledOverride_.resize(view_.header.layerCount);
        for (uint32_t i = 0; i < view_.header.layerCount; ++i)
            enabledOverride_[i] = view_.layers[i].header->enabled;
        return true;
    }

    const Yeroket::Sdf::Generated::VoxelDocumentView& View() const { return view_; }
    const std::string& SourcePath() const { return sourcePath_; }
    uint32_t LayerCount() const { return view_.header.layerCount; }

    std::string LayerName(uint32_t i) const {
        const auto* h = view_.layers[i].header;
        // name is fixed uint8[32], UTF-8, NUL-padded.
        const char* bytes = reinterpret_cast<const char*>(h->nameBytes);
        size_t len = 0;
        while (len < sizeof(h->nameBytes) && bytes[len] != '\0') ++len;
        return std::string(bytes, len);
    }

    static const char* OpName(uint8_t op) {
        switch (op) {
            case 0: return "union";
            case 1: return "smooth_union";
            case 2: return "subtract";
            case 3: return "intersect";
            default: return "unknown";
        }
    }

    bool IsEnabled(uint32_t i) const { return enabledOverride_[i] != 0; }

    // Toggle a layer's enabled override and mark the model dirty. Does NOT re-flatten —
    // the caller re-flattens on its own cadence (editor: next Execute; test: immediately).
    void ToggleLayer(uint32_t i) {
        enabledOverride_[i] = enabledOverride_[i] ? 0 : 1;
        dirty_ = true;
    }

    bool ConsumeDirty() {
        const bool d = dirty_;
        dirty_ = false;
        return d;
    }

    // Flattens the document (with the current enabledOverride_) into a VRC1 blob.
    bool Flatten(std::vector<uint8_t>& outVrc1Blob, std::string& err) const {
        return Vixen::SVO::FlattenVoxelDocument(view_, &enabledOverride_, outVrc1Blob, err);
    }

    // Flattens + parses the VRC1 blob into a RecipeRegistry::RecipeEntry ready to
    // Register()/bake. Bridges the VRC1 blob's header fields (bakeResolution/bandVoxels/
    // brickDepth) and instruction span into the entry the existing RecipeRegistry/
    // RecipeBaker path (test_recipe_pool_render.cpp's pattern) consumes unchanged.
    bool FlattenToRecipeEntry(Vixen::SVO::RecipeRegistry::RecipeEntry& outEntry, std::string& err) const {
        std::vector<uint8_t> blob;
        if (!Flatten(blob, err)) return false;

        Yeroket::Sdf::Generated::RecipeContainerView rv{};
        if (!Yeroket::Sdf::Generated::ReadRecipeContainer(blob.data(), blob.size(), rv)) {
            err = "flattened blob failed ReadRecipeContainer";
            return false;
        }

        outEntry = Vixen::SVO::RecipeRegistry::RecipeEntry{};
        outEntry.bytecode.assign(rv.instructions, rv.instructions + rv.header.instructionCount);
        outEntry.bakeResolution = rv.header.bakeResolution;
        outEntry.bandVoxels     = rv.header.bandVoxels;
        outEntry.brickDepth     = rv.header.brickDepth;
        return true;
    }

    // Reconstructs the document bytes with `enabled` replaced by enabledOverride_
    // (everything else unchanged) and writes them to outPath. Uses WriteVoxelDocument
    // with layer headers copied from the original view (only .enabled patched).
    bool Save(const std::string& outPath, std::string& err) const {
        using namespace Yeroket::Sdf::Generated;

        std::vector<VoxelDocLayerHeader> headers(view_.header.layerCount);
        std::vector<VoxelDocLayerWrite> writes(view_.header.layerCount);
        for (uint32_t i = 0; i < view_.header.layerCount; ++i) {
            headers[i] = *view_.layers[i].header;
            headers[i].enabled = enabledOverride_[i];
            writes[i].header = headers[i];
            writes[i].instructions = view_.layers[i].instructions;
        }

        size_t need = 0;
        WriteVoxelDocument(view_.channels, view_.header.channelCount,
                           writes.data(), view_.header.layerCount,
                           nullptr, 0, need);
        std::vector<uint8_t> out(need);
        size_t written = 0;
        if (!WriteVoxelDocument(view_.channels, view_.header.channelCount,
                                writes.data(), view_.header.layerCount,
                                out.data(), out.size(), written)) {
            err = "WriteVoxelDocument failed sizing/writing the document";
            return false;
        }
        out.resize(written);

        std::ofstream f(outPath, std::ios::binary | std::ios::trunc);
        if (!f) { err = "cannot open output path: " + outPath; return false; }
        f.write(reinterpret_cast<const char*>(out.data()), static_cast<std::streamsize>(out.size()));
        return f.good();
    }

private:
    std::vector<uint8_t> rawBytes_;
    Yeroket::Sdf::Generated::VoxelDocumentView view_{};
    std::string sourcePath_;
    std::vector<uint8_t> enabledOverride_;
    bool dirty_ = false;
};

}  // namespace Vixen::Editor
