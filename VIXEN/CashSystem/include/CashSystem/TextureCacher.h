#pragma once

#include "Headers.h"
#include "TypedCacher.h"
#include "MainCacher.h"
#include <cstdint>
#include <string>
#include <memory>
#include <unordered_map>

// Forward declarations for RenderGraph types
namespace Vixen::RenderGraph {
    struct ImageDescriptor;
    struct BufferDescriptor;
}

namespace CashSystem {

/**
 * @brief Texture resource wrapper
 * 
 * Stores both Vulkan handles and metadata for cache identification.
 */
struct TextureWrapper {
    VkImage image = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    
    // Cache identification
    std::string filePath;
    VkFormat format = VK_FORMAT_UNDEFINED;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t mipLevels = 1;
    uint32_t arrayLayers = 1;
    
    // Loading parameters
    bool generateMipmaps = false;
    VkFilter minFilter = VK_FILTER_LINEAR;
    VkFilter magFilter = VK_FILTER_LINEAR;
    VkSamplerAddressMode addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    VkSamplerAddressMode addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    VkSamplerAddressMode addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
};

/**
 * @brief Texture creation parameters
 */
struct TextureCreateParams {
    std::string filePath;
    VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;
    uint32_t width = 0;
    uint32_t height = 0;
    bool generateMipmaps = false;
    VkFilter minFilter = VK_FILTER_LINEAR;
    VkFilter magFilter = VK_FILTER_LINEAR;
    VkSamplerAddressMode addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    VkSamplerAddressMode addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    VkSamplerAddressMode addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    
    // Hash for quick validation
    std::string fileChecksum;
};

/**
 * @brief TypedCacher for texture resources
 * 
 * Caches loaded textures based on:
 * - File path and content hash
 * - Texture parameters (format, dimensions, filters)
 * - Sampler configuration
 */
class TextureCacher : public TypedCacher<TextureWrapper, TextureCreateParams> {
public:
    TextureCacher() = default;
    ~TextureCacher() override = default;

    // Convenience API for texture loading
    std::shared_ptr<TextureWrapper> GetOrCreateTexture(
        const std::string& filePath,
        VkFormat format = VK_FORMAT_R8G8B8A8_UNORM,
        bool generateMipmaps = false,
        VkFilter minFilter = VK_FILTER_LINEAR,
        VkFilter magFilter = VK_FILTER_LINEAR,
        VkSamplerAddressMode addressMode = VK_SAMPLER_ADDRESS_MODE_REPEAT
    );

protected:
    // TypedCacher implementation
    std::shared_ptr<TextureWrapper> Create(const TextureCreateParams& ci) override;
    std::uint64_t ComputeKey(const TextureCreateParams& ci) const override;

    // Serialization
    bool SerializeToFile(const std::filesystem::path& path) const override;
    bool DeserializeFromFile(const std::filesystem::path& path, void* device) override;
    std::string_view name() const noexcept override { return "TextureCacher"; }

private:
    // Helper methods
    std::string ComputeFileChecksum(const std::string& filePath) const;
    void LoadTextureFromFile(const TextureCreateParams& ci, TextureWrapper& wrapper);
};

} // namespace CashSystem