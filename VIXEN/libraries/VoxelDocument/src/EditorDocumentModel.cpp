#include "EditorDocumentModel.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <fstream>
#include <iterator>
#include <optional>
#include <utility>

#include "Recipe/RecipeBounds.h"
#include "Recipe/generated/RecipeContainer.g.h"
#include "Recipe/generated/GlyphOutlineInstance.g.hpp"
#include "Recipe/generated/PathInstance.g.hpp"
#include <RecipeSimd.g.hpp>

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

using Yeroket::Sdf::Generated::SdfInstruction;
using Vixen::SVO::Recipe::SdfOpCode;

constexpr float kDegreesToRadians = 0.017453292519943295f;
constexpr float kRadiansToDegrees = 57.29577951308232f;

bool PushesPosition(uint8_t opCode) {
    switch (static_cast<SdfOpCode>(opCode)) {
        case SdfOpCode::Transform:
        case SdfOpCode::Twist:
        case SdfOpCode::Bend:
        case SdfOpCode::Elongate:
        case SdfOpCode::MirrorX:
        case SdfOpCode::MirrorY:
        case SdfOpCode::MirrorZ:
        case SdfOpCode::RepeatInfinite:
        case SdfOpCode::RepeatLimited:
        case SdfOpCode::Revolution:
            return true;
        default:
            return false;
    }
}

// True when the program is exactly one Transform ... RestorePos pair around its whole body.
bool HasTransformWrapper(const std::vector<SdfInstruction>& program) {
    if (program.size() < 2u) return false;
    if (program.front().opCode != static_cast<uint8_t>(SdfOpCode::Transform)) return false;
    if (program.back().opCode != static_cast<uint8_t>(SdfOpCode::RestorePos)) return false;
    int depth = 0;
    for (size_t i = 0; i < program.size(); ++i) {
        const uint8_t op = program[i].opCode;
        if (PushesPosition(op)) {
            ++depth;
        } else if (op == static_cast<uint8_t>(SdfOpCode::RestorePos)) {
            --depth;
            if (depth == 0 && i + 1u != program.size()) return false;
            if (depth < 0) return false;
        }
    }
    return depth == 0;
}

bool AllFinite(const std::array<float, 3>& values) {
    return std::isfinite(values[0]) && std::isfinite(values[1]) && std::isfinite(values[2]);
}

// Unit quaternion xyzw of R = Rz(z) * Ry(y) * Rx(x) (angles in radians).
std::array<float, 4> QuaternionFromEuler(float x, float y, float z) {
    const float cx = std::cos(x * 0.5f), sx = std::sin(x * 0.5f);
    const float cy = std::cos(y * 0.5f), sy = std::sin(y * 0.5f);
    const float cz = std::cos(z * 0.5f), sz = std::sin(z * 0.5f);
    return {sx * cy * cz - cx * sy * sz,
            cx * sy * cz + sx * cy * sz,
            cx * cy * sz - sx * sy * cz,
            cx * cy * cz + sx * sy * sz};
}

std::array<float, 3> EulerFromQuaternion(const std::array<float, 4>& q) {
    const float x = q[0], y = q[1], z = q[2], w = q[3];
    const float r20 = 2.0f * (x * z - y * w);
    const float sinY = std::clamp(-r20, -1.0f, 1.0f);
    float rx, rz;
    if (std::fabs(sinY) > 0.999999f) {
        rx = std::atan2(-2.0f * (y * z - x * w), 1.0f - 2.0f * (x * x + z * z));
        rz = 0.0f;
    } else {
        rx = std::atan2(2.0f * (y * z + x * w), 1.0f - 2.0f * (x * x + y * y));
        rz = std::atan2(2.0f * (x * y + z * w), 1.0f - 2.0f * (y * y + z * z));
    }
    return {rx, std::asin(sinY), rz};
}

// The index of the one primitive instruction of a shape-editable program, or nullopt.
// Shape-editable = [Transform] + exactly one Primitive-category instruction + [RestorePos].
std::optional<size_t> FindPrimitiveInstruction(const std::vector<SdfInstruction>& program) {
    size_t first = 0;
    size_t last = program.size();
    if (HasTransformWrapper(program)) {
        first = 1;
        last = program.size() - 1u;
    }
    if (last != first + 1u) return std::nullopt;
    const auto* info = Vixen::SVO::Recipe::FindSdfOpInfo(static_cast<SdfOpCode>(program[first].opCode));
    if (info == nullptr || info->category != Vixen::SVO::Recipe::SdfOpCategory::Primitive) {
        return std::nullopt;
    }
    return first;
}

