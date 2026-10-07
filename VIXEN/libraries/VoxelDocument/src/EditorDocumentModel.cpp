#include "EditorDocumentModel.h"

#include <cstddef>
#include <fstream>
#include <utility>

#include "Recipe/generated/RecipeContainer.g.h"
#include "Recipe/generated/RecipeSimd.g.hpp"

namespace Vixen::Editor {

uint32_t EditorDocumentModel::ValidMaskForLayerCount(uint32_t count) {
    if (count == 0u) return 0u;
    if (count >= kMaximumLayerCount) return 0xFFFFFFFFu;
    return (1u << count) - 1u;
}

bool EditorDocumentModel::Load(const std::string& path, DocumentDiagnostic& diagnostic) {
    diagnostic.Clear();

    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        diagnostic = {DocumentDiagnosticCode::IoError, "cannot open document: " + path};
        return false;
    }

    const std::streamsize size = file.tellg();
    if (size <= 0) {
        diagnostic = {DocumentDiagnosticCode::EmptyDocument, "empty document: " + path};
        return false;
    }

    std::vector<uint8_t> candidateBytes(static_cast<size_t>(size));
    file.seekg(0);
    if (!file.read(reinterpret_cast<char*>(candidateBytes.data()), size)) {
        diagnostic = {DocumentDiagnosticCode::IoError, "failed reading document: " + path};
        return false;
    }

    Yeroket::Sdf::Generated::VoxelDocumentView candidateView{};
    if (!Yeroket::Sdf::Generated::ReadVoxelDocument(
            candidateBytes.data(), candidateBytes.size(), candidateView)) {
        diagnostic = {DocumentDiagnosticCode::MalformedDocument,
                      "malformed VoxelDocument: " + path};
        return false;
    }
    if (candidateView.header.layerCount > kMaximumLayerCount) {
        diagnostic = {DocumentDiagnosticCode::TooManyLayers,
                      "VoxelDocument has " + std::to_string(candidateView.header.layerCount) +
                          " layers; the editor supports at most " +
                          std::to_string(kMaximumLayerCount)};
        return false;
    }

    uint32_t candidateMask = 0u;
    for (uint32_t i = 0; i < candidateView.header.layerCount; ++i) {
        if (candidateView.layers[i].header->enabled != 0u) {
            candidateMask |= (1u << i);  // Admission caps i at 31 before any mask shift.
        }
    }

    // candidateView contains pointers into candidateBytes. Swapping the vector transfers the
    // allocation without moving its bytes, so those pointers remain valid after the commit.
    rawBytes_.swap(candidateBytes);
    view_ = candidateView;
    sourcePath_ = path;
    enabledMask_ = candidateMask;
    return true;
}

const Yeroket::Sdf::Generated::VoxelDocumentView& EditorDocumentModel::View() const {
    return view_;
}

const std::string& EditorDocumentModel::SourcePath() const {
    return sourcePath_;
}

uint32_t EditorDocumentModel::LayerCount() const {
    return view_.header.layerCount;
}

uint32_t EditorDocumentModel::EnabledMask() const {
    return enabledMask_;
}

bool EditorDocumentModel::SetLayerEnabled(uint32_t index, bool enabled,
                                          DocumentDiagnostic& diagnostic) {
    diagnostic.Clear();
    if (index >= LayerCount()) {
        diagnostic = {DocumentDiagnosticCode::InvalidLayerIndex,
                      "layer index " + std::to_string(index) + " is outside the document"};
        return false;
    }

    const uint32_t bit = 1u << index;  // LayerCount is bounded to 32 at admission.
    if (enabled) {
        enabledMask_ |= bit;
    } else {
        enabledMask_ &= ~bit;
    }
    return true;
}

bool EditorDocumentModel::SetEnabledMask(uint32_t mask, DocumentDiagnostic& diagnostic) {
    diagnostic.Clear();
    if ((mask & ~ValidMaskForLayerCount(LayerCount())) != 0u) {
        diagnostic = {DocumentDiagnosticCode::InvalidLayerMask,
                      "enabled-layer mask contains bits outside the document layer range"};
        return false;
    }
    enabledMask_ = mask;
    return true;
}

std::string EditorDocumentModel::LayerName(uint32_t i) const {
    if (i >= LayerCount()) return {};
    const auto* header = view_.layers[i].header;
    // name is fixed uint8[32], UTF-8, NUL-padded.
    const char* bytes = reinterpret_cast<const char*>(header->nameBytes);
    size_t len = 0;
    while (len < sizeof(header->nameBytes) && bytes[len] != '\0') ++len;
    return std::string(bytes, len);
}

const char* EditorDocumentModel::OpName(uint8_t op) {
    switch (op) {
        case 0: return "union";
        case 1: return "smooth_union";
        case 2: return "subtract";
        case 3: return "intersect";
        default: return "unknown";
    }
}

