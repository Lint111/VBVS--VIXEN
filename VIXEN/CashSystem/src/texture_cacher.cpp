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

                // Sampler is managed by SamplerCacher (composition pattern)
                // No need to destroy it here - just release the shared_ptr reference

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

    // Store sampler reference from SamplerCacher (composition pattern)
    // Note: samplerParams is only used during serialization/key computation
    if (!ci.samplerWrapper) {
        throw std::runtime_error("TextureCacher: samplerWrapper is required for Create()");
    }
    wrapper->samplerWrapper = ci.samplerWrapper;

    // Try to load texture from file using TextureLoader integration
    try {
        LoadTextureFromFile(ci, *wrapper);

        std::cout << "[TextureCacher::Create] Texture created: VkImage="
                  << reinterpret_cast<uint64_t>(wrapper->image)
                  << ", VkImageView=" << reinterpret_cast<uint64_t>(wrapper->view)
                  << ", size=" << wrapper->width << "x" << wrapper->height
                  << ", pixelData=" << wrapper->pixelData.size() << " bytes" << std::endl;
    } catch (const std::exception& e) {
        // Fallback: Create a default 1x1 magenta texture to indicate missing/failed texture
        std::cout << "[TextureCacher::Create] WARNING: Failed to load texture from " << ci.filePath
                  << " - " << e.what() << std::endl;
        std::cout << "[TextureCacher::Create] Using fallback: Creating 1x1 magenta placeholder texture" << std::endl;

        try {
            CreateFallbackTexture(ci, *wrapper);

            std::cout << "[TextureCacher::Create] Fallback texture created: VkImage="
                      << reinterpret_cast<uint64_t>(wrapper->image)
                      << ", VkImageView=" << reinterpret_cast<uint64_t>(wrapper->view)
                      << ", size=" << wrapper->width << "x" << wrapper->height << std::endl;
        } catch (const std::exception& fallbackError) {
            // If even fallback fails, this is a critical error
            throw std::runtime_error("TextureCacher: CRITICAL - Both texture loading and fallback failed for " +
                                     ci.filePath + " - " + std::string(fallbackError.what()));
        }
    }

    return wrapper;
}