// The InstructionData arguments of an opcode (the editable fields), in table order.
std::vector<const Vixen::SVO::Recipe::SdfOpArgument*> EditableArguments(SdfOpCode opcode) {
    std::vector<const Vixen::SVO::Recipe::SdfOpArgument*> out;
    const auto* info = Vixen::SVO::Recipe::FindSdfOpInfo(opcode);
    if (info == nullptr) return out;
    const auto* arguments = Vixen::SVO::Recipe::SdfOpArguments(*info);
    for (uint32_t i = 0; i < info->argumentCount; ++i) {
        if (arguments[i].source == Vixen::SVO::Recipe::SdfArgumentSource::InstructionData) {
            out.push_back(&arguments[i]);
        }
    }
    return out;
}

// Wraps `program` in the placement Transform ... RestorePos pair (nothing for the identity placement).
void WrapInTransform(std::vector<SdfInstruction>& program, const LayerTransform& transform) {
    if (transform.IsIdentity()) return;
    SdfInstruction xform{};
    xform.opCode = static_cast<uint8_t>(SdfOpCode::Transform);
    xform.data[0] = transform.position[0];
    xform.data[1] = transform.position[1];
    xform.data[2] = transform.position[2];
    const auto q = QuaternionFromEuler(transform.rotationDegrees[0] * kDegreesToRadians,
                                       transform.rotationDegrees[1] * kDegreesToRadians,
                                       transform.rotationDegrees[2] * kDegreesToRadians);
    xform.data[4] = 0.0f - q[0];  // 0 - x keeps +0 for an identity rotation (never -0)
    xform.data[5] = 0.0f - q[1];
    xform.data[6] = 0.0f - q[2];
    xform.data[7] = q[3];
    xform.data[8] = 1.0f / transform.scale[0];
    xform.data[9] = 1.0f / transform.scale[1];
    xform.data[10] = 1.0f / transform.scale[2];
    // A distance through a non-uniform scale is only bounded, never exact; the smallest axis
    // scale keeps the bound conservative (exact for uniform scale).
    xform.data[11] = std::min({transform.scale[0], transform.scale[1], transform.scale[2]});
    SdfInstruction restore{};
    restore.opCode = static_cast<uint8_t>(SdfOpCode::RestorePos);
    program.insert(program.begin(), xform);
    program.push_back(restore);
}

DocumentDiagnostic NotEditable(uint32_t index) {
    return {DocumentDiagnosticCode::FieldNotEditable,
            "layer " + std::to_string(index) +
                " is not a Transform + one primitive (+RestorePos) program; its shape fields are not editable"};
}

}  // namespace

bool EditorDocumentModel::MakePrimitiveLayer(SdfOpCode primitive, const std::string& name,
                                             EditableDocumentLayer& out,
                                             DocumentDiagnostic& diagnostic) {
    diagnostic.Clear();
    const auto* info = Vixen::SVO::Recipe::FindSdfOpInfo(primitive);
    if (info == nullptr || info->category != Vixen::SVO::Recipe::SdfOpCategory::Primitive) {
        diagnostic = {DocumentDiagnosticCode::InvalidPrimitive,
                      "opcode " + std::to_string(static_cast<unsigned>(primitive)) +
                          " is not a Primitive in the recipe op catalogue"};
        return false;
    }
    SdfInstruction instruction{};
    instruction.opCode = static_cast<uint8_t>(primitive);
    const auto* arguments = Vixen::SVO::Recipe::SdfOpArguments(*info);
    for (uint32_t i = 0; i < info->argumentCount; ++i) {
        const auto& argument = arguments[i];
        // Position and value-stack arguments are the evaluator's inputs, not user fields.
        if (argument.source == Vixen::SVO::Recipe::SdfArgumentSource::Position ||
            argument.source == Vixen::SVO::Recipe::SdfArgumentSource::ValueStack) {
            continue;
        }
        if (!argument.hasDefault) {
            diagnostic = {DocumentDiagnosticCode::InvalidPrimitive,
                          std::string("primitive ") + info->name + " argument '" + argument.name +
                              "' declares no default; the catalogue cannot build it"};
            return false;
        }
        for (uint32_t c = 0; c < argument.width; ++c) {
            instruction.data[argument.offset + c] = argument.defaultValue[c];
        }
    }
    if (!ValidateLayerName(name, diagnostic)) return false;
    out = EditableDocumentLayer{};
    out.name = name;
    out.program.push_back(instruction);
    return true;
}

