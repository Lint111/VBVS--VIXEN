#include "CashSystem/TextureCacher.h"
#include "CashSystem/MainCacher.h"
#include "VulkanResources/VulkanDevice.h"
#include "TextureHandling/Loading/STBTextureLoader.h"
#include "Hash.h"
#include <sstream>
#include <stdexcept>
#include <vulkan/vulkan.h>
#include <iostream>
#include <fstream>
#include <filesystem>
#include <shared_mutex>

namespace CashSystem {

void TextureCacher::Cleanup() {
    std::cout << "[TextureCacher::Cleanup] Cleaning up " << m_entries.size() << " cached textures" << std::endl;

    // Destroy all cached Vulkan resources
    if (GetDevice()) {
        for (auto& [key, entry] : m_entries) {
            if (entry.resource) {
                auto& wrapper = entry.resource;

                // Destroy ImageView
                if (wrapper->view != VK_NULL_HANDLE) {
                    std::cout << "[TextureCacher::Cleanup] Destroying VkImageView: "
                              << reinterpret_cast<uint64_t>(wrapper->view) << std::endl;
                    vkDestroyImageView(GetDevice()->device, wrapper->view, nullptr);
                    wrapper->view = VK_NULL_HANDLE;
                }

                // Destroy Sampler
                if (wrapper->sampler != VK_NULL_HANDLE) {
                    std::cout << "[TextureCacher::Cleanup] Destroying VkSampler: "
                              << reinterpret_cast<uint64_t>(wrapper->sampler) << std::endl;
                    vkDestroySampler(GetDevice()->device, wrapper->sampler, nullptr);
                    wrapper->sampler = VK_NULL_HANDLE;
                }

                // Destroy Image
                if (wrapper->image != VK_NULL_HANDLE) {
                    std::cout << "[TextureCacher::Cleanup] Destroying VkImage: "
                              << reinterpret_cast<uint64_t>(wrapper->image) << std::endl;
                    vkDestroyImage(GetDevice()->device, wrapper->image, nullptr);
                    wrapper->image = VK_NULL_HANDLE;
                }

                // Free Memory
                if (wrapper->memory != VK_NULL_HANDLE) {
                    std::cout << "[TextureCacher::Cleanup] Freeing VkDeviceMemory: "
                              << reinterpret_cast<uint64_t>(wrapper->memory) << std::endl;
                    vkFreeMemory(GetDevice()->device, wrapper->memory, nullptr);
                    wrapper->memory = VK_NULL_HANDLE;
                }

                // Clear pixel data
                wrapper->pixelData.clear();
            }
        }
    }

    // Clear the cache entries after destroying resources
    Clear();

    std::cout << "[TextureCacher::Cleanup] Cleanup complete" << std::endl;
}

std::shared_ptr<TextureWrapper> TextureCacher::GetOrCreate(const TextureCreateParams& ci) {
    auto key = ComputeKey(ci);
    std::string resourceName = ci.filePath + " [" + std::to_string(ci.format) + "]";

    // Check cache first
    {
        std::shared_lock rlock(m_lock);
        auto it = m_entries.find(key);
        if (it != m_entries.end()) {
            std::cout << "[TextureCacher::GetOrCreate] CACHE HIT for " << resourceName
                      << " (key=" << key << ", VkImage="
                      << reinterpret_cast<uint64_t>(it->second.resource->image)
                      << ", pixelData=" << it->second.resource->pixelData.size() << " bytes)" << std::endl;
            return it->second.resource;
        }
        auto pit = m_pending.find(key);
        if (pit != m_pending.end()) {
            std::cout << "[TextureCacher::GetOrCreate] CACHE PENDING for " << resourceName
                      << " (key=" << key << "), waiting..." << std::endl;
            return pit->second.get();
        }
    }

    std::cout << "[TextureCacher::GetOrCreate] CACHE MISS for " << resourceName
              << " (key=" << key << "), creating new texture..." << std::endl;

    // Call parent implementation which will invoke Create()
    return TypedCacher<TextureWrapper, TextureCreateParams>::GetOrCreate(ci);
}

std::shared_ptr<TextureWrapper> TextureCacher::Create(const TextureCreateParams& ci) {
    std::cout << "[TextureCacher::Create] CACHE MISS - Creating new texture from " << ci.filePath << std::endl;

    auto wrapper = std::make_shared<TextureWrapper>();

    // Store metadata
    wrapper->filePath = ci.filePath;
    wrapper->format = ci.format;
    wrapper->generateMipmaps = ci.generateMipmaps;
    wrapper->minFilter = ci.minFilter;
    wrapper->magFilter = ci.magFilter;
    wrapper->addressModeU = ci.addressModeU;
    wrapper->addressModeV = ci.addressModeV;
    wrapper->addressModeW = ci.addressModeW;

    // Load texture from file using TextureLoader integration
    try {
        LoadTextureFromFile(ci, *wrapper);
    } catch (const std::exception& e) {
        throw std::runtime_error("TextureCacher: Failed to load texture from " + ci.filePath +
                                 " - " + std::string(e.what()));
    }

    std::cout << "[TextureCacher::Create] Texture created: VkImage="
              << reinterpret_cast<uint64_t>(wrapper->image)
              << ", VkImageView=" << reinterpret_cast<uint64_t>(wrapper->view)
              << ", size=" << wrapper->width << "x" << wrapper->height
              << ", pixelData=" << wrapper->pixelData.size() << " bytes" << std::endl;

    return wrapper;
}

std::uint64_t TextureCacher::ComputeKey(const TextureCreateParams& ci) const {
    // Combine all parameters into a unique key string
    std::ostringstream keyStream;
    keyStream << ci.filePath << "|"
              << static_cast<uint32_t>(ci.format) << "|"
              << ci.generateMipmaps << "|"
              << static_cast<uint32_t>(ci.minFilter) << "|"
              << static_cast<uint32_t>(ci.magFilter) << "|"
              << static_cast<uint32_t>(ci.addressModeU) << "|"
              << static_cast<uint32_t>(ci.addressModeV) << "|"
              << static_cast<uint32_t>(ci.addressModeW) << "|"
              << ci.fileChecksum;

    // Use standard hash function (matching template pattern)
    const std::string keyString = keyStream.str();
    return std::hash<std::string>{}(keyString);
}

std::string TextureCacher::ComputeFileChecksum(const std::string& filePath) const {
    try {
        std::ifstream file(filePath, std::ios::binary);
        if (!file.is_open()) {
            return "";
        }
        std::vector<char> buffer(std::istreambuf_iterator<char>(file), {});
        // For now, return simple hash - can enhance with SHA256 later
        return std::to_string(std::hash<std::string>{}(std::string(buffer.begin(), buffer.end())));
    } catch (const std::exception&) {
        return "";
    }
}

void TextureCacher::LoadTextureFromFile(const TextureCreateParams& ci, TextureWrapper& wrapper) {
    // Get device handle
    auto* devicePtr = GetDevice();
    if (!devicePtr || devicePtr->device == VK_NULL_HANDLE) {
        throw std::runtime_error("TextureCacher: Invalid device handle");
    }

    // Create temporary command pool for texture loading
    VkCommandPool commandPool = VK_NULL_HANDLE;
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = devicePtr->graphicsQueueIndex;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

    VkResult result = vkCreateCommandPool(devicePtr->device, &poolInfo, nullptr, &commandPool);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("TextureCacher: Failed to create command pool");
    }

