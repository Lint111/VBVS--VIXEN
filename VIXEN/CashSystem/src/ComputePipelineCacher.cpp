#include "CashSystem/ComputePipelineCacher.h"
#include "CashSystem/PipelineLayoutCacher.h"
#include "CashSystem/MainCacher.h"
#include "VulkanResources/VulkanDevice.h"
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace CashSystem {

ComputePipelineCacher::ComputePipelineCacher(
    Vixen::Vulkan::Resources::VulkanDevice* device,
    VkPipelineCache sharedPipelineCache)
    : TypedCacher<ComputePipelineWrapper>(device)
    , device_(device)
    , pipelineCache_(sharedPipelineCache)
{
    std::cout << "[ComputePipelineCacher] Initialized" << std::endl;

    // Use shared cache if provided, otherwise create own (fallback)
    if (pipelineCache_ != VK_NULL_HANDLE) {
        std::cout << "[ComputePipelineCacher] Using shared VkPipelineCache: "
                  << reinterpret_cast<uint64_t>(pipelineCache_) << std::endl;
        ownsCache_ = false;
    } else {
        std::cout << "[ComputePipelineCacher] WARNING: No shared cache provided, creating own VkPipelineCache" << std::endl;

        VkPipelineCacheCreateInfo cacheInfo{};
        cacheInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;

        VkResult result = vkCreatePipelineCache(device_->device, &cacheInfo, nullptr, &pipelineCache_);
        if (result != VK_SUCCESS) {
            throw std::runtime_error("[ComputePipelineCacher] Failed to create pipeline cache");
        }

        ownsCache_ = true;
        std::cout << "[ComputePipelineCacher] Created own VkPipelineCache" << std::endl;
    }
}

ComputePipelineCacher::~ComputePipelineCacher() {
    std::cout << "[ComputePipelineCacher] Destructor called" << std::endl;
    std::cout << "[ComputePipelineCacher] Cache Stats - Hits: " << cacheHits_
              << ", Misses: " << cacheMisses_ << std::endl;
}

void ComputePipelineCacher::Cleanup() {
    std::cout << "[ComputePipelineCacher::Cleanup] Cleaning up " << m_entries.size()
              << " cached compute pipelines" << std::endl;

    // Destroy all cached Vulkan resources
    if (GetDevice()) {
        for (auto& [key, entry] : m_entries) {
            if (entry.resource) {
                if (entry.resource->pipeline != VK_NULL_HANDLE) {
                    std::cout << "[ComputePipelineCacher::Cleanup] Destroying VkPipeline: "
                              << reinterpret_cast<uint64_t>(entry.resource->pipeline) << std::endl;
                    vkDestroyPipeline(GetDevice()->device, entry.resource->pipeline, nullptr);
                    entry.resource->pipeline = VK_NULL_HANDLE;
                }

                // Pipeline layout is owned by PipelineLayoutCacher (shared resource)
                if (entry.resource->pipelineLayoutWrapper) {
                    std::cout << "[ComputePipelineCacher::Cleanup] Releasing shared pipeline layout wrapper" << std::endl;
                    entry.resource->pipelineLayoutWrapper.reset();
                }
            }
        }

        // Only destroy cache if we own it (not shared)
        if (ownsCache_ && pipelineCache_ != VK_NULL_HANDLE) {
            std::cout << "[ComputePipelineCacher::Cleanup] Destroying owned VkPipelineCache" << std::endl;
            vkDestroyPipelineCache(GetDevice()->device, pipelineCache_, nullptr);
            pipelineCache_ = VK_NULL_HANDLE;
            ownsCache_ = false;
        } else if (pipelineCache_ != VK_NULL_HANDLE) {
            std::cout << "[ComputePipelineCacher::Cleanup] Shared VkPipelineCache not destroyed (owned externally)" << std::endl;
        }
    }

    // Clear the cache entries after destroying resources
    Clear();

    std::cout << "[ComputePipelineCacher::Cleanup] Cleanup complete" << std::endl;
}

std::shared_ptr<ComputePipelineWrapper> ComputePipelineCacher::GetOrCreate(
    const ComputePipelineCreateParams& params)
{
    std::string cacheKey = GenerateCacheKey(params);

    // Check cache first
    {
        std::shared_lock rlock(m_lock);
        auto it = m_entries.find(std::hash<std::string>{}(cacheKey));
        if (it != m_entries.end()) {
            cacheHits_++;
            std::cout << "[ComputePipelineCacher::GetOrCreate] CACHE HIT for pipeline "
                      << params.shaderKey << " (key=" << cacheKey << ")" << std::endl;
            return it->second.resource;
        }
    }

    cacheMisses_++;
    std::cout << "[ComputePipelineCacher::GetOrCreate] CACHE MISS for pipeline "
              << params.shaderKey << " (key=" << cacheKey << "), creating new resource..." << std::endl;

    // Create new pipeline
    auto wrapper = CreatePipeline(params);

    // Store in cache
    {
        std::unique_lock wlock(m_lock);
        uint64_t keyHash = std::hash<std::string>{}(cacheKey);
        CacheEntry entry;
        entry.resource = wrapper;
        entry.key = keyHash;
        m_entries[keyHash] = std::move(entry);
    }

    std::cout << "[ComputePipelineCacher::GetOrCreate] VkComputePipeline created: "
              << reinterpret_cast<uint64_t>(wrapper->pipeline) << std::endl;

    return wrapper;
}

std::shared_ptr<ComputePipelineWrapper> ComputePipelineCacher::CreatePipeline(
    const ComputePipelineCreateParams& params)
{
    auto wrapper = std::make_shared<ComputePipelineWrapper>();
    wrapper->shaderKey = params.shaderKey;
    wrapper->layoutKey = params.layoutKey;
    wrapper->workgroupSizeX = params.workgroupSizeX;
    wrapper->workgroupSizeY = params.workgroupSizeY;
    wrapper->workgroupSizeZ = params.workgroupSizeZ;
    wrapper->cache = pipelineCache_;

    // Get or create pipeline layout
    if (params.pipelineLayoutWrapper) {
        // Explicit mode: Use provided layout wrapper
        wrapper->pipelineLayoutWrapper = params.pipelineLayoutWrapper;
        std::cout << "[ComputePipelineCacher::CreatePipeline] Using provided pipeline layout wrapper" << std::endl;
    } else {
        // Convenience mode: Create layout from descriptor set layout
        auto& layoutCacher = MainCacher::GetInstance().GetOrRegisterCacher<PipelineLayoutCacher>(device_);

        PipelineLayoutCreateParams layoutParams;
        layoutParams.descriptorSetLayouts.push_back(params.descriptorSetLayout);
        layoutParams.pushConstantRanges = params.pushConstantRanges;
        layoutParams.layoutKey = params.layoutKey;

        wrapper->pipelineLayoutWrapper = layoutCacher.GetOrCreate(layoutParams);
        std::cout << "[ComputePipelineCacher::CreatePipeline] Created pipeline layout via PipelineLayoutCacher" << std::endl;
    }

    VkPipelineLayout pipelineLayout = wrapper->pipelineLayoutWrapper->pipelineLayout;

    // Setup shader stage
    VkPipelineShaderStageCreateInfo shaderStageInfo{};
    shaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    shaderStageInfo.module = params.shaderModule;
    shaderStageInfo.pName = params.entryPoint;

    // Setup specialization constants (if provided)
    VkSpecializationInfo specInfo{};
    if (!params.specMapEntries.empty()) {
        specInfo.mapEntryCount = static_cast<uint32_t>(params.specMapEntries.size());
        specInfo.pMapEntries = params.specMapEntries.data();
        specInfo.dataSize = params.specData.size();
        specInfo.pData = params.specData.data();
        shaderStageInfo.pSpecializationInfo = &specInfo;
    }

    // Create compute pipeline
    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage = shaderStageInfo;
    pipelineInfo.layout = pipelineLayout;
    pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;
    pipelineInfo.basePipelineIndex = -1;

    VkResult result = vkCreateComputePipelines(
        device_->device,
        pipelineCache_,
        1,
        &pipelineInfo,
        nullptr,
        &wrapper->pipeline
    );

    if (result != VK_SUCCESS) {
        std::ostringstream err;
        err << "[ComputePipelineCacher::CreatePipeline] Failed to create compute pipeline"
            << " (VkResult=" << result << ")";
        throw std::runtime_error(err.str());
    }

    std::cout << "[ComputePipelineCacher::CreatePipeline] Created VkComputePipeline successfully"
              << " (workgroup: " << params.workgroupSizeX << "x"
              << params.workgroupSizeY << "x" << params.workgroupSizeZ << ")" << std::endl;

    return wrapper;
}

std::string ComputePipelineCacher::GenerateCacheKey(const ComputePipelineCreateParams& params) const
{
    std::ostringstream keyStream;
    keyStream << params.shaderKey << "|"
              << params.layoutKey << "|"
              << params.workgroupSizeX << "x"
              << params.workgroupSizeY << "x"
              << params.workgroupSizeZ;
    return keyStream.str();
}

} // namespace CashSystem
