#pragma once

#include <vulkan/vulkan.h>
#include <filesystem>
#include <string>
#include <vector>
#include <memory>
#include "QueueSubmitRequest.h"

struct SwapChainPublicVariables;
namespace Vixen::Vulkan::Resources { class VulkanDevice; }

namespace Vixen::Profiler {

/// Resolution mode for frame capture
enum class CaptureResolution {
    Full,      // Capture at full resolution
    Quarter    // Capture at 1/4 resolution (half width, half height)
};

/// Configuration for a frame capture operation
struct CaptureConfig {
    std::filesystem::path outputPath;
    std::string testName;
    uint32_t frameNumber = 0;
    CaptureResolution resolution = CaptureResolution::Full;
};

/// Result of a capture operation
struct CaptureResult {
    bool success = false;
    bool pending = false;
    std::filesystem::path savedPath;
    std::string errorMessage;
    uint32_t capturedWidth = 0;
    uint32_t capturedHeight = 0;
};

/**
 * @brief Captures rendered frames to PNG files
 *
 * Publishes GPU->CPU readback of swapchain images to the queue owner and polls the next-frame
 * completion. Uses a staging buffer for the copy and stb_image_write for PNG encoding.
 *
 * Thread safety: NOT thread-safe. Call only from render thread.
 */
class FrameCapture {
public:
    FrameCapture() = default;
    ~FrameCapture();

    // Non-copyable
    FrameCapture(const FrameCapture&) = delete;
    FrameCapture& operator=(const FrameCapture&) = delete;

    // Movable
    FrameCapture(FrameCapture&&) noexcept;
    FrameCapture& operator=(FrameCapture&&) noexcept;

    /// Initialize with Vulkan resources (call once after device creation)
    /// @param device Logical device
    /// @param physicalDevice Physical device (for memory type queries)
    /// @param queue Graphics queue for copy commands
    /// @param queueFamilyIndex Queue family index for command pool
    /// @param maxWidth Maximum expected image width
    /// @param maxHeight Maximum expected image height
    /// @return true if initialization succeeded
    bool Initialize(
        VkDevice device,
        VkPhysicalDevice physicalDevice,
        VkQueue queue,
        uint32_t queueFamilyIndex,
        uint32_t maxWidth,
        uint32_t maxHeight
    );

    /// Initialize with the queue owner. Captures published through this overload complete on a
    /// later graph phase; the producer never submits or waits on the physical queue.
    bool Initialize(
        Vixen::Vulkan::Resources::VulkanDevice* queueOwner,
        VkDevice device,
        VkPhysicalDevice physicalDevice,
        VkQueue queue,
        uint32_t queueFamilyIndex,
        uint32_t maxWidth,
        uint32_t maxHeight
    );

    /// Cleanup Vulkan resources
    void Cleanup();

    /// Queue the current swapchain image for a next-frame PNG result. `pending` is set when the
    /// queue owner accepted the request; call Poll() from a later frame for the file result.
    /// @param swapchainVars Swapchain public variables (provides image access)
    /// @param imageIndex Current swapchain image index
    /// @param config Capture configuration
    /// @return Result with pending=true when the owner accepted the request
    CaptureResult Capture(
        const SwapChainPublicVariables* swapchainVars,
        uint32_t imageIndex,
        const CaptureConfig& config
    );

    /// Poll the completion published by the previous Capture call. Returns true only when the
    /// next-frame result is available and fills `result` with the PNG outcome.
    bool Poll(CaptureResult& result);

    /// Check if capture system is initialized
    bool IsInitialized() const { return initialized_; }

    /// Generate filename for a capture
    /// @param testName Test identifier
    /// @param frameNumber Frame number within test
    /// @return Filename like "testName_frameNumber.png"
    static std::string GenerateFilename(
        const std::string& testName,
        uint32_t frameNumber
    );

private:
    // Vulkan resources
    VkDevice device_ = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VkQueue queue_ = VK_NULL_HANDLE;
    Vixen::Vulkan::Resources::VulkanDevice* queueOwner_ = nullptr;
    uint32_t queueFamilyIndex_ = 0;

    // Staging buffer (reusable)
    VkBuffer stagingBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory stagingMemory_ = VK_NULL_HANDLE;
    VkDeviceSize stagingBufferSize_ = 0;

    // Command pool and buffer
    VkCommandPool commandPool_ = VK_NULL_HANDLE;
    VkCommandBuffer commandBuffer_ = VK_NULL_HANDLE;
    bool initialized_ = false;
    std::shared_ptr<Vixen::Vulkan::Resources::QueueSubmitCompletion> completion_;
    CaptureConfig pendingConfig_;
    uint32_t pendingWidth_ = 0;
    uint32_t pendingHeight_ = 0;
    bool pendingCapture_ = false;

    // Internal helpers
    bool CreateStagingBuffer(VkDeviceSize size);
    void DestroyStagingBuffer();
    uint32_t FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);
    void RecordCopyCommands(VkImage srcImage, uint32_t width, uint32_t height, VkFormat format);
    bool SaveToPNG(
        const std::filesystem::path& path,
        uint32_t width,
        uint32_t height,
        CaptureResolution resolution
    );
};

} // namespace Vixen::Profiler