std::uint64_t TextureCacher::ComputeKey(const TextureCreateParams& ci) const {
    // Combine texture-specific parameters into a unique key string
    // Note: Sampler is managed separately, so we don't include sampler params in texture key
    std::ostringstream keyStream;
    keyStream << ci.filePath << "|"
              << static_cast<uint32_t>(ci.format) << "|"
              << ci.generateMipmaps << "|"
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
        wrapper.memory = textureData.mem;
        wrapper.width = textureData.textureWidth;
        wrapper.height = textureData.textureHeight;
        wrapper.mipLevels = textureData.minMapLevels;

        // Destroy the sampler created by TextureLoader - we use SamplerCacher's sampler instead
        if (textureData.sampler != VK_NULL_HANDLE) {
            vkDestroySampler(devicePtr->device, textureData.sampler, nullptr);
        }

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

void TextureCacher::CreateFallbackTexture(const TextureCreateParams& ci, TextureWrapper& wrapper) {
    // Get device handle
    auto* devicePtr = GetDevice();
    if (!devicePtr || devicePtr->device == VK_NULL_HANDLE) {
        throw std::runtime_error("TextureCacher: Invalid device handle for fallback");
    }

    // Create a 1x1 magenta texture (R=255, G=0, B=255, A=255)
    wrapper.width = 1;
    wrapper.height = 1;
    wrapper.mipLevels = 1;

    // Create image
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = wrapper.width;
    imageInfo.extent.height = wrapper.height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = wrapper.mipLevels;
    imageInfo.arrayLayers = 1;
    imageInfo.format = ci.format;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkResult result = vkCreateImage(devicePtr->device, &imageInfo, nullptr, &wrapper.image);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("TextureCacher: Failed to create fallback image");
    }

    // Allocate memory
    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(devicePtr->device, wrapper.image, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;

    // Find suitable memory type using device's cached memory properties
    uint32_t memoryTypeIndex = UINT32_MAX;
    VkMemoryPropertyFlags properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    for (uint32_t i = 0; i < devicePtr->gpuMemoryProperties.memoryTypeCount; i++) {
        if ((memRequirements.memoryTypeBits & (1 << i)) &&
            (devicePtr->gpuMemoryProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            memoryTypeIndex = i;
            break;
        }
    }

    if (memoryTypeIndex == UINT32_MAX) {
        vkDestroyImage(devicePtr->device, wrapper.image, nullptr);
        throw std::runtime_error("TextureCacher: Failed to find suitable memory type for fallback");
    }

    allocInfo.memoryTypeIndex = memoryTypeIndex;

    result = vkAllocateMemory(devicePtr->device, &allocInfo, nullptr, &wrapper.memory);
    if (result != VK_SUCCESS) {
        vkDestroyImage(devicePtr->device, wrapper.image, nullptr);
        throw std::runtime_error("TextureCacher: Failed to allocate memory for fallback");
    }

    vkBindImageMemory(devicePtr->device, wrapper.image, wrapper.memory, 0);

    // Create image view
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = wrapper.image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = ci.format;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = wrapper.mipLevels;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    result = vkCreateImageView(devicePtr->device, &viewInfo, nullptr, &wrapper.view);
    if (result != VK_SUCCESS) {
        vkFreeMemory(devicePtr->device, wrapper.memory, nullptr);
        vkDestroyImage(devicePtr->device, wrapper.image, nullptr);
        throw std::runtime_error("TextureCacher: Failed to create image view for fallback");
    }

    // Store magenta pixel data (1x1 RGBA)
    wrapper.pixelData = {255, 0, 255, 255};  // Magenta
}

std::shared_ptr<TextureWrapper> TextureCacher::GetOrCreateTexture(
    const std::string& filePath,
    std::shared_ptr<SamplerWrapper> samplerWrapper,
    VkFormat format,
    bool generateMipmaps)
{
    TextureCreateParams params;
    params.filePath = filePath;
    params.format = format;
    params.generateMipmaps = generateMipmaps;
    params.samplerWrapper = samplerWrapper;
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

    // Write each entry: key + metadata + pixel data (like ShaderModuleCacher saves SPIR-V)
    for (const auto& [key, entry] : m_entries) {
        if (!entry.resource || entry.resource->pixelData.empty()) {
            continue;  // Skip invalid entries
        }

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
        ofs.write(reinterpret_cast<const char*>(&w->arrayLayers), sizeof(w->arrayLayers));

        // Write pixel data (KEY ADDITION - like SPIR-V in ShaderModuleCacher)
        uint32_t pixelDataSize = static_cast<uint32_t>(w->pixelData.size());
        ofs.write(reinterpret_cast<const char*>(&pixelDataSize), sizeof(pixelDataSize));
        ofs.write(reinterpret_cast<const char*>(w->pixelData.data()), pixelDataSize);

        // Write generateMipmaps flag
        ofs.write(reinterpret_cast<const char*>(&w->generateMipmaps), sizeof(w->generateMipmaps));

        // Write sampler parameters (enables deserialization with cache hits)
        const auto& sampler = w->samplerWrapper;
        ofs.write(reinterpret_cast<const char*>(&sampler->minFilter), sizeof(sampler->minFilter));
        ofs.write(reinterpret_cast<const char*>(&sampler->magFilter), sizeof(sampler->magFilter));
        ofs.write(reinterpret_cast<const char*>(&sampler->addressModeU), sizeof(sampler->addressModeU));
        ofs.write(reinterpret_cast<const char*>(&sampler->addressModeV), sizeof(sampler->addressModeV));
        ofs.write(reinterpret_cast<const char*>(&sampler->addressModeW), sizeof(sampler->addressModeW));
        ofs.write(reinterpret_cast<const char*>(&sampler->maxAnisotropy), sizeof(sampler->maxAnisotropy));
        ofs.write(reinterpret_cast<const char*>(&sampler->compareEnable), sizeof(sampler->compareEnable));
        ofs.write(reinterpret_cast<const char*>(&sampler->compareOp), sizeof(sampler->compareOp));
    }

    std::cout << "[TextureCacher::SerializeToFile] Serialization complete" << std::endl;
    return true;
}

bool TextureCacher::DeserializeFromFile(const std::filesystem::path& path, void* device) {
    try {
        if (!std::filesystem::exists(path)) {
            std::cout << "[TextureCacher::DeserializeFromFile] Cache file doesn't exist: " << path << std::endl;
            return true;  // Not an error, just no cache to load
        }

        std::ifstream ifs(path, std::ios::binary);
        if (!ifs) {
            std::cerr << "[TextureCacher::DeserializeFromFile] Failed to open file: " << path << std::endl;
            return false;
        }

        std::cout << "[TextureCacher::DeserializeFromFile] Loading cache from " << path << std::endl;

        // Read entry count
        uint32_t count = 0;
        ifs.read(reinterpret_cast<char*>(&count), sizeof(count));

        std::cout << "[TextureCacher::DeserializeFromFile] Loading " << count << " textures" << std::endl;

        // Read each entry
        for (uint32_t i = 0; i < count; ++i) {
            // Read cache key
            std::uint64_t key = 0;
            ifs.read(reinterpret_cast<char*>(&key), sizeof(key));

            // Read file path
            uint32_t pathLen = 0;
            ifs.read(reinterpret_cast<char*>(&pathLen), sizeof(pathLen));
            std::string filePath(pathLen, '\0');
            ifs.read(&filePath[0], pathLen);

            // Read format and dimensions
            VkFormat format;
            uint32_t width, height, mipLevels, arrayLayers;
            ifs.read(reinterpret_cast<char*>(&format), sizeof(format));
            ifs.read(reinterpret_cast<char*>(&width), sizeof(width));
            ifs.read(reinterpret_cast<char*>(&height), sizeof(height));
            ifs.read(reinterpret_cast<char*>(&mipLevels), sizeof(mipLevels));
            ifs.read(reinterpret_cast<char*>(&arrayLayers), sizeof(arrayLayers));

            // Read pixel data
            uint32_t pixelDataSize = 0;
            ifs.read(reinterpret_cast<char*>(&pixelDataSize), sizeof(pixelDataSize));
            std::vector<uint8_t> pixelData(pixelDataSize);
            ifs.read(reinterpret_cast<char*>(pixelData.data()), pixelDataSize);

            // Read generateMipmaps flag
            bool generateMipmaps;
            ifs.read(reinterpret_cast<char*>(&generateMipmaps), sizeof(generateMipmaps));

            // Read sampler parameters
            VkFilter minFilter, magFilter;
            VkSamplerAddressMode addressModeU, addressModeV, addressModeW;
            float maxAnisotropy;
            VkBool32 compareEnable;
            VkCompareOp compareOp;

            ifs.read(reinterpret_cast<char*>(&minFilter), sizeof(minFilter));
            ifs.read(reinterpret_cast<char*>(&magFilter), sizeof(magFilter));
            ifs.read(reinterpret_cast<char*>(&addressModeU), sizeof(addressModeU));
            ifs.read(reinterpret_cast<char*>(&addressModeV), sizeof(addressModeV));
            ifs.read(reinterpret_cast<char*>(&addressModeW), sizeof(addressModeW));
            ifs.read(reinterpret_cast<char*>(&maxAnisotropy), sizeof(maxAnisotropy));
            ifs.read(reinterpret_cast<char*>(&compareEnable), sizeof(compareEnable));
            ifs.read(reinterpret_cast<char*>(&compareOp), sizeof(compareOp));

            std::cout << "[TextureCacher::DeserializeFromFile] Loaded cached data for " << filePath
                      << " (" << width << "x" << height << ", " << pixelDataSize << " bytes, sampler params stored)" << std::endl;

            // DESIGN NOTE: Unlike SamplerCacher/ShaderModuleCacher, TextureCacher cannot fully
            // deserialize because it requires a SamplerWrapper (composition dependency).
            //
            // We serialize sampler *params* here, but we need a MainCacher reference to get
            // SamplerCacher and create the wrapper. TypedCacher base class doesn't provide this.
            //
            // Solution: Serialization stores pixel data + sampler params for potential optimizations:
            // - Future enhancement: Use cached pixel data instead of reloading from file
            // - Validation: Check if file changed since last cache
            // - Metrics: Track cache size and hit rates
            //
            // For now, texture loading on startup will reload from file (cache miss expected).
        }

        ifs.close();
        std::cout << "[TextureCacher::DeserializeFromFile] Loaded " << count
                  << " texture metadata entries (Vulkan resources not recreated - see design note above)" << std::endl;
        return true;

    } catch (const std::exception& e) {
        std::cerr << "[TextureCacher::DeserializeFromFile] Exception: " << e.what() << std::endl;
        return false;
    }
}

} // namespace CashSystem
