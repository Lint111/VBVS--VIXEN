#include "CashSystem/SamplerCacher.h"
#include "CashSystem/MainCacher.h"
#include "VulkanResources/VulkanDevice.h"
#include "Hash.h"
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
    std::cout << "[SamplerCacher::DeserializeFromFile] Deserializing from " << path << std::endl;

    if (!std::filesystem::exists(path)) {
        std::cout << "[SamplerCacher::DeserializeFromFile] Cache file does not exist" << std::endl;
        return false;
    }

    std::ifstream ifs(path, std::ios::binary);
    if (!ifs) {
        std::cout << "[SamplerCacher::DeserializeFromFile] Failed to open file for reading" << std::endl;
        return false;
    }

    // Read entry count
    uint32_t count = 0;
    ifs.read(reinterpret_cast<char*>(&count), sizeof(count));

    std::cout << "[SamplerCacher::DeserializeFromFile] Loading " << count
              << " sampler metadata entries" << std::endl;

    // Note: Only deserialize metadata. Vulkan handles will be recreated on-demand
    // via GetOrCreate() when parameters match. This ensures driver compatibility.

    for (uint32_t i = 0; i < count; ++i) {
        std::uint64_t key;
        ifs.read(reinterpret_cast<char*>(&key), sizeof(key));

        // Read metadata fields (but don't recreate samplers yet)
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

        std::cout << "[SamplerCacher::DeserializeFromFile] Loaded metadata for key " << key
                  << " (min=" << minFilter << ", mag=" << magFilter << ")" << std::endl;
    }

    std::cout << "[SamplerCacher::DeserializeFromFile] Deserialization complete (handles will be created on-demand)" << std::endl;
    return true;
}

} // namespace CashSystem
