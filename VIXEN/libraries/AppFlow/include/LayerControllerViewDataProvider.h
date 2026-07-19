#pragma once
#include "IViewDataProvider.h"
#include "LayerController.h"

namespace Vixen::AppFlow {

// Day-one direct-field IViewDataProvider (seam doc's "~3 lines"): noun EditorNouns_layerMask <->
// LayerController::Mask()/SetMask(). Always readable; ignores `instance` (LayerController is a
// singleton, no per-entity identity yet -- Inc-B swaps this provider for a Gaia-backed one without
// touching any caller of the seam).
class LayerControllerViewDataProvider final : public IViewDataProvider {
public:
    explicit LayerControllerViewDataProvider(LayerController& layers) : layers_(layers) {}

    bool ReadU32(ViewNounKey key, uint32_t& out) const override {
        if (key.noun != ViewNounId::EditorNouns_layerMask) return false;
        out = layers_.Mask();
        return true;
    }

    void WriteU32(ViewNounKey key, uint32_t value) override {
        if (key.noun != ViewNounId::EditorNouns_layerMask) return;
        layers_.SetMask(value);
    }

private:
    LayerController& layers_;
};

}  // namespace Vixen::AppFlow
