#include "Profiler/FrameCapture.h"
#include "VulkanSwapChain.h"  // For SwapChainPublicVariables

#include <algorithm>
#include <cstring>
#include <iostream>
#include <stdexcept>

// STB implementations - only define here to avoid multiple definition errors
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "stb_image_resize2.h"

namespace Vixen::Profiler {

FrameCapture::~FrameCapture() {
    Cleanup();
}

FrameCapture::FrameCapture(FrameCapture&& other) noexcept
    : device_(other.device_)
    , physicalDevice_(other.physicalDevice_)
    , queue_(other.queue_)
    , queueFamilyIndex_(other.queueFamilyIndex_)
    , stagingBuffer_(other.stagingBuffer_)
    , stagingMemory_(other.stagingMemory_)
    , stagingBufferSize_(other.stagingBufferSize_)
    , commandPool_(other.commandPool_)
    , commandBuffer_(other.commandBuffer_)
    , fence_(other.fence_)
    , initialized_(other.initialized_)
{
    // Clear source
    other.device_ = VK_NULL_HANDLE;
    other.stagingBuffer_ = VK_NULL_HANDLE;
    other.stagingMemory_ = VK_NULL_HANDLE;
    other.commandPool_ = VK_NULL_HANDLE;
    other.commandBuffer_ = VK_NULL_HANDLE;
    other.fence_ = VK_NULL_HANDLE;
    other.initialized_ = false;
}

FrameCapture& FrameCapture::operator=(FrameCapture&& other) noexcept {
    if (this != &other) {
        Cleanup();

        device_ = other.device_;
        physicalDevice_ = other.physicalDevice_;
        queue_ = other.queue_;
        queueFamilyIndex_ = other.queueFamilyIndex_;
        stagingBuffer_ = other.stagingBuffer_;
        stagingMemory_ = other.stagingMemory_;
        stagingBufferSize_ = other.stagingBufferSize_;
        commandPool_ = other.commandPool_;
        commandBuffer_ = other.commandBuffer_;
        fence_ = other.fence_;
        initialized_ = other.initialized_;

        other.device_ = VK_NULL_HANDLE;
        other.stagingBuffer_ = VK_NULL_HANDLE;
        other.stagingMemory_ = VK_NULL_HANDLE;
        other.commandPool_ = VK_NULL_HANDLE;
        other.commandBuffer_ = VK_NULL_HANDLE;
        other.fence_ = VK_NULL_HANDLE;
        other.initialized_ = false;
    }
    return *this;
}

bool FrameCapture::Initialize(
    VkDevice device,
    VkPhysicalDevice physicalDevice,
    VkQueue queue,
    uint32_t queueFamilyIndex,
    uint32_t maxWidth,
    uint32_t maxHeight
) {
    if (initialized_) {
        return true;  // Already initialized
    }

    device_ = device;
    physicalDevice_ = physicalDevice;
    queue_ = queue;
    queueFamilyIndex_ = queueFamilyIndex;

    // Validate inputs
    if (device == VK_NULL_HANDLE || physicalDevice == VK_NULL_HANDLE || queue == VK_NULL_HANDLE) {
        std::cerr << "[FrameCapture] Initialize failed: null device/physicalDevice/queue" << std::endl;
        return false;
    }

    // Create command pool
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = queueFamilyIndex;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

    VkResult result = vkCreateCommandPool(device_, &poolInfo, nullptr, &commandPool_);
    if (result != VK_SUCCESS) {
        std::cerr << "[FrameCapture] vkCreateCommandPool failed: " << result << std::endl;
        return false;
    }

    // Allocate command buffer
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = commandPool_;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;

    if (vkAllocateCommandBuffers(device_, &allocInfo, &commandBuffer_) != VK_SUCCESS) {
        vkDestroyCommandPool(device_, commandPool_, nullptr);
        commandPool_ = VK_NULL_HANDLE;
        return false;
    }

    // Create fence
    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = 0;  // Start unsignaled

    if (vkCreateFence(device_, &fenceInfo, nullptr, &fence_) != VK_SUCCESS) {
        vkDestroyCommandPool(device_, commandPool_, nullptr);
        commandPool_ = VK_NULL_HANDLE;
        return false;
    }

    // Pre-allocate staging buffer for max size (RGBA, 4 bytes per pixel)
    VkDeviceSize bufferSize = static_cast<VkDeviceSize>(maxWidth) * maxHeight * 4;
    if (!CreateStagingBuffer(bufferSize)) {
        vkDestroyFence(device_, fence_, nullptr);
        vkDestroyCommandPool(device_, commandPool_, nullptr);
        fence_ = VK_NULL_HANDLE;
        commandPool_ = VK_NULL_HANDLE;
        return false;
    }

    initialized_ = true;
    return true;
}

void FrameCapture::Cleanup() {
    if (!device_) {
        return;
    }

    // Wait for any pending operations
    if (fence_ != VK_NULL_HANDLE) {
        vkWaitForFences(device_, 1, &fence_, VK_TRUE, UINT64_MAX);
    }

    DestroyStagingBuffer();

    if (fence_ != VK_NULL_HANDLE) {
        vkDestroyFence(device_, fence_, nullptr);
        fence_ = VK_NULL_HANDLE;
    }

    if (commandPool_ != VK_NULL_HANDLE) {
        vkDestroyCommandPool(device_, commandPool_, nullptr);
        commandPool_ = VK_NULL_HANDLE;
        commandBuffer_ = VK_NULL_HANDLE;  // Freed with pool
    }

    initialized_ = false;
}

bool FrameCapture::CreateStagingBuffer(VkDeviceSize size) {
    // Destroy existing if any
    DestroyStagingBuffer();

    // Create buffer
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(device_, &bufferInfo, nullptr, &stagingBuffer_) != VK_SUCCESS) {
        return false;
    }

