#pragma once

#include "Headers.h"
#include "TypedCacher.h"
#include "MainCacher.h"
#include "SamplerCacher.h"
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <optional>
#include <vector>

// Forward declarations for RenderGraph types
namespace Vixen::RenderGraph {
    struct ImageDescriptor;
    struct BufferDescriptor;
}

namespace CashSystem {

// Forward declaration
struct SamplerWrapper;

/**
 * @brief Texture resource wrapper
 *
 * Stores Vulkan handles and metadata for a resident image.
 * Decoded CPU pixels are intentionally not retained; the file path and format
 * identify the cache entry and the GPU image is the resident value.
 *
 * NOTE: Sampler is managed separately via SamplerCacher (composition pattern).
 */
struct TextureWrapper {
    VkImage image = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;

    // Reference to cached sampler (managed by SamplerCacher)
    std::shared_ptr<SamplerWrapper> samplerWrapper;

    // Cache identification
    std::string filePath;
    VkFormat format = VK_FORMAT_UNDEFINED;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t mipLevels = 1;
    uint32_t arrayLayers = 1;

};

/**
 * @brief Texture creation parameters
 *
 * TextureCacher uses composition pattern - accepts sampler via one of two methods:
 * 1. Runtime path: Pass pre-created samplerWrapper from SamplerCacher
 * 2. Deserialization path: Pass samplerParams, TextureCacher will get/create wrapper
 *
 * This dual approach enables cache hits after deserialization.
 */
struct TextureCreateParams {
    std::string filePath;
    VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;
    uint32_t width = 0;
    uint32_t height = 0;
    // Sampler from SamplerCacher (runtime path - preferred)
    std::shared_ptr<SamplerWrapper> samplerWrapper;

    // OR sampler parameters (deserialization path)
    std::optional<SamplerCreateParams> samplerParams;

};

/**
 * @brief TypedCacher for texture resources
 *
 * Caches textures based on file path and format.
 * Textures are expensive to create because:
 * - Heavy I/O (file loading from disk)
 * - Expensive decode (PNG, JPEG)
 * - GPU resource allocation and upload
 *
 * This cacher stores the resident Vulkan resources; mip generation and streaming
 * are intentionally deferred to later R649 slices.
 *
 * Usage (with composition pattern):
 * ```cpp
 * auto& mainCacher = GetOwningGraph()->GetMainCacher();
 *
 * // Step 1: Get sampler from SamplerCacher
 * auto* samplerCacher = mainCacher.GetCacher<SamplerCacher, SamplerWrapper, SamplerCreateParams>(...);
 * SamplerCreateParams samplerParams{};
 * samplerParams.minFilter = VK_FILTER_LINEAR;
 * samplerParams.magFilter = VK_FILTER_LINEAR;
 * auto samplerWrapper = samplerCacher->GetOrCreate(samplerParams);
 *
 * // Step 2: Get texture from TextureCacher (passing sampler wrapper)
 * auto* textureCacher = mainCacher.GetCacher<TextureCacher, TextureWrapper, TextureCreateParams>(...);
 * TextureCreateParams params{};
 * params.filePath = "textures/sample.png";
 * params.format = VK_FORMAT_R8G8B8A8_UNORM;
 * params.samplerWrapper = samplerWrapper;  // Pass sampler from step 1
 *
 * // Get or create cached texture
 * auto textureWrapper = textureCacher->GetOrCreate(params);
 * VkImage image = textureWrapper->image;
 * VkSampler sampler = textureWrapper->samplerWrapper->resource;
 * ```
 */
class TextureCacher : public TypedCacher<TextureWrapper, TextureCreateParams> {
public:
    TextureCacher() {
        InitializeLogger("TextureCacher", false);
    }
    ~TextureCacher() override = default;

    // Override to add cache hit/miss logging
    std::shared_ptr<TextureWrapper> GetOrCreate(const TextureCreateParams& ci);

    // Convenience API for texture loading (accepts sampler from SamplerCacher)
    std::shared_ptr<TextureWrapper> GetOrCreateTexture(
        const std::string& filePath,
        std::shared_ptr<SamplerWrapper> samplerWrapper,
        VkFormat format = VK_FORMAT_R8G8B8A8_UNORM
    );

    // Serialization
    bool SerializePayloadToFile(const std::filesystem::path& path) const override;
    bool DeserializePayloadFromFile(const std::filesystem::path& path, void* device) override;
    std::string_view name() const noexcept override { return "TextureCacher"; }

protected:
    // TypedCacher implementation
    std::shared_ptr<TextureWrapper> Create(const TextureCreateParams& ci) override;
    std::uint64_t ComputeKey(const TextureCreateParams& ci) const override;

    // Resource cleanup
    void Cleanup() override;

private:
    // Decode pixels (from file via STB, or generate a default checkerboard when the
    // file path is empty) and upload them to a real VkImage/VkImageView/VkDeviceMemory
    // through the shared TextureLoader GPU path. The wrapper's sampler is supplied
    // separately by SamplerCacher (composition) and is left untouched here.
    void LoadTextureFromFile(const TextureCreateParams& ci, TextureWrapper& wrapper);

    // Generate an in-memory RGBA8 checkerboard (used when no file path is provided),
    // returned as tightly-packed pixels plus its square dimension.
    static std::vector<uint8_t> GenerateCheckerboard(uint32_t& outSize);

    // Lazily-created transient command pool used for one-shot texture uploads.
    // Destroyed in Cleanup(). Mirrors AccelerationStructureCacher's pattern.
    VkCommandPool m_uploadCommandPool = VK_NULL_HANDLE;
};

} // namespace CashSystem
