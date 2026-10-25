#include "pch.h"
#include "ComputePipelineCacher.h"
#include "CacheKeyHasher.h"
#include "PipelineLayoutCacher.h"
#include "VulkanDevice.h"
#include "PipelineCacher.h"
#include "CacheCodec.h"
#include "SharedPipelineCacheStore.h"
#include <stdexcept>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>  // std::cout for VIXEN_PIPELINE_STATS -- see LogPipelineExecutableStatistics

namespace CashSystem {

namespace {
constexpr std::string_view kSharedCacheKind = "compute";
} // namespace

// E3-T1 probe (temporary, log-only). Guarded by the same lock-free single-writer assumption the
// rest of boot-time pipeline creation already makes; the counter is atomic so a threaded builder
// still yields a coherent monotonic ordinal.
namespace {
std::atomic<unsigned> g_computeCreateCounter{0};
ComputePipelineCreationProbe g_computeCreationProbe{};
} // namespace

const ComputePipelineCreationProbe& GetComputePipelineCreationProbe() { return g_computeCreationProbe; }

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

    // E3-T1 probe: ordinal of this ACTUAL Create call (1-based), recorded for the two measured
    // programs. shaderKey is "<programName>:<spirvSha>" (ComputePipelineNode), so match the prefix.
    {
        const unsigned ordinal = g_computeCreateCounter.fetch_add(1) + 1;
        const int vkCacheNonNull = (wrapper->cache != VK_NULL_HANDLE) ? 1 : 0;
        if (ci.shaderKey.rfind("BodyInstanceRayMarch:", 0) == 0) {
            g_computeCreationProbe.marchOrdinal = ordinal;
            g_computeCreationProbe.marchVkCacheNonNull = vkCacheNonNull;
        } else if (ci.shaderKey.rfind("ShadowVisibilityWave:", 0) == 0) {
            g_computeCreationProbe.waveOrdinal = ordinal;
            g_computeCreationProbe.waveVkCacheNonNull = vkCacheNonNull;
        }
    }

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
          .Add(ci.workgroupSizeZ)
          .Add(std::string_view(ci.entryPoint ? ci.entryPoint : ""));
    // Specialization constants change the compiled pipeline, so they are part of its identity.
    hasher.Add(static_cast<uint32_t>(ci.specMapEntries.size()));
    for (const VkSpecializationMapEntry& entry : ci.specMapEntries) {
        hasher.Add(entry.constantID).Add(entry.offset).Add(static_cast<uint64_t>(entry.size));
    }
    hasher.Add(static_cast<uint32_t>(ci.specData.size())).AddBytes(ci.specData.data(), ci.specData.size());
    return hasher.Finalize();
}