    // Get memory requirements
    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(device_, stagingBuffer_, &memRequirements);

    // Allocate memory (HOST_VISIBLE | HOST_COHERENT for CPU readback)
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = FindMemoryType(
        memRequirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    );

    if (allocInfo.memoryTypeIndex == UINT32_MAX) {
        vkDestroyBuffer(device_, stagingBuffer_, nullptr);
        stagingBuffer_ = VK_NULL_HANDLE;
        return false;
    }

    if (vkAllocateMemory(device_, &allocInfo, nullptr, &stagingMemory_) != VK_SUCCESS) {
        vkDestroyBuffer(device_, stagingBuffer_, nullptr);
        stagingBuffer_ = VK_NULL_HANDLE;
        return false;
    }

    // Bind memory
    if (vkBindBufferMemory(device_, stagingBuffer_, stagingMemory_, 0) != VK_SUCCESS) {
        vkFreeMemory(device_, stagingMemory_, nullptr);
        vkDestroyBuffer(device_, stagingBuffer_, nullptr);
        stagingBuffer_ = VK_NULL_HANDLE;
        stagingMemory_ = VK_NULL_HANDLE;
        return false;
    }

    stagingBufferSize_ = size;
    return true;
}

void FrameCapture::DestroyStagingBuffer() {
    if (stagingBuffer_ != VK_NULL_HANDLE) {
        vkDestroyBuffer(device_, stagingBuffer_, nullptr);
        stagingBuffer_ = VK_NULL_HANDLE;
    }
    if (stagingMemory_ != VK_NULL_HANDLE) {
        vkFreeMemory(device_, stagingMemory_, nullptr);
        stagingMemory_ = VK_NULL_HANDLE;
    }
    stagingBufferSize_ = 0;
}

uint32_t FrameCapture::FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice_, &memProperties);

    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) &&
            (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    return UINT32_MAX;  // Not found
}

CaptureResult FrameCapture::Capture(
    const SwapChainPublicVariables* swapchainVars,
    uint32_t imageIndex,
    const CaptureConfig& config
) {
    CaptureResult result;

    if (!initialized_) {
        result.errorMessage = "FrameCapture not initialized";
        return result;
    }

    if (!swapchainVars) {
        result.errorMessage = "Null swapchain variables";
        return result;
    }

    if (imageIndex >= swapchainVars->colorBuffers.size()) {
        result.errorMessage = "Image index out of range";
        return result;
    }

    uint32_t width = swapchainVars->Extent.width;
    uint32_t height = swapchainVars->Extent.height;
    VkImage srcImage = swapchainVars->colorBuffers[imageIndex].image;
    VkFormat format = swapchainVars->Format;

    // Ensure staging buffer is large enough
    VkDeviceSize requiredSize = static_cast<VkDeviceSize>(width) * height * 4;
    if (requiredSize > stagingBufferSize_) {
        if (!CreateStagingBuffer(requiredSize)) {
            result.errorMessage = "Failed to resize staging buffer";
            return result;
        }
    }

    // Record and submit copy commands
    if (commandBuffer_ == VK_NULL_HANDLE) {
        result.errorMessage = "Command buffer is null - Initialize() failed";
        return result;
    }
    RecordCopyCommands(srcImage, width, height, format);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer_;

    vkResetFences(device_, 1, &fence_);
    if (vkQueueSubmit(queue_, 1, &submitInfo, fence_) != VK_SUCCESS) {
        result.errorMessage = "Failed to submit copy commands";
        return result;
    }

    // Wait for copy to complete
    VkResult waitResult = vkWaitForFences(device_, 1, &fence_, VK_TRUE, 5000000000ULL);  // 5 second timeout
    if (waitResult != VK_SUCCESS) {
        result.errorMessage = "Fence wait failed or timed out";
        return result;
    }

    // Create output directory
    std::filesystem::path outputDir = config.outputPath / "debug_images";
    std::error_code ec;
    std::filesystem::create_directories(outputDir, ec);
    if (ec) {
        result.errorMessage = "Failed to create output directory: " + ec.message();
        return result;
    }

    // Generate filename and save
    std::string filename = GenerateFilename(config.testName, config.frameNumber);
    std::filesystem::path fullPath = outputDir / filename;

    if (!SaveToPNG(fullPath, width, height, config.resolution)) {
        result.errorMessage = "Failed to write PNG file";
        return result;
    }

    result.success = true;
    result.savedPath = fullPath;
    result.capturedWidth = (config.resolution == CaptureResolution::Quarter) ? width / 2 : width;
    result.capturedHeight = (config.resolution == CaptureResolution::Quarter) ? height / 2 : height;
    return result;
}

