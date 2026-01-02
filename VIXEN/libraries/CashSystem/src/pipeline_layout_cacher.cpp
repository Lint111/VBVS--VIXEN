#include "pch.h"
#include "PipelineLayoutCacher.h"
#include "CacheKeyHasher.h"
#include "VulkanDevice.h"
#include <stdexcept>

namespace CashSystem {

std::shared_ptr<PipelineLayoutWrapper> PipelineLayoutCacher::GetOrCreate(const PipelineLayoutCreateParams& ci) {
    auto key = ComputeKey(ci);

    // Check cache first
    {
        std::shared_lock rlock(m_lock);
        auto it = m_entries.find(key);
        if (it != m_entries.end()) {
            LOG_DEBUG("CACHE HIT for layout " + ci.layoutKey + " (key=" + std::to_string(key) + ")");
            return it->second.resource;
        }
        auto pit = m_pending.find(key);
        if (pit != m_pending.end()) {
            LOG_DEBUG("CACHE PENDING for layout " + ci.layoutKey);
            return pit->second.get();
        }
    }

    LOG_DEBUG("CACHE MISS for layout " + ci.layoutKey + " (key=" + std::to_string(key) + "), creating new resource...");

    // Call parent implementation
    return TypedCacher<PipelineLayoutWrapper, PipelineLayoutCreateParams>::GetOrCreate(ci);
}

std::shared_ptr<PipelineLayoutWrapper> PipelineLayoutCacher::Create(const PipelineLayoutCreateParams& ci) {
    if (!GetDevice()) {
        throw std::runtime_error("PipelineLayoutCacher: No device available");
    }

    LOG_DEBUG("Creating pipeline layout: " + ci.layoutKey);

    auto wrapper = std::make_shared<PipelineLayoutWrapper>();
    wrapper->descriptorSetLayout = ci.descriptorSetLayout;
    wrapper->pushConstantRanges = ci.pushConstantRanges;

    // Create pipeline layout
    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.pNext = nullptr;
    layoutInfo.setLayoutCount = (ci.descriptorSetLayout != VK_NULL_HANDLE) ? 1 : 0;
    layoutInfo.pSetLayouts = (ci.descriptorSetLayout != VK_NULL_HANDLE) ? &ci.descriptorSetLayout : nullptr;
    layoutInfo.pushConstantRangeCount = static_cast<uint32_t>(ci.pushConstantRanges.size());
    layoutInfo.pPushConstantRanges = ci.pushConstantRanges.empty() ? nullptr : ci.pushConstantRanges.data();

    VkResult result = vkCreatePipelineLayout(
        GetDevice()->device,
        &layoutInfo,
        nullptr,
        &wrapper->layout
    );

    if (result != VK_SUCCESS) {
        throw std::runtime_error("PipelineLayoutCacher: Failed to create pipeline layout");
    }

    LOG_DEBUG("Created VkPipelineLayout: " + std::to_string(reinterpret_cast<uint64_t>(wrapper->layout)));

    return wrapper;
}

std::uint64_t PipelineLayoutCacher::ComputeKey(const PipelineLayoutCreateParams& ci) const {
    // Use CacheKeyHasher for deterministic, binary hashing
    // NOTE: Still uses descriptor set layout handle as we don't have access to
    // the layout's CreateInfo here. Two layouts with identical content but
    // different handles will have different keys - this is a known limitation.
    // TODO: Store DescriptorSetLayoutCreateInfo in PipelineLayoutCreateParams
    // to enable true content-based hashing.
    CacheKeyHasher hasher;

    // Hash descriptor set layout handle
    hasher.Add(reinterpret_cast<uint64_t>(ci.descriptorSetLayout));

    // Hash push constant ranges (content-based)
    hasher.Add(static_cast<uint32_t>(ci.pushConstantRanges.size()));
    for (const auto& range : ci.pushConstantRanges) {
        hasher.Add(range.stageFlags);
        hasher.Add(range.offset);
        hasher.Add(range.size);
    }

    uint64_t hash = hasher.Finalize();
    LOG_DEBUG("ComputeKey: hash=" + std::to_string(hash));

    return hash;
}

void PipelineLayoutCacher::Cleanup() {
    LOG_INFO("Cleaning up " + std::to_string(m_entries.size()) + " cached layouts");

    if (GetDevice()) {
        for (auto& [key, entry] : m_entries) {
            if (entry.resource && entry.resource->layout != VK_NULL_HANDLE) {
                LOG_DEBUG("Destroying VkPipelineLayout: " + std::to_string(reinterpret_cast<uint64_t>(entry.resource->layout)));
                vkDestroyPipelineLayout(GetDevice()->device, entry.resource->layout, nullptr);
                entry.resource->layout = VK_NULL_HANDLE;
            }
        }
    }

    Clear();
    LOG_INFO("Cleanup complete");
}

bool PipelineLayoutCacher::SerializeToFile(const std::filesystem::path& path) const {
    // Not implemented - layouts are derived from descriptor layouts
    (void)path;
    return true;
}

bool PipelineLayoutCacher::DeserializeFromFile(const std::filesystem::path& path, void* device) {
    // Not implemented - layouts are derived from descriptor layouts
    (void)path;
    (void)device;
    return true;
}

} // namespace CashSystem
