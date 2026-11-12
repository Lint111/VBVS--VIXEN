#pragma once
// Inc1 editor state: a loaded VoxelDocument, its enabled-layer operations, and the
// flatten/save seam. The application owns input history, while each mutation is applied here so
// the headless module remains the single authority for accepted document state.

#include <cstddef>
#include <cstdint>
#include <array>
#include <string>
#include <string_view>
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
    InvalidBlendRadius,
    InvalidLayerName,
    InvalidTransform,
    StaleBakeSnapshot,
    FlattenError,
    WriteError,
    // Catalogue-driven shape edits (appended: ids are stable).
    InvalidPrimitive,
    InvalidLayerOp,
    FieldNotEditable,
    InvalidPrimitiveField,
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

// A layer's placement as the editor shows it: position, Euler rotation (degrees, R = Rz * Ry * Rx)
// and per-axis scale. It is stored as the layer program's leading Transform instruction wrapped
// with a closing RestorePos (translation data[0..2], inverse rotation xyzw data[4..7], inverse
// scale data[8..10], distance scale data[11]); the identity placement stores no wrapper at all.
struct LayerTransform {
    std::array<float, 3> position{0.0f, 0.0f, 0.0f};
    std::array<float, 3> rotationDegrees{0.0f, 0.0f, 0.0f};
    std::array<float, 3> scale{1.0f, 1.0f, 1.0f};

    bool IsIdentity() const {
        return position == std::array<float, 3>{0.0f, 0.0f, 0.0f} &&
               rotationDegrees == std::array<float, 3>{0.0f, 0.0f, 0.0f} &&
               scale == std::array<float, 3>{1.0f, 1.0f, 1.0f};
    }
};

// One editable component group of a layer's primitive: an InstructionData argument of the
// generated catalogue (name, width in floats, declared range), with its current value. `name` and
// `type` point into the generated argument table.
struct PrimitiveFieldInfo {
    const char* name = "";
    const char* type = "";
    const char* label = "";   // the catalogue's display label (points into the generated table)
    uint32_t width = 0;
    bool hasRange = false;
    float minimum = 0.0f;
    float maximum = 0.0f;
    bool hasDefault = false;
    std::array<float, 4> defaultValue{0.0f, 0.0f, 0.0f, 0.0f};
    std::array<float, 4> value{0.0f, 0.0f, 0.0f, 0.0f};
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
    static constexpr size_t kMaximumLayerNameBytes = 31;

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

    // Layer blend radius (finite, >= 0), name (1..31 bytes: the VDC1 header keeps nameBytes[32]
    // NUL-terminated) and placement. Every setter validates before it rebuilds, so a rejected
    // call leaves the accepted document and its revision untouched.
    // The value rules are public so a caller can refuse an invalid edit before it records one.
    static bool ValidateBlendRadius(float radius, DocumentDiagnostic& diagnostic);
    static bool ValidateLayerName(const std::string& name, DocumentDiagnostic& diagnostic);
    static bool ValidateLayerTransform(const LayerTransform& transform,
                                       DocumentDiagnostic& diagnostic);
    bool SetLayerBlendRadius(uint32_t index, float radius, DocumentDiagnostic& diagnostic);
    bool SetLayerName(uint32_t index, const std::string& name, DocumentDiagnostic& diagnostic);
    bool GetLayerTransform(uint32_t index, LayerTransform& out,
                           DocumentDiagnostic& diagnostic) const;
    bool SetLayerTransform(uint32_t index, const LayerTransform& transform,
                           DocumentDiagnostic& diagnostic);
    // Replaces one layer's whole program (finite fields, masks whole bytes). It is the exact
    // inverse of any program edit: undo of a placement edit restores the snapshot taken before it.
    bool SetLayerProgram(uint32_t index,
                         const std::vector<Yeroket::Sdf::Generated::SdfInstruction>& program,
                         DocumentDiagnostic& diagnostic);

