#pragma once

#include "Headers.h"
#include "ILoggable.h"

// Forward declaration
namespace Vixen::Vulkan::Resources {
    class VulkanDevice;
}

namespace Vixen::TextureHandling {

using Vixen::Vulkan::Resources::VulkanDevice;

// Texture data owned by the caller after loading
struct TextureData {
    VkSampler sampler;
    VkImage image;
    VkImageLayout imageLayout;
    VkMemoryAllocateInfo memAllocInfo;
    VkDeviceMemory mem;
    VkImageView view;
    VkCommandBuffer cmdTexture;  // Command buffer used for texture upload
    uint32_t minMapLevels;
    uint32_t layerCount;
    uint32_t textureWidth, textureHeight;
    VkDescriptorImageInfo descsImageInfo;
};

// Pixel data loaded from file
struct PixelData {
    void* pixels = nullptr;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t mipLevels = 1;
    VkDeviceSize size = 0;

    ~PixelData() {
        // Derived classes responsible for cleanup
    }
};

// Configuration for texture upload
struct TextureLoadConfig {
    enum class UploadMode {
        Linear,   // CPU-visible linear tiling (VK_IMAGE_TILING_LINEAR)
        Optimal   // GPU-optimized optimal tiling (VK_IMAGE_TILING_OPTIMAL) via staging
    };

    UploadMode uploadMode = UploadMode::Optimal;
    VkImageUsageFlags usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;
};

// Base class for texture loaders - handles all Vulkan operations
class TextureLoader : public ILoggable {
public:
    // Constructor takes device and command pool references
    TextureLoader(VulkanDevice* device, VkCommandPool commandPool);
    virtual ~TextureLoader() = default;

    // Load texture from file and return TextureData for caller to own
    TextureData Load(const char* fileName, const TextureLoadConfig& config);

protected:
    // Override this to load pixel data from file (library-specific)
    virtual PixelData LoadPixelData(const char* fileName) = 0;

    // Override this to free pixel data (library-specific)
    virtual void FreePixelData(PixelData& data) = 0;

    // Access to device and command pool
    VulkanDevice* deviceObj;
    VkCommandPool cmdPool;

private:
    // Upload linear - map memory directly, no staging buffer
    void UploadLinear(
        const PixelData& pixelData,
        TextureData* texture,
        const TextureLoadConfig& config
    );

    // Upload optimal - use staging buffer for GPU-optimal layout
    void UploadOptimal(
        const PixelData& pixelData,
        TextureData* texture,
        const TextureLoadConfig& config
    );

    void CreateImage(
        TextureData* texture,
        VkImageUsageFlags usage,
        VkFormat format,
        VkImageTiling tiling,
        uint32_t width,
        uint32_t height,
        uint32_t mipLevels
    );

    void CreateImageView(
        TextureData* texture,
        VkFormat format,
        uint32_t mipLevels
    );

    void CreateSampler(
        TextureData* texture,
        uint32_t mipLevels
    );

    public:
    void SetImageLayout(
        VkImage image,
        VkImageAspectFlags aspectMask,
        VkImageLayout oldLayout,
        VkImageLayout newLayout,
        VkImageSubresourceRange& subresourceRange,
        VkCommandBuffer cmdBuf
    );
};

} // namespace Vixen::TextureHandling
