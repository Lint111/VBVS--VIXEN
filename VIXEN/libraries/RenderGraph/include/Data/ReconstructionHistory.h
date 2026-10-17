#pragma once

#include <glm/glm.hpp>
#include <vulkan/vulkan.h>
#include <cstdint>

namespace Vixen::RenderGraph {

enum class ReconstructionHistoryReset : uint32_t {
    FirstUse = 1u,
    CameraCut = 2u,
    ViewportSwitch = 4u,
    ProjectionChanged = 8u,
    ExtentChanged = 16u,
    ProviderChanged = 32u,
    SceneChanged = 64u,
    Recompile = 128u,
    WriteNotCommitted = 256u,
};

// Transient renderer state, owned by one viewport's graph instance. It does not
// identify scene instances and is never serialized into simulation or saves.
class ReconstructionHistoryState {
public:
    struct Frame {
        uint32_t reset;
        uint32_t valid;
        uint32_t frameIndex;
        uint32_t writeIndex;
        uint32_t resetReasons;
    };

    void Invalidate(ReconstructionHistoryReset reason) {
        pendingReasons_ |= static_cast<uint32_t>(reason);
    }

    void SetViewportActive(bool active) {
        if (active != active_) Invalidate(ReconstructionHistoryReset::ViewportSwitch);
        active_ = active;
    }

    Frame BeginFrame(const glm::mat4& inverseProjection, VkExtent2D internal, VkExtent2D output,
                     uint32_t cameraCutEpoch = 0u) {
        if (frameOpen_)
            Invalidate(ReconstructionHistoryReset::WriteNotCommitted);
        if (initialized_) {
            if (cameraCutEpoch != cameraCutEpoch_)
                Invalidate(ReconstructionHistoryReset::CameraCut);
            if (inverseProjection != projection_)
                Invalidate(ReconstructionHistoryReset::ProjectionChanged);
            if (internal.width != internal_.width || internal.height != internal_.height ||
                output.width != output_.width || output.height != output_.height)
                Invalidate(ReconstructionHistoryReset::ExtentChanged);
        }
        const uint32_t reasons = pendingReasons_;
        if (reasons) {
            valid_ = false;
            frameIndex_ = 0;
        }
        pendingReasons_ = 0;
        cameraCutEpoch_ = cameraCutEpoch;
        projection_ = inverseProjection;
        internal_ = internal;
        output_ = output;
        initialized_ = true;
        frameOpen_ = true;
        return {!valid_, valid_, frameIndex_, frameIndex_ & 1u, reasons};
    }

    // Call only after the selected provider has successfully recorded/submitted
    // the current history write. A skipped write invalidates correspondence at
    // the next BeginFrame, even if an older history image was previously valid.
    void CommitHistoryFrame() {
        if (!frameOpen_ || !active_) return;
        valid_ = true;
        ++frameIndex_;
        frameOpen_ = false;
    }

private:
    glm::mat4 projection_{1.0f};
    VkExtent2D internal_{}, output_{};
    uint32_t pendingReasons_ = static_cast<uint32_t>(ReconstructionHistoryReset::FirstUse);
    uint32_t frameIndex_ = 0;
    uint32_t cameraCutEpoch_ = 0;
    bool active_ = true;
    bool initialized_ = false;
    bool valid_ = false;
    bool frameOpen_ = false;
};

} // namespace Vixen::RenderGraph
