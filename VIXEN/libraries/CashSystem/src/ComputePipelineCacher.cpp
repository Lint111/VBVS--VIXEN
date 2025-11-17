#include "CashSystem/ComputePipelineCacher.h"
#include "CashSystem/PipelineLayoutCacher.h"
#include "VulkanDevice.h"
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace CashSystem {

// ============================================================================
// PUBLIC API
// ============================================================================

std::shared_ptr<ComputePipelineWrapper> ComputePipelineCacher::GetOrCreate(
    const ComputePipelineCreateParams& ci
) {
    // Call base class GetOrCreate (which uses Create() override)
    return TypedCacher<ComputePipelineWrapper, ComputePipelineCreateParams>::GetOrCreate(ci);
}

// ============================================================================
// PROTECTED: TypedCacher Implementation
// ============================================================================

std::shared_ptr<ComputePipelineWrapper> ComputePipelineCacher::Create(
    const ComputePipelineCreateParams& ci
) {
    std::cout << "[ComputePipelineCacher::Create] Creating compute pipeline for shader: "
              << ci.shaderKey << std::endl;

    auto wrapper = std::make_shared<ComputePipelineWrapper>();
    wrapper->shaderKey = ci.shaderKey;
    wrapper->layoutKey = ci.layoutKey;
    wrapper->workgroupSizeX = ci.workgroupSizeX;
    wrapper->workgroupSizeY = ci.workgroupSizeY;
    wrapper->workgroupSizeZ = ci.workgroupSizeZ;

    // 1. Create or retrieve pipeline layout
    CreatePipelineLayout(ci, *wrapper);

    // 2. Create compute pipeline
    CreateComputePipeline(ci, *wrapper);

    std::cout << "[ComputePipelineCacher::Create] Compute pipeline created successfully" << std::endl;
    return wrapper;
}

std::uint64_t ComputePipelineCacher::ComputeKey(const ComputePipelineCreateParams& ci) const {
    // Hash based on shader key, layout key, and workgroup size
    size_t hash = std::hash<std::string>{}(ci.shaderKey);
    hash ^= std::hash<std::string>{}(ci.layoutKey) << 1;
    hash ^= std::hash<uint32_t>{}(ci.workgroupSizeX) << 2;
    hash ^= std::hash<uint32_t>{}(ci.workgroupSizeY) << 3;
    hash ^= std::hash<uint32_t>{}(ci.workgroupSizeZ) << 4;
    return static_cast<std::uint64_t>(hash);
}

void ComputePipelineCacher::Cleanup() {
    std::cout << "[ComputePipelineCacher::Cleanup] Cleaning up compute pipelines" << std::endl;

    // Destroy all cached pipelines
    for (auto& [key, entry] : m_entries) {
        if (entry.resource && entry.resource->pipeline != VK_NULL_HANDLE) {
            std::cout << "[ComputePipelineCacher::Cleanup] Destroying pipeline: "
                      << entry.resource->shaderKey << std::endl;
            vkDestroyPipeline(m_device->device, entry.resource->pipeline, nullptr);
            entry.resource->pipeline = VK_NULL_HANDLE;
        }

        // Don't destroy pipelineLayout (owned by PipelineLayoutCacher)
        // Don't destroy cache (shared, owned by PipelineCacher or DeviceNode)
    }

    // Destroy global cache if we own it (shouldn't happen - should be shared)
    if (m_globalCache != VK_NULL_HANDLE) {
        std::cout << "[ComputePipelineCacher::Cleanup] WARNING: Destroying owned pipeline cache (should be shared)" << std::endl;
        vkDestroyPipelineCache(m_device->device, m_globalCache, nullptr);
        m_globalCache = VK_NULL_HANDLE;
    }

    // Clear entries
    Clear();
}

// ============================================================================
// PRIVATE: Helper Methods
// ============================================================================

void ComputePipelineCacher::CreatePipelineLayout(
    const ComputePipelineCreateParams& ci,
    ComputePipelineWrapper& wrapper
) {
    // Use explicit pipelineLayoutWrapper if provided
    if (ci.pipelineLayoutWrapper) {
        wrapper.pipelineLayoutWrapper = ci.pipelineLayoutWrapper;
        std::cout << "[ComputePipelineCacher::CreatePipelineLayout] Using provided pipeline layout" << std::endl;
        return;
    }

    // Convenience fallback: Create layout from descriptor set layout + push constants
    std::cout << "[ComputePipelineCacher::CreatePipelineLayout] Using convenience fallback to create pipeline layout" << std::endl;

    // Get PipelineLayoutCacher from MainCacher
    auto& mainCacher = MainCacher::Instance();
    auto* layoutCacher = mainCacher.GetCacher<PipelineLayoutCacher, PipelineLayoutWrapper, PipelineLayoutCreateParams>(
        std::type_index(typeid(PipelineLayoutWrapper)),
        GetDevice()
    );

    if (!layoutCacher) {
        throw std::runtime_error("[ComputePipelineCacher::CreatePipelineLayout] PipelineLayoutCacher not registered");
    }

    // Build create params for pipeline layout
    PipelineLayoutCreateParams layoutParams{};
    layoutParams.descriptorSetLayout = ci.descriptorSetLayout;
    layoutParams.pushConstantRanges = ci.pushConstantRanges;

    // Get or create the layout through PipelineLayoutCacher
    wrapper.pipelineLayoutWrapper = layoutCacher->GetOrCreate(layoutParams);

    std::cout << "[ComputePipelineCacher::CreatePipelineLayout] Created pipeline layout via fallback" << std::endl;
}

void ComputePipelineCacher::CreateComputePipeline(
    const ComputePipelineCreateParams& ci,
    ComputePipelineWrapper& wrapper
) {
    if (!ci.shaderModule || ci.shaderModule == VK_NULL_HANDLE) {
        throw std::runtime_error("[ComputePipelineCacher::CreateComputePipeline] Invalid shader module");
    }

    if (!wrapper.pipelineLayoutWrapper || !wrapper.pipelineLayoutWrapper->layout) {
        throw std::runtime_error("[ComputePipelineCacher::CreateComputePipeline] Pipeline layout not set");
    }

    // Setup shader stage
    VkPipelineShaderStageCreateInfo shaderStageInfo{};
    shaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    shaderStageInfo.module = ci.shaderModule;
    shaderStageInfo.pName = ci.entryPoint;

    // Setup specialization constants (if provided)
    VkSpecializationInfo specInfo{};
    if (!ci.specMapEntries.empty() && !ci.specData.empty()) {
        specInfo.mapEntryCount = static_cast<uint32_t>(ci.specMapEntries.size());
        specInfo.pMapEntries = ci.specMapEntries.data();
        specInfo.dataSize = ci.specData.size();
        specInfo.pData = ci.specData.data();
        shaderStageInfo.pSpecializationInfo = &specInfo;
    }

    // Create compute pipeline
    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage = shaderStageInfo;
    pipelineInfo.layout = wrapper.pipelineLayoutWrapper->layout;

    // Use global cache if available (shared with graphics)
    VkPipelineCache cacheToUse = m_globalCache;

    VkResult result = vkCreateComputePipelines(
        m_device->device,
        cacheToUse,
        1,
        &pipelineInfo,
        nullptr,
        &wrapper.pipeline
    );

    if (result != VK_SUCCESS) {
        throw std::runtime_error("[ComputePipelineCacher::CreateComputePipeline] Failed to create compute pipeline: " + std::to_string(result));
    }

    wrapper.cache = cacheToUse;

    std::cout << "[ComputePipelineCacher::CreateComputePipeline] Created VkPipeline: "
              << reinterpret_cast<uint64_t>(wrapper.pipeline) << std::endl;
}

// ============================================================================
// SERIALIZATION (Stub implementations)
// ============================================================================

bool ComputePipelineCacher::SerializeToFile(const std::filesystem::path& path) const {
    // Compute pipelines are device-specific and expensive to serialize
    // Better approach: serialize shader keys + layout keys, recompile on load
    // Pipeline cache (VkPipelineCache) can be serialized separately for warm starts

    std::cout << "[ComputePipelineCacher::SerializeToFile] Compute pipeline serialization deferred" << std::endl;
    std::cout << "  Recommendation: Serialize pipeline cache (VkPipelineCache) instead" << std::endl;

    // TODO: Optionally serialize VkPipelineCache data for warm startup
    return true;  // Return success (nothing to serialize currently)
}

bool ComputePipelineCacher::DeserializeFromFile(const std::filesystem::path& path, void* device) {
    // Compute pipelines are recreated on demand from shader modules
    // Pipeline cache (VkPipelineCache) deserialization provides warm startup

    std::cout << "[ComputePipelineCacher::DeserializeFromFile] Compute pipeline deserialization deferred" << std::endl;
    std::cout << "  Recommendation: Deserialize pipeline cache (VkPipelineCache) instead" << std::endl;

    // TODO: Optionally deserialize VkPipelineCache data for warm startup
    return true;  // Return success (nothing to deserialize currently)
}

} // namespace CashSystem
