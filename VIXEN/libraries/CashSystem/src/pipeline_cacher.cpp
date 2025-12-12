#include "pch.h"
#include "PipelineCacher.h"
#include "PipelineLayoutCacher.h"
#include "MainCacher.h"
#include "VulkanDevice.h"
#include "VixenHash.h"
#include <sstream>
#include <stdexcept>
#include <vulkan/vulkan.h>
#include <fstream>
#include <filesystem>
#include <shared_mutex>

namespace CashSystem {

void PipelineCacher::Cleanup() {
    LOG_INFO("Cleaning up " + std::to_string(m_entries.size()) + " cached pipelines");

    // Destroy all cached Vulkan resources
    if (GetDevice()) {
        for (auto& [key, entry] : m_entries) {
            if (entry.resource) {
                if (entry.resource->pipeline != VK_NULL_HANDLE) {
                    LOG_DEBUG("Destroying VkPipeline: " + std::to_string(reinterpret_cast<uint64_t>(entry.resource->pipeline)));
                    vkDestroyPipeline(GetDevice()->device, entry.resource->pipeline, nullptr);
                    entry.resource->pipeline = VK_NULL_HANDLE;
                }
                // Pipeline layout is owned by PipelineLayoutCacher (shared resource)
                if (entry.resource->pipelineLayoutWrapper) {
                    LOG_DEBUG("Releasing shared pipeline layout wrapper");
                    entry.resource->pipelineLayoutWrapper.reset();
                }
                // Don't destroy individual caches if they're pointing to m_globalCache
                if (entry.resource->cache != VK_NULL_HANDLE && entry.resource->cache != m_globalCache) {
                    LOG_DEBUG("Destroying VkPipelineCache: " + std::to_string(reinterpret_cast<uint64_t>(entry.resource->cache)));
                    vkDestroyPipelineCache(GetDevice()->device, entry.resource->cache, nullptr);
                    entry.resource->cache = VK_NULL_HANDLE;
                }
            }
        }

        // Destroy global cache
        if (m_globalCache != VK_NULL_HANDLE) {
            LOG_DEBUG("Destroying global pipeline cache");
            vkDestroyPipelineCache(GetDevice()->device, m_globalCache, nullptr);
            m_globalCache = VK_NULL_HANDLE;
        }
    }

    // Clear the cache entries after destroying resources
    Clear();

    LOG_INFO("Cleanup complete");
}

std::shared_ptr<PipelineWrapper> PipelineCacher::GetOrCreate(const PipelineCreateParams& ci) {
    auto key = ComputeKey(ci);
    std::string pipelineName = ci.vertexShaderKey + "+" + ci.fragmentShaderKey;

    // Check cache first
    {
        std::shared_lock rlock(m_lock);
        auto it = m_entries.find(key);
        if (it != m_entries.end()) {
            LOG_DEBUG("CACHE HIT for pipeline " + pipelineName + " (key=" + std::to_string(key) + ")");
            return it->second.resource;
        }
        auto pit = m_pending.find(key);
        if (pit != m_pending.end()) {
            LOG_DEBUG("CACHE PENDING for pipeline " + pipelineName + " (key=" + std::to_string(key) + "), waiting...");
            return pit->second.get();
        }
    }

    LOG_DEBUG("CACHE MISS for pipeline " + pipelineName + " (key=" + std::to_string(key) + "), creating new resource...");

    // Call parent implementation which will invoke Create()
    return TypedCacher<PipelineWrapper, PipelineCreateParams>::GetOrCreate(ci);
}

std::shared_ptr<PipelineWrapper> PipelineCacher::GetOrCreatePipeline(
    const std::string& vertexShaderKey,
    const std::string& fragmentShaderKey,
    const std::string& layoutKey,
    const std::string& renderPassKey,
    bool enableDepthTest,
    VkCullModeFlags cullMode,
    VkPolygonMode polygonMode)
{
    LOG_DEBUG("GetOrCreatePipeline: " + vertexShaderKey + " + " + fragmentShaderKey);

    PipelineCreateParams params;
    params.vertexShaderKey = vertexShaderKey;
    params.fragmentShaderKey = fragmentShaderKey;
    params.layoutKey = layoutKey;
    params.renderPassKey = renderPassKey;
    params.enableDepthTest = enableDepthTest;
    params.cullMode = cullMode;
    params.polygonMode = polygonMode;

    return GetOrCreate(params);
}

std::shared_ptr<PipelineWrapper> PipelineCacher::Create(const PipelineCreateParams& ci) {
    LOG_DEBUG("Creating new pipeline: " + ci.vertexShaderKey + " + " + ci.fragmentShaderKey);

    auto wrapper = std::make_shared<PipelineWrapper>();
    wrapper->vertexShaderKey = ci.vertexShaderKey;
    wrapper->fragmentShaderKey = ci.fragmentShaderKey;
    wrapper->layoutKey = ci.layoutKey;
    wrapper->renderPassKey = ci.renderPassKey;
    wrapper->enableDepthTest = ci.enableDepthTest;
    wrapper->enableDepthWrite = ci.enableDepthWrite;
    wrapper->cullMode = ci.cullMode;
    wrapper->polygonMode = ci.polygonMode;
    wrapper->topology = ci.topology;

    // Create pipeline components
    LOG_DEBUG("Creating pipeline cache...");
    CreatePipelineCache(ci, *wrapper);
    LOG_DEBUG("Creating pipeline layout...");
    CreatePipelineLayout(ci, *wrapper);
    LOG_DEBUG("Creating VkPipeline...");
    CreatePipeline(ci, *wrapper);

    LOG_DEBUG("VkPipeline created: " + std::to_string(reinterpret_cast<uint64_t>(wrapper->pipeline)));

    return wrapper;
}

std::uint64_t PipelineCacher::ComputeKey(const PipelineCreateParams& ci) const {
    // Combine all parameters into a unique key
    std::ostringstream keyStream;
    keyStream << ci.vertexShaderKey << "|"
              << ci.fragmentShaderKey << "|"
              << ci.layoutKey << "|"
              << ci.renderPassKey << "|"
              << ci.enableDepthTest << "|"
              << ci.enableDepthWrite << "|"
              << ci.cullMode << "|"
              << ci.polygonMode << "|"
              << ci.topology;

    // Use hash function to create 64-bit key
    const std::string keyString = keyStream.str();
    std::uint64_t hash = std::hash<std::string>{}(keyString);

    LOG_DEBUG("ComputeKey: hash=" + std::to_string(hash));

    return hash;
}

void PipelineCacher::CreatePipeline(const PipelineCreateParams& ci, PipelineWrapper& wrapper) {
    if (!GetDevice()) {
        throw std::runtime_error("PipelineCacher: No device available for pipeline creation");
    }

    // Use dynamic shader stages (supports all 14 stage types)
    std::vector<VkPipelineShaderStageCreateInfo> shaderStages = ci.shaderStages;

    if (shaderStages.empty()) {
        throw std::runtime_error("PipelineCacher::CreatePipeline: No shader stages provided");
    }

    LOG_DEBUG("CreatePipeline: Using " + std::to_string(shaderStages.size()) + " shader stages");

    // Vertex input state
    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = static_cast<uint32_t>(ci.vertexBindings.size());
    vertexInputInfo.pVertexBindingDescriptions = ci.vertexBindings.data();
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(ci.vertexAttributes.size());
    vertexInputInfo.pVertexAttributeDescriptions = ci.vertexAttributes.data();

    // Input assembly
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = ci.topology;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    // Viewport/scissor state (dynamic)
    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    // Rasterization
    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = ci.polygonMode;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = ci.cullMode;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;

    // Multisampling
    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // Depth/stencil
    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = ci.enableDepthTest ? VK_TRUE : VK_FALSE;
    depthStencil.depthWriteEnable = ci.enableDepthWrite ? VK_TRUE : VK_FALSE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    // Color blending
    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    // Dynamic state
    VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = 2;
    dynamicState.pDynamicStates = dynamicStates;

    // Pipeline create info
    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
    pipelineInfo.pStages = shaderStages.data();
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = wrapper.pipelineLayoutWrapper->layout;  // Use shared layout
    pipelineInfo.renderPass = ci.renderPass;
    pipelineInfo.subpass = 0;

    VkResult result = vkCreateGraphicsPipelines(
        GetDevice()->device,
        wrapper.cache,
        1,
        &pipelineInfo,
        nullptr,
        &wrapper.pipeline
    );

    if (result != VK_SUCCESS) {
        throw std::runtime_error("Failed to create graphics pipeline");
    }
}

void PipelineCacher::CreatePipelineLayout(const PipelineCreateParams& ci, PipelineWrapper& wrapper) {
    if (!GetDevice()) {
        throw std::runtime_error("PipelineCacher: No device available for pipeline layout creation");
    }

    // ===== Explicit Path: Use provided wrapper (transparent) =====
    if (ci.pipelineLayoutWrapper) {
        LOG_DEBUG("Using explicitly provided VkPipelineLayout: " + std::to_string(reinterpret_cast<uint64_t>(ci.pipelineLayoutWrapper->layout)));
        wrapper.pipelineLayoutWrapper = ci.pipelineLayoutWrapper;
        return;
    }

    // ===== Convenience Path: Create from descriptor set layout =====
    LOG_DEBUG("No layout wrapper provided, using convenience path (PipelineLayoutCacher)");

    // Get PipelineLayoutCacher from MainCacher (register if needed)
    auto& mainCacher = MainCacher::Instance();

    if (!mainCacher.IsRegistered(typeid(PipelineLayoutWrapper))) {
        LOG_DEBUG("Registering PipelineLayoutCacher");
        mainCacher.RegisterCacher<
            PipelineLayoutCacher,
            PipelineLayoutWrapper,
            PipelineLayoutCreateParams
        >(
            typeid(PipelineLayoutWrapper),
            "PipelineLayout",
            true  // device-dependent
        );
    }

    auto* layoutCacher = mainCacher.GetCacher<
        PipelineLayoutCacher,
        PipelineLayoutWrapper,
        PipelineLayoutCreateParams
    >(typeid(PipelineLayoutWrapper), GetDevice());

    if (!layoutCacher) {
        throw std::runtime_error("PipelineCacher: Failed to get PipelineLayoutCacher");
    }

    // Get or create shared pipeline layout
    PipelineLayoutCreateParams layoutParams;
    layoutParams.descriptorSetLayout = ci.descriptorSetLayout;
    layoutParams.pushConstantRanges = ci.pushConstantRanges;  // Phase 5: Use push constants from reflection
    layoutParams.layoutKey = ci.layoutKey;

    wrapper.pipelineLayoutWrapper = layoutCacher->GetOrCreate(layoutParams);

    if (!wrapper.pipelineLayoutWrapper || wrapper.pipelineLayoutWrapper->layout == VK_NULL_HANDLE) {
        throw std::runtime_error("PipelineCacher: Failed to create/get pipeline layout");
    }

    LOG_DEBUG("Using shared VkPipelineLayout: " + std::to_string(reinterpret_cast<uint64_t>(wrapper.pipelineLayoutWrapper->layout)));
}

void PipelineCacher::CreatePipelineCache(const PipelineCreateParams& ci, PipelineWrapper& wrapper) {
    if (!GetDevice()) {
        return;
    }

    // If we have a global cache, merge it with the new cache
    // This allows new pipelines to benefit from cached data
    if (m_globalCache != VK_NULL_HANDLE) {
        // Just use the global cache directly instead of creating individual caches
        wrapper.cache = m_globalCache;
        return;
    }

    // Create pipeline cache for performance (fallback if no global cache)
    VkPipelineCacheCreateInfo cacheInfo{};
    cacheInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;

    VkResult result = vkCreatePipelineCache(
        GetDevice()->device,
        &cacheInfo,
        nullptr,
        &wrapper.cache
    );

    if (result != VK_SUCCESS) {
        // Non-fatal - can still create pipelines without cache
        wrapper.cache = VK_NULL_HANDLE;
    }
}

bool PipelineCacher::SerializeToFile(const std::filesystem::path& path) const {
    if (!GetDevice()) {
        LOG_ERROR("Cannot serialize: no device available");
        return false;
    }

    // Collect all valid pipeline caches from entries
    std::vector<VkPipelineCache> caches;
    for (const auto& [key, entry] : m_entries) {
        if (entry.resource && entry.resource->cache != VK_NULL_HANDLE) {
            caches.push_back(entry.resource->cache);
        }
    }

    if (caches.empty()) {
        LOG_INFO("No pipeline caches to serialize");
        return true;
    }

    // Merge all caches into a single cache for serialization
    VkPipelineCacheCreateInfo mergedCacheInfo{};
    mergedCacheInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;

    VkPipelineCache mergedCache = VK_NULL_HANDLE;
    VkResult result = vkCreatePipelineCache(GetDevice()->device, &mergedCacheInfo, nullptr, &mergedCache);
    if (result != VK_SUCCESS) {
        LOG_ERROR("Failed to create merged cache: " + std::to_string(result));
        return false;
    }

    // Merge all individual caches into the merged cache
    result = vkMergePipelineCaches(GetDevice()->device, mergedCache,
                                   static_cast<uint32_t>(caches.size()), caches.data());
    if (result != VK_SUCCESS) {
        LOG_ERROR("Failed to merge pipeline caches: " + std::to_string(result));
        vkDestroyPipelineCache(GetDevice()->device, mergedCache, nullptr);
        return false;
    }

    // Get size of cache data
    size_t cacheSize = 0;
    result = vkGetPipelineCacheData(GetDevice()->device, mergedCache, &cacheSize, nullptr);
    if (result != VK_SUCCESS || cacheSize == 0) {
        LOG_ERROR("Failed to get cache size: " + std::to_string(result));
        vkDestroyPipelineCache(GetDevice()->device, mergedCache, nullptr);
        return false;
    }

    // Get cache data
    std::vector<uint8_t> cacheData(cacheSize);
    result = vkGetPipelineCacheData(GetDevice()->device, mergedCache, &cacheSize, cacheData.data());
    if (result != VK_SUCCESS) {
        LOG_ERROR("Failed to get cache data: " + std::to_string(result));
        vkDestroyPipelineCache(GetDevice()->device, mergedCache, nullptr);
        return false;
    }

    // Destroy merged cache (no longer needed)
    vkDestroyPipelineCache(GetDevice()->device, mergedCache, nullptr);

    // Write cache data to file
    std::ofstream file(path, std::ios::binary);
    if (!file) {
        LOG_ERROR("Failed to open file for writing: " + path.string());
        return false;
    }

    // Write version header (for future compatibility)
    uint32_t version = 1;
    file.write(reinterpret_cast<const char*>(&version), sizeof(version));

    // Write cache size
    uint64_t size64 = static_cast<uint64_t>(cacheSize);
    file.write(reinterpret_cast<const char*>(&size64), sizeof(size64));

    // Write cache data
    file.write(reinterpret_cast<const char*>(cacheData.data()), cacheSize);

    if (!file) {
        LOG_ERROR("Failed to write cache data to file");
        return false;
    }

    LOG_INFO("Serialized " + std::to_string(caches.size()) + " pipeline caches (" + std::to_string(cacheSize) + " bytes) to " + path.string());
    return true;
}

bool PipelineCacher::DeserializeFromFile(const std::filesystem::path& path, void* device) {
    // Not an error if cache file doesn't exist yet
    if (!std::filesystem::exists(path)) {
        LOG_INFO("No cache file found at " + path.string() + " (first run)");
        return true;
    }

    if (!GetDevice()) {
        LOG_ERROR("Cannot deserialize: no device available");
        return false;
    }

    // Open cache file
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        LOG_ERROR("Failed to open cache file: " + path.string());
        return false;
    }