    try {
        // Create STB texture loader
        auto textureLoader = std::make_unique<Vixen::TextureHandling::STBTextureLoader>(
            devicePtr,
            commandPool
        );

        // Configure load settings
        Vixen::TextureHandling::TextureLoadConfig config;
        config.uploadMode = Vixen::TextureHandling::TextureLoadConfig::UploadMode::Optimal;
        config.format = ci.format;

        // Load the texture
        Vixen::TextureHandling::TextureData textureData = textureLoader->Load(ci.filePath.c_str(), config);

        // Store Vulkan handles
        wrapper.image = textureData.image;
        wrapper.view = textureData.view;
        wrapper.sampler = textureData.sampler;
        wrapper.memory = textureData.mem;
        wrapper.width = textureData.textureWidth;
        wrapper.height = textureData.textureHeight;
        wrapper.mipLevels = textureData.minMapLevels;

        // Cache decoded pixel data - this is the KEY BENEFIT of TextureCacher
        // For now, we don't have direct access to pixel data from TextureLoader
        // TODO: Enhance TextureLoader to return pixel data or load it separately
        // For MVP, we'll store size info but not actual pixel data
        wrapper.pixelData.clear(); // Placeholder - enhance later

    } catch (const std::exception& e) {
        // Cleanup command pool on error
        vkDestroyCommandPool(devicePtr->device, commandPool, nullptr);
        throw;
    }

    // Cleanup command pool
    vkDestroyCommandPool(devicePtr->device, commandPool, nullptr);
}

