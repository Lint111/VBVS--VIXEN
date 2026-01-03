#pragma once

#include <cstdint>
#include <memory>
#include <vulkan/vulkan.h>

namespace ResourceManagement {

/**
 * @brief Update operation types
 *
 * Each type maps to a specific backend recording operation.
 * Extensible - add new types as needed.
 */
enum class UpdateType : uint8_t {
    TLASRebuild,      // Rebuild/update acceleration structure
    BufferWrite,      // Write to mapped buffer (host-visible)
    // Future types:
    // ImageTransition,
    // DescriptorUpdate,
    // ComputeDispatch,
};

/**
 * @brief Base class for GPU update requests
 *
 * Part of Phase 3.5: Generalized Update API
 *
 * Derived classes implement type-specific command recording.
 * BatchedUpdater collects these and invokes Record() during
 * command buffer recording phase.
 *
 * Pattern mirrors BatchedUploader but for per-frame GPU commands
 * rather than CPU→GPU data transfers.
 */
class UpdateRequestBase {
public:
    UpdateType type;
    uint32_t imageIndex = 0;    // Frame/swapchain image index
    uint8_t priority = 128;     // For ordering (lower = earlier, 128 = default)

    explicit UpdateRequestBase(UpdateType t) : type(t) {}
    virtual ~UpdateRequestBase() = default;

    // Non-copyable (may hold GPU resources)
    UpdateRequestBase(const UpdateRequestBase&) = delete;
    UpdateRequestBase& operator=(const UpdateRequestBase&) = delete;

    // Movable
    UpdateRequestBase(UpdateRequestBase&&) = default;
    UpdateRequestBase& operator=(UpdateRequestBase&&) = default;

    /**
     * @brief Record this update's commands into the command buffer
     *
     * Called by BatchedUpdater during the Execute phase.
     * Derived classes implement type-specific command recording.
     *
     * @param cmd Active command buffer in recording state
     */
    virtual void Record(VkCommandBuffer cmd) = 0;

    /**
     * @brief Get estimated GPU cost for scheduling hints
     *
     * @return Relative cost estimate (higher = more expensive)
     */
    virtual uint32_t GetEstimatedCost() const { return 1; }

    /**
     * @brief Check if this update requires memory barriers before/after
     *
     * @return true if barriers needed
     */
    virtual bool RequiresBarriers() const { return false; }
};

/**
 * @brief Convenience alias for update request pointers
 */
using UpdateRequestPtr = std::unique_ptr<UpdateRequestBase>;

} // namespace ResourceManagement
