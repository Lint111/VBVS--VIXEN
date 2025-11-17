#include "PipelineCacher.h"
#include "PipelineLayoutCacher.h"
#include "MainCacher.h"
#include "VulkanDevice.h"
#include "VixenHash.h"
#include <sstream>
#include <stdexcept>
#include <vulkan/vulkan.h>
#include <iostream>
#include <fstream>
#include <filesystem>
#include <shared_mutex>

namespace CashSystem {

void PipelineCacher::Cleanup() {
    std::cout << "[PipelineCacher::Cleanup] Cleaning up " << m_entries.size() << " cached pipelines" << std::endl;

    // Destroy all cached Vulkan resources
    if (GetDevice()) {
        for (auto& [key, entry] : m_entries) {
            if (entry.resource) {
                if (entry.resource->pipeline != VK_NULL_HANDLE) {
                    std::cout << "[PipelineCacher::Cleanup] Destroying VkPipeline: "
                              << reinterpret_cast<uint64_t>(entry.resource->pipeline) << std::endl;
                    vkDestroyPipeline(GetDevice()->device, entry.resource->pipeline, nullptr);
                    entry.resource->pipeline = VK_NULL_HANDLE;
                }
                // Pipeline layout is owned by PipelineLayoutCacher (shared resource)
                if (entry.resource->pipelineLayoutWrapper) {
                    std::cout << "[PipelineCacher::Cleanup] Releasing shared pipeline layout wrapper" << std::endl;
                    entry.resource->pipelineLayoutWrapper.reset();
                }
                // Don't destroy individual caches if they're pointing to m_globalCache
                if (entry.resource->cache != VK_NULL_HANDLE && entry.resource->cache != m_globalCache) {
                    std::cout << "[PipelineCacher::Cleanup] Destroying VkPipelineCache: "
                              << reinterpret_cast<uint64_t>(entry.resource->cache) << std::endl;
                    vkDestroyPipelineCache(GetDevice()->device, entry.resource->cache, nullptr);
                    entry.resource->cache = VK_NULL_HANDLE;
                }
            }
        }

        // Destroy global cache
        if (m_globalCache != VK_NULL_HANDLE) {
            std::cout << "[PipelineCacher::Cleanup] Destroying global pipeline cache" << std::endl;
            vkDestroyPipelineCache(GetDevice()->device, m_globalCache, nullptr);
            m_globalCache = VK_NULL_HANDLE;
        }
    }

    // Clear the cache entries after destroying resources
    Clear();

    std::cout << "[PipelineCacher::Cleanup] Cleanup complete" << std::endl;
}

std::shared_ptr<PipelineWrapper> PipelineCacher::GetOrCreate(const PipelineCreateParams& ci) {
    auto key = ComputeKey(ci);
    std::string pipelineName = ci.vertexShaderKey + "+" + ci.fragmentShaderKey;

    // Check cache first
    {
        std::shared_lock rlock(m_lock);
        auto it = m_entries.find(key);
        if (it != m_entries.end()) {
            std::cout << "[PipelineCacher::GetOrCreate] CACHE HIT for pipeline " << pipelineName
                      << " (key=" << key << ", VkPipeline="
                      << reinterpret_cast<uint64_t>(it->second.resource->pipeline) << ")" << std::endl;
            return it->second.resource;
        }
        auto pit = m_pending.find(key);
        if (pit != m_pending.end()) {
            std::cout << "[PipelineCacher::GetOrCreate] CACHE PENDING for pipeline " << pipelineName
                      << " (key=" << key << "), waiting..." << std::endl;
            return pit->second.get();
        }
    }

    std::cout << "[PipelineCacher::GetOrCreate] CACHE MISS for pipeline " << pipelineName
              << " (key=" << key << "), creating new resource..." << std::endl;

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
    std::cout << "[PipelineCacher] GetOrCreatePipeline ENTRY: " << vertexShaderKey << " + " << fragmentShaderKey << std::endl;

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
    std::cout << "[PipelineCacher::Create] CACHE MISS - Creating new pipeline: "
              << ci.vertexShaderKey << " + " << ci.fragmentShaderKey << std::endl;

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
    std::cout << "[PipelineCacher::Create] Creating pipeline cache..." << std::endl;
    CreatePipelineCache(ci, *wrapper);
    std::cout << "[PipelineCacher::Create] Creating pipeline layout..." << std::endl;
    CreatePipelineLayout(ci, *wrapper);
    std::cout << "[PipelineCacher::Create] Creating VkPipeline..." << std::endl;
    CreatePipeline(ci, *wrapper);

    std::cout << "[PipelineCacher::Create] VkPipeline created: "
              << reinterpret_cast<uint64_t>(wrapper->pipeline) << std::endl;

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

    std::cout << "[PipelineCacher::ComputeKey] keyString=\"" << keyString << "\" hash=" << hash << std::endl;

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

    std::cout << "[PipelineCacher::CreatePipeline] Using " << shaderStages.size()
              << " shader stages" << std::endl;

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
        std::cout << "[PipelineCacher] Using explicitly provided VkPipelineLayout: "
                  << reinterpret_cast<uint64_t>(ci.pipelineLayoutWrapper->layout) << std::endl;
        wrapper.pipelineLayoutWrapper = ci.pipelineLayoutWrapper;
        return;
    }

    // ===== Convenience Path: Create from descriptor set layout =====
    std::cout << "[PipelineCacher] No layout wrapper provided, using convenience path (PipelineLayoutCacher)" << std::endl;