std::shared_ptr<TextureWrapper> TextureCacher::GetOrCreateTexture(
    const std::string& filePath,
    VkFormat format,
    bool generateMipmaps,
    VkFilter minFilter,
    VkFilter magFilter,
    VkSamplerAddressMode addressMode)
{
    TextureCreateParams params;
    params.filePath = filePath;
    params.format = format;
    params.generateMipmaps = generateMipmaps;
    params.minFilter = minFilter;
    params.magFilter = magFilter;
    params.addressModeU = addressMode;
    params.addressModeV = addressMode;
    params.addressModeW = addressMode;
    params.fileChecksum = ComputeFileChecksum(filePath);

    return GetOrCreate(params);
}

bool TextureCacher::SerializeToFile(const std::filesystem::path& path) const {
    std::cout << "[TextureCacher::SerializeToFile] Serializing " << m_entries.size()
              << " texture configs to " << path << std::endl;

    std::ofstream ofs(path, std::ios::binary);
    if (!ofs) {
        std::cout << "[TextureCacher::SerializeToFile] Failed to open file for writing" << std::endl;
        return false;
    }

    // Write entry count
    std::shared_lock rlock(m_lock);
    uint32_t count = static_cast<uint32_t>(m_entries.size());
    ofs.write(reinterpret_cast<const char*>(&count), sizeof(count));

    // Write each entry: key + metadata (not Vulkan handles or pixel data)
    for (const auto& [key, entry] : m_entries) {
        ofs.write(reinterpret_cast<const char*>(&key), sizeof(key));

        // Serialize metadata for recreation
        const auto& w = entry.resource;

        // Write file path length and string
        uint32_t pathLen = static_cast<uint32_t>(w->filePath.size());
        ofs.write(reinterpret_cast<const char*>(&pathLen), sizeof(pathLen));
        ofs.write(w->filePath.data(), pathLen);

        // Write format and dimensions
        ofs.write(reinterpret_cast<const char*>(&w->format), sizeof(w->format));
        ofs.write(reinterpret_cast<const char*>(&w->width), sizeof(w->width));
        ofs.write(reinterpret_cast<const char*>(&w->height), sizeof(w->height));
        ofs.write(reinterpret_cast<const char*>(&w->mipLevels), sizeof(w->mipLevels));
    }

    std::cout << "[TextureCacher::SerializeToFile] Serialization complete" << std::endl;
    return true;
}

bool TextureCacher::DeserializeFromFile(const std::filesystem::path& path, void* device) {
    std::cout << "[TextureCacher::DeserializeFromFile] Deserializing from " << path << std::endl;

    if (!std::filesystem::exists(path)) {
        std::cout << "[TextureCacher::DeserializeFromFile] Cache file does not exist" << std::endl;
        return false;
    }

    std::ifstream ifs(path, std::ios::binary);
    if (!ifs) {
        std::cout << "[TextureCacher::DeserializeFromFile] Failed to open file for reading" << std::endl;
        return false;
    }

    // Read entry count
    uint32_t count = 0;
    ifs.read(reinterpret_cast<char*>(&count), sizeof(count));

    std::cout << "[TextureCacher::DeserializeFromFile] Loading " << count
              << " texture metadata entries" << std::endl;

    // Note: Only deserialize metadata. Vulkan handles and pixel data will be recreated on-demand
    // via GetOrCreate() when parameters match. This ensures driver compatibility.

    for (uint32_t i = 0; i < count; ++i) {
        std::uint64_t key;
        ifs.read(reinterpret_cast<char*>(&key), sizeof(key));

        // Read file path
        uint32_t pathLen;
        ifs.read(reinterpret_cast<char*>(&pathLen), sizeof(pathLen));
        std::string filePath(pathLen, '\0');
        ifs.read(&filePath[0], pathLen);

        // Read format and dimensions
        VkFormat format;
        uint32_t width, height, mipLevels;
        ifs.read(reinterpret_cast<char*>(&format), sizeof(format));
        ifs.read(reinterpret_cast<char*>(&width), sizeof(width));
        ifs.read(reinterpret_cast<char*>(&height), sizeof(height));
        ifs.read(reinterpret_cast<char*>(&mipLevels), sizeof(mipLevels));

        std::cout << "[TextureCacher::DeserializeFromFile] Loaded metadata for key " << key
                  << " (" << filePath << ", " << width << "x" << height << ")" << std::endl;
    }

    std::cout << "[TextureCacher::DeserializeFromFile] Deserialization complete (handles will be created on-demand)" << std::endl;
    return true;
}

} // namespace CashSystem
