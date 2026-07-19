#include "Ui/BlobView.h"

#include <RmlUi/Core/DataTypeRegister.h>
#include <RmlUi/Core/Log.h>
#include <atomic>

namespace Vixen::RenderGraph {

namespace {

// Reads/writes a ViewRow's cell at a fixed member index, dispatching to the cell's active member
// by kind — mirrors what ScalarDefinition<T>::Get/Set do, but indexed into ViewRow::cells instead
// of a bare T*. One instance per element field of an ArrayOfStruct.
class ViewRowMemberDefinition final : public Rml::VariableDefinition {
public:
    ViewRowMemberDefinition(size_t memberIndex, ViewKind kind)
        : Rml::VariableDefinition(Rml::DataVariableType::Scalar), memberIndex_(memberIndex), kind_(kind) {}

    bool Get(void* ptr, Rml::Variant& variant) override {
        const ViewCell& cell = static_cast<ViewRow*>(ptr)->cells[memberIndex_];
        switch (kind_) {
            case ViewKind::Int:    variant = cell.i; return true;
            case ViewKind::Float:  variant = cell.f; return true;
            case ViewKind::Bool:   variant = cell.b; return true;
            case ViewKind::String: variant = cell.s; return true;
            // Vector/SubjectRef/U64: no RmlUi Rml::Variant representation (no vec3/u64 variant
            // type) -- not bindable through this data-model path, same precedent as the
            // pre-existing Vector/SubjectRef gap; consumers read these via TypedAccessorSection
            // (Ui/ViewStore.h's ScalarSlotPtr/Cell) instead.
            case ViewKind::Vector: case ViewKind::SubjectRef: case ViewKind::U64:
            case ViewKind::ArrayOfStruct: return false;
        }
        return false;
    }

    bool Set(void* ptr, const Rml::Variant& variant) override {
        ViewCell& cell = static_cast<ViewRow*>(ptr)->cells[memberIndex_];
        switch (kind_) {
            case ViewKind::Int:    return variant.GetInto<int>(cell.i);
            case ViewKind::Float:  return variant.GetInto<float>(cell.f);
            case ViewKind::Bool:   return variant.GetInto<bool>(cell.b);
            case ViewKind::String: return variant.GetInto<Rml::String>(cell.s);
            case ViewKind::Vector: case ViewKind::SubjectRef: case ViewKind::U64:
            case ViewKind::ArrayOfStruct: return false;
        }
        return false;
    }

private:
    size_t memberIndex_;
    ViewKind kind_;
};

// Synthetic FamilyIds for the per-array StructDefinitions registered below, seeded well above any
// count of RmlUi's real type-derived families (Family<T>::Id() starts at 0 and increments) so the
// two id spaces never collide.
Rml::FamilyId NextSyntheticFamilyId() {
    static std::atomic<int> next{0x7000'0000};
    return static_cast<Rml::FamilyId>(next.fetch_add(1));
}

Rml::UniquePtr<Rml::VariableDefinition> MakeScalarDefinition(ViewKind kind) {
    switch (kind) {
        case ViewKind::Int:    return Rml::MakeUnique<Rml::ScalarDefinition<int>>();
        case ViewKind::Float:  return Rml::MakeUnique<Rml::ScalarDefinition<float>>();
        case ViewKind::Bool:   return Rml::MakeUnique<Rml::ScalarDefinition<bool>>();
        case ViewKind::String: return Rml::MakeUnique<Rml::ScalarDefinition<Rml::String>>();
        // Vector/SubjectRef/U64: not RmlUi-bindable top-level scalar kinds, same as ViewRowMemberDefinition above.
        case ViewKind::Vector: case ViewKind::SubjectRef: case ViewKind::U64:
        case ViewKind::ArrayOfStruct: return nullptr;
    }
    return nullptr;
}

}  // namespace

BlobView::BlobView(const ViewBlob& blob, std::string documentPath)
    : blob_(blob), documentPath_(std::move(documentPath)), modelName_(blob.model),
      store_(std::in_place, blob_, blob_.version) {}

void BlobView::SetConsumerVersion(uint32_t version) {
    store_.emplace(blob_, version);
}

void BlobView::Register(Rml::DataModelConstructor& c) {
    if (store_->Version() != blob_.version) {
        Rml::Log::Message(Rml::Log::LT_ERROR,
            "View '%s' version mismatch: engine %u vs consumer %u — skipping register",
            modelName_.c_str(), blob_.version, store_->Version());
        registered_ = false;
        return;
    }

    for (size_t idx = 0; idx < blob_.fields.size(); ++idx) {
        const ViewFieldDesc& field = blob_.fields[idx];
        const Rml::String name(field.name);

        if (field.kind == ViewKind::ArrayOfStruct) {
            auto structDef = Rml::MakeUnique<Rml::StructDefinition>();
            for (size_t mi = 0; mi < field.elem.size(); ++mi) {
                const ViewFieldDesc& member = field.elem[mi];
                structDef->AddMember(Rml::String(member.name),
                                      Rml::MakeUnique<ViewRowMemberDefinition>(mi, member.kind));
            }
            Rml::VariableDefinition* structDefRaw = structDef.get();
            c.GetDataTypeRegister()->RegisterDefinition(NextSyntheticFamilyId(), std::move(structDef));

            auto arrayDef = Rml::MakeUnique<Rml::ArrayDefinition<std::vector<ViewRow>>>(structDefRaw);
            Rml::VariableDefinition* arrayDefRaw = arrayDef.get();
            c.GetDataTypeRegister()->RegisterDefinition(NextSyntheticFamilyId(), std::move(arrayDef));

            c.BindCustomDataVariable(name, Rml::DataVariable(arrayDefRaw, store_->ArraySlotPtr(idx)));
        } else {
            ownedDefs_.push_back(MakeScalarDefinition(field.kind));
            c.BindCustomDataVariable(name, Rml::DataVariable(ownedDefs_.back().get(), store_->ScalarSlotPtr(idx)));
        }
    }

    model_ = c.GetModelHandle();
    registered_ = true;
}

}  // namespace Vixen::RenderGraph
