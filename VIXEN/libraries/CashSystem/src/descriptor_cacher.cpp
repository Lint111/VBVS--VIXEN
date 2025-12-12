#include "pch.h"
#include "DescriptorCacher.h"
#include "VixenHash.h"
#include "DescriptorLayoutSpec.h"
#include <sstream>
#include <stdexcept>
#include <typeindex>
#include <map>

using namespace Vixen::Hash;

// Helper function to compute descriptor layout hash using project Hash library
static std::string ComputeLayoutHash_Helper(const ShaderManagement::DescriptorLayoutSpec* spec) {
    std::ostringstream hashStream;
    if (spec) {
        for (const auto& binding : spec->bindings) {
            hashStream << "|" << binding.binding << "|"
                       << binding.descriptorType << "|"
                       << binding.descriptorCount << "|"
                       << binding.stageFlags;
        }
    }
    std::string hashData = hashStream.str();
    // Use project-wide hash function
    return ComputeSHA256Hex(hashData.data(), hashData.size());
}

namespace CashSystem {

std::shared_ptr<DescriptorWrapper> DescriptorCacher::GetOrCreateDescriptors(
    const ShaderManagement::DescriptorLayoutSpec* layoutSpec,
    uint32_t maxSets)
{
    DescriptorCreateParams params;
    params.layoutSpec = layoutSpec;
    params.maxSets = maxSets;
    CalculateLayoutHash(layoutSpec, params.layoutHash);
    CalculatePoolSizes(layoutSpec, params.poolSizes);
    
    return GetOrCreate(params);
}

std::shared_ptr<DescriptorWrapper> DescriptorCacher::Create(const DescriptorCreateParams& ci) {
    auto wrapper = std::make_shared<DescriptorWrapper>();
    wrapper->layoutSpec = ci.layoutSpec;
    wrapper->maxSets = ci.maxSets;
    wrapper->layoutHash = ci.layoutHash;
    
    // Create descriptor set layout
    if (ci.layoutSpec) {
        auto vkBindings = ci.layoutSpec->ToVulkanBindings();
        
        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = static_cast<uint32_t>(vkBindings.size());
        layoutInfo.pBindings = vkBindings.data();
        
        VkResult result = vkCreateDescriptorSetLayout(/* device */ nullptr, &layoutInfo, nullptr, &wrapper->layout);
        if (result != VK_SUCCESS) {
            throw std::runtime_error("Failed to create descriptor set layout");
        }
    }
    
    // Create descriptor pool
    if (!ci.poolSizes.empty()) {
        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.poolSizeCount = static_cast<uint32_t>(ci.poolSizes.size());
        poolInfo.pPoolSizes = ci.poolSizes.data();
        poolInfo.maxSets = ci.maxSets;
        
        VkResult result = vkCreateDescriptorPool(/* device */ nullptr, &poolInfo, nullptr, &wrapper->pool);
        if (result != VK_SUCCESS) {
            throw std::runtime_error("Failed to create descriptor pool");
        }
    }
    
    // Allocate descriptor sets
    if (wrapper->layout && wrapper->pool) {
        std::vector<VkDescriptorSetLayout> layouts(ci.maxSets, wrapper->layout);
        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = wrapper->pool;
        allocInfo.descriptorSetCount = ci.maxSets;
        allocInfo.pSetLayouts = layouts.data();
        
        wrapper->sets.resize(ci.maxSets);
        VkResult result = vkAllocateDescriptorSets(/* device */ nullptr, &allocInfo, wrapper->sets.data());
        if (result != VK_SUCCESS) {
            throw std::runtime_error("Failed to allocate descriptor sets");
        }
    }
    
    return wrapper;
}

std::uint64_t DescriptorCacher::ComputeKey(const DescriptorCreateParams& ci) const {
    // Create unique key based on layout hash and pool configuration
    std::ostringstream keyStream;
    keyStream << ci.layoutHash << "|"
              << ci.maxSets << "|";
    
    for (const auto& poolSize : ci.poolSizes) {
        keyStream << poolSize.type << ":" << poolSize.descriptorCount << ",";
    }
    
    const std::string keyString = keyStream.str();
    return std::hash<std::string>{}(keyString);
}

void DescriptorCacher::CalculatePoolSizes(const ShaderManagement::DescriptorLayoutSpec* spec, 
                                         std::vector<VkDescriptorPoolSize>& poolSizes) const {
    poolSizes.clear();
    
    if (!spec) return;
    
    // Aggregate descriptor counts by type
    std::map<VkDescriptorType, uint32_t> typeCounts;
    
    for (const auto& binding : spec->bindings) {
        typeCounts[binding.descriptorType] += binding.descriptorCount;
    }
    
    // Convert to pool sizes
    for (const auto& [type, count] : typeCounts) {
        VkDescriptorPoolSize poolSize{};
        poolSize.type = type;
        poolSize.descriptorCount = count * 10; // Multiply by 10 for safety margin
        poolSizes.push_back(poolSize);
    }
}

void DescriptorCacher::CalculateLayoutHash(const ShaderManagement::DescriptorLayoutSpec* spec, 
                                          std::string& hash) const {
    if (!spec) {
        hash = "empty";
        return;
    }
    
    std::ostringstream hashStream;
    hashStream << spec->bindings.size();
    
    for (const auto& binding : spec->bindings) {
        hashStream << "|" << binding.binding << "|"
                   << binding.descriptorType << "|"
                   << binding.descriptorCount << "|"
                   << binding.stageFlags;
    }
    
    hash = ComputeLayoutHash_Helper(spec);
}

bool DescriptorCacher::SerializeToFile(const std::filesystem::path& path) const {
    // Descriptor layouts are device-independent metadata
    // Serialize layout specifications that can be recreated on any device

    std::ofstream file(path, std::ios::binary);
    if (!file.is_open()) {
        LOG_ERROR("SerializeToFile: Failed to open file: " + path.string());
        return false;
    }

    // Write version header
    uint32_t version = 1;
    file.write(reinterpret_cast<const char*>(&version), sizeof(version));

    // Write number of cached descriptors
    std::shared_lock lock(m_lock);
    uint32_t cacheSize = static_cast<uint32_t>(m_entries.size());
    file.write(reinterpret_cast<const char*>(&cacheSize), sizeof(cacheSize));

    // Serialize each descriptor layout metadata
    for (const auto& [key, entry] : m_entries) {
        const auto& wrapper = entry.resource;

        // Write layout hash
        uint32_t hashLen = static_cast<uint32_t>(wrapper->layoutHash.size());
        file.write(reinterpret_cast<const char*>(&hashLen), sizeof(hashLen));
        file.write(wrapper->layoutHash.data(), hashLen);

        // Write maxSets
        file.write(reinterpret_cast<const char*>(&wrapper->maxSets), sizeof(wrapper->maxSets));

        // Write pool sizes
        uint32_t poolSizeCount = static_cast<uint32_t>(wrapper->poolSizeCount);
        file.write(reinterpret_cast<const char*>(&poolSizeCount), sizeof(poolSizeCount));

        // Write layout spec metadata (if available)
        if (wrapper->layoutSpec) {
            uint32_t bindingCount = static_cast<uint32_t>(wrapper->layoutSpec->bindings.size());
            file.write(reinterpret_cast<const char*>(&bindingCount), sizeof(bindingCount));

            for (const auto& binding : wrapper->layoutSpec->bindings) {
                file.write(reinterpret_cast<const char*>(&binding.binding), sizeof(binding.binding));
                file.write(reinterpret_cast<const char*>(&binding.descriptorType), sizeof(binding.descriptorType));
                file.write(reinterpret_cast<const char*>(&binding.descriptorCount), sizeof(binding.descriptorCount));
                file.write(reinterpret_cast<const char*>(&binding.stageFlags), sizeof(binding.stageFlags));
            }
        } else {
            uint32_t bindingCount = 0;
            file.write(reinterpret_cast<const char*>(&bindingCount), sizeof(bindingCount));
        }
    }

    LOG_INFO("SerializeToFile: Serialized " + std::to_string(cacheSize) + " descriptor layouts to " + path.string());
    return true;
}

bool DescriptorCacher::DeserializeFromFile(const std::filesystem::path& path, void* device) {
    // Descriptor layouts must be recreated with device-specific Vulkan handles
    // This deserialization loads layout metadata to inform recreation

    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        LOG_ERROR("DeserializeFromFile: Failed to open file: " + path.string());
        return false;
    }

