#pragma once

#include "Memory/IMemoryAllocator.h"
#include "State/StatefulContainer.h"
#include "ILoggable.h"

#include <vulkan/vulkan.h>

#include <vector>
#include <cstdint>
#include <span>

// Forward declarations
namespace Vixen::Vulkan::Resources {
    class VulkanDevice;
}

namespace CashSystem {

/**
 * @brief Per-frame GPU ring buffer for TLAS instance data
 *
 * Manages per-swapchain-image instance buffers for stall-free TLAS updates.
 * Uses persistent mapping for zero-overhead CPU writes.
 *
 * Frame count is obtained from SwapChainNode (not hardcoded), following
 * the PerFrameResources pattern from RenderGraph.
 *
 * Part of Sprint 5 Phase 3: TLAS Lifecycle
 */
class TLASInstanceBuffer : public ILoggable {
public:
    /**
     * @brief Configuration for buffer initialization
     */
    struct Config {
        uint32_t maxInstances = 1024;   ///< Pre-allocated instance capacity
    };

    TLASInstanceBuffer() = default;
    ~TLASInstanceBuffer();

    // Non-copyable, movable
    TLASInstanceBuffer(const TLASInstanceBuffer&) = delete;
    TLASInstanceBuffer& operator=(const TLASInstanceBuffer&) = delete;
    TLASInstanceBuffer(TLASInstanceBuffer&&) noexcept;
    TLASInstanceBuffer& operator=(TLASInstanceBuffer&&) noexcept;

    // ========================================================================
    // Lifecycle
    // ========================================================================

    /**
     * @brief Initialize buffers for given image count
     *
     * Uses VulkanDevice's centralized allocation API.
     *
     * @param device Vulkan device for allocation
     * @param imageCount Number of swapchain images (from SwapChainNode)
     * @param config Buffer configuration
     * @return true on success
     */
    bool Initialize(
        Vixen::Vulkan::Resources::VulkanDevice* device,
        uint32_t imageCount,
        const Config& config);

    /**
     * @brief Cleanup all buffers
     */
    void Cleanup();

    /**
     * @brief Check if initialized
     */
    bool IsInitialized() const { return device_ != nullptr; }

    // ========================================================================
    // Per-Frame Buffer Access
    // ========================================================================

    /**
     * @brief Get buffer for specific image index
     * @param imageIndex Swapchain image index (from SwapChainNode)
     */
    VkBuffer GetBuffer(uint32_t imageIndex) const;

    /**
     * @brief Get device address for specific image index
     * @param imageIndex Swapchain image index
     */
    VkDeviceAddress GetDeviceAddress(uint32_t imageIndex) const;

    /**
     * @brief Get persistently mapped pointer for specific image index
     * @param imageIndex Swapchain image index
     * @return Pointer to HOST_COHERENT memory, or nullptr if not mapped
     */
    void* GetMappedPtr(uint32_t imageIndex) const;

    // ========================================================================
    // Instance Operations
    // ========================================================================

    /**
     * @brief Write instance data to specific frame's buffer
     *
     * @param imageIndex Swapchain image index
     * @param instances Instance data to write
     *
     * Uses persistently mapped memory - no explicit flush needed
     * (HOST_COHERENT). Updates state to Ready after write.
     */
    void WriteInstances(
        uint32_t imageIndex,
        std::span<const VkAccelerationStructureInstanceKHR> instances);

    /**
     * @brief Get instance count for specific frame
     * @param imageIndex Swapchain image index
     */
    uint32_t GetInstanceCount(uint32_t imageIndex) const;

    /**
     * @brief Get maximum instance capacity
     */
    uint32_t GetMaxInstances() const { return maxInstances_; }

    /**
     * @brief Get number of frame buffers
     */
    uint32_t GetFrameCount() const {
        return static_cast<uint32_t>(frameBuffers_.size());
    }

    // ========================================================================
    // State Tracking (via StatefulContainer)
    // ========================================================================

    /**
     * @brief Get state of specific frame buffer
     */
    ResourceManagement::ContainerState GetState(uint32_t imageIndex) const;

    /**
     * @brief Mark frame buffer as needing update
     */
    void MarkDirty(uint32_t imageIndex);

    /**
     * @brief Check if any frame buffer needs update
     */
    bool AnyDirty() const;

private:
    /**
     * @brief Per-frame buffer data
     */
    struct FrameBuffer {
        ResourceManagement::BufferAllocation allocation{};
        void* mappedPtr = nullptr;
        uint32_t instanceCount = 0;
    };

    Vixen::Vulkan::Resources::VulkanDevice* device_ = nullptr;
    uint32_t maxInstances_ = 0;

    ResourceManagement::StatefulContainer<FrameBuffer> frameBuffers_;

    /**
     * @brief Validate image index
     */
    bool ValidateImageIndex(uint32_t imageIndex) const;
};

} // namespace CashSystem
