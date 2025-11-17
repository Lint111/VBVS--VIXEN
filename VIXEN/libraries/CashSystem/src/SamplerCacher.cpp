#include "SamplerCacher.h"
#include "MainCacher.h"
#include "VulkanDevice.h"
#include "VixenHash.h"
#include <sstream>
#include <stdexcept>
#include <vulkan/vulkan.h>
#include <iostream>
#include <fstream>
#include <filesystem>
#include <shared_mutex>

namespace CashSystem {

void SamplerCacher::Cleanup() {
    std::cout << "[SamplerCacher::Cleanup] Cleaning up " << m_entries.size() << " cached samplers" << std::endl;

    // Destroy all cached Vulkan samplers
    if (GetDevice()) {
        for (auto& [key, entry] : m_entries) {
            if (entry.resource) {
                if (entry.resource->resource != VK_NULL_HANDLE) {
                    std::cout << "[SamplerCacher::Cleanup] Destroying VkSampler: "
                              << reinterpret_cast<uint64_t>(entry.resource->resource) << std::endl;
                    vkDestroySampler(GetDevice()->device, entry.resource->resource, nullptr);
                    entry.resource->resource = VK_NULL_HANDLE;
                }
            }
        }
    }

    // Clear the cache entries after destroying resources
    Clear();

    std::cout << "[SamplerCacher::Cleanup] Cleanup complete" << std::endl;
}

std::shared_ptr<SamplerWrapper> SamplerCacher::GetOrCreate(const SamplerCreateParams& ci) {
    auto key = ComputeKey(ci);

    // Build descriptive name for logging
    std::ostringstream nameStream;
    nameStream << "Sampler(min=" << ci.minFilter
               << ",mag=" << ci.magFilter
               << ",addrU=" << ci.addressModeU
               << ",aniso=" << ci.maxAnisotropy << ")";
    std::string resourceName = nameStream.str();

    // Check cache first
    {
        std::shared_lock rlock(m_lock);
        auto it = m_entries.find(key);
        if (it != m_entries.end()) {
            std::cout << "[SamplerCacher::GetOrCreate] CACHE HIT for " << resourceName
                      << " (key=" << key << ", VkSampler="
                      << reinterpret_cast<uint64_t>(it->second.resource->resource) << ")" << std::endl;
            return it->second.resource;
        }
        auto pit = m_pending.find(key);
        if (pit != m_pending.end()) {
            std::cout << "[SamplerCacher::GetOrCreate] CACHE PENDING for " << resourceName
                      << " (key=" << key << "), waiting..." << std::endl;
            return pit->second.get();
        }
    }

    std::cout << "[SamplerCacher::GetOrCreate] CACHE MISS for " << resourceName
              << " (key=" << key << "), creating new sampler..." << std::endl;

    // Call parent implementation which will invoke Create()
    return TypedCacher<SamplerWrapper, SamplerCreateParams>::GetOrCreate(ci);
}

std::shared_ptr<SamplerWrapper> SamplerCacher::Create(const SamplerCreateParams& ci) {
    std::cout << "[SamplerCacher::Create] Creating new sampler" << std::endl;

    auto wrapper = std::make_shared<SamplerWrapper>();

    // Store metadata
    wrapper->minFilter = ci.minFilter;
    wrapper->magFilter = ci.magFilter;
    wrapper->addressModeU = ci.addressModeU;
    wrapper->addressModeV = ci.addressModeV;
    wrapper->addressModeW = ci.addressModeW;
    wrapper->maxAnisotropy = ci.maxAnisotropy;
    wrapper->compareEnable = ci.compareEnable;
    wrapper->compareOp = ci.compareOp;

    // Create Vulkan sampler
    VkSamplerCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    createInfo.magFilter = ci.magFilter;
    createInfo.minFilter = ci.minFilter;
    createInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    createInfo.addressModeU = ci.addressModeU;
    createInfo.addressModeV = ci.addressModeV;
    createInfo.addressModeW = ci.addressModeW;
    createInfo.mipLodBias = ci.mipLodBias;
    createInfo.anisotropyEnable = (ci.maxAnisotropy > 1.0f) ? VK_TRUE : VK_FALSE;
    createInfo.maxAnisotropy = ci.maxAnisotropy;
    createInfo.compareEnable = ci.compareEnable;
    createInfo.compareOp = ci.compareOp;
    createInfo.minLod = ci.minLod;
    createInfo.maxLod = ci.maxLod;
    createInfo.borderColor = ci.borderColor;
    createInfo.unnormalizedCoordinates = ci.unnormalizedCoordinates;

    VkResult result = vkCreateSampler(GetDevice()->device, &createInfo, nullptr, &wrapper->resource);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("SamplerCacher: Failed to create sampler (VkResult: " +
                                 std::to_string(result) + ")");
    }

