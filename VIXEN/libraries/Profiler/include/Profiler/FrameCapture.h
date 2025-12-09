#pragma once

#include <vulkan/vulkan.h>
#include <filesystem>
#include <string>
#include <vector>

struct SwapChainPublicVariables;

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
    std::filesystem::path savedPath;
    std::string errorMessage;
    uint32_t capturedWidth = 0;
    uint32_t capturedHeight = 0;
};

/**
 * @brief Captures rendered frames to PNG files
 *
 * Performs synchronous GPU->CPU readback of swapchain images.
 * Uses a staging buffer for the copy and stb_image_write for PNG encoding.
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

    /// Cleanup Vulkan resources
    void Cleanup();

    /// Capture current swapchain image to PNG
    /// @param swapchainVars Swapchain public variables (provides image access)
    /// @param imageIndex Current swapchain image index
    /// @param config Capture configuration
    /// @return Result with success status and saved path
    CaptureResult Capture(
        const SwapChainPublicVariables* swapchainVars,
        uint32_t imageIndex,
        const CaptureConfig& config
    );

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
    uint32_t queueFamilyIndex_ = 0;

    // Staging buffer (reusable)
    VkBuffer stagingBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory stagingMemory_ = VK_NULL_HANDLE;
    VkDeviceSize stagingBufferSize_ = 0;

    // Command pool and buffer
    VkCommandPool commandPool_ = VK_NULL_HANDLE;
    VkCommandBuffer commandBuffer_ = VK_NULL_HANDLE;
    VkFence fence_ = VK_NULL_HANDLE;

    bool initialized_ = false;

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
