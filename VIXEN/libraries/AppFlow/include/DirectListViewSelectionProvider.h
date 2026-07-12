#pragma once
#include "IViewSelectionProvider.h"

namespace Vixen::AppFlow {

// Day-one direct-list IViewSelectionProvider (mirrors LayerControllerViewDataProvider.h's "day-one
// direct-field provider" role for IViewDataProvider): a trivial std::vector<SelectionEntityID>-
// backed selection set, useful for tests and any non-Gaia consumer. Order is exactly the vector's
// own order (insertion order as given to the constructor/SetSelection) -- no implicit sorting.
class DirectListViewSelectionProvider final : public IViewSelectionProvider {
public:
    DirectListViewSelectionProvider() = default;
    explicit DirectListViewSelectionProvider(std::vector<SelectionEntityID> ids) : ids_(std::move(ids)) {}

    // Replaces the selection set wholesale (mirrors a "commit" -- infrequent, per design §6).
    void SetSelection(std::vector<SelectionEntityID> ids) { ids_ = std::move(ids); }

    size_t ids(std::vector<SelectionEntityID>& out) const override {
        out = ids_;
        return out.size();
    }

    bool at(size_t index, SelectionEntityID& out) const override {
        if (index >= ids_.size()) return false;
        out = ids_[index];
        return true;
    }

private:
    std::vector<SelectionEntityID> ids_;
};

}  // namespace Vixen::AppFlow