    // Read version header
    uint32_t version = 0;
    file.read(reinterpret_cast<char*>(&version), sizeof(version));
    if (version != 1) {
        LOG_ERROR("DeserializeFromFile: Unsupported version: " + std::to_string(version));
        return false;
    }

    // Read number of cached descriptors
    uint32_t cacheSize = 0;
    file.read(reinterpret_cast<char*>(&cacheSize), sizeof(cacheSize));

    LOG_INFO("DeserializeFromFile: Loading " + std::to_string(cacheSize) + " descriptor layouts from " + path.string());

    // Deserialize metadata (actual descriptor resources must be recreated on-demand)
    for (uint32_t i = 0; i < cacheSize; ++i) {
        // Read layout hash
        uint32_t hashLen = 0;
        file.read(reinterpret_cast<char*>(&hashLen), sizeof(hashLen));
        std::string layoutHash(hashLen, '\0');
        file.read(layoutHash.data(), hashLen);

        // Read maxSets
        uint32_t maxSets = 0;
        file.read(reinterpret_cast<char*>(&maxSets), sizeof(maxSets));

        // Read pool size count
        uint32_t poolSizeCount = 0;
        file.read(reinterpret_cast<char*>(&poolSizeCount), sizeof(poolSizeCount));

        // Read binding count
        uint32_t bindingCount = 0;
        file.read(reinterpret_cast<char*>(&bindingCount), sizeof(bindingCount));

        // Skip binding data (layout specs are owned by ShaderManagement)
        for (uint32_t j = 0; j < bindingCount; ++j) {
            uint32_t binding, descriptorType, descriptorCount, stageFlags;
            file.read(reinterpret_cast<char*>(&binding), sizeof(binding));
            file.read(reinterpret_cast<char*>(&descriptorType), sizeof(descriptorType));
            file.read(reinterpret_cast<char*>(&descriptorCount), sizeof(descriptorCount));
            file.read(reinterpret_cast<char*>(&stageFlags), sizeof(stageFlags));
        }

        // Note: Actual descriptor resources are recreated on-demand via GetOrCreate
        // This deserialization just validates the file format
    }

    LOG_INFO("DeserializeFromFile: Descriptor layout metadata validated (resources will be recreated on-demand)");

    (void)device;  // Device used when recreating resources on-demand
    return true;
}

} // namespace CashSystem