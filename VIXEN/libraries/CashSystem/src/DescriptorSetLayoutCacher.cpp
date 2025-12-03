#include "pch.h"
#include "DescriptorSetLayoutCacher.h"
#include "VulkanDevice.h"  // For Vixen::Vulkan::Resources::VulkanDevice
#include <iostream>
#include <sstream>
#include <functional>

namespace CashSystem {

// ===== DescriptorSetLayoutCacher Implementation =====

std::shared_ptr<DescriptorSetLayoutWrapper> DescriptorSetLayoutCacher::GetOrCreate(
    const DescriptorSetLayoutCreateParams& ci
) {
    auto key = ComputeKey(ci);

    // Check cache first
    {
        std::shared_lock rlock(m_lock);
        auto it = m_entries.find(key);
        if (it != m_entries.end()) {
            std::cout << "[DescriptorSetLayoutCacher] CACHE HIT for layout: " << ci.layoutKey << std::endl;
            return it->second.resource;
        }
    }

    std::cout << "[DescriptorSetLayoutCacher] CACHE MISS - Creating new layout for key: "
              << ci.layoutKey << std::endl;

    // Call parent implementation which will invoke Create()
    return TypedCacher<DescriptorSetLayoutWrapper, DescriptorSetLayoutCreateParams>::GetOrCreate(ci);
}

std::shared_ptr<DescriptorSetLayoutWrapper> DescriptorSetLayoutCacher::Create(
    const DescriptorSetLayoutCreateParams& ci
) {
    if (!ci.device) {
        std::cerr << "[DescriptorSetLayoutCacher] Error: No device provided" << std::endl;
        return nullptr;
    }

    auto wrapper = std::make_shared<DescriptorSetLayoutWrapper>();
    wrapper->layoutKey = ci.layoutKey;

    // Extract bindings (from bundle or manual)
    if (ci.shaderBundle) {
        // Mode 1: Extract from ShaderDataBundle
        wrapper->bindings = ExtractBindingsFromBundle(*ci.shaderBundle, ci.descriptorSetIndex);
        std::cout << "[DescriptorSetLayoutCacher] Extracted " << wrapper->bindings.size()
                  << " bindings from shader bundle (set " << ci.descriptorSetIndex << ")" << std::endl;
    } else {
        // Mode 2: Use manual bindings
        wrapper->bindings = ci.manualBindings;
        std::cout << "[DescriptorSetLayoutCacher] Using " << wrapper->bindings.size()
                  << " manual bindings" << std::endl;
    }

    if (wrapper->bindings.empty()) {
        std::cout << "[DescriptorSetLayoutCacher] Warning: No bindings found - creating empty layout" << std::endl;
    }

    // Create VkDescriptorSetLayout
    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(wrapper->bindings.size());
    layoutInfo.pBindings = wrapper->bindings.empty() ? nullptr : wrapper->bindings.data();

    VkResult result = vkCreateDescriptorSetLayout(
        ci.device->device,
        &layoutInfo,
        nullptr,
        &wrapper->layout
    );

    if (result != VK_SUCCESS) {
        std::cerr << "[DescriptorSetLayoutCacher] Failed to create descriptor set layout: "
                  << result << std::endl;
        return nullptr;
    }

    std::cout << "[DescriptorSetLayoutCacher] Successfully created VkDescriptorSetLayout" << std::endl;
    return wrapper;
}

std::uint64_t DescriptorSetLayoutCacher::ComputeKey(const DescriptorSetLayoutCreateParams& ci) const {
    // Use layoutKey directly (typically descriptorInterfaceHash from bundle)
    // This enables content-based caching - same layout = same cache entry
    std::hash<std::string> hasher;
    return hasher(ci.layoutKey);
}

void DescriptorSetLayoutCacher::Cleanup() {
    std::cout << "[DescriptorSetLayoutCacher] Cleanup: Destroying " << m_entries.size()
              << " descriptor set layouts" << std::endl;

    for (auto& [key, entry] : m_entries) {
        if (entry.resource && entry.resource->layout != VK_NULL_HANDLE) {
            // Note: We don't have direct access to VkDevice here
            // In production, store device pointer or use deferred cleanup
            std::cout << "[DescriptorSetLayoutCacher] Warning: Skipping VkDescriptorSetLayout cleanup - device required" << std::endl;
        }
    }

    Clear();  // Use base class Clear()
}

bool DescriptorSetLayoutCacher::SerializeToFile(const std::filesystem::path& path) const {
    // Descriptor layouts are created from reflection data, no need to serialize
    return false;
}

bool DescriptorSetLayoutCacher::DeserializeFromFile(const std::filesystem::path& path, void* device) {
    // Descriptor layouts are created from reflection data, no need to deserialize
    return false;
}

std::vector<VkDescriptorSetLayoutBinding> DescriptorSetLayoutCacher::ExtractBindingsFromBundle(
    const ShaderManagement::ShaderDataBundle& bundle,
    uint32_t setIndex
) const {
    std::vector<VkDescriptorSetLayoutBinding> vkBindings;

    if (!bundle.reflectionData) {
        std::cerr << "[DescriptorSetLayoutCacher] Error: No reflection data in bundle" << std::endl;
        return vkBindings;
    }

    // Get bindings for the requested set
    auto descriptorSet = bundle.GetDescriptorSet(setIndex);
    if (descriptorSet.empty()) {
        std::cout << "[DescriptorSetLayoutCacher] Warning: No bindings found for set "
                  << setIndex << std::endl;
        return vkBindings;
    }

    // Convert SPIR-V descriptor bindings to Vulkan bindings
    vkBindings.reserve(descriptorSet.size());
    for (const auto& spirvBinding : descriptorSet) {
        VkDescriptorSetLayoutBinding vkBinding{};
        vkBinding.binding = spirvBinding.binding;
        vkBinding.descriptorType = spirvBinding.descriptorType;
        vkBinding.descriptorCount = spirvBinding.descriptorCount;
        vkBinding.stageFlags = spirvBinding.stageFlags;
        vkBinding.pImmutableSamplers = nullptr;

        vkBindings.push_back(vkBinding);

        std::cout << "[DescriptorSetLayoutCacher]   Binding " << vkBinding.binding
                  << ": " << spirvBinding.name
                  << " (type=" << vkBinding.descriptorType
                  << ", count=" << vkBinding.descriptorCount
                  << ", stages=0x" << std::hex << vkBinding.stageFlags << std::dec << ")"
                  << std::endl;
    }

    return vkBindings;
}

// ===== Helper Functions =====

VkDescriptorSetLayout BuildDescriptorSetLayoutFromReflection(
    ::Vixen::Vulkan::Resources::VulkanDevice* device,
    const ShaderManagement::ShaderDataBundle& bundle,
    uint32_t setIndex
) {
    if (!device) {
        std::cerr << "[BuildDescriptorSetLayoutFromReflection] Error: No device provided" << std::endl;
        return VK_NULL_HANDLE;
    }

    if (!bundle.reflectionData) {
        std::cerr << "[BuildDescriptorSetLayoutFromReflection] Error: No reflection data" << std::endl;
        return VK_NULL_HANDLE;
    }

    // Extract bindings
    auto descriptorSet = bundle.GetDescriptorSet(setIndex);
    std::vector<VkDescriptorSetLayoutBinding> vkBindings;
    vkBindings.reserve(descriptorSet.size());

    for (const auto& spirvBinding : descriptorSet) {
        VkDescriptorSetLayoutBinding vkBinding{};
        vkBinding.binding = spirvBinding.binding;
        vkBinding.descriptorType = spirvBinding.descriptorType;
        vkBinding.descriptorCount = spirvBinding.descriptorCount;
        vkBinding.stageFlags = spirvBinding.stageFlags;
        vkBinding.pImmutableSamplers = nullptr;
        vkBindings.push_back(vkBinding);
    }

    // Create layout
    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(vkBindings.size());
    layoutInfo.pBindings = vkBindings.empty() ? nullptr : vkBindings.data();

    VkDescriptorSetLayout layout = VK_NULL_HANDLE;
    VkResult result = vkCreateDescriptorSetLayout(device->device, &layoutInfo, nullptr, &layout);

    if (result != VK_SUCCESS) {
        std::cerr << "[BuildDescriptorSetLayoutFromReflection] Failed to create layout: "
                  << result << std::endl;
        return VK_NULL_HANDLE;
    }

    return layout;
}

std::vector<VkPushConstantRange> ExtractPushConstantsFromReflection(
    const ShaderManagement::ShaderDataBundle& bundle
) {
    std::vector<VkPushConstantRange> vkRanges;

    if (!bundle.reflectionData) {
        return vkRanges;
    }

    const auto& spirvRanges = bundle.GetPushConstants();
    vkRanges.reserve(spirvRanges.size());

    for (const auto& spirvRange : spirvRanges) {
        VkPushConstantRange vkRange{};
        vkRange.stageFlags = spirvRange.stageFlags;
        vkRange.offset = spirvRange.offset;
        vkRange.size = spirvRange.size;
        vkRanges.push_back(vkRange);

        std::cout << "[ExtractPushConstantsFromReflection] Push constant: "
                  << spirvRange.name
                  << " (offset=" << vkRange.offset
                  << ", size=" << vkRange.size
                  << ", stages=0x" << std::hex << vkRange.stageFlags << std::dec << ")"
                  << std::endl;
    }

    return vkRanges;
}

std::vector<VkDescriptorPoolSize> CalculateDescriptorPoolSizes(
    const ShaderManagement::ShaderDataBundle& bundle,
    uint32_t setIndex,
    uint32_t maxSets
) {
    std::vector<VkDescriptorPoolSize> poolSizes;

    if (!bundle.reflectionData) {
        std::cerr << "[CalculateDescriptorPoolSizes] Error: No reflection data in bundle" << std::endl;
        return poolSizes;
    }

    // Get bindings for the requested set
    auto descriptorSet = bundle.GetDescriptorSet(setIndex);
    if (descriptorSet.empty()) {
        std::cout << "[CalculateDescriptorPoolSizes] Warning: No bindings found for set "
                  << setIndex << std::endl;
        return poolSizes;
    }

    // Count descriptor types
    std::unordered_map<VkDescriptorType, uint32_t> descriptorTypeCounts;
    for (const auto& spirvBinding : descriptorSet) {
        descriptorTypeCounts[spirvBinding.descriptorType] += spirvBinding.descriptorCount;
    }

    // Create pool sizes (multiply by maxSets for multiple descriptor set allocation)
    poolSizes.reserve(descriptorTypeCounts.size());
    for (const auto& [type, count] : descriptorTypeCounts) {
        VkDescriptorPoolSize poolSize{};
        poolSize.type = type;
        poolSize.descriptorCount = count * maxSets;  // Scale by number of sets
        poolSizes.push_back(poolSize);

        std::cout << "[CalculateDescriptorPoolSizes] Pool size: type=" << type
                  << ", count=" << poolSize.descriptorCount
                  << " (per-set=" << count << ", maxSets=" << maxSets << ")"
                  << std::endl;
    }

    return poolSizes;
}

} // namespace CashSystem
