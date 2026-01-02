#include "pch.h"
#include "SamplerCacher.h"
#include "CacheKeyHasher.h"
#include "MainCacher.h"
#include "VulkanDevice.h"
#include <sstream>
#include <stdexcept>
#include <vulkan/vulkan.h>
#include <fstream>
#include <filesystem>
#include <shared_mutex>

namespace CashSystem {

void SamplerCacher::Cleanup() {
    LOG_INFO("Cleaning up " + std::to_string(m_entries.size()) + " cached samplers");

    // Destroy all cached Vulkan samplers
    if (GetDevice()) {
        for (auto& [key, entry] : m_entries) {
            if (entry.resource) {
                if (entry.resource->resource != VK_NULL_HANDLE) {
                    LOG_DEBUG("Destroying VkSampler: " + std::to_string(reinterpret_cast<uint64_t>(entry.resource->resource)));
                    vkDestroySampler(GetDevice()->device, entry.resource->resource, nullptr);
                    entry.resource->resource = VK_NULL_HANDLE;
                }
            }
        }
    }

    // Clear the cache entries after destroying resources
    Clear();

    LOG_INFO("Cleanup complete");
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
            LOG_DEBUG("CACHE HIT for " + resourceName + " (key=" + std::to_string(key) + ")");
            return it->second.resource;
        }
        auto pit = m_pending.find(key);
        if (pit != m_pending.end()) {
            LOG_DEBUG("CACHE PENDING for " + resourceName + " (key=" + std::to_string(key) + "), waiting...");
            return pit->second.get();
        }
    }

    LOG_DEBUG("CACHE MISS for " + resourceName + " (key=" + std::to_string(key) + "), creating new sampler...");

    // Call parent implementation which will invoke Create()
    return TypedCacher<SamplerWrapper, SamplerCreateParams>::GetOrCreate(ci);
}

std::shared_ptr<SamplerWrapper> SamplerCacher::Create(const SamplerCreateParams& ci) {
    LOG_DEBUG("Creating new sampler");

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

    LOG_DEBUG("VkSampler created: " + std::to_string(reinterpret_cast<uint64_t>(wrapper->resource)));

    return wrapper;
}

std::uint64_t SamplerCacher::ComputeKey(const SamplerCreateParams& ci) const {
    // Use CacheKeyHasher for deterministic, binary hashing
    // Note: floats are quantized to avoid floating-point instability
    CacheKeyHasher hasher;
    hasher.Add(static_cast<int32_t>(ci.minFilter))
          .Add(static_cast<int32_t>(ci.magFilter))
          .Add(static_cast<int32_t>(ci.addressModeU))
          .Add(static_cast<int32_t>(ci.addressModeV))
          .Add(static_cast<int32_t>(ci.addressModeW))
          .Add(static_cast<uint32_t>(ci.maxAnisotropy * 100.0f))  // Quantize float
          .Add(static_cast<int32_t>(ci.compareEnable))
          .Add(static_cast<int32_t>(ci.compareOp))
          .Add(static_cast<int32_t>(ci.mipLodBias * 1000.0f))     // Quantize float
          .Add(static_cast<int32_t>(ci.minLod * 1000.0f))         // Quantize float
          .Add(static_cast<int32_t>(ci.maxLod * 1000.0f))         // Quantize float
          .Add(static_cast<int32_t>(ci.borderColor))
          .Add(static_cast<int32_t>(ci.unnormalizedCoordinates));

    return hasher.Finalize();
}

bool SamplerCacher::SerializeToFile(const std::filesystem::path& path) const {
    LOG_INFO("SerializeToFile: Serializing " + std::to_string(m_entries.size()) + " sampler configs to " + path.string());

    std::ofstream ofs(path, std::ios::binary);
    if (!ofs) {
        LOG_ERROR("SerializeToFile: Failed to open file for writing");
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

    LOG_INFO("SerializeToFile: Serialization complete");
    return true;
}

bool SamplerCacher::DeserializeFromFile(const std::filesystem::path& path, void* device) {
    try {
        if (!std::filesystem::exists(path)) {
            LOG_INFO("DeserializeFromFile: Cache file doesn't exist: " + path.string());
            return true;  // Not an error, just no cache to load
        }

        std::ifstream ifs(path, std::ios::binary);
        if (!ifs) {
            LOG_ERROR("DeserializeFromFile: Failed to open file: " + path.string());
            return false;
        }

        LOG_INFO("DeserializeFromFile: Loading cache from " + path.string());

        // Read entry count
        uint32_t count = 0;
        ifs.read(reinterpret_cast<char*>(&count), sizeof(count));

        LOG_INFO("DeserializeFromFile: Loading " + std::to_string(count) + " samplers");

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
                    LOG_ERROR("DeserializeFromFile: Failed to recreate VkSampler");
                    continue;  // Skip this entry
                }

                LOG_DEBUG("DeserializeFromFile: Recreated VkSampler: " + std::to_string(reinterpret_cast<uint64_t>(wrapper->resource)));
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
        LOG_INFO("DeserializeFromFile: Successfully loaded " + std::to_string(m_entries.size()) + " samplers from cache");
        return true;

    } catch (const std::exception& e) {
        LOG_ERROR("DeserializeFromFile: Exception: " + std::string(e.what()));
        return false;
    }
}

} // namespace CashSystem