bool EditorDocumentModel::Flatten(std::vector<uint8_t>& outVrc1Blob,
                                  DocumentDiagnostic& diagnostic) const {
    diagnostic.Clear();
    std::vector<uint8_t> enabledOverride(LayerCount());
    for (uint32_t i = 0; i < LayerCount(); ++i) {
        enabledOverride[i] = static_cast<uint8_t>((enabledMask_ >> i) & 1u);
    }

    std::string error;
    if (!Vixen::SVO::FlattenVoxelDocument(view_, &enabledOverride, outVrc1Blob, error)) {
        diagnostic = {DocumentDiagnosticCode::FlattenError, std::move(error)};
        return false;
    }
    return true;
}

bool EditorDocumentModel::FlattenToRecipeEntry(
    Vixen::SVO::RecipeRegistry::RecipeEntry& outEntry,
    DocumentDiagnostic& diagnostic) const {
    using Yeroket::Sdf::Generated::SdfInstruction;
    using Vixen::SVO::Recipe::SdfOpCode;

    diagnostic.Clear();
    std::vector<uint8_t> enabledOverride(LayerCount());
    for (uint32_t i = 0; i < LayerCount(); ++i) {
        enabledOverride[i] = static_cast<uint8_t>((enabledMask_ >> i) & 1u);
    }

    std::vector<SdfInstruction> instructions;
    int sp = 0;
    int psp = 0;
    bool haveBase = false;
    for (uint32_t layerIndex = 0; layerIndex < LayerCount(); ++layerIndex) {
        if (enabledOverride[layerIndex] == 0) continue;
        const auto& layer = view_.layers[layerIndex];
        const uint32_t count = layer.header->instructionCount;
        std::string error;
        if (!Vixen::SVO::GeneratedRecipePipelineDetail::ValidateProgram(
                layer.instructions, count, sp, psp, error)) {
            diagnostic = {DocumentDiagnosticCode::FlattenError, std::move(error)};
            return false;
        }
        if (!haveBase) {
            instructions.insert(instructions.end(), layer.instructions,
                                layer.instructions + count);
            haveBase = true;
            continue;
        }

        SdfOpCode combineOpcode{};
        if (!Vixen::SVO::GeneratedRecipePipelineDetail::LayerCombineOpcode(
                layer.header->op, combineOpcode)) {
            diagnostic = {
                DocumentDiagnosticCode::FlattenError,
                "unknown layer op " + std::to_string(layer.header->op) +
                    " on layer index " + std::to_string(layerIndex)};
            return false;
        }
        instructions.insert(instructions.end(), layer.instructions,
                            layer.instructions + count);
        SdfInstruction combine{};
        combine.opCode = static_cast<uint8_t>(combineOpcode);
        combine.inputMask = 3;
        combine.data[2] = layer.header->blendRadius;
        if (!Vixen::SVO::GeneratedRecipePipelineDetail::ValidateProgram(
                &combine, 1, sp, psp, error)) {
            diagnostic = {DocumentDiagnosticCode::FlattenError, std::move(error)};
            return false;
        }
        instructions.push_back(combine);
    }
    if (!haveBase) {
        diagnostic = {DocumentDiagnosticCode::FlattenError,
                      "zero enabled layers — nothing to render"};
        return false;
    }

    outEntry = Vixen::SVO::RecipeRegistry::RecipeEntry{};
    outEntry.bytecode = std::move(instructions);
    outEntry.bakeResolution = 64u;
    outEntry.bandVoxels = 2.5f;
    outEntry.brickDepth = 3u;
    return true;
}

bool EditorDocumentModel::Save(const std::string& outPath,
                               DocumentDiagnostic& diagnostic) const {
    using namespace Yeroket::Sdf::Generated;

    diagnostic.Clear();
    std::vector<VoxelDocLayerHeader> headers(LayerCount());
    std::vector<VoxelDocLayerWrite> writes(LayerCount());
    for (uint32_t i = 0; i < LayerCount(); ++i) {
        headers[i] = *view_.layers[i].header;
        headers[i].enabled = static_cast<uint8_t>((enabledMask_ >> i) & 1u);
        writes[i].header = headers[i];
        writes[i].instructions = view_.layers[i].instructions;
    }

    size_t required = 0;
    WriteVoxelDocument(view_.channels, view_.header.channelCount,
                       writes.data(), LayerCount(), nullptr, 0, required);
    std::vector<uint8_t> bytes(required);
    size_t written = 0;
    if (!WriteVoxelDocument(view_.channels, view_.header.channelCount,
                            writes.data(), LayerCount(), bytes.data(), bytes.size(), written)) {
        diagnostic = {DocumentDiagnosticCode::WriteError,
                      "WriteVoxelDocument failed sizing/writing the document"};
        return false;
    }
    bytes.resize(written);

    std::ofstream file(outPath, std::ios::binary | std::ios::trunc);
    if (!file) {
        diagnostic = {DocumentDiagnosticCode::IoError, "cannot open output path: " + outPath};
        return false;
    }
    file.write(reinterpret_cast<const char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
    if (!file.good()) {
        diagnostic = {DocumentDiagnosticCode::IoError, "failed writing output path: " + outPath};
        return false;
    }
    return true;
}

}  // namespace Vixen::Editor
