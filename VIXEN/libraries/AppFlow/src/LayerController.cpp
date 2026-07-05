#include "LayerController.h"

namespace Vixen::AppFlow {

void LayerController::SetLayerCount(uint32_t n) {
    count_ = (n > 32u) ? 32u : n;
    mask_ = (count_ >= 32u) ? 0xFFFFFFFFu : ((1u << count_) - 1u);
}

bool LayerController::Toggle(uint32_t i) {
    if (i >= count_) return false;
    mask_ ^= (1u << i);
    return true;
}

void LayerController::SetMask(uint32_t m) {
    mask_ = m & ((count_ >= 32u) ? 0xFFFFFFFFu : ((1u << count_) - 1u));
}

} // namespace Vixen::AppFlow
