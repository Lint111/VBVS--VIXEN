#pragma once
// Trailing-edge debounce of window-size observations (see WindowNode for why a live drag must not
// recreate the swapchain per event). Pure state: the caller supplies the clock, so it is testable
// without a window.

#include <cstdint>

namespace Vixen::RenderGraph {

class ResizeDebouncer {
public:
    explicit ResizeDebouncer(double debounceSeconds = 0.1) : debounceSeconds_(debounceSeconds) {}

    /// Fold one observed client size, given the size consumers currently use (`appliedW/H`).
    /// - 0x0 (minimised) is ignored: the last real extent stays.
    /// - A size equal to the applied one cancels any pending size (a drag that returned must not
    ///   later apply the stale intermediate size).
    /// - A repeat of the unsettled pending size does not restart the debounce clock.
    void Observe(uint32_t w, uint32_t h, uint32_t appliedW, uint32_t appliedH, double now) {
        if (w == 0 || h == 0) return;
        if (w == appliedW && h == appliedH) {
            pending_ = false;
            return;
        }
        if (!pending_ || w != width_ || h != height_) {
            pending_ = true;
            width_ = w;
            height_ = h;
            lastChange_ = now;
        }
    }

    [[nodiscard]] bool HasPending() const { return pending_; }
    [[nodiscard]] bool Settled(double now) const { return pending_ && (now - lastChange_) >= debounceSeconds_; }
    [[nodiscard]] uint32_t Width() const { return width_; }
    [[nodiscard]] uint32_t Height() const { return height_; }
    void Clear() { pending_ = false; }

private:
    double debounceSeconds_;
    bool pending_ = false;
    uint32_t width_ = 0;
    uint32_t height_ = 0;
    double lastChange_ = 0.0;
};

} // namespace Vixen::RenderGraph
