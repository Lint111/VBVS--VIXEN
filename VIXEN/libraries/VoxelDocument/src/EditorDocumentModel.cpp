#include "EditorDocumentModel.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <fstream>
#include <iterator>
#include <limits>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "Recipe/RecipeBounds.h"
#include "Recipe/generated/RecipeContainer.g.h"
#include "Recipe/generated/GlyphOutlineInstance.g.hpp"
#include "Recipe/generated/PathInstance.g.hpp"
#include <RecipeSimd.g.hpp>

namespace Vixen::Editor {

namespace {

using Yeroket::Sdf::Generated::SdfInstruction;
using Vixen::SVO::Recipe::SdfOpCode;

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

constexpr uint32_t kVdc1LegacyVersion = 1u;
constexpr uint32_t kVdc1ParameterVersion = 2u;
constexpr uint32_t kVdc1MaterialVersion = 3u;
constexpr uint32_t kVdc1ReferenceVersion = 4u;
constexpr uint32_t kVdc1LayerPortVersion = 5u;
constexpr uint32_t kMaximumReferences = 32u;
constexpr uint32_t kMaximumWires = 64u;
constexpr uint32_t kMaximumBindings = 32u;
constexpr size_t kReferenceIdBytes = 64u;
constexpr size_t kPortNameBytes = 32u;
constexpr size_t kQueryIdBytes = 64u;

bool IsValidMaterialTriple(uint32_t major, uint32_t minor, uint32_t majorPercent) {
    if (major == 0u && minor == 0u && majorPercent == 0u) return true;
    return major != 0u && minor != 0u && majorPercent >= 1u && majorPercent <= 99u;
}

bool HasTransformWrapper(const std::vector<SdfInstruction>& program);

bool IsRecipeInstruction(const std::vector<SdfInstruction>& program, uint32_t& tableIndex) {
    size_t instructionIndex = 0u;
    if (program.size() == 3u && HasTransformWrapper(program)) {
        instructionIndex = 1u;
    } else if (program.size() != 1u) {
        return false;
    }
    if (program[instructionIndex].opCode != static_cast<uint8_t>(SdfOpCode::InvokeRecipe)) return false;
    const float rawIndex = program[instructionIndex].data[0];
    if (!std::isfinite(rawIndex) || rawIndex < 0.0f ||
        rawIndex >= static_cast<float>(std::numeric_limits<uint32_t>::max()) ||
        std::floor(rawIndex) != rawIndex) return false;
    tableIndex = static_cast<uint32_t>(rawIndex);
    return true;
}

bool IsWireIndexedArgument(Vixen::SVO::Recipe::SdfArgumentSource source) {
    using Source = Vixen::SVO::Recipe::SdfArgumentSource;
    return source == Source::FieldPort || source == Source::SetPort;
}

void RewriteWireIndices(std::vector<EditableDocumentLayer>& layers, size_t removedIndex) {
    for (auto& layer : layers) {
        for (auto& instruction : layer.program) {
            const auto* info = Vixen::SVO::Recipe::FindSdfOpInfo(
                static_cast<Vixen::SVO::Recipe::SdfOpCode>(instruction.opCode));
            if (info == nullptr) continue;
            if (static_cast<SdfOpCode>(instruction.opCode) == SdfOpCode::ReadPort) {
                const float raw = instruction.data[0];
                if (std::isfinite(raw) && raw >= 0.0f && std::floor(raw) == raw) {
                    const size_t index = static_cast<size_t>(raw);
                    if (index == removedIndex) instruction.data[0] = 0.0f;
                    else if (index > removedIndex) instruction.data[0] = static_cast<float>(index - 1u);
                }
            }
            const auto* args = Vixen::SVO::Recipe::SdfOpArguments(*info);
            for (uint32_t i = 0; i < info->argumentCount; ++i) {
                if (!IsWireIndexedArgument(args[i].source)) continue;
                const float raw = instruction.data[args[i].offset];
                if (!std::isfinite(raw) || raw < 0.0f || std::floor(raw) != raw) continue;
                const size_t index = static_cast<size_t>(raw);
                if (index == removedIndex) instruction.data[args[i].offset] = 0.0f;
                else if (index > removedIndex) instruction.data[args[i].offset] = static_cast<float>(index - 1u);
            }
        }
    }
}

void BindWireIndex(std::vector<EditableDocumentLayer>& layers, uint32_t readerIndex,
                   std::string_view input, size_t wireIndex) {
    if (readerIndex >= layers.size()) return;
    for (auto& instruction : layers[readerIndex].program) {
        const auto* info = Vixen::SVO::Recipe::FindSdfOpInfo(
            static_cast<Vixen::SVO::Recipe::SdfOpCode>(instruction.opCode));
        if (info == nullptr) continue;
        if (input == "read" && static_cast<SdfOpCode>(instruction.opCode) == SdfOpCode::ReadPort) {
            instruction.data[0] = static_cast<float>(wireIndex);
        }
        const auto* args = Vixen::SVO::Recipe::SdfOpArguments(*info);
        for (uint32_t i = 0; i < info->argumentCount; ++i) {
            if (IsWireIndexedArgument(args[i].source) && input == args[i].name) {
                instruction.data[args[i].offset] = static_cast<float>(wireIndex);
            }
        }
    }
}

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

// The index of the one primitive instruction in a graph-shaped layer, or nullopt. The graph
// editor keeps the existing primitive inspector as a convenience projection: it is valid for a
// program with exactly one primitive anywhere in its generated instruction graph (including the
// Transform/domain nodes around the primitive), not only for the old three-instruction shape.
std::optional<size_t> FindPrimitiveInstruction(const std::vector<SdfInstruction>& program) {
    std::optional<size_t> found;
    for (size_t i = 0; i < program.size(); ++i) {
        const auto* info = Vixen::SVO::Recipe::FindSdfOpInfo(static_cast<SdfOpCode>(program[i].opCode));
        if (info == nullptr || info->category != Vixen::SVO::Recipe::SdfOpCategory::Primitive) continue;
        if (found.has_value()) return std::nullopt;
        found = i;
    }
    return found;
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
    const bool oldV5 = v5FeaturesUsed_;
    if (layerOp == 4u) v5FeaturesUsed_ = true;
    if (Rebuild(std::move(layers), CopyParameters(), enabledMask_, diagnostic)) return true;
    v5FeaturesUsed_ = oldV5;
    return false;
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
    if (!IsValidMaterialTriple(candidateView.header.majorMaterialId,
                               candidateView.header.minorMaterialId,
                               candidateView.header.majorWeightPercent)) {
        diagnostic = {DocumentDiagnosticCode::InvalidDocumentMaterial,
                      "VoxelDocument contains an invalid document material triple"};
        return false;
    }
    if (candidateView.referenceCount > kMaximumReferences) {
        diagnostic = {DocumentDiagnosticCode::TooManyReferences,
                      "VoxelDocument contains more than 32 recipe references"};
        return false;
    }
    std::vector<std::string> candidateReferences;
    candidateReferences.reserve(candidateView.referenceCount);
    for (uint32_t i = 0; i < candidateView.referenceCount; ++i) {
        const std::string id = DecodeFixedText(candidateView.references[i].id);
        if (!ValidateReferenceId(id, diagnostic)) return false;
        if (std::find(candidateReferences.begin(), candidateReferences.end(), id) !=
            candidateReferences.end()) {
            diagnostic = {DocumentDiagnosticCode::DuplicateReference,
                          "VoxelDocument contains duplicate recipe reference: " + id};
            return false;
        }
        candidateReferences.push_back(id);
    }

    std::vector<uint32_t> candidateLayerIds(candidateView.header.layerCount);
    uint32_t candidateNextLayerId = candidateView.header.layerCount + 1u;
    bool candidateV5 = candidateView.header.formatVersion >= kVdc1LayerPortVersion;
    if (candidateV5) {
        if (candidateView.layerIdCount != candidateView.header.layerCount ||
            candidateView.nextLayerId == 0u) {
            diagnostic = {DocumentDiagnosticCode::InvalidLayerId,
                          "v5 layer-id table does not match the layer count"};
            return false;
        }
        std::unordered_set<uint32_t> ids;
        ids.reserve(candidateView.layerIdCount);
        uint32_t maximumId = 0u;
        for (uint32_t i = 0; i < candidateView.layerIdCount; ++i) {
            const uint32_t id = candidateView.layerIds[i];
            if (id == 0u || !ids.insert(id).second) {
                diagnostic = {DocumentDiagnosticCode::InvalidLayerId,
                              "v5 layer-id table contains a zero or duplicate id"};
                return false;
            }
            candidateLayerIds[i] = id;
            maximumId = std::max(maximumId, id);
        }
        if (candidateView.nextLayerId <= maximumId) {
            diagnostic = {DocumentDiagnosticCode::InvalidLayerId,
                          "v5 nextLayerId must be greater than every layer id"};
            return false;
        }
        candidateNextLayerId = candidateView.nextLayerId;
    } else {
        for (uint32_t i = 0; i < candidateView.header.layerCount; ++i)
            candidateLayerIds[i] = i + 1u;
    }

    std::vector<LayerWire> candidateWires;
    candidateWires.reserve(candidateView.wireCount);
    for (uint32_t i = 0; i < candidateView.wireCount; ++i) {
        const auto& row = candidateView.wires[i];
        const std::string input = DecodeFixedText(row.input);
        const std::string output = DecodeFixedText(row.output);
        DocumentDiagnostic portDiagnostic;
        if (!ValidatePortName(input, portDiagnostic) || !ValidatePortName(output, portDiagnostic)) {
            diagnostic = portDiagnostic;
            return false;
        }
        if (row.type > static_cast<uint8_t>(LayerPortKind::Grid)) {
            diagnostic = {DocumentDiagnosticCode::InvalidWire,
                          "v5 wire has an unknown port kind"};
            return false;
        }
        candidateWires.push_back(LayerWire{row.readerLayerId, row.sourceLayerId,
                                           static_cast<LayerPortKind>(row.type), input, output});
    }
    std::vector<LayerBinding> candidateBindings;
    candidateBindings.reserve(candidateView.bindingCount);
    for (uint32_t i = 0; i < candidateView.bindingCount; ++i) {
        const auto& row = candidateView.bindings[i];
        const std::string input = DecodeFixedText(row.input);
        const std::string query = DecodeFixedText(row.query);
        DocumentDiagnostic portDiagnostic;
        if (!ValidatePortName(input, portDiagnostic) || query.empty() || query.size() >= kQueryIdBytes) {
            diagnostic = {DocumentDiagnosticCode::InvalidWire,
                          "v5 binding has an invalid input or query id"};
            return false;
        }
        candidateBindings.push_back(LayerBinding{row.layerId, input, query});
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
    authoredFormatVersion_ = view_.header.formatVersion;
    nextLayerId_ = candidateNextLayerId;
    v5FeaturesUsed_ = candidateV5;
    layerIds_ = std::move(candidateLayerIds);
    wires_ = std::move(candidateWires);
    bindings_ = std::move(candidateBindings);
    soloLayerId_.reset();
    provideFallbackOps_.clear();
    majorMaterialId_ = view_.header.majorMaterialId;
    minorMaterialId_ = view_.header.minorMaterialId;
    majorMaterialPercent_ = view_.header.majorWeightPercent;
    references_ = std::move(candidateReferences);
    serializationDirty_ = false;
    RefreshLayerGraphs();
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

uint32_t EditorDocumentModel::FormatVersion() const {
    return view_.header.formatVersion;
}

uint32_t EditorDocumentModel::LayerId(uint32_t index) const {
    return index < layerIds_.size() ? layerIds_[index] : 0u;
}

bool EditorDocumentModel::LayerIndexForId(uint32_t id, uint32_t& index) const {
    const auto found = std::find(layerIds_.begin(), layerIds_.end(), id);
    if (found == layerIds_.end()) return false;
    index = static_cast<uint32_t>(found - layerIds_.begin());
    return true;
}

std::vector<std::string> EditorDocumentModel::LayerWarnings(uint32_t layerId) const {
    std::vector<std::string> warnings;
    for (const auto& wire : wires_) {
        if (wire.readerLayerId != layerId) continue;
        uint32_t sourceIndex = 0u;
        if (!LayerIndexForId(wire.sourceLayerId, sourceIndex)) {
            warnings.push_back("input " + wire.input + " reads missing layer " +
                               std::to_string(wire.sourceLayerId) + "; using default");
            continue;
        }
        if ((enabledMask_ & (1u << sourceIndex)) == 0u) {
            warnings.push_back("input " + wire.input + " reads disabled layer " +
                               LayerName(sourceIndex) + "; using default");
        }
    }
    return warnings;
}

uint32_t EditorDocumentModel::ParameterCount() const {
    return view_.header.formatVersion == kVdc1LegacyVersion ? 0u : view_.header.parameterCount;
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

bool EditorDocumentModel::SetDocumentMaterials(uint32_t major, uint32_t minor,
                                               uint32_t majorPercent,
                                               DocumentDiagnostic& diagnostic) {
    diagnostic.Clear();
    if (!IsValidMaterialTriple(major, minor, majorPercent)) {
        diagnostic = {DocumentDiagnosticCode::InvalidDocumentMaterial,
                      "document materials require two nonzero ids and a major percent from 1 to 99"};
        return false;
    }
    if (major == majorMaterialId_ && minor == minorMaterialId_ && majorPercent == majorMaterialPercent_) {
        return true;
    }
    const uint32_t oldMajor = majorMaterialId_;
    const uint32_t oldMinor = minorMaterialId_;
    const uint32_t oldPercent = majorMaterialPercent_;
    majorMaterialId_ = major;
    minorMaterialId_ = minor;
    majorMaterialPercent_ = majorPercent;
    if (Rebuild(CopyLayers(), CopyParameters(), enabledMask_, diagnostic)) return true;
    majorMaterialId_ = oldMajor;
    minorMaterialId_ = oldMinor;
    majorMaterialPercent_ = oldPercent;
    return false;
}

bool EditorDocumentModel::ValidateReferenceId(const std::string& id,
                                              DocumentDiagnostic& diagnostic) {
    diagnostic.Clear();
    const size_t colon = id.find(':');
    if (colon == std::string::npos || colon == 0u || colon + 1u >= id.size() ||
        id.find(':', colon + 1u) != std::string::npos || id.size() >= kReferenceIdBytes ||
        id.find('\0') != std::string::npos) {
        diagnostic = {DocumentDiagnosticCode::InvalidReferenceId,
                      "reference id must be <pack>:<recipe> with one colon and at most 63 bytes"};
        return false;
    }
    for (const unsigned char byte : id) {
        if (byte < 0x20u || byte == 0x7Fu) {
            diagnostic = {DocumentDiagnosticCode::InvalidReferenceId,
                          "reference id contains a control character"};
            return false;
        }
    }
    return true;
}

bool EditorDocumentModel::AddReference(const std::string& id, DocumentDiagnostic& diagnostic) {
    if (!ValidateReferenceId(id, diagnostic)) return false;
    if (references_.size() >= kMaximumReferences) {
        diagnostic = {DocumentDiagnosticCode::TooManyReferences,
                      "a document supports at most 32 recipe references"};
        return false;
    }
    if (std::find(references_.begin(), references_.end(), id) != references_.end()) {
        diagnostic = {DocumentDiagnosticCode::DuplicateReference,
                      "recipe reference already exists: " + id};
        return false;
    }
    references_.push_back(id);
    if (Rebuild(CopyLayers(), CopyParameters(), enabledMask_, diagnostic)) return true;
    references_.pop_back();
    return false;
}

bool EditorDocumentModel::RemoveReference(const std::string& id, DocumentDiagnostic& diagnostic) {
    diagnostic.Clear();
    const auto found = std::find(references_.begin(), references_.end(), id);
    if (found == references_.end()) {
        diagnostic = {DocumentDiagnosticCode::ReferenceNotFound,
                      "recipe reference does not exist: " + id};
        return false;
    }
    const uint32_t referenceIndex = static_cast<uint32_t>(found - references_.begin());
    for (uint32_t layerIndex = 0; layerIndex < LayerCount(); ++layerIndex) {
        const auto& layer = view_.layers[layerIndex];
        for (uint32_t instructionIndex = 0; instructionIndex < layer.header->instructionCount;
             ++instructionIndex) {
            const auto& instruction = layer.instructions[instructionIndex];
            if (instruction.opCode != static_cast<uint8_t>(SdfOpCode::InvokeRecipe)) continue;
            const float rawIndex = instruction.data[0];
            if (std::isfinite(rawIndex) && rawIndex >= 0.0f &&
                rawIndex < static_cast<float>(std::numeric_limits<uint32_t>::max()) &&
                std::floor(rawIndex) == rawIndex &&
                static_cast<uint32_t>(rawIndex) == referenceIndex) {
                diagnostic = {DocumentDiagnosticCode::ReferenceInUse,
                              "cannot remove recipe reference used by layer " +
                                  std::to_string(layerIndex)};
                return false;
            }
        }
    }
    const std::string saved = *found;
    references_.erase(found);
    if (Rebuild(CopyLayers(), CopyParameters(), enabledMask_, diagnostic)) return true;
    references_.insert(references_.begin() + referenceIndex, saved);
    return false;
}

bool EditorDocumentModel::MakeRecipeReferenceLayer(const std::string& id, const std::string& name,
                                                   EditableDocumentLayer& out,
                                                   DocumentDiagnostic& diagnostic) const {
    diagnostic.Clear();
    const auto found = std::find(references_.begin(), references_.end(), id);
    if (found == references_.end()) {
        diagnostic = {DocumentDiagnosticCode::ReferenceNotFound,
                      "recipe reference does not exist: " + id};
        return false;
    }
    if (!ValidateLayerName(name, diagnostic)) return false;
    const size_t index = static_cast<size_t>(found - references_.begin());
    SdfInstruction instruction{};
    instruction.opCode = static_cast<uint8_t>(SdfOpCode::InvokeRecipe);
    instruction.data[0] = static_cast<float>(index);
    out = EditableDocumentLayer{};
    out.name = name;
    out.program.push_back(instruction);
    return true;
}

bool EditorDocumentModel::IsRecipeReferenceLayer(uint32_t index, std::string* id,
                                                 DocumentDiagnostic& diagnostic) const {
    diagnostic.Clear();
    if (index >= LayerCount()) {
        diagnostic = {DocumentDiagnosticCode::InvalidLayerIndex,
                      "layer index " + std::to_string(index) + " is outside the document"};
        return false;
    }
    const auto& layer = view_.layers[index];
    std::vector<SdfInstruction> program(layer.instructions,
                                         layer.instructions + layer.header->instructionCount);
    uint32_t tableIndex = 0u;
    if (!IsRecipeInstruction(program, tableIndex) || tableIndex >= references_.size()) {
        diagnostic = {DocumentDiagnosticCode::InvalidReferenceLayer,
                      "layer " + std::to_string(index) + " is not a valid recipe reference layer"};
        return false;
    }
    if (id != nullptr) *id = references_[tableIndex];
    return true;
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
    if (wasEnabled != enabled) {
        serializationDirty_ = true;
        ++revision_;
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
    if (enabledMask_ != mask) {
        enabledMask_ = mask;
        serializationDirty_ = true;
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
        layer.id = LayerId(i);
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

std::vector<Yeroket::Sdf::Generated::VoxelDocReference>
EditorDocumentModel::CopyReferences() const {
    std::vector<Yeroket::Sdf::Generated::VoxelDocReference> references(references_.size());
    for (size_t i = 0; i < references_.size(); ++i) {
        EncodeFixedText(references_[i], references[i].id);
    }
    return references;
}

std::vector<Yeroket::Sdf::Generated::VoxelDocBinding>
EditorDocumentModel::CopyBindings() const {
    std::vector<Yeroket::Sdf::Generated::VoxelDocBinding> rows(bindings_.size());
    for (size_t i = 0; i < bindings_.size(); ++i) {
        rows[i].layerId = bindings_[i].layerId;
        EncodeFixedText(bindings_[i].input, rows[i].input);
        EncodeFixedText(bindings_[i].query, rows[i].query);
    }
    return rows;
}

LayerPortKind EditorDocumentModel::InferPortKind(std::string_view name) {
    std::string lower;
    lower.reserve(name.size());
    for (const unsigned char c : name) lower.push_back(static_cast<char>(std::tolower(c)));
    if (lower.find("field") != std::string::npos || lower == "sdf" || lower == "distance" ||
        lower == "surface" || lower == "element") return LayerPortKind::Field;
    if (lower.find("set") != std::string::npos || lower == "points" || lower == "at")
        return LayerPortKind::Set;
    if (lower.find("channel") != std::string::npos || lower == "material" || lower == "color")
        return LayerPortKind::Channel;
    if (lower.find("vector") != std::string::npos || lower == "normal" || lower == "position")
        return LayerPortKind::Vector;
    if (lower.find("grid") != std::string::npos) return LayerPortKind::Grid;
    if (lower == "radius" || lower == "count" || lower == "seed" || lower == "size" ||
        lower == "value" || lower == "scalar" || lower == "parameter") return LayerPortKind::Scalar;
    return LayerPortKind::Unknown;
}

bool EditorDocumentModel::ValidatePortName(std::string_view name, DocumentDiagnostic& diagnostic) {
    diagnostic.Clear();
    if (name.empty() || name.size() >= kPortNameBytes || name.find('\0') != std::string_view::npos) {
        diagnostic = {DocumentDiagnosticCode::InvalidWire,
                      "port names must be 1..31 bytes and contain no NUL"};
        return false;
    }
    for (const unsigned char byte : name) {
        if (byte < 0x20u || byte == 0x7Fu) {
            diagnostic = {DocumentDiagnosticCode::InvalidWire,
                          "port names may not contain control characters"};
            return false;
        }
    }
    return true;
}

bool EditorDocumentModel::ValidateWireSet(const std::vector<LayerWire>& wires,
                                          const std::vector<EditableDocumentLayer>& layers,
                                          DocumentDiagnostic& diagnostic) const {
    diagnostic.Clear();
    if (wires.size() > kMaximumWires) {
        diagnostic = {DocumentDiagnosticCode::WireCapacityExceeded,
                      "a document supports at most 64 layer wires"};
        return false;
    }
    std::unordered_set<uint32_t> ids;
    ids.reserve(layers.size());
    std::unordered_map<uint32_t, uint32_t> positions;
    positions.reserve(layers.size());
    for (uint32_t i = 0; i < layers.size(); ++i) {
        if (layers[i].id == 0u || !ids.insert(layers[i].id).second) {
            diagnostic = {DocumentDiagnosticCode::InvalidLayerId,
                          "layer ids must be nonzero and unique"};
            return false;
        }
        positions.emplace(layers[i].id, i);
    }
    std::unordered_set<std::string> keys;
    keys.reserve(wires.size());
    for (const auto& wire : wires) {
        DocumentDiagnostic nameDiagnostic;
        if (!ValidatePortName(wire.input, nameDiagnostic) ||
            !ValidatePortName(wire.output, nameDiagnostic)) {
            diagnostic = nameDiagnostic;
            return false;
        }
        const auto reader = positions.find(wire.readerLayerId);
        const auto source = positions.find(wire.sourceLayerId);
        if (reader == positions.end() || source == positions.end()) {
            // A file may contain a dangling wire. It remains detached and is surfaced by the
            // located warning, but a newly authored wire must always name admitted layers.
            if (v5FeaturesUsed_) continue;
            diagnostic = {DocumentDiagnosticCode::InvalidWire,
                          "wire names a layer that is not in the document"};
            return false;
        }
        if (reader->second <= source->second) {
            diagnostic = {DocumentDiagnosticCode::LayerDependencyOrder,
                          "wire " + wire.input + " reads " + wire.output +
                              " from a layer below it; reorder would move the source above the reader"};
            return false;
        }
        if (wire.type > LayerPortKind::Grid) {
            diagnostic = {DocumentDiagnosticCode::InvalidWire, "wire has an unknown port kind"};
            return false;
        }
        const LayerPortKind expected = InferPortKind(wire.input);
        const LayerPortKind actual = InferPortKind(wire.output);
        if (expected != LayerPortKind::Unknown && actual != LayerPortKind::Unknown && expected != actual) {
            diagnostic = {DocumentDiagnosticCode::WireTypeMismatch,
                          "wire " + wire.input + " expects " + std::to_string(static_cast<int>(expected)) +
                              " but " + wire.output + " provides " +
                              std::to_string(static_cast<int>(actual))};
            return false;
        }
        const std::string key = std::to_string(wire.readerLayerId) + "\n" + wire.input;
        if (!keys.insert(key).second) {
            diagnostic = {DocumentDiagnosticCode::WireAlreadyExists,
                          "layer input already has a wire: " + wire.input};
            return false;
        }
    }
    return true;
}

bool EditorDocumentModel::RemoveWiresForLayer(uint32_t layerId) {
    const auto oldSize = wires_.size();
    wires_.erase(std::remove_if(wires_.begin(), wires_.end(),
                                [layerId](const LayerWire& wire) {
                                    return wire.readerLayerId == layerId || wire.sourceLayerId == layerId;
                                }),
                wires_.end());
    return oldSize != wires_.size();
}

bool EditorDocumentModel::BuildEvaluationMask(std::vector<uint8_t>& mask,
                                              DocumentDiagnostic& diagnostic) const {
    diagnostic.Clear();
    mask.assign(LayerCount(), 0u);
    if (!soloLayerId_) {
        for (uint32_t i = 0; i < LayerCount(); ++i)
            mask[i] = static_cast<uint8_t>((enabledMask_ >> i) & 1u);
        return true;
    }
    uint32_t soloIndex = 0u;
    if (!LayerIndexForId(*soloLayerId_, soloIndex)) {
        diagnostic = {DocumentDiagnosticCode::InvalidLayerId, "solo layer no longer exists"};
        return false;
    }
    std::vector<uint32_t> pending{soloIndex};
    while (!pending.empty()) {
        const uint32_t index = pending.back();
        pending.pop_back();
        if (mask[index] != 0u) continue;
        mask[index] = 1u;
        const uint32_t id = LayerId(index);
        for (const auto& wire : wires_) {
            if (wire.readerLayerId != id) continue;
            uint32_t source = 0u;
            if (LayerIndexForId(wire.sourceLayerId, source) &&
                (enabledMask_ & (1u << source)) != 0u) pending.push_back(source);
        }
    }
    return true;
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

    std::unordered_set<uint32_t> usedIds;
    usedIds.reserve(layers.size());
    uint32_t candidateNextLayerId = nextLayerId_ == 0u ? 1u : nextLayerId_;
    for (auto& layer : layers) {
        if (layer.id == 0u) {
            while (candidateNextLayerId == 0u || usedIds.contains(candidateNextLayerId)) ++candidateNextLayerId;
            layer.id = candidateNextLayerId++;
        }
        if (!usedIds.insert(layer.id).second) {
            diagnostic = {DocumentDiagnosticCode::InvalidLayerId,
                          "layer ids must be nonzero and unique"};
            return false;
        }
    }
    const bool hasV5 = v5FeaturesUsed_ || !wires_.empty() || !bindings_.empty() ||
                       std::any_of(layers.begin(), layers.end(), [](const EditableDocumentLayer& layer) {
                           return layer.op == 4u;
                       });
    if (hasV5 && layers.size() > kMaximumLayerCount) {
        diagnostic = {DocumentDiagnosticCode::TooManyLayers,
                      "v5 documents support at most 32 layers"};
        return false;
    }
    if (!ValidateWireSet(wires_, layers, diagnostic)) return false;

    std::vector<VoxelDocLayerHeader> headers(layers.size());
    std::vector<VoxelDocLayerWrite> writes(layers.size());
    for (size_t i = 0; i < layers.size(); ++i) {
        const auto& source = layers[i];
        if (source.type != 0u || source.op > 4u || !std::isfinite(source.blendRadius)) {
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

    const auto referenceRows = CopyReferences();
    std::vector<uint32_t> idsForWrite;
    idsForWrite.reserve(layers.size());
    for (const auto& layer : layers) idsForWrite.push_back(layer.id);
    std::vector<VoxelDocWire> wireRows(wires_.size());
    for (size_t i = 0; i < wires_.size(); ++i) {
        wireRows[i].readerLayerId = wires_[i].readerLayerId;
        wireRows[i].sourceLayerId = wires_[i].sourceLayerId;
        wireRows[i].type = static_cast<uint8_t>(wires_[i].type);
        EncodeFixedText(wires_[i].input, wireRows[i].input);
        EncodeFixedText(wires_[i].output, wireRows[i].output);
    }
    const auto bindingRows = CopyBindings();
    size_t required = 0;
    if (hasV5) {
        WriteVoxelDocumentV5(view_.channels, view_.header.channelCount,
                             parameters.data(), static_cast<uint32_t>(parameters.size()),
                             writes.data(), static_cast<uint32_t>(writes.size()),
                             referenceRows.data(), static_cast<uint32_t>(referenceRows.size()),
                             candidateNextLayerId, idsForWrite.data(), static_cast<uint32_t>(idsForWrite.size()),
                             wireRows.data(), static_cast<uint32_t>(wireRows.size()),
                             bindingRows.data(), static_cast<uint32_t>(bindingRows.size()),
                             majorMaterialId_, minorMaterialId_, majorMaterialPercent_,
                             nullptr, 0, required, authoredFormatVersion_);
    } else {
        WriteVoxelDocument(view_.channels, view_.header.channelCount,
                           parameters.data(), static_cast<uint32_t>(parameters.size()),
                           writes.data(), static_cast<uint32_t>(writes.size()),
                           referenceRows.data(), static_cast<uint32_t>(referenceRows.size()),
                           majorMaterialId_, minorMaterialId_, majorMaterialPercent_,
                           nullptr, 0, required, authoredFormatVersion_);
    }
    std::vector<uint8_t> candidateBytes(required);
    size_t written = 0;
    const bool wrote = hasV5
        ? WriteVoxelDocumentV5(view_.channels, view_.header.channelCount,
                               parameters.data(), static_cast<uint32_t>(parameters.size()),
                               writes.data(), static_cast<uint32_t>(writes.size()),
                               referenceRows.data(), static_cast<uint32_t>(referenceRows.size()),
                               candidateNextLayerId, idsForWrite.data(), static_cast<uint32_t>(idsForWrite.size()),
                               wireRows.data(), static_cast<uint32_t>(wireRows.size()),
                               bindingRows.data(), static_cast<uint32_t>(bindingRows.size()),
                               majorMaterialId_, minorMaterialId_, majorMaterialPercent_,
                               candidateBytes.data(), candidateBytes.size(), written, authoredFormatVersion_)
        : WriteVoxelDocument(view_.channels, view_.header.channelCount,
                             parameters.data(), static_cast<uint32_t>(parameters.size()),
                             writes.data(), static_cast<uint32_t>(writes.size()),
                             referenceRows.data(), static_cast<uint32_t>(referenceRows.size()),
                             majorMaterialId_, minorMaterialId_, majorMaterialPercent_,
                             candidateBytes.data(), candidateBytes.size(), written, authoredFormatVersion_);
    if (!wrote) {
        diagnostic = {DocumentDiagnosticCode::WriteError,
                      hasV5 ? "WriteVoxelDocumentV5 failed to rebuild the candidate document"
                            : "WriteVoxelDocument failed to rebuild the candidate document"};
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
    nextLayerId_ = candidateNextLayerId;
    v5FeaturesUsed_ = hasV5;
    layerIds_ = std::move(idsForWrite);
    serializationDirty_ = true;
    RefreshLayerGraphs();
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
    const auto savedWires = wires_;
    RemoveWiresForLayer(deleted.id);
    uint32_t mask = 0u;
    for (uint32_t i = 0; i < layers.size(); ++i)
        if (layers[i].enabled) mask |= (1u << i);
    if (!Rebuild(std::move(layers), CopyParameters(), mask, diagnostic)) {
        wires_ = savedWires;
        return false;
    }
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

bool EditorDocumentModel::SetLayerProvide(uint32_t layerId, bool provide,
                                          DocumentDiagnostic& diagnostic) {
    diagnostic.Clear();
    uint32_t index = 0u;
    if (!LayerIndexForId(layerId, index)) {
        diagnostic = {DocumentDiagnosticCode::InvalidLayerId,
                      "layer id " + std::to_string(layerId) + " does not exist"};
        return false;
    }
    const uint8_t requested = provide
        ? 4u
        : (provideFallbackOps_.contains(layerId) ? provideFallbackOps_.at(layerId) : 0u);
    if (view_.layers[index].header->op == requested) return true;
    auto layers = CopyLayers();
    if (provide && layers[index].op != 4u) provideFallbackOps_[layerId] = layers[index].op;
    layers[index].op = requested;
    const bool oldV5 = v5FeaturesUsed_;
    v5FeaturesUsed_ = true;
    if (Rebuild(std::move(layers), CopyParameters(), enabledMask_, diagnostic)) {
        if (!provide) provideFallbackOps_.erase(layerId);
        return true;
    }
    v5FeaturesUsed_ = oldV5;
    if (provide) provideFallbackOps_.erase(layerId);
    return false;
}

bool EditorDocumentModel::AddWire(uint32_t readerLayerId, const std::string& input,
                                  uint32_t sourceLayerId, const std::string& port,
                                  DocumentDiagnostic& diagnostic) {
    diagnostic.Clear();
    uint32_t readerIndex = 0u;
    uint32_t sourceIndex = 0u;
    if (!LayerIndexForId(readerLayerId, readerIndex) || !LayerIndexForId(sourceLayerId, sourceIndex)) {
        diagnostic = {DocumentDiagnosticCode::InvalidLayerId,
                      "wire reader and source must name existing layers"};
        return false;
    }
    if (readerLayerId == sourceLayerId || sourceIndex >= readerIndex) {
        diagnostic = {DocumentDiagnosticCode::LayerDependencyOrder,
                      "wire source '" + port + "' must be below reader input '" + input + "'"};
        return false;
    }
    if (wires_.size() >= kMaximumWires) {
        diagnostic = {DocumentDiagnosticCode::WireCapacityExceeded,
                      "a document supports at most 64 layer wires"};
        return false;
    }
    const LayerPortKind inputKind = InferPortKind(input);
    const LayerPortKind portKind = InferPortKind(port);
    if (inputKind != LayerPortKind::Unknown && portKind != LayerPortKind::Unknown && inputKind != portKind) {
        diagnostic = {DocumentDiagnosticCode::WireTypeMismatch,
                      "wire input '" + input + "' and source port '" + port + "' have different types"};
        return false;
    }
    DocumentDiagnostic nameDiagnostic;
    if (!ValidatePortName(input, nameDiagnostic) || !ValidatePortName(port, nameDiagnostic)) {
        diagnostic = nameDiagnostic;
        return false;
    }
    for (const auto& wire : wires_) {
        if (wire.readerLayerId == readerLayerId && wire.input == input) {
            diagnostic = {DocumentDiagnosticCode::WireAlreadyExists,
                          "layer input already has a wire: " + input};
            return false;
        }
    }
    const auto saved = wires_;
    const LayerPortKind wireKind = portKind == LayerPortKind::Unknown ? inputKind : portKind;
    auto candidateLayers = CopyLayers();
    const size_t wireIndex = wires_.size();
    wires_.push_back(LayerWire{readerLayerId, sourceLayerId,
                               wireKind == LayerPortKind::Unknown ? LayerPortKind::Field : wireKind,
                               input, port});
    BindWireIndex(candidateLayers, readerIndex, input, wireIndex);
    const bool oldV5 = v5FeaturesUsed_;
    v5FeaturesUsed_ = true;
    if (Rebuild(std::move(candidateLayers), CopyParameters(), enabledMask_, diagnostic)) return true;
    wires_ = saved;
    v5FeaturesUsed_ = oldV5;
    return false;
}

bool EditorDocumentModel::RemoveWire(uint32_t readerLayerId, const std::string& input,
                                     DocumentDiagnostic& diagnostic) {
    diagnostic.Clear();
    const auto found = std::find_if(wires_.begin(), wires_.end(),
                                    [readerLayerId, &input](const LayerWire& wire) {
                                        return wire.readerLayerId == readerLayerId && wire.input == input;
                                    });
    if (found == wires_.end()) {
        diagnostic = {DocumentDiagnosticCode::WireNotFound,
                      "no wire exists for layer input: " + input};
        return false;
    }
    const auto saved = wires_;
    const size_t removedIndex = static_cast<size_t>(found - wires_.begin());
    auto candidateLayers = CopyLayers();
    wires_.erase(found);
    RewriteWireIndices(candidateLayers, removedIndex);
    if (Rebuild(std::move(candidateLayers), CopyParameters(), enabledMask_, diagnostic)) return true;
    wires_ = saved;
    return false;
}

bool EditorDocumentModel::RestoreWires(const std::vector<LayerWire>& wires,
                                       DocumentDiagnostic& diagnostic) {
    diagnostic.Clear();
    const auto saved = wires_;
    wires_ = wires;
    const bool oldV5 = v5FeaturesUsed_;
    v5FeaturesUsed_ = oldV5 || !wires.empty();
    if (Rebuild(CopyLayers(), CopyParameters(), enabledMask_, diagnostic)) return true;
    wires_ = saved;
    v5FeaturesUsed_ = oldV5;
    return false;
}

bool EditorDocumentModel::InsertPortRead(uint32_t layerId, uint32_t afterNode,
                                         const std::string& source,
                                         DocumentDiagnostic& diagnostic) {
    diagnostic.Clear();
    const size_t dot = source.find('.');
    if (dot == std::string::npos || dot == 0u || dot + 1u >= source.size()) {
        diagnostic = {DocumentDiagnosticCode::InvalidWire,
                      "port read source must be <layerName>.<port>"};
        return false;
    }
    uint32_t layerIndex = 0u;
    if (!LayerIndexForId(layerId, layerIndex)) {
        diagnostic = {DocumentDiagnosticCode::InvalidLayerId,
                      "layer id " + std::to_string(layerId) + " does not exist"};
        return false;
    }
    const std::string layerName = source.substr(0u, dot);
    const std::string port = source.substr(dot + 1u);
    uint32_t sourceIndex = 0u;
    bool found = false;
    for (uint32_t i = 0; i < LayerCount(); ++i) {
        if (LayerName(i) == layerName) {
            if (found) {
                diagnostic = {DocumentDiagnosticCode::InvalidWire,
                              "port read layer name is ambiguous: " + layerName};
                return false;
            }
            sourceIndex = i;
            found = true;
        }
    }
    if (!found || sourceIndex >= layerIndex) {
        diagnostic = {DocumentDiagnosticCode::LayerDependencyOrder,
                      "port read source must name a unique layer below the reader"};
        return false;
    }
    if (!AddWire(layerId, "read", LayerId(sourceIndex), port, diagnostic)) return false;
    auto layers = CopyLayers();
    auto& program = layers[layerIndex].program;
    if (afterNode >= program.size()) {
        RemoveWire(layerId, "read", diagnostic);
        diagnostic = {DocumentDiagnosticCode::InvalidGraphEdit,
                      "port read insertion node is outside the layer program"};
        return false;
    }
    SdfInstruction read{};
    read.opCode = static_cast<uint8_t>(SdfOpCode::ReadPort);
    const auto wire = std::find_if(wires_.begin(), wires_.end(),
                                   [layerId](const LayerWire& row) {
                                       return row.readerLayerId == layerId && row.input == "read";
                                   });
    read.data[0] = wire == wires_.end() ? 0.0f
                                        : static_cast<float>(wire - wires_.begin());
    const auto originalProgram = program;
    std::vector<SdfInstruction> candidate(originalProgram.begin(), originalProgram.end());
    if (candidate.size() == 1u && afterNode == 0u) {
        // A ReadPort is itself a complete Field-producing node. Replacing the seed primitive keeps
        // the render program valid in the same action; exposing [primitive, ReadPort] would leave
        // two values on the recipe stack and can reach the live evaluator before a follow-up edit.
        candidate.front() = read;
    } else {
        candidate.insert(candidate.begin() + afterNode + 1u, read);
    }
    Vixen::SVO::Recipe::SdfGraphDiagnostic graphDiagnostic{};
    if (!Vixen::SVO::Recipe::ValidateSdfGraphProgram(
            std::span<const SdfInstruction>(candidate.data(), candidate.size()), graphDiagnostic)) {
        // A selected domain wrapper may precede the actual Field producer. Replace the first
        // zero-input Field-producing instruction while retaining its Transform/RestorePos frame.
        candidate.assign(originalProgram.begin(), originalProgram.end());
        bool replaced = false;
        for (size_t instructionIndex = 0u; instructionIndex < candidate.size(); ++instructionIndex) {
            const auto* info = Vixen::SVO::Recipe::FindSdfOpInfo(
                static_cast<SdfOpCode>(candidate[instructionIndex].opCode));
            if (info == nullptr || info->valuePop != 0 || info->valuePush != 1 ||
                static_cast<SdfOpCode>(candidate[instructionIndex].opCode) == SdfOpCode::ReadPort) continue;
            candidate[instructionIndex] = read;
            replaced = true;
            break;
        }
        if (!replaced || !Vixen::SVO::Recipe::ValidateSdfGraphProgram(
                std::span<const SdfInstruction>(candidate.data(), candidate.size()), graphDiagnostic)) {
            RemoveWire(layerId, "read", diagnostic);
            diagnostic = {DocumentDiagnosticCode::InvalidGraphEdit,
                          "port read would leave an invalid program: " +
                              GraphDiagnosticMessage(graphDiagnostic)};
            return false;
        }
    }
    program = std::move(candidate);
    if (Rebuild(std::move(layers), CopyParameters(), enabledMask_, diagnostic)) return true;
    RemoveWire(layerId, "read", diagnostic);
    return false;
}

bool EditorDocumentModel::SetSoloLayer(uint32_t layerId, DocumentDiagnostic& diagnostic) {
    diagnostic.Clear();
    if (layerId != 0u) {
        uint32_t index = 0u;
        if (!LayerIndexForId(layerId, index)) {
            diagnostic = {DocumentDiagnosticCode::InvalidLayerId,
                          "layer id " + std::to_string(layerId) + " does not exist"};
            return false;
        }
    }
    soloLayerId_ = layerId == 0u ? std::nullopt : std::optional<uint32_t>(layerId);
    return true;
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

namespace {

const char* GraphDiagnosticCodeName(Vixen::SVO::Recipe::SdfGraphDiagnosticCode code) {
    using Code = Vixen::SVO::Recipe::SdfGraphDiagnosticCode;
    switch (code) {
        case Code::None: return "None";
        case Code::UnknownOpcode: return "UnknownOpcode";
        case Code::ValueStackUnderflow: return "ValueStackUnderflow";
        case Code::ValueStackLeftover: return "ValueStackLeftover";
        case Code::ValueTypeMismatch: return "ValueTypeMismatch";
        case Code::PositionStackUnderflow: return "PositionStackUnderflow";
        case Code::UnbalancedDomainFrame: return "UnbalancedDomainFrame";
        case Code::ArgumentOutOfRange: return "ArgumentOutOfRange";
        case Code::BufferTooSmall: return "BufferTooSmall";
        case Code::InvalidEdit: return "InvalidEdit";
        case Code::DanglingSocket: return "DanglingSocket";
    }
    return "Unknown";
}

const char* GraphValueTypeName(Vixen::SVO::Recipe::SdfGraphValueType type) {
    using Type = Vixen::SVO::Recipe::SdfGraphValueType;
    switch (type) {
        case Type::Float: return "Float";
        case Type::U32: return "U32";
        case Type::Q16_16: return "Q16_16";
        case Type::Field: return "Field";
        case Type::Scalar: return "Scalar";
        case Type::Vector: return "Vector";
        case Type::Channel: return "Channel";
        case Type::Set: return "Set";
        case Type::Grid: return "Grid";
        case Type::Unknown: return "Unknown";
    }
    return "Unknown";
}

}  // namespace

std::string EditorDocumentModel::GraphDiagnosticMessage(
    const Vixen::SVO::Recipe::SdfGraphDiagnostic& graphDiagnostic) {
    std::string text = std::string(GraphDiagnosticCodeName(graphDiagnostic.code)) +
                       " at instruction " + std::to_string(graphDiagnostic.instructionIndex);
    if (graphDiagnostic.argumentName != nullptr && graphDiagnostic.argumentName[0] != '\0') {
        text += " (";
        text += graphDiagnostic.argumentName;
        text += ")";
    }
    if (graphDiagnostic.expected != Vixen::SVO::Recipe::SdfGraphValueType::Unknown ||
        graphDiagnostic.actual != Vixen::SVO::Recipe::SdfGraphValueType::Unknown) {
        text += ": expected ";
        text += GraphValueTypeName(graphDiagnostic.expected);
        text += ", actual ";
        text += GraphValueTypeName(graphDiagnostic.actual);
    }
    return text;
}

void EditorDocumentModel::RefreshLayerGraphs() {
    const uint32_t count = LayerCount();
    layerGraphs_.resize(count);
    layerGraphNodes_.resize(count);
    layerGraphInputs_.resize(count);
    layerGraphOutputs_.resize(count);
    for (uint32_t layerIndex = 0; layerIndex < count; ++layerIndex) {
        const auto& layer = view_.layers[layerIndex];
        const uint32_t instructionCount = layer.header->instructionCount;
        auto& nodes = layerGraphNodes_[layerIndex];
        auto& inputs = layerGraphInputs_[layerIndex];
        auto& outputs = layerGraphOutputs_[layerIndex];
        nodes.resize(instructionCount);
        inputs.resize(Vixen::SVO::Recipe::SdfGraphInputCapacityFor(instructionCount));
        outputs.resize(Vixen::SVO::Recipe::SdfGraphOutputCapacityFor(instructionCount));

        Vixen::SVO::Recipe::SdfGraphBuffers buffers{
            std::span<Vixen::SVO::Recipe::SdfGraphNode>(nodes.data(), nodes.size()),
            std::span<Vixen::SVO::Recipe::SdfGraphInputSocket>(inputs.data(), inputs.size()),
            std::span<Vixen::SVO::Recipe::SdfGraphOutputSocket>(outputs.data(), outputs.size())};
        auto& graph = layerGraphs_[layerIndex];
        Vixen::SVO::Recipe::ClearSdfGraphDiagnostic(graph.diagnostic);
        graph.raw = !Vixen::SVO::Recipe::DeriveSdfGraph(
            std::span<const SdfInstruction>(layer.instructions, instructionCount), buffers,
            graph.view, graph.diagnostic);
    }
}

const LayerGraph* EditorDocumentModel::GetLayerGraph(uint32_t index,
                                                     DocumentDiagnostic& diagnostic) const {
    diagnostic.Clear();
    if (index >= LayerCount()) {
        diagnostic = {DocumentDiagnosticCode::InvalidLayerIndex,
                      "layer index " + std::to_string(index) + " is outside the document"};
        return nullptr;
    }
    return &layerGraphs_[index];
}

bool EditorDocumentModel::SetNodeArgument(uint32_t layerIndex, uint32_t nodeIndex,
                                          std::string_view argumentName,
                                          const std::array<float, 4>& value,
                                          DocumentDiagnostic& diagnostic) {
    diagnostic.Clear();
    const LayerGraph* graph = GetLayerGraph(layerIndex, diagnostic);
    if (graph == nullptr) return false;
    auto reject = [&](const Vixen::SVO::Recipe::SdfGraphDiagnostic& graphDiagnostic) {
        diagnostic = {DocumentDiagnosticCode::InvalidGraphEdit,
                      "generated graph diagnostic: " + GraphDiagnosticMessage(graphDiagnostic)};
        return false;
    };
    if (graph->raw) return reject(graph->diagnostic);
    if (nodeIndex >= graph->view.nodeCount) {
        Vixen::SVO::Recipe::SdfGraphDiagnostic invalid{};
        Vixen::SVO::Recipe::SetSdfGraphDiagnostic(
            invalid, Vixen::SVO::Recipe::SdfGraphDiagnosticCode::InvalidEdit, nodeIndex);
        return reject(invalid);
    }
    const auto& node = graph->view.nodes[nodeIndex];
    std::string_view catalogueName = argumentName;
    uint32_t componentOnly = 4u;
    if (argumentName.size() > 2u && argumentName[argumentName.size() - 2u] == '.') {
        const char component = argumentName.back();
        if (component == 'x' || component == 'y' || component == 'z' || component == 'w') {
            componentOnly = static_cast<uint32_t>(component == 'x' ? 0u : component == 'y' ? 1u : component == 'z' ? 2u : 3u);
            catalogueName = argumentName.substr(0, argumentName.size() - 2u);
        }
    }
    const auto* argument = [&]() -> const Vixen::SVO::Recipe::SdfOpArgument* {
        const auto* args = Vixen::SVO::Recipe::SdfOpArguments(*node.opcodeInfo);
        for (uint32_t i = 0; i < node.opcodeInfo->argumentCount; ++i) {
            if (args[i].source == Vixen::SVO::Recipe::SdfArgumentSource::InstructionData &&
                catalogueName == args[i].name) {
                return &args[i];
            }
        }
        return nullptr;
    }();
    if (argument == nullptr || argument->width == 0u || argument->width > 4u) {
        diagnostic = {DocumentDiagnosticCode::FieldNotEditable,
                      "node " + std::to_string(nodeIndex) + " has no editable argument '" +
                          std::string(argumentName) + "'"};
        return false;
    }
    if (componentOnly < argument->width && !std::isfinite(value[componentOnly])) {
        diagnostic = {DocumentDiagnosticCode::InvalidPrimitiveField,
                      "node argument '" + std::string(argumentName) + "' must be finite"};
        return false;
    }
    for (uint32_t component = 0; component < argument->width && componentOnly == 4u; ++component) {
        if (!std::isfinite(value[component])) {
            diagnostic = {DocumentDiagnosticCode::InvalidPrimitiveField,
                          "node argument '" + std::string(argumentName) + "' must be finite"};
            return false;
        }
    }

    auto layers = CopyLayers();
    auto& program = layers[layerIndex].program;
    const uint32_t instructionIndex = node.instructionIndex;
    if (instructionIndex >= program.size()) {
        Vixen::SVO::Recipe::SdfGraphDiagnostic invalid{};
        Vixen::SVO::Recipe::SetSdfGraphDiagnostic(
            invalid, Vixen::SVO::Recipe::SdfGraphDiagnosticCode::InvalidEdit, instructionIndex);
        return reject(invalid);
    }
    if (componentOnly < argument->width) {
        program[instructionIndex].data[argument->offset + static_cast<int16_t>(componentOnly)] = value[componentOnly];
    } else {
        for (uint32_t component = 0; component < argument->width; ++component) {
            program[instructionIndex].data[argument->offset + static_cast<int16_t>(component)] = value[component];
        }
    }
    Vixen::SVO::Recipe::SdfGraphDiagnostic graphDiagnostic{};
    if (!Vixen::SVO::Recipe::ValidateSdfGraphProgram(
            std::span<const SdfInstruction>(program.data(), program.size()), graphDiagnostic)) {
        return reject(graphDiagnostic);
    }
    return Rebuild(std::move(layers), CopyParameters(), enabledMask_, diagnostic);
}

bool EditorDocumentModel::InsertNodeAfter(uint32_t layerIndex, uint32_t nodeIndex,
                                          std::string_view opcode,
                                          DocumentDiagnostic& diagnostic) {
    diagnostic.Clear();
    const LayerGraph* graph = GetLayerGraph(layerIndex, diagnostic);
    if (graph == nullptr) return false;
    if (graph->raw) {
        diagnostic = {DocumentDiagnosticCode::InvalidGraphEdit,
                      "generated graph diagnostic: " + GraphDiagnosticMessage(graph->diagnostic)};
        return false;
    }
    Vixen::SVO::Recipe::SdfOpCode op{};
    if (!Vixen::SVO::Recipe::FindSdfOpCode(opcode, op)) {
        diagnostic = {DocumentDiagnosticCode::InvalidGraphEdit,
                      "generated graph diagnostic: unknown opcode '" + std::string(opcode) + "'"};
        return false;
    }
    if (nodeIndex >= graph->view.nodeCount) {
        diagnostic = {DocumentDiagnosticCode::InvalidGraphEdit,
                      "generated graph diagnostic: InvalidEdit at node " + std::to_string(nodeIndex)};
        return false;
    }
    const auto& source = view_.layers[layerIndex];
    const std::span<const SdfInstruction> program(source.instructions,
                                                   source.header->instructionCount);
    std::vector<SdfInstruction> candidate(program.size() + 1u);
    uint32_t candidateCount = 0u;
    Vixen::SVO::Recipe::SdfGraphDiagnostic graphDiagnostic{};
    if (!Vixen::SVO::Recipe::InsertNodeAfter(
            program, graph->view.nodes[nodeIndex].instructionIndex, static_cast<uint8_t>(op),
            std::span<SdfInstruction>(candidate.data(), candidate.size()), candidateCount,
            graphDiagnostic)) {
        diagnostic = {DocumentDiagnosticCode::InvalidGraphEdit,
                      "generated graph diagnostic: " + GraphDiagnosticMessage(graphDiagnostic)};
        return false;
    }
    candidate.resize(candidateCount);
    return SetLayerProgram(layerIndex, candidate, diagnostic);
}

bool EditorDocumentModel::RemoveNode(uint32_t layerIndex, uint32_t nodeIndex,
                                     DocumentDiagnostic& diagnostic) {
    diagnostic.Clear();
    const LayerGraph* graph = GetLayerGraph(layerIndex, diagnostic);
    if (graph == nullptr) return false;
    // DeriveSdfGraph retains ReadPort nodes before reporting the surrounding program's leftover
    // stack. Let removal validate the candidate in that partial view; empty/raw programs remain
    // rejected here.
    if (graph->raw && graph->view.nodeCount == 0u) {
        diagnostic = {DocumentDiagnosticCode::InvalidGraphEdit,
                      "generated graph diagnostic: " + GraphDiagnosticMessage(graph->diagnostic)};
        return false;
    }
    if (nodeIndex >= graph->view.nodeCount) {
        diagnostic = {DocumentDiagnosticCode::InvalidGraphEdit,
                      "generated graph diagnostic: InvalidEdit at node " + std::to_string(nodeIndex)};
        return false;
    }
    const auto& source = view_.layers[layerIndex];
    const std::span<const SdfInstruction> program(source.instructions,
                                                   source.header->instructionCount);
    std::vector<SdfInstruction> candidate(program.size() > 0u ? program.size() - 1u : 0u);
    uint32_t candidateCount = 0u;
    Vixen::SVO::Recipe::SdfGraphDiagnostic graphDiagnostic{};
    const uint32_t instructionIndex = graph->view.nodes[nodeIndex].instructionIndex;
    const bool removesPortRead = static_cast<SdfOpCode>(program[instructionIndex].opCode) == SdfOpCode::ReadPort;
    const bool removed = Vixen::SVO::Recipe::RemoveNode(
        program, instructionIndex, std::span<SdfInstruction>(candidate.data(), candidate.size()), candidateCount,
        graphDiagnostic);
    if (!removed && !removesPortRead) {
        diagnostic = {DocumentDiagnosticCode::InvalidGraphEdit,
                      "generated graph diagnostic: " + GraphDiagnosticMessage(graphDiagnostic)};
        return false;
    }
    candidate.resize(candidateCount);
    if (removesPortRead) {
        // Removing a read inside a generated Transform/RestorePos wrapper must keep the frame
        // balanced. Prefer the declared-default transport in place of the node; if the read was
        // an unwrapped stack term, the generated removal below may still produce the smaller valid
        // program.
        std::vector<SdfInstruction> fallback(program.begin(), program.end());
        fallback[instructionIndex] = {};
        fallback[instructionIndex].opCode =
            static_cast<uint8_t>(SdfOpCode::PushParam);
        Vixen::SVO::Recipe::SdfGraphDiagnostic fallbackDiagnostic{};
        if (Vixen::SVO::Recipe::ValidateSdfGraphProgram(
                std::span<const SdfInstruction>(fallback.data(), fallback.size()), fallbackDiagnostic)) {
            candidate = std::move(fallback);
            candidateCount = static_cast<uint32_t>(candidate.size());
        }
    }
    const auto savedWires = wires_;
    size_t removedWire = wires_.size();
    if (removesPortRead) {
        const float rawWire = program[graph->view.nodes[nodeIndex].instructionIndex].data[0];
        if (std::isfinite(rawWire) && rawWire >= 0.0f && std::floor(rawWire) == rawWire &&
            rawWire < static_cast<float>(wires_.size())) {
            removedWire = static_cast<size_t>(rawWire);
            wires_.erase(wires_.begin() + static_cast<std::ptrdiff_t>(removedWire));
        }
    }
    auto candidateLayers = CopyLayers();
    candidateLayers[layerIndex].program = std::move(candidate);
    if (removedWire != savedWires.size()) RewriteWireIndices(candidateLayers, removedWire);
    if (Rebuild(std::move(candidateLayers), CopyParameters(), enabledMask_, diagnostic)) return true;
    wires_ = savedWires;
    return false;
}

bool EditorDocumentModel::WrapNodes(uint32_t layerIndex, uint32_t firstNode, uint32_t lastNode,
                                    std::string_view domainOpcode,
                                    DocumentDiagnostic& diagnostic) {
    diagnostic.Clear();
    const LayerGraph* graph = GetLayerGraph(layerIndex, diagnostic);
    if (graph == nullptr) return false;
    if (graph->raw) {
        diagnostic = {DocumentDiagnosticCode::InvalidGraphEdit,
                      "generated graph diagnostic: " + GraphDiagnosticMessage(graph->diagnostic)};
        return false;
    }
    Vixen::SVO::Recipe::SdfOpCode op{};
    if (!Vixen::SVO::Recipe::FindSdfOpCode(domainOpcode, op)) {
        diagnostic = {DocumentDiagnosticCode::InvalidGraphEdit,
                      "generated graph diagnostic: unknown opcode '" + std::string(domainOpcode) + "'"};
        return false;
    }
    if (firstNode > lastNode || lastNode >= graph->view.nodeCount) {
        diagnostic = {DocumentDiagnosticCode::InvalidGraphEdit,
                      "generated graph diagnostic: InvalidEdit at node " + std::to_string(firstNode)};
        return false;
    }
    const auto& source = view_.layers[layerIndex];
    const std::span<const SdfInstruction> program(source.instructions,
                                                   source.header->instructionCount);
    std::vector<SdfInstruction> candidate(program.size() + 2u);
    uint32_t candidateCount = 0u;
    Vixen::SVO::Recipe::SdfGraphDiagnostic graphDiagnostic{};
    if (!Vixen::SVO::Recipe::WrapInDomain(
            program, graph->view.nodes[firstNode].instructionIndex,
            graph->view.nodes[lastNode].instructionIndex, static_cast<uint8_t>(op),
            std::span<SdfInstruction>(candidate.data(), candidate.size()), candidateCount,
            graphDiagnostic)) {
        diagnostic = {DocumentDiagnosticCode::InvalidGraphEdit,
                      "generated graph diagnostic: " + GraphDiagnosticMessage(graphDiagnostic)};
        return false;
    }
    candidate.resize(candidateCount);
    return SetLayerProgram(layerIndex, candidate, diagnostic);
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
    if (!BuildEvaluationMask(enabledOverride, diagnostic)) return false;

    if (v5FeaturesUsed_) {
        const DocumentBakeSnapshot snapshot = CaptureBakeSnapshot();
        Vixen::SVO::RecipeRegistry::RecipeEntry entry;
        if (!FlattenToRecipeEntry(entry, snapshot, diagnostic)) return false;
        // V5 document export is the persistence-side VRC1 bake face. The preview entry keeps
        // ReadParam dynamic so a parameter edit can update the viewport without recompiling;
        // the generated authoring flattener resolves the same scalar reads to PushParam bytes.
        for (auto& instruction : entry.bytecode) {
            if (static_cast<SdfOpCode>(instruction.opCode) != SdfOpCode::ReadParam) continue;
            const float rawSlot = instruction.data[0];
            if (!std::isfinite(rawSlot) || rawSlot < 0.0f || std::floor(rawSlot) != rawSlot ||
                rawSlot >= static_cast<float>(snapshot.parameterValues.size())) {
                diagnostic = {DocumentDiagnosticCode::FlattenError,
                              "ReadParam export slot is outside the document parameter table"};
                return false;
            }
            instruction = {};
            instruction.opCode = static_cast<uint8_t>(SdfOpCode::PushParam);
            instruction.data[0] = snapshot.parameterValues[static_cast<size_t>(rawSlot)];
        }
        using namespace Yeroket::Sdf::Generated;
        RecipeContainerHeader header{};
        header.magic = kRecipeContainerMagic;
        header.formatVersion = kRecipeContainerVersion;
        header.instructionCount = static_cast<uint32_t>(entry.bytecode.size());
        header.bakeResolution = entry.bakeResolution;
        header.bandVoxels = entry.bandVoxels;
        header.brickDepth = entry.brickDepth;
        outVrc1Blob.resize(sizeof(header) + entry.bytecode.size() * sizeof(SdfInstruction));
        std::memcpy(outVrc1Blob.data(), &header, sizeof(header));
        if (!entry.bytecode.empty()) {
            std::memcpy(outVrc1Blob.data() + sizeof(header), entry.bytecode.data(),
                        entry.bytecode.size() * sizeof(SdfInstruction));
        }
        return true;
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
    return FlattenToRecipeEntry(outEntry, snapshot, RecipeReferenceResolver{}, false, diagnostic);
}

bool EditorDocumentModel::FlattenToRecipeEntry(
    Vixen::SVO::RecipeRegistry::RecipeEntry& outEntry,
    const DocumentBakeSnapshot& snapshot,
    const RecipeReferenceResolver& resolver,
    bool skipUnresolvedReferences,
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
    if (!BuildEvaluationMask(enabledOverride, diagnostic)) return false;

    std::function<bool(uint32_t, std::vector<SdfInstruction>&, std::vector<uint32_t>&)> expandLayer;
    expandLayer = [&](uint32_t layerIndex, std::vector<SdfInstruction>& output,
                      std::vector<uint32_t>& path) -> bool {
        if (std::find(path.begin(), path.end(), layerIndex) != path.end()) {
            diagnostic = {DocumentDiagnosticCode::InvalidWire,
                          "layer-port wire cycle while flattening layer " +
                              std::to_string(LayerId(layerIndex))};
            return false;
        }
        path.push_back(layerIndex);
        const auto& source = view_.layers[layerIndex];
        for (uint32_t instructionIndex = 0; instructionIndex < source.header->instructionCount;
             ++instructionIndex) {
            const SdfInstruction& instruction = source.instructions[instructionIndex];
            if (static_cast<SdfOpCode>(instruction.opCode) != SdfOpCode::ReadPort) {
                output.push_back(instruction);
                continue;
            }
            const float rawWire = instruction.data[0];
            if (!std::isfinite(rawWire) || rawWire < 0.0f || std::floor(rawWire) != rawWire ||
                rawWire >= static_cast<float>(wires_.size())) {
                diagnostic = {DocumentDiagnosticCode::InvalidWire,
                              "ReadPort at layer " + std::to_string(LayerId(layerIndex)) +
                                  " names an invalid wire"};
                path.pop_back();
                return false;
            }
            const LayerWire& wire = wires_[static_cast<size_t>(rawWire)];
            uint32_t sourceIndex = 0u;
            if (!LayerIndexForId(wire.sourceLayerId, sourceIndex) || enabledOverride[sourceIndex] == 0u) {
                // The generated default transport is PushParam. A wire does not store a sampled
                // value; zero is the neutral fallback for an unbound Field read and the located
                // warning remains available through LayerWarnings().
                SdfInstruction fallback{};
                fallback.opCode = static_cast<uint8_t>(SdfOpCode::PushParam);
                fallback.data[0] = 0.0f;
                output.push_back(fallback);
                continue;
            }
            std::vector<SdfInstruction> sourceProgram;
            if (!expandLayer(sourceIndex, sourceProgram, path)) {
                path.pop_back();
                return false;
            }
            output.insert(output.end(), sourceProgram.begin(), sourceProgram.end());
        }
        path.pop_back();
        return true;
    };

    std::vector<SdfInstruction> instructions;
    int sp = 0;
    int psp = 0;
    bool haveBase = false;
    for (uint32_t layerIndex = 0; layerIndex < LayerCount(); ++layerIndex) {
        if (enabledOverride[layerIndex] == 0) continue;
        const auto& layer = view_.layers[layerIndex];
        if (layer.header->op == 4u) continue;  // Provide is a readable source, never a spine term.
        const uint32_t count = layer.header->instructionCount;
        std::vector<SdfInstruction> layerProgram(layer.instructions, layer.instructions + count);
        std::vector<SdfInstruction> expandedProgram;
        std::vector<uint32_t> path;
        if (v5FeaturesUsed_ && !expandLayer(layerIndex, expandedProgram, path)) return false;
        if (v5FeaturesUsed_) layerProgram = std::move(expandedProgram);
        bool skipLayer = false;
        if (view_.header.formatVersion >= kVdc1ReferenceVersion) {
            for (uint32_t instructionIndex = 0; instructionIndex < layerProgram.size(); ++instructionIndex) {
                auto& instruction = layerProgram[instructionIndex];
                if (static_cast<SdfOpCode>(instruction.opCode) != SdfOpCode::InvokeRecipe) continue;
                const float rawIndex = instruction.data[0];
                if (!std::isfinite(rawIndex) || rawIndex < 0.0f ||
                    rawIndex >= static_cast<float>(std::numeric_limits<uint32_t>::max()) ||
                    std::floor(rawIndex) != rawIndex ||
                    static_cast<uint32_t>(rawIndex) >= references_.size()) {
                    diagnostic = {DocumentDiagnosticCode::FlattenError,
                                  "layer " + std::to_string(layerIndex) + " instruction " +
                                      std::to_string(instructionIndex) + ": invalid recipe reference index"};
                    return false;
                }
                const std::string& reference = references_[static_cast<uint32_t>(rawIndex)];
                uint32_t recipeId = 0u;
                if (!resolver || !resolver(reference, recipeId)) {
                    uint32_t recipeTableIndex = 0u;
                    if (skipUnresolvedReferences && IsRecipeInstruction(layerProgram, recipeTableIndex)) {
                        diagnostic = {DocumentDiagnosticCode::FlattenError,
                                      "layer " + std::to_string(layerIndex) + ": recipe '" + reference +
                                          "' is inert until its pack is available"};
                        skipLayer = true;
                        break;
                    }
                    diagnostic = {DocumentDiagnosticCode::FlattenError,
                                  "layer " + std::to_string(layerIndex) + " instruction " +
                                      std::to_string(instructionIndex) + ": unknown recipe '" + reference + "'"};
                    return false;
                }
                instruction.data[0] = static_cast<float>(recipeId);
            }
        }
        if (skipLayer) continue;
        std::string error;
        if (!Vixen::SVO::GeneratedRecipePipelineDetail::ValidateProgram(
                layerProgram.data(), static_cast<uint32_t>(layerProgram.size()), sp, psp, error)) {
            diagnostic = {DocumentDiagnosticCode::FlattenError, std::move(error)};
            return false;
        }
        if (!haveBase) {
            instructions.insert(instructions.end(), layerProgram.begin(), layerProgram.end());
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
        instructions.insert(instructions.end(), layerProgram.begin(), layerProgram.end());
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
    if (!serializationDirty_ && !rawBytes_.empty()) {
        bytes = rawBytes_;
        return true;
    }
    std::vector<VoxelDocLayerHeader> headers(LayerCount());
    std::vector<VoxelDocLayerWrite> writes(LayerCount());
    for (uint32_t i = 0; i < LayerCount(); ++i) {
        headers[i] = *view_.layers[i].header;
        headers[i].enabled = static_cast<uint8_t>((enabledMask_ >> i) & 1u);
        writes[i].header = headers[i];
        writes[i].instructions = view_.layers[i].instructions;
    }
    const auto parameters = CopyParameters();
    const auto referenceRows = CopyReferences();
    std::vector<VoxelDocWire> wireRows(wires_.size());
    for (size_t i = 0; i < wires_.size(); ++i) {
        wireRows[i].readerLayerId = wires_[i].readerLayerId;
        wireRows[i].sourceLayerId = wires_[i].sourceLayerId;
        wireRows[i].type = static_cast<uint8_t>(wires_[i].type);
        EncodeFixedText(wires_[i].input, wireRows[i].input);
        EncodeFixedText(wires_[i].output, wireRows[i].output);
    }
    const auto bindingRows = CopyBindings();

    size_t required = 0;
    if (v5FeaturesUsed_) {
        WriteVoxelDocumentV5(view_.channels, view_.header.channelCount,
                             parameters.data(), static_cast<uint32_t>(parameters.size()),
                             writes.data(), LayerCount(),
                             referenceRows.data(), static_cast<uint32_t>(referenceRows.size()),
                             nextLayerId_, layerIds_.data(), static_cast<uint32_t>(layerIds_.size()),
                             wireRows.data(), static_cast<uint32_t>(wireRows.size()),
                             bindingRows.data(), static_cast<uint32_t>(bindingRows.size()),
                             majorMaterialId_, minorMaterialId_, majorMaterialPercent_,
                             nullptr, 0, required, authoredFormatVersion_);
    } else {
        WriteVoxelDocument(view_.channels, view_.header.channelCount,
                           parameters.data(), static_cast<uint32_t>(parameters.size()),
                           writes.data(), LayerCount(),
                           referenceRows.data(), static_cast<uint32_t>(referenceRows.size()),
                           majorMaterialId_, minorMaterialId_, majorMaterialPercent_,
                           nullptr, 0, required, authoredFormatVersion_);
    }
    bytes.assign(required, 0u);
    size_t written = 0;
    const bool wrote = v5FeaturesUsed_
        ? WriteVoxelDocumentV5(view_.channels, view_.header.channelCount,
                               parameters.data(), static_cast<uint32_t>(parameters.size()),
                               writes.data(), LayerCount(),
                               referenceRows.data(), static_cast<uint32_t>(referenceRows.size()),
                               nextLayerId_, layerIds_.data(), static_cast<uint32_t>(layerIds_.size()),
                               wireRows.data(), static_cast<uint32_t>(wireRows.size()),
                               bindingRows.data(), static_cast<uint32_t>(bindingRows.size()),
                               majorMaterialId_, minorMaterialId_, majorMaterialPercent_,
                               bytes.data(), bytes.size(), written, authoredFormatVersion_)
        : WriteVoxelDocument(view_.channels, view_.header.channelCount,
                             parameters.data(), static_cast<uint32_t>(parameters.size()),
                             writes.data(), LayerCount(),
                             referenceRows.data(), static_cast<uint32_t>(referenceRows.size()),
                             majorMaterialId_, minorMaterialId_, majorMaterialPercent_,
                             bytes.data(), bytes.size(), written, authoredFormatVersion_);
    if (!wrote) {
        diagnostic = {DocumentDiagnosticCode::WriteError,
                      v5FeaturesUsed_ ? "WriteVoxelDocumentV5 failed sizing/writing the document"
                                     : "WriteVoxelDocument failed sizing/writing the document"};
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
