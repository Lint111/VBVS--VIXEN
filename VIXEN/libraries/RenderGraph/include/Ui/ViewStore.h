#pragma once
#include "Ui/ViewBlob.h"
#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/Types.h>   // Rml::String
#include <cstddef>
#include <string_view>
#include <vector>

namespace Vixen::RenderGraph {

// One fixed-layout cell per element field; BlobView binds a ScalarDefinition to the active member
// by the field's kind. Only the member matching the field kind is ever read. `vec` is a Vector-kind
// cell (3 floats); a struct-array element CAN declare a Vector-kind column (View Contract Inc-5b
// Milestone 2.4b's row/element-Vector fix) and reads/writes through this same member, alongside
// i/f/b/s, so ViewRow's per-element cell layout stays uniform across all ViewKinds.
struct ViewCell { int i = 0; float f = 0.0f; bool b = false; Rml::String s; Vec3f vec; };

struct ViewRow {
    std::vector<ViewCell> cells;
    ViewCell& Cell(size_t memberIndex) { return cells[memberIndex]; }
};

// Generic typed storage for a described view + the by-field, blob-validated setter API. Setters
// validate field name + kind against the blob; a mismatch is logged and dropped (never a bad write).
class ViewStore {
public:
    ViewStore(const ViewBlob& blob, uint32_t consumerVersion);

    void SetScalar(std::string_view field, ViewValue v);

    struct RowHandle {
        ViewStore* store; size_t fieldIndex;
        void Set(size_t row, std::string_view elemField, ViewValue v);
    };
    RowHandle ResizeArray(std::string_view field, size_t n);

    void Flush(Rml::DataModelHandle& model);   // DirtyVariable every top-level field
    uint32_t Version() const { return consumerVersion_; }
    const ViewBlob& Blob() const { return blob_; }

    // For BlobView binding: raw pointers into the typed slots.
    void* ScalarSlotPtr(size_t fieldIndex);          // &int / &float / &bool / &Rml::String
    std::vector<ViewRow>& Array(size_t fieldIndex);  // the row container for an ArrayOfStruct field
    void* ArraySlotPtr(size_t fieldIndex) { return &Array(fieldIndex); }

private:
    struct ScalarSlot { int i = 0; float f = 0.0f; bool b = false; Rml::String s; Vec3f vec; };
    int FindField(std::string_view name) const;      // -1 if absent
    int FindElemField(size_t fieldIndex, std::string_view name) const;

    const ViewBlob& blob_;
    uint32_t consumerVersion_;
    std::vector<ScalarSlot> scalars_;                        // one per top-level field (unused for arrays)
    std::vector<std::vector<ViewRow>> arrays_;               // one per top-level field (unused for scalars)
};

}  // namespace Vixen::RenderGraph
