#pragma once
// EditorDocumentModel — Inc1 editor state: a loaded VoxelDocument (VDC1) plus the
// flatten/save seam. Inc-2: the per-layer enabled state moved out to LayerController (the
// app owns it as the source of truth, dispatched through AppFlowRuntime) — the model is now
// a pure function of a caller-supplied enabledMask bitmask and holds no mutable edit state
// of its own. Kept Vulkan-free and app-free so the headless live-gate test can drive the
// same load/flatten/save code paths the real vixen_editor app uses, without pulling in the
// graph.
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

#include "Recipe/generated/VoxelDocument.g.h"
#include "Recipe/generated/RecipeContainer.g.h"
#include "Recipe/generated/RecipeSimd.g.hpp"
#include "Recipe/RecipeRegistry.h"

namespace Vixen::Editor {

// Loads a .vxd file and keeps the raw bytes alive (VoxelDocumentView holds pointers into
// them). Enabled/disabled per layer is supplied by the caller at flatten/save time as an
// enabledMask bitmask (bit i = layer i), not tracked here.
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

    // Flattens the document (with the caller-supplied enabledMask, bit i = layer i) into a
    // VRC1 blob.
    bool Flatten(uint32_t enabledMask, std::vector<uint8_t>& outVrc1Blob, std::string& err) const {
        std::vector<uint8_t> ovr(view_.header.layerCount);
        for (uint32_t i = 0; i < view_.header.layerCount; ++i)
            ovr[i] = (enabledMask >> i) & 1u;
        return Vixen::SVO::FlattenVoxelDocument(view_, &ovr, outVrc1Blob, err);
    }

    // Flattens directly into a RecipeRegistry::RecipeEntry ready to Register()/bake. VRC1 remains
    // the persistence/export format through Flatten(); this preview-only seam no longer allocates,
    // serializes, parses, and copies a container round-trip.
    bool FlattenToRecipeEntry(uint32_t enabledMask, Vixen::SVO::RecipeRegistry::RecipeEntry& outEntry,
                               std::string& err) const {
        using Yeroket::Sdf::Generated::SdfInstruction;
        using Vixen::SVO::Recipe::SdfOpCode;

        err.clear();
        std::vector<uint8_t> enabledOverride(view_.header.layerCount);
        for (uint32_t i = 0; i < view_.header.layerCount; ++i)
            enabledOverride[i] = (enabledMask >> i) & 1u;

        std::vector<SdfInstruction> instructions;
        int sp = 0;
        int psp = 0;
        bool haveBase = false;
        for (uint32_t layerIndex = 0; layerIndex < view_.header.layerCount; ++layerIndex) {
            if (enabledOverride[layerIndex] == 0) continue;
            const auto& layer = view_.layers[layerIndex];
            const uint32_t count = layer.header->instructionCount;
            if (!Vixen::SVO::GeneratedRecipePipelineDetail::ValidateProgram(
                    layer.instructions, count, sp, psp, err)) {
                return false;
            }
            if (!haveBase) {
                instructions.insert(instructions.end(), layer.instructions, layer.instructions + count);
                haveBase = true;
                continue;
            }

            SdfOpCode combineOpcode{};
            if (!Vixen::SVO::GeneratedRecipePipelineDetail::LayerCombineOpcode(
                    layer.header->op, combineOpcode)) {
                err = "unknown layer op " + std::to_string(layer.header->op) +
                      " on layer index " + std::to_string(layerIndex);
                return false;
            }
            instructions.insert(instructions.end(), layer.instructions, layer.instructions + count);
            SdfInstruction combine{};
            combine.opCode = static_cast<uint8_t>(combineOpcode);
            combine.inputMask = 3;
            combine.data[2] = layer.header->blendRadius;
            if (!Vixen::SVO::GeneratedRecipePipelineDetail::ValidateProgram(
                    &combine, 1, sp, psp, err)) {
                return false;
            }
            instructions.push_back(combine);
        }
        if (!haveBase) {
            err = "zero enabled layers — nothing to render";
            return false;
        }

        outEntry = Vixen::SVO::RecipeRegistry::RecipeEntry{};
        outEntry.bytecode = std::move(instructions);
        outEntry.bakeResolution = 64u;
        outEntry.bandVoxels = 2.5f;
        outEntry.brickDepth = 3u;
        return true;
    }

    // Reconstructs the document bytes with `enabled` replaced by the caller-supplied
    // enabledMask (everything else unchanged) and writes them to outPath. Uses
    // WriteVoxelDocument with layer headers copied from the original view (only .enabled
    // patched).
    bool Save(uint32_t enabledMask, const std::string& outPath, std::string& err) const {
        using namespace Yeroket::Sdf::Generated;

        std::vector<VoxelDocLayerHeader> headers(view_.header.layerCount);
        std::vector<VoxelDocLayerWrite> writes(view_.header.layerCount);
        for (uint32_t i = 0; i < view_.header.layerCount; ++i) {
            headers[i] = *view_.layers[i].header;
            headers[i].enabled = (enabledMask >> i) & 1u;
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
};

}  // namespace Vixen::Editor
