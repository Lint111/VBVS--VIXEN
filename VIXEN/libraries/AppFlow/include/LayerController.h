#pragma once
#include <cstdint>
#include "generated/AppFlow.g.h"

namespace Vixen::AppFlow {

using Generated::LayerState;

// Layer enabled-mask source of truth (design §2.1). Caps at 32 layers (the bitmask
// width) — out-of-range layer indices are no-ops (mirrors EditorApplication::ToggleLayer's
// `if (i >= LayerCount()) return`). SetLayerCount defaults all layers to enabled.
class LayerController {
public:
    void SetLayerCount(uint32_t n);
    uint32_t LayerCount() const { return count_; }

    bool IsEnabled(uint32_t i) const { return i < count_ && ((mask_ >> i) & 1u) != 0u; }

    // Flips bit i. Out of range → no-op, returns false.
    bool Toggle(uint32_t i);

    uint32_t Mask() const { return mask_; }
    void SetMask(uint32_t m);

    LayerState Snapshot() const { return LayerState{mask_}; }
    void Restore(const LayerState& s) { SetMask(s.enabledMask); }

private:
    uint32_t mask_ = 0;
    uint32_t count_ = 0;
};

} // namespace Vixen::AppFlow
