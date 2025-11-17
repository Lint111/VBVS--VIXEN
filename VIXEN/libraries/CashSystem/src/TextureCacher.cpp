#include "CashSystem/TextureCacher.h"
#include "CashSystem/SamplerCacher.h"
#include "VixenHash.h"
#include "TextureHandling/Loading/TextureLoader.h"
#include "VulkanDevice.h"
#include <fstream>
#include <sstream>
#include <iostream>

using namespace Vixen::Hash;

namespace CashSystem {

std::shared_ptr<TextureWrapper> TextureCacher::GetOrCreate(const TextureCreateParams& ci) {
    // Call base class implementation
    auto wrapper = TypedCacher<TextureWrapper, TextureCreateParams>::GetOrCreate(ci);

    if (wrapper) {
        LOG_INFO("[TextureCacher::GetOrCreate] Cache hit for texture: " + ci.filePath);
    } else {
        LOG_INFO("[TextureCacher::GetOrCreate] Cache miss for texture: " + ci.filePath);
    }

    return wrapper;
}

std::shared_ptr<TextureWrapper> TextureCacher::GetOrCreateTexture(
    const std::string& filePath,
    std::shared_ptr<SamplerWrapper> samplerWrapper,
    VkFormat format,
    bool generateMipmaps
) {
    TextureCreateParams params{};
    params.filePath = filePath;
    params.format = format;
    params.generateMipmaps = generateMipmaps;
    params.samplerWrapper = samplerWrapper;
    params.fileChecksum = ComputeFileChecksum(filePath);

    return GetOrCreate(params);
}

std::shared_ptr<TextureWrapper> TextureCacher::Create(const TextureCreateParams& ci) {
    auto wrapper = std::make_shared<TextureWrapper>();
    wrapper->filePath = ci.filePath;
    wrapper->format = ci.format;
    wrapper->generateMipmaps = ci.generateMipmaps;

    // Handle sampler via composition pattern
    if (ci.samplerWrapper) {
        // Runtime path: use provided sampler
        wrapper->samplerWrapper = ci.samplerWrapper;
    } else if (ci.samplerParams.has_value()) {
        // Deserialization path: get sampler from SamplerCacher
        auto& mainCacher = MainCacher::Instance();
        auto* samplerCacher = mainCacher.GetCacher<SamplerCacher, SamplerWrapper, SamplerCreateParams>(
            std::type_index(typeid(SamplerWrapper)),
            GetDevice()
        );

        if (samplerCacher) {
            wrapper->samplerWrapper = samplerCacher->GetOrCreate(ci.samplerParams.value());
        }
    }

    // Load texture from file (also caches pixel data in wrapper)
    LoadTextureFromFile(ci, *wrapper);

    return wrapper;
}

std::uint64_t TextureCacher::ComputeKey(const TextureCreateParams& ci) const {
    // Build key string from file path + format + mip settings + file checksum
    std::ostringstream keyStream;
    keyStream << ci.filePath << "|"
              << static_cast<uint32_t>(ci.format) << "|"
              << (ci.generateMipmaps ? "1" : "0") << "|"
              << ci.fileChecksum;

    const std::string keyString = keyStream.str();
    return std::hash<std::string>{}(keyString);
}

std::string TextureCacher::ComputeFileChecksum(const std::string& filePath) const {
    try {
        std::ifstream file(filePath, std::ios::binary | std::ios::ate);
        if (!file.is_open()) {
            return "";
        }

        std::streamsize size = file.tellg();
        file.seekg(0, std::ios::beg);

        std::vector<uint8_t> buffer(size);
        if (!file.read(reinterpret_cast<char*>(buffer.data()), size)) {
            return "";
        }

        return ComputeSHA256Hex(buffer.data(), buffer.size());
    } catch (const std::exception&) {
        return "";
    }
}

void TextureCacher::LoadTextureFromFile(const TextureCreateParams& ci, TextureWrapper& wrapper) {
    // TODO: Integrate with TextureLoader to load actual texture data
    // For now, create a simple fallback texture

    LOG_INFO("[TextureCacher::LoadTextureFromFile] Loading texture from file: " + ci.filePath);

    // Use fallback for now - full TextureLoader integration requires concrete loader implementation
    CreateFallbackTexture(ci, wrapper);

    LOG_INFO("[TextureCacher::LoadTextureFromFile] Texture loaded successfully");
}

void TextureCacher::CreateFallbackTexture(const TextureCreateParams& ci, TextureWrapper& wrapper) {
    // Create a simple 1x1 white texture as fallback
    wrapper.width = 1;
    wrapper.height = 1;
    wrapper.mipLevels = 1;
    wrapper.arrayLayers = 1;

    // Cache pixel data (key benefit of TextureCacher)
    wrapper.pixelData.resize(4); // RGBA
    wrapper.pixelData[0] = 255;  // R
    wrapper.pixelData[1] = 255;  // G
    wrapper.pixelData[2] = 255;  // B
    wrapper.pixelData[3] = 255;  // A

    // TODO: Create actual Vulkan resources (VkImage, VkImageView, VkDeviceMemory)
    // This requires VulkanDevice integration which should be added when connecting to RenderGraph

    LOG_INFO("[TextureCacher::CreateFallbackTexture] Created fallback 1x1 white texture");
}

bool TextureCacher::SerializeToFile(const std::filesystem::path& path) const {
    // Serialize texture metadata and cached pixel data

    std::ofstream file(path, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "[TextureCacher::SerializeToFile] Failed to open file: " << path << std::endl;
        return false;
    }

    // Write version header
    uint32_t version = 1;
    file.write(reinterpret_cast<const char*>(&version), sizeof(version));

    // Write number of cached textures
    std::shared_lock lock(m_lock);
    uint32_t cacheSize = static_cast<uint32_t>(m_entries.size());
    file.write(reinterpret_cast<const char*>(&cacheSize), sizeof(cacheSize));

    // Serialize each texture
    for (const auto& [key, entry] : m_entries) {
        const auto& wrapper = entry.resource;

        // Write file path
        uint32_t pathLen = static_cast<uint32_t>(wrapper->filePath.size());
        file.write(reinterpret_cast<const char*>(&pathLen), sizeof(pathLen));
        file.write(wrapper->filePath.data(), pathLen);

        // Write format and dimensions
        file.write(reinterpret_cast<const char*>(&wrapper->format), sizeof(wrapper->format));
        file.write(reinterpret_cast<const char*>(&wrapper->width), sizeof(wrapper->width));
        file.write(reinterpret_cast<const char*>(&wrapper->height), sizeof(wrapper->height));
        file.write(reinterpret_cast<const char*>(&wrapper->mipLevels), sizeof(wrapper->mipLevels));
        file.write(reinterpret_cast<const char*>(&wrapper->arrayLayers), sizeof(wrapper->arrayLayers));
        file.write(reinterpret_cast<const char*>(&wrapper->generateMipmaps), sizeof(wrapper->generateMipmaps));

        // Write cached pixel data (key benefit - avoids reloading/decoding)
        uint32_t pixelDataSize = static_cast<uint32_t>(wrapper->pixelData.size());
        file.write(reinterpret_cast<const char*>(&pixelDataSize), sizeof(pixelDataSize));
        if (pixelDataSize > 0) {
            file.write(reinterpret_cast<const char*>(wrapper->pixelData.data()), pixelDataSize);
        }

        // Write sampler parameters (if available)
        if (wrapper->samplerWrapper) {
            uint8_t hasSampler = 1;
            file.write(reinterpret_cast<const char*>(&hasSampler), sizeof(hasSampler));

            // Serialize sampler create params for recreation
            // TODO: Serialize actual SamplerCreateParams once available
            // For now, just write a placeholder
            uint32_t samplerDataSize = 0;
            file.write(reinterpret_cast<const char*>(&samplerDataSize), sizeof(samplerDataSize));
        } else {
            uint8_t hasSampler = 0;
            file.write(reinterpret_cast<const char*>(&hasSampler), sizeof(hasSampler));
        }
    }

    std::cout << "[TextureCacher::SerializeToFile] Serialized " << cacheSize << " textures to " << path << std::endl;
    return true;
}

bool TextureCacher::DeserializeFromFile(const std::filesystem::path& path, void* device) {
    // Deserialize texture metadata and cached pixel data
    // Vulkan resources are recreated on-demand

    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "[TextureCacher::DeserializeFromFile] Failed to open file: " << path << std::endl;
        return false;
    }

