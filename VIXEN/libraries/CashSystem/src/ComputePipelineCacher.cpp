#include "pch.h"
#include "ComputePipelineCacher.h"
#include "CacheKeyHasher.h"
#include "PipelineLayoutCacher.h"
#include "VulkanDevice.h"
#include <stdexcept>
#include <iostream>  // std::cout for VIXEN_PIPELINE_STATS -- see LogPipelineExecutableStatistics

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
    LOG_INFO("[ComputePipelineCacher::Create] Creating compute pipeline for shader: " + ci.shaderKey);

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

    LOG_INFO("[ComputePipelineCacher::Create] Compute pipeline created successfully");
    return wrapper;
}

std::uint64_t ComputePipelineCacher::ComputeKey(const ComputePipelineCreateParams& ci) const {
    // Use CacheKeyHasher for deterministic, binary hashing
    CacheKeyHasher hasher;
    hasher.Add(ci.shaderKey)
          .Add(ci.layoutKey)
          .Add(ci.workgroupSizeX)
          .Add(ci.workgroupSizeY)
          .Add(ci.workgroupSizeZ);
    return hasher.Finalize();
}

void ComputePipelineCacher::Cleanup() {
    LOG_INFO("[ComputePipelineCacher::Cleanup] Cleaning up compute pipelines");

    // Destroy all cached pipelines. Locked: m_entries/m_globalCache are mutated here while
    // DeviceRegistry can be running Serialize/DeserializeFromFile for this same cacher on
    // another thread via std::async, and m_globalCache is read unlocked by
    // CreateComputePipeline() on the pipeline-creation hot path (audit V-M9). Released before
    // Clear(), which takes its own unique_lock.
    {
        std::unique_lock wlock(m_lock);
        for (auto& [key, entry] : m_entries) {
            if (entry.resource && entry.resource->pipeline != VK_NULL_HANDLE) {
                LOG_DEBUG("[ComputePipelineCacher::Cleanup] Destroying pipeline: " + entry.resource->shaderKey);
                vkDestroyPipeline(m_device->device, entry.resource->pipeline, nullptr);
                entry.resource->pipeline = VK_NULL_HANDLE;
            }

            // Don't destroy pipelineLayout (owned by PipelineLayoutCacher)
            // Don't destroy cache (shared, owned by PipelineCacher or DeviceNode)
        }

        // Destroy global cache if we own it (shouldn't happen - should be shared)
        if (m_globalCache != VK_NULL_HANDLE) {
            LOG_WARNING("[ComputePipelineCacher::Cleanup] WARNING: Destroying owned pipeline cache (should be shared)");
            vkDestroyPipelineCache(m_device->device, m_globalCache, nullptr);
            m_globalCache = VK_NULL_HANDLE;
        }
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
        LOG_DEBUG("[ComputePipelineCacher::CreatePipelineLayout] Using provided pipeline layout");
        return;
    }

    // Convenience fallback: Create layout from descriptor set layout + push constants
    LOG_DEBUG("[ComputePipelineCacher::CreatePipelineLayout] Using convenience fallback to create pipeline layout");

    // Get PipelineLayoutCacher from our owning MainCacher (AR#8: was MainCacher::Instance())
    MainCacher* owner = GetMainCacher();
    if (!owner) {
        throw std::runtime_error("[ComputePipelineCacher::CreatePipelineLayout] no owning MainCacher");
    }
    auto& mainCacher = *owner;
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

    LOG_DEBUG("[ComputePipelineCacher::CreatePipelineLayout] Created pipeline layout via fallback");
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

    // VIXEN_PIPELINE_STATS: request register/spill statistics be captured for this pipeline.
    // m_device->IsPipelineStatsEnabled() is false unless the env var was set AND the device
    // supports VK_KHR_pipeline_executable_properties (see DeviceNode::CreateLogicalDevice), so
    // this is a no-op flag addition in the default case.
    if (m_device->IsPipelineStatsEnabled()) {
        pipelineInfo.flags |= VK_PIPELINE_CREATE_CAPTURE_STATISTICS_BIT_KHR;
    }

    // Use global cache if available (shared with graphics). Locked read: m_globalCache can be
    // destroyed concurrently by Cleanup() (audit V-M9).
    VkPipelineCache cacheToUse;
    {
        std::shared_lock rlock(m_lock);
        cacheToUse = m_globalCache;
    }

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

    LOG_DEBUG("[ComputePipelineCacher::CreateComputePipeline] Created VkPipeline: " + std::to_string(reinterpret_cast<uint64_t>(wrapper.pipeline)));

    if (m_device->IsPipelineStatsEnabled()) {
        LogPipelineExecutableStatistics(ci.shaderKey, wrapper.pipeline);
    }
}