std::string EditorDocumentModel::DisplayName(std::string_view opcodeName) {
    const auto isUpper = [](char ch) { return ch >= 'A' && ch <= 'Z'; };
    const auto isLower = [](char ch) { return ch >= 'a' && ch <= 'z'; };
    std::string out;
    out.reserve(opcodeName.size() + 4);
    for (size_t i = 0; i < opcodeName.size(); ++i) {
        const char ch = opcodeName[i];
        const bool boundary = i != 0 && isUpper(ch) &&
            (isLower(opcodeName[i - 1]) || (isUpper(opcodeName[i - 1]) && i + 1 < opcodeName.size() && isLower(opcodeName[i + 1])));
        if (boundary) out.push_back(' ');
        out.push_back(ch);
    }
    return out;
}

int EditorDocumentModel::FilterRank(std::string_view opcodeName, std::string_view filter) {
    const auto lower = [](std::string_view text) {
        std::string out(text);
        for (char& ch : out) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        return out;
    };
    const std::string needle = lower(filter);
    if (needle.empty()) return 0;
    const std::string name = lower(opcodeName);
    const std::string display = lower(DisplayName(opcodeName));
    if (name.rfind(needle, 0) == 0 || display.rfind(needle, 0) == 0) return 0;
    for (size_t i = 1; i < display.size(); ++i) {
        if (display[i - 1] == ' ' && display.compare(i, needle.size(), needle) == 0) return 1;
    }
    if (name.find(needle) != std::string::npos || display.find(needle) != std::string::npos) return 2;
    return -1;
}

std::vector<EditorDocumentModel::PrimitiveChoice> EditorDocumentModel::CreatablePrimitives(std::string_view filter) {
    static constexpr const char* kCategories[] = {"Control", "Domain", "Vector", "Primitive",
                                                  "Combine", "Modifier", "Input", "Expression"};
    struct Ranked { int rank; PrimitiveChoice choice; };
    std::vector<Ranked> ranked;
    for (const auto& info : Vixen::SVO::Recipe::SdfOpInfoTable) {
        if (info.category != Vixen::SVO::Recipe::SdfOpCategory::Primitive ||
            !CanMakePrimitive(static_cast<SdfOpCode>(info.opcode))) continue;
        const int rank = FilterRank(info.name, filter);
        if (rank < 0) continue;
        ranked.push_back({rank, {info.name, DisplayName(info.name), kCategories[static_cast<size_t>(info.category)]}});
    }
    std::sort(ranked.begin(), ranked.end(), [](const Ranked& a, const Ranked& b) {
        return a.rank != b.rank ? a.rank < b.rank : a.choice.name < b.choice.name;
    });
    std::vector<PrimitiveChoice> out;
    out.reserve(ranked.size());
    for (auto& entry : ranked) out.push_back(std::move(entry.choice));
    return out;
}

bool EditorDocumentModel::CanMakePrimitive(SdfOpCode primitive, std::string* reason) {
    EditableDocumentLayer scratch;
    DocumentDiagnostic diagnostic;
    const bool ok = MakePrimitiveLayer(primitive, "probe", scratch, diagnostic);
    if (!ok && reason != nullptr) *reason = diagnostic.message;
    return ok;
}