void FrameCapture::RecordCopyCommands(VkImage srcImage, uint32_t width, uint32_t height, VkFormat /*format*/) {
    if (commandBuffer_ == VK_NULL_HANDLE) {
        return;  // Invalid state - Initialize() must have failed
    }
    vkResetCommandBuffer(commandBuffer_, 0);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(commandBuffer_, &beginInfo);

    // Transition: PRESENT_SRC -> TRANSFER_SRC
    VkImageMemoryBarrier toTransferSrc{};
    toTransferSrc.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toTransferSrc.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
    toTransferSrc.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    toTransferSrc.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    toTransferSrc.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    toTransferSrc.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toTransferSrc.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toTransferSrc.image = srcImage;
    toTransferSrc.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    toTransferSrc.subresourceRange.baseMipLevel = 0;
    toTransferSrc.subresourceRange.levelCount = 1;
    toTransferSrc.subresourceRange.baseArrayLayer = 0;
    toTransferSrc.subresourceRange.layerCount = 1;

    vkCmdPipelineBarrier(
        commandBuffer_,
        VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &toTransferSrc
    );

    // Copy image to staging buffer
    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;  // Tightly packed
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {0, 0, 0};
    region.imageExtent = {width, height, 1};

    vkCmdCopyImageToBuffer(
        commandBuffer_,
        srcImage,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        stagingBuffer_,
        1, &region
    );

    // Transition: TRANSFER_SRC -> PRESENT_SRC
    VkImageMemoryBarrier toPresent{};
    toPresent.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toPresent.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    toPresent.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
    toPresent.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    toPresent.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    toPresent.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toPresent.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toPresent.image = srcImage;
    toPresent.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    toPresent.subresourceRange.baseMipLevel = 0;
    toPresent.subresourceRange.levelCount = 1;
    toPresent.subresourceRange.baseArrayLayer = 0;
    toPresent.subresourceRange.layerCount = 1;

    vkCmdPipelineBarrier(
        commandBuffer_,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
        0, 0, nullptr, 0, nullptr, 1, &toPresent
    );

    vkEndCommandBuffer(commandBuffer_);
}

bool FrameCapture::SaveToPNG(
    const std::filesystem::path& path,
    uint32_t width,
    uint32_t height,
    CaptureResolution resolution
) {
    // Map staging buffer
    void* data;
    if (vkMapMemory(device_, stagingMemory_, 0, VK_WHOLE_SIZE, 0, &data) != VK_SUCCESS) {
        return false;
    }

    bool success = false;

    // Handle BGRA -> RGBA swizzle (VK_FORMAT_B8G8R8A8_UNORM is typical swapchain format)
    uint8_t* pixels = static_cast<uint8_t*>(data);
    const size_t pixelCount = static_cast<size_t>(width) * height;
    for (size_t i = 0; i < pixelCount; ++i) {
        std::swap(pixels[i * 4 + 0], pixels[i * 4 + 2]);  // Swap B and R
    }

    if (resolution == CaptureResolution::Quarter) {
        // Resize to quarter resolution
        uint32_t newWidth = std::max(width / 2, 1u);
        uint32_t newHeight = std::max(height / 2, 1u);
        std::vector<uint8_t> resized(static_cast<size_t>(newWidth) * newHeight * 4);

        stbir_resize_uint8_linear(
            pixels, static_cast<int>(width), static_cast<int>(height), static_cast<int>(width * 4),
            resized.data(), static_cast<int>(newWidth), static_cast<int>(newHeight), static_cast<int>(newWidth * 4),
            STBIR_RGBA
        );

        success = stbi_write_png(
            path.string().c_str(),
            static_cast<int>(newWidth),
            static_cast<int>(newHeight),
            4,
            resized.data(),
            static_cast<int>(newWidth * 4)
        ) != 0;
    } else {
        success = stbi_write_png(
            path.string().c_str(),
            static_cast<int>(width),
            static_cast<int>(height),
            4,
            pixels,
            static_cast<int>(width * 4)
        ) != 0;
    }

    vkUnmapMemory(device_, stagingMemory_);
    return success;
}

std::string FrameCapture::GenerateFilename(
    const std::string& testName,
    uint32_t frameNumber
) {
    return testName + "_frame" + std::to_string(frameNumber) + ".png";
}

} // namespace Vixen::Profiler
