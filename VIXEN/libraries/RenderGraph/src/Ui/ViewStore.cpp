#include "Ui/ViewStore.h"
#include <RmlUi/Core/Log.h>

namespace Vixen::RenderGraph {

ViewStore::ViewStore(const ViewBlob& blob, uint32_t consumerVersion)
    : blob_(blob), consumerVersion_(consumerVersion) {
    scalars_.resize(blob_.fields.size());
    arrays_.resize(blob_.fields.size());
}

int ViewStore::FindField(std::string_view name) const {
    for (size_t k = 0; k < blob_.fields.size(); ++k)
        if (blob_.fields[k].name == name) return static_cast<int>(k);
    return -1;
}

int ViewStore::FindElemField(size_t fieldIndex, std::string_view name) const {
    const auto& elem = blob_.fields[fieldIndex].elem;
    for (size_t k = 0; k < elem.size(); ++k) if (elem[k].name == name) return static_cast<int>(k);
    return -1;
}

static void AssignCell(ViewCell& c, ViewKind kind, const ViewValue& v) {
    switch (kind) {
        case ViewKind::Int:        c.i = v.i; break;
        case ViewKind::Float:      c.f = v.f; break;
        case ViewKind::Bool:       c.b = v.b; break;
        case ViewKind::String:     c.s = v.s; break;
        case ViewKind::Vector:     c.vec = v.vec; break;
        case ViewKind::SubjectRef: c.subj = v.subj; break;
        default: break;
    }
}

void ViewStore::SetScalar(std::string_view field, ViewValue v) {
    int idx = FindField(field);
    if (idx < 0) { Rml::Log::Message(Rml::Log::LT_ERROR, "ViewStore: unknown field '%.*s'", (int)field.size(), field.data()); return; }
    ViewKind k = blob_.fields[idx].kind;
    if (!KindAcceptsValue(k, v)) { Rml::Log::Message(Rml::Log::LT_ERROR, "ViewStore: kind mismatch for '%.*s'", (int)field.size(), field.data()); return; }
    auto& slot = scalars_[idx];
    switch (k) {
        case ViewKind::Int:        slot.i = v.i; break;
        case ViewKind::Float:      slot.f = v.f; break;
        case ViewKind::Bool:       slot.b = v.b; break;
        case ViewKind::String:     slot.s = v.s; break;
        case ViewKind::Vector:     slot.vec = v.vec; break;
        case ViewKind::SubjectRef: slot.subj = v.subj; break;
        default: break;
    }
}

ViewStore::RowHandle ViewStore::ResizeArray(std::string_view field, size_t n) {
    int idx = FindField(field);
    if (idx < 0 || blob_.fields[idx].kind != ViewKind::ArrayOfStruct) {
        Rml::Log::Message(Rml::Log::LT_ERROR, "ViewStore: '%.*s' is not an array field", (int)field.size(), field.data());
        return RowHandle{this, static_cast<size_t>(-1)};
    }
    auto& rows = arrays_[idx];
    const size_t members = blob_.fields[idx].elem.size();
    rows.assign(n, ViewRow{});
    for (auto& r : rows) r.cells.resize(members);
    return RowHandle{this, static_cast<size_t>(idx)};
}

void ViewStore::RowHandle::Set(size_t row, std::string_view elemField, ViewValue v) {
    if (fieldIndex == static_cast<size_t>(-1)) return;
    int mi = store->FindElemField(fieldIndex, elemField);
    if (mi < 0) { Rml::Log::Message(Rml::Log::LT_ERROR, "ViewStore: unknown elem field '%.*s'", (int)elemField.size(), elemField.data()); return; }
    ViewKind k = store->blob_.fields[fieldIndex].elem[mi].kind;
    if (!KindAcceptsValue(k, v)) { Rml::Log::Message(Rml::Log::LT_ERROR, "ViewStore: elem kind mismatch for '%.*s'", (int)elemField.size(), elemField.data()); return; }
    auto& rows = store->arrays_[fieldIndex];
    if (row >= rows.size()) return;
    AssignCell(rows[row].cells[mi], k, v);
}

void* ViewStore::ScalarSlotPtr(size_t fieldIndex) {
    auto& slot = scalars_[fieldIndex];
    switch (blob_.fields[fieldIndex].kind) {
        case ViewKind::Int:        return &slot.i;
        case ViewKind::Float:      return &slot.f;
        case ViewKind::Bool:       return &slot.b;
        case ViewKind::String:     return &slot.s;
        case ViewKind::Vector:     return &slot.vec;
        case ViewKind::SubjectRef: return &slot.subj;
        default: return nullptr;
    }
}

std::vector<ViewRow>& ViewStore::Array(size_t fieldIndex) { return arrays_[fieldIndex]; }

void ViewStore::Flush(Rml::DataModelHandle& model) {
    for (const auto& f : blob_.fields) {
        // f.name is a string_view into constexpr/parser-owned storage; DirtyVariable takes a String.
        model.DirtyVariable(Rml::String(f.name));
    }
}

}  // namespace Vixen::RenderGraph