void EditorDocumentModel::DeriveBoundsOf(const Vixen::SVO::RecipeRegistry::RecipeEntry& entry,
                                         std::array<float, 3>& center, float& radius) const {
    // Non-whitelisted programs use the finite editor bake domain as a conservative envelope (64 voxels,
    // its half diagonal): the same fallback the preview and the camera framing use.
    constexpr float kBakeResolution = 64.0f;
    center = {0.0f, 0.0f, 0.0f};
    radius = kBakeResolution * 0.86602540378f;
    std::array<float, 6> maxima{};
    for (uint32_t i = 0; i < ParameterCount(); ++i) {
        const uint32_t slot = ParameterSlot(i);
        if (slot < maxima.size()) maxima[slot] = ParameterMax(i);
    }
    const auto derived = Vixen::SVO::Recipe::DeriveConservativeBounds(
        entry.bytecode.data(), static_cast<uint32_t>(entry.bytecode.size()), maxima);
    if (derived.ok && std::isfinite(derived.radius) && derived.radius > 0.0f) {
        center = {derived.center.x, derived.center.y, derived.center.z};
        radius = derived.radius;
    }
}

void EditorDocumentModel::DerivePreviewBounds(std::array<float, 3>& center, float& radius) const {
    Vixen::SVO::RecipeRegistry::RecipeEntry entry;
    DocumentDiagnostic ignored;
    if (!FlattenToRecipeEntry(entry, ignored)) entry.bytecode.clear();  // falls back to the bake domain
    DeriveBoundsOf(entry, center, radius);
}

bool EditorDocumentModel::PlaceNewPrimitive(EditableDocumentLayer& layer, std::optional<uint32_t> selectedLayer,
                                            DocumentDiagnostic& diagnostic) const {
    diagnostic.Clear();
    std::array<float, 3> documentCenter{};
    float documentRadius = 0.0f;
    DerivePreviewBounds(documentCenter, documentRadius);

    // The primitive's own default bound radius (its catalogue defaults stay as they are); a primitive the
    // bound whitelist does not cover counts as unit size.
    float defaultRadius = 1.0f;
    if (const auto own = Vixen::SVO::Recipe::DeriveConservativeBounds(
            layer.program.data(), static_cast<uint32_t>(layer.program.size()));
        own.ok && std::isfinite(own.radius) && own.radius > 0.0f) {
        defaultRadius = own.radius;
    }
    const float scale = kNewPrimitiveBoundsFraction * documentRadius / defaultRadius;

    LayerTransform placement;
    placement.scale = {scale, scale, scale};
    placement.position = documentCenter;
    if (selectedLayer && *selectedLayer < LayerCount()) {
        LayerTransform selected;
        DocumentDiagnostic ignored;
        GetLayerTransform(*selectedLayer, selected, ignored);
        // The selected layer's bound radius: its unwrapped program's bound times its largest axis scale;
        // a layer the whitelist cannot bound lands on the document radius instead.
        const auto& source = view_.layers[*selectedLayer];
        std::vector<SdfInstruction> program(source.instructions, source.instructions + source.header->instructionCount);
        if (HasTransformWrapper(program)) {
            program.pop_back();
            program.erase(program.begin());
        }
        float selectedRadius = documentRadius;
        if (const auto bounds = Vixen::SVO::Recipe::DeriveConservativeBounds(
                program.data(), static_cast<uint32_t>(program.size()));
            bounds.ok && std::isfinite(bounds.radius) && bounds.radius > 0.0f) {
            selectedRadius = bounds.radius * std::max({selected.scale[0], selected.scale[1], selected.scale[2]});
        }
        for (size_t axis = 0; axis < 3; ++axis) {
            placement.position[axis] = selected.position[axis] + kNewPrimitiveAxis[axis] * selectedRadius;
        }
    }
    if (!ValidateLayerTransform(placement, diagnostic)) return false;
    WrapInTransform(layer.program, placement);
    return true;
}

bool EditorDocumentModel::SetLayerOp(uint32_t index, uint8_t layerOp, DocumentDiagnostic& diagnostic) {
    diagnostic.Clear();
    if (index >= LayerCount()) {
        diagnostic = {DocumentDiagnosticCode::InvalidLayerIndex,
                      "layer index " + std::to_string(index) + " is outside the document"};
        return false;
    }
    if (Vixen::SVO::Recipe::FindSdfLayerOp(layerOp) == nullptr) {
        diagnostic = {DocumentDiagnosticCode::InvalidLayerOp,
                      "layer op " + std::to_string(layerOp) + " is not in the layer-op table"};
        return false;
    }
    if (view_.layers[index].header->op == layerOp) return true;
    auto layers = CopyLayers();
    layers[index].op = layerOp;
    return Rebuild(std::move(layers), CopyParameters(), enabledMask_, diagnostic);
}