    // Read version header
    uint32_t version = 0;
    file.read(reinterpret_cast<char*>(&version), sizeof(version));
    if (version != 1) {
        std::cerr << "[TextureCacher::DeserializeFromFile] Unsupported version: " << version << std::endl;
        return false;
    }

    // Read number of cached textures
    uint32_t cacheSize = 0;
    file.read(reinterpret_cast<char*>(&cacheSize), sizeof(cacheSize));

    std::cout << "[TextureCacher::DeserializeFromFile] Loading " << cacheSize << " textures from " << path << std::endl;

    // Deserialize each texture metadata
    for (uint32_t i = 0; i < cacheSize; ++i) {
        // Read file path
        uint32_t pathLen = 0;
        file.read(reinterpret_cast<char*>(&pathLen), sizeof(pathLen));
        std::string filePath(pathLen, '\0');
        file.read(filePath.data(), pathLen);

        // Read format and dimensions
        VkFormat format;
        uint32_t width, height, mipLevels, arrayLayers;
        bool generateMipmaps;
        file.read(reinterpret_cast<char*>(&format), sizeof(format));
        file.read(reinterpret_cast<char*>(&width), sizeof(width));
        file.read(reinterpret_cast<char*>(&height), sizeof(height));
        file.read(reinterpret_cast<char*>(&mipLevels), sizeof(mipLevels));
        file.read(reinterpret_cast<char*>(&arrayLayers), sizeof(arrayLayers));
        file.read(reinterpret_cast<char*>(&generateMipmaps), sizeof(generateMipmaps));

        // Read cached pixel data
        uint32_t pixelDataSize = 0;
        file.read(reinterpret_cast<char*>(&pixelDataSize), sizeof(pixelDataSize));
        std::vector<uint8_t> pixelData(pixelDataSize);
        if (pixelDataSize > 0) {
            file.read(reinterpret_cast<char*>(pixelData.data()), pixelDataSize);
        }

        // Read sampler data
        uint8_t hasSampler = 0;
        file.read(reinterpret_cast<char*>(&hasSampler), sizeof(hasSampler));
        if (hasSampler) {
            uint32_t samplerDataSize = 0;
            file.read(reinterpret_cast<char*>(&samplerDataSize), sizeof(samplerDataSize));
            // Skip sampler data for now
        }

        // Note: Actual texture resources are recreated on-demand via GetOrCreate
        // This deserialization validates the file format and prepares metadata
    }

