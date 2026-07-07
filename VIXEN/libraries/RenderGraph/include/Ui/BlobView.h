#pragma once
// The generic IView host for the reflection-blob path (Inc-2b): given a ViewBlob alone (no
// per-schema C++), builds a live RmlUi data model — scalars via ScalarDefinition<T>, arrays of
// struct via a synthetic StructDefinition assembled from the blob's element fields — and binds it
// to a ViewStore the caller fills through the by-field setter API. A version mismatch between the
// blob and the store's consumer version hard-skips registration (see Register()).
#include "Ui/IView.h"
#include "Ui/ViewBlob.h"
#include "Ui/ViewStore.h"
#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/DataVariable.h>
#include <optional>
#include <string>
#include <vector>

namespace Vixen::RenderGraph {

class BlobView final : public IView {
public:
    BlobView(const ViewBlob& blob, std::string documentPath);

    const char* ModelName() const override { return modelName_.c_str(); }
    void Register(Rml::DataModelConstructor& c) override;
    const char* DocumentPath() const override { return documentPath_.c_str(); }

    ViewStore& Store() { return *store_; }
    bool Registered() const { return registered_; }

    // Forces the ViewStore's consumer version, overriding the constructor default (blob.version).
    // Used to simulate a stale consumer for the version-mismatch path. ViewStore has no assignment
    // (it holds a reference to the blob), so this re-emplaces it — safe before Register() is called.
    void SetConsumerVersion(uint32_t version);

private:
    const ViewBlob& blob_;
    std::string documentPath_;
    std::string modelName_;   // cached copy of blob_.model so ModelName()'s const char* outlives it
    std::optional<ViewStore> store_;
    std::vector<Rml::UniquePtr<Rml::VariableDefinition>> ownedDefs_;  // top-level scalar defs
    Rml::DataModelHandle model_;
    bool registered_ = false;
};

}  // namespace Vixen::RenderGraph