bool EditorDocumentModel::GetLayerPrimitive(uint32_t index, SdfOpCode& out,
                                            DocumentDiagnostic& diagnostic) const {
    diagnostic.Clear();
    if (index >= LayerCount()) {
        diagnostic = {DocumentDiagnosticCode::InvalidLayerIndex,
                      "layer index " + std::to_string(index) + " is outside the document"};
        return false;
    }
    const auto& layer = view_.layers[index];
    const std::vector<SdfInstruction> program(layer.instructions,
                                              layer.instructions + layer.header->instructionCount);
    const auto at = FindPrimitiveInstruction(program);
    if (!at) {
        diagnostic = NotEditable(index);
        return false;
    }
    out = static_cast<SdfOpCode>(program[*at].opCode);
    return true;
}

bool EditorDocumentModel::GetLayerPrimitiveFields(uint32_t index, std::vector<PrimitiveFieldInfo>& out,
                                                  DocumentDiagnostic& diagnostic) const {
    out.clear();
    SdfOpCode primitive{};
    if (!GetLayerPrimitive(index, primitive, diagnostic)) return false;
    const auto& layer = view_.layers[index];
    const std::vector<SdfInstruction> program(layer.instructions,
                                              layer.instructions + layer.header->instructionCount);
    const SdfInstruction& instruction = program[*FindPrimitiveInstruction(program)];
    for (const auto* argument : EditableArguments(primitive)) {
        PrimitiveFieldInfo field;
        field.name = argument->name;
        field.type = argument->type;
        field.label = argument->label;
        field.width = argument->width;
        field.hasRange = argument->hasRange != 0u;
        field.minimum = argument->minValue;
        field.maximum = argument->maxValue;
        field.hasDefault = argument->hasDefault != 0u;
        for (uint32_t c = 0; c < argument->width && c < 4u; ++c) field.defaultValue[c] = argument->defaultValue[c];
        for (uint32_t c = 0; c < argument->width && c < 4u; ++c) {
            field.value[c] = instruction.data[argument->offset + c];
        }
        out.push_back(field);
    }
    return true;
}

bool EditorDocumentModel::GetLayerPrimitiveField(uint32_t index, const std::string& argumentName,
                                                 std::array<float, 4>& out,
                                                 DocumentDiagnostic& diagnostic) const {
    std::vector<PrimitiveFieldInfo> fields;
    if (!GetLayerPrimitiveFields(index, fields, diagnostic)) return false;
    for (const auto& field : fields) {
        if (argumentName == field.name) {
            out = field.value;
            return true;
        }
    }
    diagnostic = {DocumentDiagnosticCode::InvalidPrimitiveField,
                  "layer " + std::to_string(index) + " primitive has no editable field '" + argumentName + "'"};
    return false;
}

bool EditorDocumentModel::ValidatePrimitiveField(const PrimitiveFieldInfo& field,
                                                 const std::array<float, 4>& value,
                                                 DocumentDiagnostic& diagnostic) {
    diagnostic.Clear();
    for (uint32_t c = 0; c < field.width && c < 4u; ++c) {
        if (!std::isfinite(value[c])) {
            diagnostic = {DocumentDiagnosticCode::InvalidPrimitiveField,
                          std::string("field '") + field.name + "' must be finite"};
            return false;
        }
        if (field.hasRange && (value[c] < field.minimum || value[c] > field.maximum)) {
            diagnostic = {DocumentDiagnosticCode::InvalidPrimitiveField,
                          std::string("field '") + field.name + "' component " + std::to_string(c) + " = " +
                              std::to_string(value[c]) + " is outside the declared range [" +
                              std::to_string(field.minimum) + ", " + std::to_string(field.maximum) + "]"};
            return false;
        }
    }
    return true;
}

