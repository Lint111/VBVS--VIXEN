#include "pch.h"
#include "DescriptorSetLayoutCacher.h"
#include "VulkanDevice.h"  // For Vixen::Vulkan::Resources::VulkanDevice
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
            LOG_DEBUG("CACHE HIT for layout: " + ci.layoutKey);
            return it->second.resource;
        }
    }

    LOG_DEBUG("CACHE MISS - Creating new layout for key: " + ci.layoutKey);

    // Call parent implementation which will invoke Create()
    return TypedCacher<DescriptorSetLayoutWrapper, DescriptorSetLayoutCreateParams>::GetOrCreate(ci);
}

std::shared_ptr<DescriptorSetLayoutWrapper> DescriptorSetLayoutCacher::Create(
    const DescriptorSetLayoutCreateParams& ci
) {
    if (!ci.device) {
        LOG_ERROR("Error: No device provided");
        return nullptr;
    }

    auto wrapper = std::make_shared<DescriptorSetLayoutWrapper>();
    wrapper->layoutKey = ci.layoutKey;

    // Extract bindings (from bundle or manual)
    if (ci.shaderBundle) {
        // Mode 1: Extract from ShaderDataBundle
        wrapper->bindings = ExtractBindingsFromBundle(*ci.shaderBundle, ci.descriptorSetIndex);
        LOG_DEBUG("Extracted " + std::to_string(wrapper->bindings.size()) + " bindings from shader bundle (set " + std::to_string(ci.descriptorSetIndex) + ")");
    } else {
        // Mode 2: Use manual bindings
        wrapper->bindings = ci.manualBindings;
        LOG_DEBUG("Using " + std::to_string(wrapper->bindings.size()) + " manual bindings");
    }

    if (wrapper->bindings.empty()) {
        LOG_WARNING("No bindings found - creating empty layout");
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
        LOG_ERROR("Failed to create descriptor set layout: " + std::to_string(result));
        return nullptr;
    }

    LOG_DEBUG("Successfully created VkDescriptorSetLayout");
    return wrapper;
}

std::uint64_t DescriptorSetLayoutCacher::ComputeKey(const DescriptorSetLayoutCreateParams& ci) const {
    // Use layoutKey directly (typically descriptorInterfaceHash from bundle)
    // This enables content-based caching - same layout = same cache entry
    std::hash<std::string> hasher;
    return hasher(ci.layoutKey);
}

void DescriptorSetLayoutCacher::Cleanup() {
    LOG_INFO("Cleanup: Destroying " + std::to_string(m_entries.size()) + " descriptor set layouts");

    for (auto& [key, entry] : m_entries) {
        if (entry.resource && entry.resource->layout != VK_NULL_HANDLE) {
            // Note: We don't have direct access to VkDevice here
            // In production, store device pointer or use deferred cleanup
            LOG_WARNING("Skipping VkDescriptorSetLayout cleanup - device required");
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
        LOG_ERROR("Error: No reflection data in bundle");
        return vkBindings;
    }

    // Get bindings for the requested set
    auto descriptorSet = bundle.GetDescriptorSet(setIndex);
    if (descriptorSet.empty()) {
        LOG_WARNING("No bindings found for set " + std::to_string(setIndex));
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

        LOG_DEBUG("Binding " + std::to_string(vkBinding.binding) + ": " + spirvBinding.name + " (type=" + std::to_string(vkBinding.descriptorType) + ", count=" + std::to_string(vkBinding.descriptorCount) + ")");
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
        return VK_NULL_HANDLE;
    }

    if (!bundle.reflectionData) {
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
        return poolSizes;
    }

    // Get bindings for the requested set
    auto descriptorSet = bundle.GetDescriptorSet(setIndex);
    if (descriptorSet.empty()) {
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
    }

    return poolSizes;
}

} // namespace CashSystem