    std::cout << "[SamplerCacher::Create] VkSampler created: "
              << reinterpret_cast<uint64_t>(wrapper->resource) << std::endl;

    return wrapper;
}

std::uint64_t SamplerCacher::ComputeKey(const SamplerCreateParams& ci) const {
    // Combine all parameters into a unique key string
    std::ostringstream keyStream;
    keyStream << static_cast<int>(ci.minFilter) << "|"
              << static_cast<int>(ci.magFilter) << "|"
              << static_cast<int>(ci.addressModeU) << "|"
              << static_cast<int>(ci.addressModeV) << "|"
              << static_cast<int>(ci.addressModeW) << "|"
              << ci.maxAnisotropy << "|"
              << static_cast<int>(ci.compareEnable) << "|"
              << static_cast<int>(ci.compareOp) << "|"
              << ci.mipLodBias << "|"
              << ci.minLod << "|"
              << ci.maxLod << "|"
              << static_cast<int>(ci.borderColor) << "|"
              << static_cast<int>(ci.unnormalizedCoordinates);

    // Use standard hash function (matching PipelineCacher pattern)
    const std::string keyString = keyStream.str();
    return std::hash<std::string>{}(keyString);
}

bool SamplerCacher::SerializeToFile(const std::filesystem::path& path) const {
    std::cout << "[SamplerCacher::SerializeToFile] Serializing " << m_entries.size()
              << " sampler configs to " << path << std::endl;

    std::ofstream ofs(path, std::ios::binary);
    if (!ofs) {
        std::cout << "[SamplerCacher::SerializeToFile] Failed to open file for writing" << std::endl;
        return false;
    }

    // Write entry count
    std::shared_lock rlock(m_lock);
    uint32_t count = static_cast<uint32_t>(m_entries.size());
    ofs.write(reinterpret_cast<const char*>(&count), sizeof(count));

    // Write each entry: key + metadata (not Vulkan handle)
    for (const auto& [key, entry] : m_entries) {
        ofs.write(reinterpret_cast<const char*>(&key), sizeof(key));

        // Serialize metadata for recreation
        const auto& w = entry.resource;
        ofs.write(reinterpret_cast<const char*>(&w->minFilter), sizeof(w->minFilter));
        ofs.write(reinterpret_cast<const char*>(&w->magFilter), sizeof(w->magFilter));
        ofs.write(reinterpret_cast<const char*>(&w->addressModeU), sizeof(w->addressModeU));
        ofs.write(reinterpret_cast<const char*>(&w->addressModeV), sizeof(w->addressModeV));
        ofs.write(reinterpret_cast<const char*>(&w->addressModeW), sizeof(w->addressModeW));
        ofs.write(reinterpret_cast<const char*>(&w->maxAnisotropy), sizeof(w->maxAnisotropy));
        ofs.write(reinterpret_cast<const char*>(&w->compareEnable), sizeof(w->compareEnable));
        ofs.write(reinterpret_cast<const char*>(&w->compareOp), sizeof(w->compareOp));
    }

    std::cout << "[SamplerCacher::SerializeToFile] Serialization complete" << std::endl;
    return true;
}