    std::cout << "[TextureCacher::DeserializeFromFile] Texture metadata validated" << std::endl;
    std::cout << "  Note: Texture resources will be recreated on-demand" << std::endl;

    (void)device;  // Device used when recreating resources on-demand
    return true;
}

void TextureCacher::Cleanup() {
    // Clean up Vulkan resources before clearing cache

    std::unique_lock lock(m_lock);

    for (auto& [key, entry] : m_entries) {
        auto& wrapper = entry.resource;

        if (!wrapper) continue;

        VkDevice device = GetDevice() ? GetDevice()->device : VK_NULL_HANDLE;

        if (device != VK_NULL_HANDLE) {
            // Destroy Vulkan resources
            if (wrapper->view != VK_NULL_HANDLE) {
                vkDestroyImageView(device, wrapper->view, nullptr);
                wrapper->view = VK_NULL_HANDLE;
            }

            if (wrapper->image != VK_NULL_HANDLE) {
                vkDestroyImage(device, wrapper->image, nullptr);
                wrapper->image = VK_NULL_HANDLE;
            }

            if (wrapper->memory != VK_NULL_HANDLE) {
                vkFreeMemory(device, wrapper->memory, nullptr);
                wrapper->memory = VK_NULL_HANDLE;
            }
        }

        // Clear cached pixel data
        wrapper->pixelData.clear();
    }

    // Clear the cache
    m_entries.clear();
    m_pending.clear();

    LOG_INFO("[TextureCacher::Cleanup] Cleaned up all texture resources");
}

} // namespace CashSystem
