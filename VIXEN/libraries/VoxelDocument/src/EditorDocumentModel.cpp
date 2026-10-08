#include "EditorDocumentModel.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <fstream>
#include <iterator>
#include <utility>

#include "Recipe/generated/RecipeContainer.g.h"
#include "Recipe/generated/RecipeSimd.g.hpp"

namespace Vixen::Editor {

namespace {

template <size_t N>
std::string DecodeFixedText(const uint8_t (&bytes)[N]) {
    size_t size = 0;
    while (size < N && bytes[size] != 0u) ++size;
    return std::string(reinterpret_cast<const char*>(bytes), size);
}

template <size_t N>
void EncodeFixedText(const std::string& value, uint8_t (&bytes)[N]) {
    std::fill(std::begin(bytes), std::end(bytes), 0u);
    const size_t count = std::min(value.size(), N);
    if (count != 0u) std::memcpy(bytes, value.data(), count);
}

bool IsByteValue(float value) {
    return std::isfinite(value) && value >= 0.0f && value <= 255.0f &&
           std::floor(value) == value;
}

}  // namespace

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
    ++revision_;
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

uint64_t EditorDocumentModel::Revision() const {
    return revision_;
}

uint32_t EditorDocumentModel::ParameterCount() const {
    return view_.header.formatVersion == 2u ? view_.header.reserved0 : 0u;
}

std::string EditorDocumentModel::ParameterName(uint32_t index) const {
    if (index >= ParameterCount()) return {};
    return DecodeFixedText(view_.parameters[index].nameBytes);
}

std::string EditorDocumentModel::ParameterUnit(uint32_t index) const {
    if (index >= ParameterCount()) return {};
    return DecodeFixedText(view_.parameters[index].unitBytes);
}

uint32_t EditorDocumentModel::ParameterSlot(uint32_t index) const {
    return index < ParameterCount() ? view_.parameters[index].slot : 0u;
}

float EditorDocumentModel::ParameterValue(uint32_t index) const {
    return index < ParameterCount() ? view_.parameters[index].value : 0.0f;
}

float EditorDocumentModel::ParameterDefault(uint32_t index) const {
    return index < ParameterCount() ? view_.parameters[index].defaultValue : 0.0f;
}

float EditorDocumentModel::ParameterMin(uint32_t index) const {
    return index < ParameterCount() ? view_.parameters[index].minValue : 0.0f;
}

float EditorDocumentModel::ParameterMax(uint32_t index) const {
    return index < ParameterCount() ? view_.parameters[index].maxValue : 0.0f;
}

std::array<float, 6> EditorDocumentModel::EffectiveParameterValues() const {
    std::array<float, 6> values{};
    for (uint32_t i = 0; i < ParameterCount(); ++i) {
        const auto& parameter = view_.parameters[i];
        if (parameter.slot < values.size()) values[parameter.slot] = parameter.value;
    }
    return values;
}

DocumentBakeSnapshot EditorDocumentModel::CaptureBakeSnapshot() const {
    return DocumentBakeSnapshot{revision_, EffectiveParameterValues()};
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
    const bool wasEnabled = (enabledMask_ & bit) != 0u;
    if (enabled) {
        enabledMask_ |= bit;
    } else {
        enabledMask_ &= ~bit;
    }
    if (wasEnabled != enabled) ++revision_;
    return true;
}

bool EditorDocumentModel::SetEnabledMask(uint32_t mask, DocumentDiagnostic& diagnostic) {
    diagnostic.Clear();
    if ((mask & ~ValidMaskForLayerCount(LayerCount())) != 0u) {
        diagnostic = {DocumentDiagnosticCode::InvalidLayerMask,
                      "enabled-layer mask contains bits outside the document layer range"};
        return false;
    }
    if (enabledMask_ != mask) {
        enabledMask_ = mask;
        ++revision_;
    }
    return true;
}

std::vector<EditableDocumentLayer> EditorDocumentModel::CopyLayers() const {
    std::vector<EditableDocumentLayer> layers;
    layers.reserve(LayerCount());
    for (uint32_t i = 0; i < LayerCount(); ++i) {
        const auto& source = view_.layers[i];
        EditableDocumentLayer layer;
        layer.type = source.header->type;
        layer.op = source.header->op;
        layer.enabled = ((enabledMask_ >> i) & 1u) != 0u;
        layer.blendRadius = source.header->blendRadius;
        layer.name = DecodeFixedText(source.header->nameBytes);
        layer.program.assign(source.instructions,
                             source.instructions + source.header->instructionCount);
        layers.push_back(std::move(layer));
    }
    return layers;
}

std::vector<Yeroket::Sdf::Generated::VoxelDocParameterHeader>
EditorDocumentModel::CopyParameters() const {
    const uint32_t count = ParameterCount();
    if (count == 0u) return {};
    return std::vector<Yeroket::Sdf::Generated::VoxelDocParameterHeader>(
        view_.parameters, view_.parameters + count);
}

bool EditorDocumentModel::Rebuild(
    std::vector<EditableDocumentLayer> layers,
    std::vector<Yeroket::Sdf::Generated::VoxelDocParameterHeader> parameters,
    uint32_t enabledMask, DocumentDiagnostic& diagnostic) {
    using namespace Yeroket::Sdf::Generated;
    diagnostic.Clear();
    if (layers.size() > kMaximumLayerCount) {
        diagnostic = {DocumentDiagnosticCode::TooManyLayers,
                      "the editor supports at most " + std::to_string(kMaximumLayerCount) +
                          " document layers"};
        return false;
    }
    if ((enabledMask & ~ValidMaskForLayerCount(static_cast<uint32_t>(layers.size()))) != 0u) {
        diagnostic = {DocumentDiagnosticCode::InvalidLayerMask,
                      "enabled-layer mask contains bits outside the document layer range"};
        return false;
    }

    std::vector<VoxelDocLayerHeader> headers(layers.size());
    std::vector<VoxelDocLayerWrite> writes(layers.size());
    for (size_t i = 0; i < layers.size(); ++i) {
        const auto& source = layers[i];
        if (source.type != 0u || source.op > 3u || !std::isfinite(source.blendRadius)) {
            diagnostic = {DocumentDiagnosticCode::InvalidProgramField,
                          "layer type, operation, or blend radius is invalid"};
            return false;
        }
        auto& header = headers[i];
        header.type = source.type;
        header.op = source.op;
        header.enabled = static_cast<uint8_t>((enabledMask >> i) & 1u);
        header.blendRadius = source.blendRadius;
        header.instructionCount = static_cast<uint32_t>(source.program.size());
        EncodeFixedText(source.name, header.nameBytes);
        writes[i].header = header;
        writes[i].instructions = source.program.data();
    }

    size_t required = 0;
    WriteVoxelDocument(view_.channels, view_.header.channelCount,
                       parameters.data(), static_cast<uint32_t>(parameters.size()),
                       writes.data(), static_cast<uint32_t>(writes.size()),
                       nullptr, 0, required);
    std::vector<uint8_t> candidateBytes(required);
    size_t written = 0;
    if (!WriteVoxelDocument(view_.channels, view_.header.channelCount,
                            parameters.data(), static_cast<uint32_t>(parameters.size()),
                            writes.data(), static_cast<uint32_t>(writes.size()),
                            candidateBytes.data(), candidateBytes.size(), written)) {
        diagnostic = {DocumentDiagnosticCode::WriteError,
                      "WriteVoxelDocument failed to rebuild the candidate document"};
        return false;
    }
    candidateBytes.resize(written);

    VoxelDocumentView candidateView{};
    if (!ReadVoxelDocument(candidateBytes.data(), candidateBytes.size(), candidateView)) {
        diagnostic = {DocumentDiagnosticCode::MalformedDocument,
                      "edited document did not pass generated VoxelDocument validation"};
        return false;
    }
    rawBytes_.swap(candidateBytes);
    view_ = candidateView;
    enabledMask_ = enabledMask;
    ++revision_;
    return true;
}

bool EditorDocumentModel::GetLayer(uint32_t index, EditableDocumentLayer& out,
                                   DocumentDiagnostic& diagnostic) const {
    diagnostic.Clear();
    if (index >= LayerCount()) {
        diagnostic = {DocumentDiagnosticCode::InvalidLayerIndex,
                      "layer index " + std::to_string(index) + " is outside the document"};
        return false;
    }
    out = CopyLayers()[index];
    return true;
}

bool EditorDocumentModel::InsertLayer(uint32_t index, const EditableDocumentLayer& layer,
                                      DocumentDiagnostic& diagnostic) {
    diagnostic.Clear();
    if (index > LayerCount()) {
        diagnostic = {DocumentDiagnosticCode::InvalidLayerIndex,
                      "insert index " + std::to_string(index) + " is outside the document"};
        return false;
    }
    if (LayerCount() >= kMaximumLayerCount) {
        diagnostic = {DocumentDiagnosticCode::TooManyLayers,
                      "the editor supports at most " + std::to_string(kMaximumLayerCount) +
                          " document layers"};
        return false;
    }

    auto layers = CopyLayers();
    layers.insert(layers.begin() + index, layer);
    uint32_t mask = 0u;
    for (uint32_t i = 0; i < layers.size(); ++i)
        if (layers[i].enabled) mask |= (1u << i);
    return Rebuild(std::move(layers), CopyParameters(), mask, diagnostic);
}

bool EditorDocumentModel::DeleteLayer(uint32_t index, EditableDocumentLayer* removed,
                                      DocumentDiagnostic& diagnostic) {
    diagnostic.Clear();
    if (index >= LayerCount()) {
        diagnostic = {DocumentDiagnosticCode::InvalidLayerIndex,
                      "layer index " + std::to_string(index) + " is outside the document"};
        return false;
    }

    auto layers = CopyLayers();
    EditableDocumentLayer deleted = std::move(layers[index]);
    layers.erase(layers.begin() + index);
    uint32_t mask = 0u;
    for (uint32_t i = 0; i < layers.size(); ++i)
        if (layers[i].enabled) mask |= (1u << i);
    if (!Rebuild(std::move(layers), CopyParameters(), mask, diagnostic)) return false;
    if (removed) *removed = std::move(deleted);
    return true;
}

bool EditorDocumentModel::MoveLayer(uint32_t from, uint32_t to,
                                    DocumentDiagnostic& diagnostic) {
    diagnostic.Clear();
    if (from >= LayerCount() || to >= LayerCount()) {
        diagnostic = {DocumentDiagnosticCode::InvalidLayerIndex,
                      "move indices must identify layers in the document"};
        return false;
    }
    if (from == to) return true;

    auto layers = CopyLayers();
    EditableDocumentLayer moved = std::move(layers[from]);
    layers.erase(layers.begin() + from);
    layers.insert(layers.begin() + to, std::move(moved));
    uint32_t mask = 0u;
    for (uint32_t i = 0; i < layers.size(); ++i)
        if (layers[i].enabled) mask |= (1u << i);
    return Rebuild(std::move(layers), CopyParameters(), mask, diagnostic);
}

bool EditorDocumentModel::GetLayerProgramField(uint32_t layerIndex,
                                               uint32_t instructionIndex,
                                               uint32_t fieldIndex, float& out,
                                               DocumentDiagnostic& diagnostic) const {
    diagnostic.Clear();
    if (layerIndex >= LayerCount()) {
        diagnostic = {DocumentDiagnosticCode::InvalidLayerIndex,
                      "layer index " + std::to_string(layerIndex) + " is outside the document"};
        return false;
    }
    const auto& layer = view_.layers[layerIndex];
    if (instructionIndex >= layer.header->instructionCount || fieldIndex >= 35u) {
        diagnostic = {DocumentDiagnosticCode::InvalidProgramField,
                      "instruction or program-field index is outside the layer"};
        return false;
    }
    const auto& instruction = layer.instructions[instructionIndex];
    switch (fieldIndex) {
        case 0: out = static_cast<float>(instruction.opCode); break;
        case 1: out = static_cast<float>(instruction.inputMask); break;
        case 2: out = static_cast<float>(instruction.paramMask); break;
        default: out = instruction.data[fieldIndex - 3u]; break;
    }
    return true;
}

bool EditorDocumentModel::SetLayerProgramField(uint32_t layerIndex,
                                               uint32_t instructionIndex,
                                               uint32_t fieldIndex, float value,
                                               DocumentDiagnostic& diagnostic) {
    diagnostic.Clear();
    if (layerIndex >= LayerCount()) {
        diagnostic = {DocumentDiagnosticCode::InvalidLayerIndex,
                      "layer index " + std::to_string(layerIndex) + " is outside the document"};
        return false;
    }
    if (!std::isfinite(value)) {
        diagnostic = {DocumentDiagnosticCode::InvalidProgramField,
                      "program fields must be finite"};
        return false;
    }

    auto layers = CopyLayers();
    if (instructionIndex >= layers[layerIndex].program.size() || fieldIndex >= 35u) {
        diagnostic = {DocumentDiagnosticCode::InvalidProgramField,
                      "instruction or program-field index is outside the layer"};
        return false;
    }
    auto& instruction = layers[layerIndex].program[instructionIndex];
    switch (fieldIndex) {
        case 0:
            if (!IsByteValue(value)) break;
            instruction.opCode = static_cast<uint8_t>(value);
            break;
        case 1:
            if (!IsByteValue(value)) break;
            instruction.inputMask = static_cast<uint8_t>(value);
            break;
        case 2:
            if (!IsByteValue(value)) break;
            instruction.paramMask = static_cast<uint8_t>(value);
            break;
        default:
            instruction.data[fieldIndex - 3u] = value;
            break;
    }
    if (fieldIndex < 3u && !IsByteValue(value)) {
        diagnostic = {DocumentDiagnosticCode::InvalidProgramField,
                      "opcode and masks must be whole-byte values"};
        return false;
    }
    return Rebuild(std::move(layers), CopyParameters(), enabledMask_, diagnostic);
}

bool EditorDocumentModel::SetParameterValue(uint32_t index, float value,
                                            DocumentDiagnostic& diagnostic) {
    diagnostic.Clear();
    if (index >= ParameterCount()) {
        diagnostic = {DocumentDiagnosticCode::InvalidParameterValue,
                      "parameter index " + std::to_string(index) + " is outside the document"};
        return false;
    }
    const auto& current = view_.parameters[index];
    if (!std::isfinite(value) || value < current.minValue || value > current.maxValue) {
        diagnostic = {DocumentDiagnosticCode::InvalidParameterValue,
                      "parameter value must be finite and within its declared range"};
        return false;
    }
    if (value == current.value) return true;
    auto parameters = CopyParameters();
    parameters[index].value = value;
    return Rebuild(CopyLayers(), std::move(parameters), enabledMask_, diagnostic);
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
    return FlattenToRecipeEntry(outEntry, CaptureBakeSnapshot(), diagnostic);
}

bool EditorDocumentModel::FlattenToRecipeEntry(
    Vixen::SVO::RecipeRegistry::RecipeEntry& outEntry,
    const DocumentBakeSnapshot& snapshot,
    DocumentDiagnostic& diagnostic) const {
    using Yeroket::Sdf::Generated::SdfInstruction;
    using Vixen::SVO::Recipe::SdfOpCode;

    diagnostic.Clear();
    if (snapshot.revision != revision_) {
        diagnostic = {DocumentDiagnosticCode::StaleBakeSnapshot,
                      "bake snapshot revision does not match the accepted document revision"};
        return false;
    }
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
    outEntry.parameterValues.assign(snapshot.parameterValues.begin(),
                                    snapshot.parameterValues.end());
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
    const auto parameters = CopyParameters();

    size_t required = 0;
    WriteVoxelDocument(view_.channels, view_.header.channelCount,
                       parameters.data(), static_cast<uint32_t>(parameters.size()),
                       writes.data(), LayerCount(), nullptr, 0, required);
    std::vector<uint8_t> bytes(required);
    size_t written = 0;
    if (!WriteVoxelDocument(view_.channels, view_.header.channelCount,
                            parameters.data(), static_cast<uint32_t>(parameters.size()),
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