// VIXEN_PIPELINE_STATS: log the driver-reported register/spill/instruction stats for one pipeline
// executable. Two-call enumerate (count, then fill) per the VK_KHR_pipeline_executable_properties
// pattern. Mesa/Dozen may report zero executables or zero statistics for a given executable --
// that is a valid, expected response (not every driver implements every stat), so an empty result
// logs one graceful line rather than being treated as an error.
//
// Writes std::cout directly rather than LOG_INFO: this cacher's ILoggable logger is constructed
// disabled (InitializeLogger default) and nothing in MainCacher/GetCacher ever enables it, so
// LOG_INFO here would silently vanish -- same trap as DeviceNode's NODE_LOG_INFO before the
// adapter-visibility fix, but flipping it on for every cacher via the shared MainCacher::GetCacher
// template is out of scope for one opt-in diagnostic. Gated on VIXEN_PIPELINE_STATS (this function
// only runs when that's set), so it stays a no-op by default like every other line in this feature.
void ComputePipelineCacher::LogPipelineExecutableStatistics(const std::string& shaderKey, VkPipeline pipeline) {
    if (!m_device->fpGetPipelineExecutableProperties || !m_device->fpGetPipelineExecutableStatistics) {
        std::cout << "[PipelineStats] " << shaderKey << ": unavailable on this device (functions failed to resolve)" << std::endl;
        return;
    }

    VkPipelineInfoKHR pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_INFO_KHR;
    pipelineInfo.pipeline = pipeline;

    uint32_t executableCount = 0;
    m_device->fpGetPipelineExecutableProperties(m_device->device, &pipelineInfo, &executableCount, nullptr);
    if (executableCount == 0) {
        std::cout << "[PipelineStats] " << shaderKey << ": unavailable on this device (0 executables reported)" << std::endl;
        return;
    }

    std::vector<VkPipelineExecutablePropertiesKHR> executables(executableCount);
    for (auto& exe : executables) {
        exe.sType = VK_STRUCTURE_TYPE_PIPELINE_EXECUTABLE_PROPERTIES_KHR;
    }
    m_device->fpGetPipelineExecutableProperties(m_device->device, &pipelineInfo, &executableCount, executables.data());

    bool loggedAny = false;
    for (uint32_t i = 0; i < executableCount; ++i) {
        VkPipelineExecutableInfoKHR exeInfo{};
        exeInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_EXECUTABLE_INFO_KHR;
        exeInfo.pipeline = pipeline;
        exeInfo.executableIndex = i;

        uint32_t statCount = 0;
        m_device->fpGetPipelineExecutableStatistics(m_device->device, &exeInfo, &statCount, nullptr);
        if (statCount == 0) continue;

        std::vector<VkPipelineExecutableStatisticKHR> stats(statCount);
        for (auto& stat : stats) {
            stat.sType = VK_STRUCTURE_TYPE_PIPELINE_EXECUTABLE_STATISTIC_KHR;
        }
        m_device->fpGetPipelineExecutableStatistics(m_device->device, &exeInfo, &statCount, stats.data());

        std::string line = "[PipelineStats] " + shaderKey + "/" + executables[i].name + ":";
        for (const auto& stat : stats) {
            line += " " + std::string(stat.name) + "=";
            switch (stat.format) {
                case VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_BOOL32_KHR:
                    line += stat.value.b32 ? "true" : "false";
                    break;
                case VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_INT64_KHR:
                    line += std::to_string(stat.value.i64);
                    break;
                case VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_UINT64_KHR:
                    line += std::to_string(stat.value.u64);
                    break;
                case VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_FLOAT64_KHR:
                    line += std::to_string(stat.value.f64);
                    break;
                default:
                    line += "?";
                    break;
            }
        }
        std::cout << line << std::endl;
        loggedAny = true;
    }

    if (!loggedAny) {
        std::cout << "[PipelineStats] " << shaderKey << ": unavailable on this device (0 statistics reported for any executable)" << std::endl;
    }
}

// ============================================================================
// SERIALIZATION (Stub implementations)
// ============================================================================

bool ComputePipelineCacher::SerializeToFile(const std::filesystem::path& path) const {
    // Compute pipelines are device-specific and expensive to serialize
    // Better approach: serialize shader keys + layout keys, recompile on load
    // Pipeline cache (VkPipelineCache) can be serialized separately for warm starts

    LOG_DEBUG("[ComputePipelineCacher::SerializeToFile] Compute pipeline serialization deferred");
    LOG_DEBUG("  Recommendation: Serialize pipeline cache (VkPipelineCache) instead");

    // TODO: Optionally serialize VkPipelineCache data for warm startup
    return true;  // Return success (nothing to serialize currently)
}

bool ComputePipelineCacher::DeserializeFromFile(const std::filesystem::path& path, void* device) {
    // Compute pipelines are recreated on demand from shader modules
    // Pipeline cache (VkPipelineCache) deserialization provides warm startup

    LOG_DEBUG("[ComputePipelineCacher::DeserializeFromFile] Compute pipeline deserialization deferred");
    LOG_DEBUG("  Recommendation: Deserialize pipeline cache (VkPipelineCache) instead");

    // TODO: Optionally deserialize VkPipelineCache data for warm startup
    return true;  // Return success (nothing to deserialize currently)
}

} // namespace CashSystem