    // Get PipelineLayoutCacher from MainCacher (register if needed)
    auto& mainCacher = MainCacher::Instance();

    if (!mainCacher.IsRegistered(typeid(PipelineLayoutWrapper))) {
        std::cout << "[PipelineCacher] Registering PipelineLayoutCacher" << std::endl;
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

    std::cout << "[PipelineCacher] Using shared VkPipelineLayout: "
              << reinterpret_cast<uint64_t>(wrapper.pipelineLayoutWrapper->layout) << std::endl;
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
        std::cerr << "[PipelineCacher] Cannot serialize: no device available" << std::endl;
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
        std::cout << "[PipelineCacher] No pipeline caches to serialize" << std::endl;
        return true;
    }

    // Merge all caches into a single cache for serialization
    VkPipelineCacheCreateInfo mergedCacheInfo{};
    mergedCacheInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;

    VkPipelineCache mergedCache = VK_NULL_HANDLE;
    VkResult result = vkCreatePipelineCache(GetDevice()->device, &mergedCacheInfo, nullptr, &mergedCache);
    if (result != VK_SUCCESS) {
        std::cerr << "[PipelineCacher] Failed to create merged cache: " << result << std::endl;
        return false;
    }

    // Merge all individual caches into the merged cache
    result = vkMergePipelineCaches(GetDevice()->device, mergedCache,
                                   static_cast<uint32_t>(caches.size()), caches.data());
    if (result != VK_SUCCESS) {
        std::cerr << "[PipelineCacher] Failed to merge pipeline caches: " << result << std::endl;
        vkDestroyPipelineCache(GetDevice()->device, mergedCache, nullptr);
        return false;
    }

    // Get size of cache data
    size_t cacheSize = 0;
    result = vkGetPipelineCacheData(GetDevice()->device, mergedCache, &cacheSize, nullptr);
    if (result != VK_SUCCESS || cacheSize == 0) {
        std::cerr << "[PipelineCacher] Failed to get cache size: " << result << std::endl;
        vkDestroyPipelineCache(GetDevice()->device, mergedCache, nullptr);
        return false;
    }

    // Get cache data
    std::vector<uint8_t> cacheData(cacheSize);
    result = vkGetPipelineCacheData(GetDevice()->device, mergedCache, &cacheSize, cacheData.data());
    if (result != VK_SUCCESS) {
        std::cerr << "[PipelineCacher] Failed to get cache data: " << result << std::endl;
        vkDestroyPipelineCache(GetDevice()->device, mergedCache, nullptr);
        return false;
    }

    // Destroy merged cache (no longer needed)
    vkDestroyPipelineCache(GetDevice()->device, mergedCache, nullptr);

    // Write cache data to file
    std::ofstream file(path, std::ios::binary);
    if (!file) {
        std::cerr << "[PipelineCacher] Failed to open file for writing: " << path << std::endl;
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
        std::cerr << "[PipelineCacher] Failed to write cache data to file" << std::endl;
        return false;
    }

    std::cout << "[PipelineCacher] Serialized " << caches.size() << " pipeline caches ("
              << cacheSize << " bytes) to " << path << std::endl;
    return true;
}

bool PipelineCacher::DeserializeFromFile(const std::filesystem::path& path, void* device) {
    // Not an error if cache file doesn't exist yet
    if (!std::filesystem::exists(path)) {
        std::cout << "[PipelineCacher] No cache file found at " << path << " (first run)" << std::endl;
        return true;
    }

    if (!GetDevice()) {
        std::cerr << "[PipelineCacher] Cannot deserialize: no device available" << std::endl;
        return false;
    }

    // Open cache file
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        std::cerr << "[PipelineCacher] Failed to open cache file: " << path << std::endl;
        return false;
    }

    // Read version header
    uint32_t version = 0;
    file.read(reinterpret_cast<char*>(&version), sizeof(version));
    if (!file || version != 1) {
        std::cerr << "[PipelineCacher] Unsupported cache version: " << version << std::endl;
        return false;
    }

    // Read cache size
    uint64_t cacheSize = 0;
    file.read(reinterpret_cast<char*>(&cacheSize), sizeof(cacheSize));
    if (!file || cacheSize == 0) {
        std::cerr << "[PipelineCacher] Invalid cache size in file" << std::endl;
        return false;
    }

    // Read cache data
    std::vector<uint8_t> cacheData(cacheSize);
    file.read(reinterpret_cast<char*>(cacheData.data()), cacheSize);
    if (!file) {
        std::cerr << "[PipelineCacher] Failed to read cache data from file" << std::endl;
        return false;
    }

    // Create global pipeline cache from loaded data
    VkPipelineCacheCreateInfo cacheInfo{};
    cacheInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
    cacheInfo.initialDataSize = cacheSize;
    cacheInfo.pInitialData = cacheData.data();

    VkResult result = vkCreatePipelineCache(GetDevice()->device, &cacheInfo, nullptr, &m_globalCache);
    if (result != VK_SUCCESS) {
        std::cerr << "[PipelineCacher] Failed to create pipeline cache from file: " << result << std::endl;
        m_globalCache = VK_NULL_HANDLE;
        return false;
    }

    std::cout << "[PipelineCacher] Loaded pipeline cache from " << path
              << " (" << cacheSize << " bytes)" << std::endl;
    return true;
}

} // namespace CashSystem