bool SamplerCacher::DeserializeFromFile(const std::filesystem::path& path, void* device) {
    try {
        if (!std::filesystem::exists(path)) {
            std::cout << "[SamplerCacher::DeserializeFromFile] Cache file doesn't exist: " << path << std::endl;
            return true;  // Not an error, just no cache to load
        }

        std::ifstream ifs(path, std::ios::binary);
        if (!ifs) {
            std::cerr << "[SamplerCacher::DeserializeFromFile] Failed to open file: " << path << std::endl;
            return false;
        }

        std::cout << "[SamplerCacher::DeserializeFromFile] Loading cache from " << path << std::endl;

        // Read entry count
        uint32_t count = 0;
        ifs.read(reinterpret_cast<char*>(&count), sizeof(count));

        std::cout << "[SamplerCacher::DeserializeFromFile] Loading " << count << " samplers" << std::endl;

        // Read each entry and recreate VkSampler
        for (uint32_t i = 0; i < count; ++i) {
            std::uint64_t key = 0;
            ifs.read(reinterpret_cast<char*>(&key), sizeof(key));

            // Read metadata fields
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

            // Create wrapper and recreate VkSampler (like ShaderModuleCacher does)
            auto wrapper = std::make_shared<SamplerWrapper>();
            wrapper->minFilter = minFilter;
            wrapper->magFilter = magFilter;
            wrapper->addressModeU = addressModeU;
            wrapper->addressModeV = addressModeV;
            wrapper->addressModeW = addressModeW;
            wrapper->maxAnisotropy = maxAnisotropy;
            wrapper->compareEnable = compareEnable;
            wrapper->compareOp = compareOp;

            // Recreate VkSampler if device available
            if (GetDevice()) {
                VkSamplerCreateInfo createInfo{};
                createInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
                createInfo.magFilter = magFilter;
                createInfo.minFilter = minFilter;
                createInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
                createInfo.addressModeU = addressModeU;
                createInfo.addressModeV = addressModeV;
                createInfo.addressModeW = addressModeW;
                createInfo.anisotropyEnable = (maxAnisotropy > 1.0f) ? VK_TRUE : VK_FALSE;
                createInfo.maxAnisotropy = maxAnisotropy;
                createInfo.compareEnable = compareEnable;
                createInfo.compareOp = compareOp;
                createInfo.minLod = 0.0f;
                createInfo.maxLod = VK_LOD_CLAMP_NONE;
                createInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
                createInfo.unnormalizedCoordinates = VK_FALSE;

                VkResult result = vkCreateSampler(GetDevice()->device, &createInfo, nullptr, &wrapper->resource);
                if (result != VK_SUCCESS) {
                    std::cerr << "[SamplerCacher::DeserializeFromFile] Failed to recreate VkSampler" << std::endl;
                    continue;  // Skip this entry
                }

                std::cout << "[SamplerCacher::DeserializeFromFile] Recreated VkSampler: "
                          << reinterpret_cast<uint64_t>(wrapper->resource) << std::endl;
            }

            // Insert into cache (KEY STEP - enables cache hits after deserialization)
            SamplerCreateParams ci{};
            ci.minFilter = minFilter;
            ci.magFilter = magFilter;
            ci.addressModeU = addressModeU;
            ci.addressModeV = addressModeV;
            ci.addressModeW = addressModeW;
            ci.maxAnisotropy = maxAnisotropy;
            ci.compareEnable = compareEnable;
            ci.compareOp = compareOp;

            CacheEntry entry;
            entry.key = key;
            entry.ci = ci;
            entry.resource = wrapper;

            std::unique_lock lock(m_lock);
            m_entries.emplace(key, std::move(entry));
        }

        ifs.close();
        std::cout << "[SamplerCacher::DeserializeFromFile] Successfully loaded " << m_entries.size()
                  << " samplers from cache" << std::endl;
        return true;

    } catch (const std::exception& e) {
        std::cerr << "[SamplerCacher::DeserializeFromFile] Exception: " << e.what() << std::endl;
        return false;
    }
}

} // namespace CashSystem