    // Read version header
    uint32_t version = 0;
    file.read(reinterpret_cast<char*>(&version), sizeof(version));
    if (!file || version != 1) {
        LOG_ERROR("Unsupported cache version: " + std::to_string(version));
        return false;
    }

    // Read cache size
    uint64_t cacheSize = 0;
    file.read(reinterpret_cast<char*>(&cacheSize), sizeof(cacheSize));
    if (!file || cacheSize == 0) {
        LOG_ERROR("Invalid cache size in file");
        return false;
    }

    // Read cache data
    std::vector<uint8_t> cacheData(cacheSize);
    file.read(reinterpret_cast<char*>(cacheData.data()), cacheSize);
    if (!file) {
        LOG_ERROR("Failed to read cache data from file");
        return false;
    }

    // Create global pipeline cache from loaded data
    VkPipelineCacheCreateInfo cacheInfo{};
    cacheInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
    cacheInfo.initialDataSize = cacheSize;
    cacheInfo.pInitialData = cacheData.data();

    VkResult result = vkCreatePipelineCache(GetDevice()->device, &cacheInfo, nullptr, &m_globalCache);
    if (result != VK_SUCCESS) {
        LOG_ERROR("Failed to create pipeline cache from file: " + std::to_string(result));
        m_globalCache = VK_NULL_HANDLE;
        return false;
    }

    LOG_INFO("Loaded pipeline cache from " + path.string() + " (" + std::to_string(cacheSize) + " bytes)");
    return true;
}

} // namespace CashSystem