void ComputePipelineCacher::Cleanup() {
    LOG_INFO("[ComputePipelineCacher::Cleanup] Cleaning up compute pipelines");

    // Destroy all cached pipelines. Walks the published generation (teardown is the quiescent
    // point; the walk holds nothing), then Clear() below retires it. m_globalCache is an atomic
    // handle read by CreateComputePipeline() on the pipeline-creation hot path (audit V-M9).
    {
        ForEachEntry([&](const CacheEntry& entry) {
            if (entry.resource && entry.resource->pipeline != VK_NULL_HANDLE) {
                LOG_DEBUG("[ComputePipelineCacher::Cleanup] Destroying pipeline: " + entry.resource->shaderKey);
                vkDestroyPipeline(m_device->device, entry.resource->pipeline, nullptr);
                entry.resource->pipeline = VK_NULL_HANDLE;
            }

            // Don't destroy pipelineLayout (owned by PipelineLayoutCacher)
            // Don't destroy cache (shared, owned by PipelineCacher or DeviceNode)
        });

        // The compute cacher owns its VkPipelineCache (created lazily, persisted by
        // SerializePayloadToFile); no other cacher shares it.
        if (const VkPipelineCache owned = m_globalCache.exchange(VK_NULL_HANDLE, std::memory_order_acq_rel);
            owned != VK_NULL_HANDLE) {
            vkDestroyPipelineCache(m_device->device, owned, nullptr);
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
    pipelineInfo.flags = VK_PIPELINE_CREATE_DISPATCH_BASE_BIT;
    pipelineInfo.stage = shaderStageInfo;
    pipelineInfo.layout = wrapper.pipelineLayoutWrapper->layout;

    // VIXEN_PIPELINE_STATS: request register/spill statistics be captured for this pipeline.
    // m_device->IsPipelineStatsEnabled() is false unless the env var was set AND the device
    // supports VK_KHR_pipeline_executable_properties (see DeviceNode::CreateLogicalDevice), so
    // this is a no-op flag addition in the default case.
    if (m_device->IsPipelineStatsEnabled()) {
        pipelineInfo.flags |= VK_PIPELINE_CREATE_CAPTURE_STATISTICS_BIT_KHR;
    }

    const VkPipelineCache cacheToUse = SharedPipelineCache();

    const bool timePipeline = std::getenv("VIXEN_PHASE_TIMING") != nullptr;
    const auto createStart = std::chrono::steady_clock::now();
    VkResult result = vkCreateComputePipelines(
        m_device->device,
        cacheToUse,
        1,
        &pipelineInfo,
        nullptr,
        &wrapper.pipeline
    );
    if (timePipeline) {
        std::fprintf(stderr, "[PHASE] stage=vkCreateComputePipelines shader=%s ms=%.3f warm_cache=%d\n",
            ci.shaderKey.c_str(),
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - createStart).count(),
            m_warmCacheLoaded.load(std::memory_order_acquire) ? 1 : 0);
    }

    if (result != VK_SUCCESS) {
        throw std::runtime_error("[ComputePipelineCacher::CreateComputePipeline] Failed to create compute pipeline: " + std::to_string(result));
    }

    wrapper.cache = cacheToUse;

    LOG_DEBUG("[ComputePipelineCacher::CreateComputePipeline] Created VkPipeline: " + std::to_string(reinterpret_cast<uint64_t>(wrapper.pipeline)));

    if (m_device->IsPipelineStatsEnabled()) {
        LogPipelineExecutableStatistics(ci.shaderKey, wrapper.pipeline);
    }
}

VkPipelineCache ComputePipelineCacher::SharedPipelineCache() {
    // Atomic read: m_globalCache can be cleared concurrently by Cleanup() (audit V-M9).
    if (const VkPipelineCache cache = m_globalCache.load(std::memory_order_acquire); cache != VK_NULL_HANDLE) {
        return cache;
    }
    if (!m_device) {
        return VK_NULL_HANDLE;
    }
    // Seed from the device/driver-keyed shared store before falling back to an empty cache.
    std::string seedReason;
    VkPipelineCache created = CreatePipelineCacheFromSharedStore(
        m_device->device, m_device->gpuProperties, kSharedCacheKind, &seedReason);
    const bool seeded = created != VK_NULL_HANDLE;
    if (!seeded) {
        LOG_INFO("[ComputePipelineCacher] Shared pipeline cache not used: " + seedReason);
        VkPipelineCacheCreateInfo cacheInfo{VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO};
        if (vkCreatePipelineCache(m_device->device, &cacheInfo, nullptr, &created) != VK_SUCCESS) {
            return VK_NULL_HANDLE;
        }
    }
    VkPipelineCache expected = VK_NULL_HANDLE;
    if (m_globalCache.compare_exchange_strong(expected, created, std::memory_order_acq_rel)) {
        if (seeded) {
            m_warmCacheLoaded.store(true, std::memory_order_release);
            LOG_INFO("[ComputePipelineCacher] Seeded pipeline cache from the shared store");
        }
        return created;
    }
    vkDestroyPipelineCache(m_device->device, created, nullptr);
    return expected;
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
// SERIALIZATION: the VkPipelineCache blob (driver-compiled pipelines) is what is worth persisting
// ============================================================================

bool ComputePipelineCacher::SerializePayloadToFile(const std::filesystem::path& path) const {
    const VkPipelineCache cache = m_globalCache.load(std::memory_order_acquire);
    if (!m_device || cache == VK_NULL_HANDLE) return true;

    size_t cacheSize = 0;
    if (vkGetPipelineCacheData(m_device->device, cache, &cacheSize, nullptr) != VK_SUCCESS || cacheSize == 0) {
        LOG_ERROR("[ComputePipelineCacher] Failed to size pipeline cache data");
        return false;
    }
    std::vector<uint8_t> cacheData(cacheSize);
    if (vkGetPipelineCacheData(m_device->device, cache, &cacheSize, cacheData.data()) != VK_SUCCESS) {
        LOG_ERROR("[ComputePipelineCacher] Failed to read pipeline cache data");
        return false;
    }
    cacheData.resize(cacheSize);

    std::ofstream file(path, std::ios::binary);
    if (!file) {
        LOG_ERROR("[ComputePipelineCacher] Failed to open cache file for writing: " + path.string());
        return false;
    }
    CacheWriter writer(file);
    const uint32_t version = 1;
    writer.WritePod(version);
    writer.WriteVector(cacheData);
    if (!writer.Ok()) {
        LOG_ERROR("[ComputePipelineCacher] Failed to write pipeline cache file");
        return false;
    }
    LOG_INFO("[ComputePipelineCacher] Serialized compute pipeline cache (" + std::to_string(cacheSize) +
             " bytes) to " + path.string());
    PersistWarmState();
    return true;
}

void ComputePipelineCacher::PersistWarmState() const {
    const VkPipelineCache cache = m_globalCache.load(std::memory_order_acquire);
    if (!m_device || cache == VK_NULL_HANDLE) return;
    std::string reason;
    if (PersistPipelineCacheToSharedStore(m_device->device, cache, m_device->gpuProperties, kSharedCacheKind, &reason)) {
        LOG_INFO("[ComputePipelineCacher] Persisted pipeline cache to the shared store");
    } else {
        LOG_INFO("[ComputePipelineCacher] Shared pipeline cache not persisted: " + reason);
    }
}

bool ComputePipelineCacher::DeserializePayloadFromFile(const std::filesystem::path& path, void* /*device*/) {
    if (!std::filesystem::exists(path)) {
        LOG_INFO("[ComputePipelineCacher] No compute pipeline cache at " + path.string() + " (first run)");
        return true;
    }
    if (!m_device) return false;

    std::ifstream file(path, std::ios::binary);
    if (!file) {
        LOG_ERROR("[ComputePipelineCacher] Failed to open cache file: " + path.string());
        return false;
    }
    CacheReader reader(file);
    uint32_t version = 0;
    if (!reader.ReadPod(version) || version != 1) {
        LOG_ERROR("[ComputePipelineCacher] Unsupported cache version: " + std::to_string(version));
        return false;
    }
    constexpr size_t kMaxPipelineCacheBytes = 1u << 30;
    std::vector<uint8_t> cacheData;
    if (!reader.ReadVector(cacheData, kMaxPipelineCacheBytes) || cacheData.empty()) {
        LOG_ERROR("[ComputePipelineCacher] Invalid or oversized cache data in file");
        return false;
    }
    if (!PipelineCacheBlobMatchesDevice(cacheData, m_device->gpuProperties)) {
        LOG_INFO("[ComputePipelineCacher] Pipeline cache is for another device/driver — regenerating: " + path.string());
        return true;
    }

    VkPipelineCacheCreateInfo cacheInfo{VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO};
    cacheInfo.initialDataSize = cacheData.size();
    cacheInfo.pInitialData = cacheData.data();
    VkPipelineCache loaded = VK_NULL_HANDLE;
    if (vkCreatePipelineCache(m_device->device, &cacheInfo, nullptr, &loaded) != VK_SUCCESS) {
        LOG_ERROR("[ComputePipelineCacher] Failed to create pipeline cache from file");
        return false;
    }
    VkPipelineCache expected = VK_NULL_HANDLE;
    if (!m_globalCache.compare_exchange_strong(expected, loaded, std::memory_order_acq_rel)) {
        vkDestroyPipelineCache(m_device->device, loaded, nullptr);
        return true;
    }
    m_warmCacheLoaded.store(true, std::memory_order_release);
    LOG_INFO("[ComputePipelineCacher] Loaded compute pipeline cache from " + path.string() + " (" +
             std::to_string(cacheData.size()) + " bytes)");
    return true;
}

} // namespace CashSystem