bool EditorDocumentModel::SetLayerPrimitiveField(uint32_t index, const std::string& argumentName,
                                                 const std::array<float, 4>& value,
                                                 DocumentDiagnostic& diagnostic) {
    std::vector<PrimitiveFieldInfo> fields;
    if (!GetLayerPrimitiveFields(index, fields, diagnostic)) return false;
    const PrimitiveFieldInfo* field = nullptr;
    for (const auto& candidate : fields) {
        if (argumentName == candidate.name) field = &candidate;
    }
    if (field == nullptr) {
        diagnostic = {DocumentDiagnosticCode::InvalidPrimitiveField,
                      "layer " + std::to_string(index) + " primitive has no editable field '" + argumentName + "'"};
        return false;
    }
    if (!ValidatePrimitiveField(*field, value, diagnostic)) return false;
    auto layers = CopyLayers();
    auto& program = layers[index].program;
    SdfInstruction& instruction = program[*FindPrimitiveInstruction(program)];
    // Locate the argument's data offset again through the table (PrimitiveFieldInfo carries no offset).
    for (const auto* argument : EditableArguments(static_cast<SdfOpCode>(instruction.opCode))) {
        if (argumentName != argument->name) continue;
        for (uint32_t c = 0; c < argument->width; ++c) instruction.data[argument->offset + c] = value[c];
    }
    return Rebuild(std::move(layers), CopyParameters(), enabledMask_, diagnostic);
}

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
    return view_.header.formatVersion >= 2u ? view_.header.parameterCount : 0u;
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

bool EditorDocumentModel::ValidateBlendRadius(float radius, DocumentDiagnostic& diagnostic) {
    diagnostic.Clear();
    if (!std::isfinite(radius) || radius < 0.0f) {
        diagnostic = {DocumentDiagnosticCode::InvalidBlendRadius,
                      "layer blend radius must be finite and not negative"};
        return false;
    }
    return true;
}

bool EditorDocumentModel::ValidateLayerName(const std::string& name,
                                            DocumentDiagnostic& diagnostic) {
    diagnostic.Clear();
    if (name.empty() || name.size() > kMaximumLayerNameBytes ||
        name.find('\0') != std::string::npos) {
        diagnostic = {DocumentDiagnosticCode::InvalidLayerName,
                      "layer name must be 1 to " + std::to_string(kMaximumLayerNameBytes) +
                          " bytes without NUL (got " + std::to_string(name.size()) + " bytes)"};
        return false;
    }
    return true;
}

bool EditorDocumentModel::ValidateLayerTransform(const LayerTransform& transform,
                                                 DocumentDiagnostic& diagnostic) {
    diagnostic.Clear();
    if (!AllFinite(transform.position) || !AllFinite(transform.rotationDegrees) ||
        !AllFinite(transform.scale) || transform.scale[0] <= 0.0f ||
        transform.scale[1] <= 0.0f || transform.scale[2] <= 0.0f) {
        diagnostic = {DocumentDiagnosticCode::InvalidTransform,
                      "layer transform values must be finite and the scale positive on every axis"};
        return false;
    }
    return true;
}

bool EditorDocumentModel::SetLayerBlendRadius(uint32_t index, float radius,
                                              DocumentDiagnostic& diagnostic) {
    diagnostic.Clear();
    if (index >= LayerCount()) {
        diagnostic = {DocumentDiagnosticCode::InvalidLayerIndex,
                      "layer index " + std::to_string(index) + " is outside the document"};
        return false;
    }
    if (!ValidateBlendRadius(radius, diagnostic)) return false;
    if (radius == view_.layers[index].header->blendRadius) return true;
    auto layers = CopyLayers();
    layers[index].blendRadius = radius;
    return Rebuild(std::move(layers), CopyParameters(), enabledMask_, diagnostic);
}

bool EditorDocumentModel::SetLayerName(uint32_t index, const std::string& name,
                                       DocumentDiagnostic& diagnostic) {
    diagnostic.Clear();
    if (index >= LayerCount()) {
        diagnostic = {DocumentDiagnosticCode::InvalidLayerIndex,
                      "layer index " + std::to_string(index) + " is outside the document"};
        return false;
    }
    if (!ValidateLayerName(name, diagnostic)) return false;
    if (name == LayerName(index)) return true;
    auto layers = CopyLayers();
    layers[index].name = name;
    return Rebuild(std::move(layers), CopyParameters(), enabledMask_, diagnostic);
}