    // Catalogue-driven shape editing (R872.1). A layer is "shape-editable" only when its program is
    // [Transform] + exactly one Primitive-category instruction + [RestorePos]; any other program
    // reports its fields as not editable and refuses writes (no guessing).
    // MakePrimitiveLayer builds one layer holding one instruction of `primitive` with every
    // InstructionData argument at its declared default; it refuses a non-Primitive opcode and a
    // primitive with an argument that declares no default (the diagnostic names it).
    static bool MakePrimitiveLayer(Vixen::SVO::Recipe::SdfOpCode primitive, const std::string& name,
                                   EditableDocumentLayer& out, DocumentDiagnostic& diagnostic);
    // True when MakePrimitiveLayer can build `primitive` (the picker lists exactly these); on false
    // `reason` receives the diagnostic naming the blocking argument.
    static bool CanMakePrimitive(Vixen::SVO::Recipe::SdfOpCode primitive, std::string* reason = nullptr);
    // Sets the layer's combine op (a value of the generated SdfLayerOpTable).
    bool SetLayerOp(uint32_t index, uint8_t layerOp, DocumentDiagnostic& diagnostic);
    // The primitive opcode of an editable layer; false (with FieldNotEditable) otherwise.
    bool GetLayerPrimitive(uint32_t index, Vixen::SVO::Recipe::SdfOpCode& out,
                           DocumentDiagnostic& diagnostic) const;
    // The editable fields of the layer's primitive in argument-table order; empty with a
    // FieldNotEditable diagnostic when the layer is not shape-editable.
    bool GetLayerPrimitiveFields(uint32_t index, std::vector<PrimitiveFieldInfo>& out,
                                 DocumentDiagnostic& diagnostic) const;
    bool GetLayerPrimitiveField(uint32_t index, const std::string& argumentName,
                                std::array<float, 4>& out, DocumentDiagnostic& diagnostic) const;
    // The value rule of SetLayerPrimitiveField (finite, inside the declared range), public so a
    // caller can refuse before it records an edit.
    static bool ValidatePrimitiveField(const PrimitiveFieldInfo& field, const std::array<float, 4>& value,
                                       DocumentDiagnostic& diagnostic);
    // Writes the first `width` components of `value` (the rest are ignored). A value outside the
    // declared range, a non-finite value, an unknown argument or a non-editable layer is refused
    // and leaves the document and its revision untouched.
    bool SetLayerPrimitiveField(uint32_t index, const std::string& argumentName,
                                const std::array<float, 4>& value, DocumentDiagnostic& diagnostic);

    // Pure presentation of a catalogue opcode name: split at camel-case boundaries ("CappedTorus" ->
    // "Capped Torus"). Ids, selectors and action params keep the exact opcode name; the catalogue
    // declares no per-opcode display name (a later kernel question), so this is display only.
    static std::string DisplayName(std::string_view opcodeName);
    // How well `filter` matches a primitive (case-insensitive, over the opcode name and its display
    // text): 0 = a prefix of either, 1 = a prefix of one of the display words, 2 = a substring, -1 = no
    // match. An empty filter matches everything at rank 0. The flyout lists matches by rank, then name.
    static int FilterRank(std::string_view opcodeName, std::string_view filter);
    // The flyout's rows: every primitive MakePrimitiveLayer can build, read from the generated op table, that
    // matches `filter`, ordered by FilterRank then opcode name.
    struct PrimitiveChoice {
        std::string name;      // the exact opcode name (ids and action params)
        std::string display;   // DisplayName(name)
        std::string category;  // the catalogue category ("Primitive")
    };
    static std::vector<PrimitiveChoice> CreatablePrimitives(std::string_view filter = {});

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

    // Encodes the accepted document (layers, enabled state, parameter values) as VDC1 bytes.
    bool Serialize(std::vector<uint8_t>& bytes, DocumentDiagnostic& diagnostic) const;

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
