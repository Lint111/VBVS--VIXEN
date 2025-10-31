#include "CashSystem/PipelineCacher.h"
#include "CashSystem/PipelineLayoutCacher.h"
#include "CashSystem/MainCacher.h"
#include "VulkanResources/VulkanDevice.h"
#include "Hash.h"
#include <sstream>
#include <stdexcept>
#include <vulkan/vulkan.h>
#include <iostream>
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
                if (entry.resource->cache != VK_NULL_HANDLE) {
                    std::cout << "[PipelineCacher::Cleanup] Destroying VkPipelineCache: "
                              << reinterpret_cast<uint64_t>(entry.resource->cache) << std::endl;
                    vkDestroyPipelineCache(GetDevice()->device, entry.resource->cache, nullptr);
                    entry.resource->cache = VK_NULL_HANDLE;
                }
            }
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

    // Create pipeline cache for performance
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
    // TODO: Implement serialization of pipeline cache data
    (void)path;
    return true;
}

bool PipelineCacher::DeserializeFromFile(const std::filesystem::path& path, void* device) {
    // TODO: Implement deserialization of pipeline cache data
    (void)path;
    (void)device;
    return true;
}

} // namespace CashSystem