bool EditorDocumentModel::GetLayerTransform(uint32_t index, LayerTransform& out,
                                            DocumentDiagnostic& diagnostic) const {
    diagnostic.Clear();
    if (index >= LayerCount()) {
        diagnostic = {DocumentDiagnosticCode::InvalidLayerIndex,
                      "layer index " + std::to_string(index) + " is outside the document"};
        return false;
    }
    out = LayerTransform{};
    const auto& layer = view_.layers[index];
    const std::vector<SdfInstruction> program(layer.instructions,
                                              layer.instructions + layer.header->instructionCount);
    if (!HasTransformWrapper(program)) return true;
    const SdfInstruction& xform = program.front();
    out.position = {xform.data[0], xform.data[1], xform.data[2]};
    const auto euler = EulerFromQuaternion(
        {-xform.data[4], -xform.data[5], -xform.data[6], xform.data[7]});
    // + 0.0f turns a decoded -0 into +0, so an untouched axis reads 0 and not -0.
    out.rotationDegrees = {euler[0] * kRadiansToDegrees + 0.0f, euler[1] * kRadiansToDegrees + 0.0f,
                           euler[2] * kRadiansToDegrees + 0.0f};
    out.scale = {1.0f / xform.data[8], 1.0f / xform.data[9], 1.0f / xform.data[10]};
    return true;
}

bool EditorDocumentModel::SetLayerTransform(uint32_t index, const LayerTransform& transform,
                                            DocumentDiagnostic& diagnostic) {
    diagnostic.Clear();
    if (index >= LayerCount()) {
        diagnostic = {DocumentDiagnosticCode::InvalidLayerIndex,
                      "layer index " + std::to_string(index) + " is outside the document"};
        return false;
    }
    if (!ValidateLayerTransform(transform, diagnostic)) return false;

    auto layers = CopyLayers();
    auto& program = layers[index].program;
    const bool wrapped = HasTransformWrapper(program);
    if (wrapped && program.front().paramMask != 0u) {
        diagnostic = {DocumentDiagnosticCode::InvalidTransform,
                      "layer " + std::to_string(index) +
                          " has a parameter-driven Transform; edit its parameters instead"};
        return false;
    }
    if (wrapped) {
        program.pop_back();
        program.erase(program.begin());
    }
    WrapInTransform(program, transform);
    return Rebuild(std::move(layers), CopyParameters(), enabledMask_, diagnostic);
}

bool EditorDocumentModel::SetLayerProgram(uint32_t index,
                                          const std::vector<SdfInstruction>& program,
                                          DocumentDiagnostic& diagnostic) {
    diagnostic.Clear();
    if (index >= LayerCount()) {
        diagnostic = {DocumentDiagnosticCode::InvalidLayerIndex,
                      "layer index " + std::to_string(index) + " is outside the document"};
        return false;
    }
    for (const auto& instruction : program) {
        for (const float value : instruction.data) {
            if (!std::isfinite(value)) {
                diagnostic = {DocumentDiagnosticCode::InvalidProgramField,
                              "program fields must be finite"};
                return false;
            }
        }
    }
    auto layers = CopyLayers();
    layers[index].program = program;
    return Rebuild(std::move(layers), CopyParameters(), enabledMask_, diagnostic);
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
    const auto* entry = Vixen::SVO::Recipe::FindSdfLayerOp(op);
    return entry != nullptr ? entry->name : "unknown";
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

bool EditorDocumentModel::Serialize(std::vector<uint8_t>& bytes,
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
    bytes.assign(required, 0u);
    size_t written = 0;
    if (!WriteVoxelDocument(view_.channels, view_.header.channelCount,
                            parameters.data(), static_cast<uint32_t>(parameters.size()),
                            writes.data(), LayerCount(), bytes.data(), bytes.size(), written)) {
        diagnostic = {DocumentDiagnosticCode::WriteError,
                      "WriteVoxelDocument failed sizing/writing the document"};
        return false;
    }
    bytes.resize(written);
    return true;
}

bool EditorDocumentModel::Save(const std::string& outPath,
                               DocumentDiagnostic& diagnostic) const {
    std::vector<uint8_t> bytes;
    if (!Serialize(bytes, diagnostic)) return false;

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
