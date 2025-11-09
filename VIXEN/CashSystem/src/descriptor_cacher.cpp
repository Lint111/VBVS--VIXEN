#include "CashSystem/DescriptorCacher.h"
#include "VixenHash.h"
#include "ShaderManagement/DescriptorLayoutSpec.h"
#include <sstream>
#include <stdexcept>
#include <typeindex>
#include <map>

// Namespace alias for nested namespace
namespace VH = Vixen::Hash;

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
    return VH::ComputeSHA256Hex(hashData.data(), hashData.size());
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
    // TODO: Implement serialization of descriptor layout metadata
    (void)path;
    return true;
}

bool DescriptorCacher::DeserializeFromFile(const std::filesystem::path& path, void* device) {
    // TODO: Implement deserialization
    (void)path;
    (void)device;
    return true;
}

} // namespace CashSystem