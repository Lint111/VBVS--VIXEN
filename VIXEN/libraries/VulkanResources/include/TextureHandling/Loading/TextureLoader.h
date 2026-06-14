#pragma once

#include "Headers.h"
#include "ILoggable.h"
#include "error/VulkanError.h"

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
    VulkanResult<TextureData> Load(const char* fileName, const TextureLoadConfig& config);

    // Upload an in-memory RGBA8 image (no file I/O) through the exact same GPU
    // staging/upload path as Load(). `pixels` must be width*height*4 bytes of
    // tightly-packed RGBA8 (single mip level). The returned TextureData is owned
    // by the caller, identical in shape to what Load() produces. This is the
    // file-independent seam used for procedurally-generated textures (e.g. a
    // default checkerboard) so they go through the real upload, not a parallel one.
    VulkanResult<TextureData> LoadFromMemory(
        const uint8_t* pixels,
        uint32_t width,
        uint32_t height,
        const TextureLoadConfig& config
    );

protected:
    // Override this to load pixel data from file (library-specific)
    virtual VulkanResult<PixelData> LoadPixelData(const char* fileName) = 0;

    // Override this to free pixel data (library-specific)
    virtual void FreePixelData(PixelData& data) = 0;

    // Access to device and command pool
    VulkanDevice* deviceObj;
    VkCommandPool cmdPool;

private:
    // Upload linear - map memory directly, no staging buffer
    VulkanStatus UploadLinear(
        const PixelData& pixelData,
        TextureData* texture,
        const TextureLoadConfig& config
    );

    // Upload optimal - use staging buffer for GPU-optimal layout
    VulkanStatus UploadOptimal(
        const PixelData& pixelData,
        TextureData* texture,
        const TextureLoadConfig& config
    );

    VulkanStatus CreateImage(
        TextureData* texture,
        VkImageUsageFlags usage,
        VkFormat format,
        VkImageTiling tiling,
        uint32_t width,
        uint32_t height,
        uint32_t mipLevels
    );

    VulkanStatus CreateImageView(
        TextureData* texture,
        VkFormat format,
        uint32_t mipLevels
    );

    VulkanStatus CreateSampler(
        TextureData* texture,
        uint32_t mipLevels
    );

    // Release any GPU resources already created in `texture` when a load fails partway, so error
    // paths leak nothing (Load() calls this on upload failure).
    void DestroyPartialTexture(TextureData& texture);

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
