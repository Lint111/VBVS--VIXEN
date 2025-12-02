#include "pch.h"
#include "PipelineLayoutCacher.h"
#include "VulkanDevice.h"
#include "VixenHash.h"
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace CashSystem {

std::shared_ptr<PipelineLayoutWrapper> PipelineLayoutCacher::GetOrCreate(const PipelineLayoutCreateParams& ci) {
    auto key = ComputeKey(ci);

    // Check cache first
    {
        std::shared_lock rlock(m_lock);
        auto it = m_entries.find(key);
        if (it != m_entries.end()) {
            std::cout << "[PipelineLayoutCacher::GetOrCreate] CACHE HIT for layout " << ci.layoutKey
                      << " (key=" << key << ", VkPipelineLayout="
                      << reinterpret_cast<uint64_t>(it->second.resource->layout) << ")" << std::endl;
            return it->second.resource;
        }
        auto pit = m_pending.find(key);
        if (pit != m_pending.end()) {
            std::cout << "[PipelineLayoutCacher::GetOrCreate] CACHE PENDING for layout " << ci.layoutKey << std::endl;
            return pit->second.get();
        }
    }

    std::cout << "[PipelineLayoutCacher::GetOrCreate] CACHE MISS for layout " << ci.layoutKey
              << " (key=" << key << "), creating new resource..." << std::endl;

    // Call parent implementation
    return TypedCacher<PipelineLayoutWrapper, PipelineLayoutCreateParams>::GetOrCreate(ci);
}

std::shared_ptr<PipelineLayoutWrapper> PipelineLayoutCacher::Create(const PipelineLayoutCreateParams& ci) {
    if (!GetDevice()) {
        throw std::runtime_error("PipelineLayoutCacher: No device available");
    }

    std::cout << "[PipelineLayoutCacher::Create] Creating pipeline layout: " << ci.layoutKey << std::endl;

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

    std::cout << "[PipelineLayoutCacher::Create] Created VkPipelineLayout: "
              << reinterpret_cast<uint64_t>(wrapper->layout) << std::endl;

    return wrapper;
}

std::uint64_t PipelineLayoutCacher::ComputeKey(const PipelineLayoutCreateParams& ci) const {
    // Key is based on descriptor set layout handle + push constant ranges
    std::ostringstream keyStream;
    keyStream << reinterpret_cast<uint64_t>(ci.descriptorSetLayout) << "|";
    keyStream << ci.pushConstantRanges.size();

    for (const auto& range : ci.pushConstantRanges) {
        keyStream << "|" << range.stageFlags << ":" << range.offset << ":" << range.size;
    }

    const std::string keyString = keyStream.str();
    std::uint64_t hash = std::hash<std::string>{}(keyString);

    std::cout << "[PipelineLayoutCacher::ComputeKey] keyString=\"" << keyString << "\" hash=" << hash << std::endl;

    return hash;
}

void PipelineLayoutCacher::Cleanup() {
    std::cout << "[PipelineLayoutCacher::Cleanup] Cleaning up " << m_entries.size() << " cached layouts" << std::endl;

    if (GetDevice()) {
        for (auto& [key, entry] : m_entries) {
            if (entry.resource && entry.resource->layout != VK_NULL_HANDLE) {
                std::cout << "[PipelineLayoutCacher::Cleanup] Destroying VkPipelineLayout: "
                          << reinterpret_cast<uint64_t>(entry.resource->layout) << std::endl;
                vkDestroyPipelineLayout(GetDevice()->device, entry.resource->layout, nullptr);
                entry.resource->layout = VK_NULL_HANDLE;
            }
        }
    }

    Clear();
    std::cout << "[PipelineLayoutCacher::Cleanup] Cleanup complete" << std::endl;
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
