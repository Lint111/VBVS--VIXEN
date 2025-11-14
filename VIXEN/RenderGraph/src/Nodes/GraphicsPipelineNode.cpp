#include "Nodes/GraphicsPipelineNode.h"
#include "Core/RenderGraph.h"
#include "VulkanResources/VulkanDevice.h"
#include "VulkanSwapChain.h"
#include "Core/NodeLogging.h"
#include "CashSystem/MainCacher.h"
#include "CashSystem/PipelineCacher.h"
#include "CashSystem/PipelineLayoutCacher.h"
#include "CashSystem/DescriptorSetLayoutCacher.h"
#include "CashSystem/ShaderModuleCacher.h"
#include <ShaderManagement/ShaderDataBundle.h>
#include <ShaderManagement/ShaderStage.h>
#include "NodeHelpers/EnumParsers.h"
#include "NodeHelpers/VulkanStructHelpers.h"
#include <cstring>

using namespace RenderGraph::NodeHelpers;

namespace Vixen::RenderGraph {

// ====== GraphicsPipelineNodeType ======

std::unique_ptr<NodeInstance> GraphicsPipelineNodeType::CreateInstance(
    const std::string& instanceName
) const {
    return std::make_unique<GraphicsPipelineNode>(
        instanceName,
        const_cast<GraphicsPipelineNodeType*>(this)
    );
}

// ====== GraphicsPipelineNode ======

GraphicsPipelineNode::GraphicsPipelineNode(
    const std::string& instanceName,
    NodeType* nodeType
)
    : TypedNode<GraphicsPipelineNodeConfig>(instanceName, nodeType)
{
}

void GraphicsPipelineNode::SetupImpl(TypedSetupContext& ctx) {
    // Graph-scope initialization only (no input access)
    NODE_LOG_DEBUG("GraphicsPipelineNode: Setup (graph-scope initialization)");
}

void GraphicsPipelineNode::CompileImpl(TypedCompileContext& ctx) {
    // Access device input (compile-time dependency)
    VulkanDevice* devicePtr = ctx.In(GraphicsPipelineNodeConfig::VULKAN_DEVICE_IN);

    if (devicePtr == nullptr) {
        std::string errorMsg = "GraphicsPipelineNode: VkDevice input is null";
        NODE_LOG_ERROR(errorMsg);
        throw std::runtime_error(errorMsg);
    }

    // Set base class device member for cleanup tracking
    SetDevice(devicePtr);

    // Get parameters using typed config constants
    enableDepthTest = GetParameterValue<bool>(GraphicsPipelineNodeConfig::ENABLE_DEPTH_TEST, true);
    enableDepthWrite = GetParameterValue<bool>(GraphicsPipelineNodeConfig::ENABLE_DEPTH_WRITE, true);
    enableVertexInput = GetParameterValue<bool>(GraphicsPipelineNodeConfig::ENABLE_VERTEX_INPUT, true);

    std::string cullModeStr = GetParameterValue<std::string>(GraphicsPipelineNodeConfig::CULL_MODE, "Back");
    std::string polygonModeStr = GetParameterValue<std::string>(GraphicsPipelineNodeConfig::POLYGON_MODE, "Fill");
    std::string topologyStr = GetParameterValue<std::string>(GraphicsPipelineNodeConfig::TOPOLOGY, "TriangleList");
    std::string frontFaceStr = GetParameterValue<std::string>(GraphicsPipelineNodeConfig::FRONT_FACE, "CounterClockwise");

    cullMode = ParseCullMode(cullModeStr);
    polygonMode = ParsePolygonMode(polygonModeStr);
    topology = ParseTopology(topologyStr);
    frontFace = ParseFrontFace(frontFaceStr);

    // Get inputs
    currentShaderBundle =  ctx.In(GraphicsPipelineNodeConfig::SHADER_DATA_BUNDLE);  // Store for use in helper functions
    VkRenderPass renderPass = ctx.In(GraphicsPipelineNodeConfig::RENDER_PASS);
    VkDescriptorSetLayout manualDescriptorSetLayout = ctx.In(GraphicsPipelineNodeConfig::DESCRIPTOR_SET_LAYOUT);
    // Note: SWAPCHAIN_INFO not used during pipeline compilation (pipelines are swapchain-independent)
    // Image views are bound at execute-time via descriptors, not baked into pipeline

    // Validate inputs
    if (!currentShaderBundle) {
        throw std::runtime_error("GraphicsPipelineNode: shader bundle not set");
    }
    if (renderPass == VK_NULL_HANDLE) {
        throw std::runtime_error("GraphicsPipelineNode: render pass not set");
    }

    // Automatically generate descriptor set layout from shader reflection (Phase 4)
    // This eliminates manual configuration - layouts are extracted from SPIR-V

    if (manualDescriptorSetLayout != VK_NULL_HANDLE) {
        // Backward compatibility: Use manual layout if provided
        this->descriptorSetLayout = manualDescriptorSetLayout;
        NODE_LOG_INFO("GraphicsPipelineNode: Using manually provided descriptor set layout");
    } else {
        // NEW (Phase 4): Auto-generate from ShaderDataBundle reflection
        NODE_LOG_INFO("GraphicsPipelineNode: Auto-generating descriptor set layout from shader reflection");

        auto* renderGraph = GetOwningGraph();
        if (!renderGraph) {
            throw std::runtime_error("GraphicsPipelineNode: Owning graph not available");
        }

        auto& mainCacher = renderGraph->GetMainCacher();

        VulkanDevice* device = GetDevice();
        if (!device) {
            throw std::runtime_error("GraphicsPipelineNode: Device not available for descriptor layout creation");
        }

        auto* descLayoutCacher = mainCacher.GetCacher<
            CashSystem::DescriptorSetLayoutCacher,
            CashSystem::DescriptorSetLayoutWrapper,
            CashSystem::DescriptorSetLayoutCreateParams
        >(typeid(CashSystem::DescriptorSetLayoutWrapper), device);

        if (!descLayoutCacher) {
            throw std::runtime_error("GraphicsPipelineNode: Failed to get DescriptorSetLayoutCacher");
        }

        // Create descriptor layout from bundle
        CashSystem::DescriptorSetLayoutCreateParams layoutParams;
        layoutParams.shaderBundle = currentShaderBundle;
        layoutParams.descriptorSetIndex = 0;  // Use set 0
        layoutParams.layoutKey = currentShaderBundle->descriptorInterfaceHash;  // Content-based key
        layoutParams.device = device;

        auto layoutWrapper = descLayoutCacher->GetOrCreate(layoutParams);
        if (!layoutWrapper || layoutWrapper->layout == VK_NULL_HANDLE) {
            throw std::runtime_error("GraphicsPipelineNode: Failed to create descriptor set layout from reflection");
        }

        this->descriptorSetLayout = layoutWrapper->layout;
        NODE_LOG_INFO("GraphicsPipelineNode: Successfully auto-generated descriptor set layout");
    }

    NODE_LOG_DEBUG("GraphicsPipelineNode::Compile - descriptor set layout: " + std::to_string(reinterpret_cast<uint64_t>(this->descriptorSetLayout)));

    // Extract push constants from shader reflection (Phase 5)
    pushConstantRanges = CashSystem::ExtractPushConstantsFromReflection(*currentShaderBundle);
    NODE_LOG_INFO("GraphicsPipelineNode: Extracted " + std::to_string(pushConstantRanges.size()) + " push constant ranges from shader reflection");

    // Build shader stage create infos from reflection
    BuildShaderStages(currentShaderBundle);

    // Create graphics pipeline with caching (cacher will create pipeline layout)
    CreatePipelineWithCache(ctx);
    
    // Set outputs
    ctx.Out(GraphicsPipelineNodeConfig::PIPELINE, pipeline);
    ctx.Out(GraphicsPipelineNodeConfig::PIPELINE_LAYOUT, pipelineLayout);
    ctx.Out(GraphicsPipelineNodeConfig::PIPELINE_CACHE, pipelineCache);
    ctx.Out(GraphicsPipelineNodeConfig::VULKAN_DEVICE_OUT, device);
}

void GraphicsPipelineNode::ExecuteImpl(TypedExecuteContext& ctx) {
    // Pipeline creation happens in Compile phase
    // Execute is a no-op for this node
}

void GraphicsPipelineNode::CleanupImpl(TypedCleanupContext& ctx) {
    // If we have a cached pipeline wrapper, just release the shared_ptr
    // The cacher owns VkPipeline, VkPipelineLayout, and VkPipelineCache - will destroy when appropriate
    if (cachedPipelineWrapper) {
        NODE_LOG_DEBUG("GraphicsPipelineNode::CleanupImpl: Releasing cached pipeline wrapper (cacher owns all resources)");
        cachedPipelineWrapper.reset();
        pipeline = VK_NULL_HANDLE;
        pipelineLayout = VK_NULL_HANDLE;
        pipelineCache = VK_NULL_HANDLE;
    } else {
        // Fallback: cleanup locally-created resources (if created without cacher)
    if (pipeline != VK_NULL_HANDLE && device != nullptr) {
            NODE_LOG_DEBUG("GraphicsPipelineNode::CleanupImpl: Destroying locally-created pipeline");
            vkDestroyPipeline(device->device, pipeline, nullptr);
            pipeline = VK_NULL_HANDLE;
        }

    if (pipelineLayout != VK_NULL_HANDLE && device != nullptr) {
            NODE_LOG_DEBUG("GraphicsPipelineNode::CleanupImpl: Destroying locally-created pipeline layout");
            vkDestroyPipelineLayout(device->device, pipelineLayout, nullptr);
            pipelineLayout = VK_NULL_HANDLE;
        }

    if (pipelineCache != VK_NULL_HANDLE && device != nullptr) {
            NODE_LOG_DEBUG("GraphicsPipelineNode::CleanupImpl: Destroying locally-created pipeline cache");
            vkDestroyPipelineCache(device->device, pipelineCache, nullptr);
            pipelineCache = VK_NULL_HANDLE;
        }
    }
}

void GraphicsPipelineNode::CreatePipelineCache() {
    // Only create if not already created
    if (pipelineCache != VK_NULL_HANDLE) {
        NODE_LOG_DEBUG("GraphicsPipelineNode: Pipeline cache already exists, reusing");
        return;
    }

    VkPipelineCacheCreateInfo cacheInfo{};
    cacheInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
    cacheInfo.pNext = nullptr;
    cacheInfo.initialDataSize = 0;
    cacheInfo.pInitialData = nullptr;
    cacheInfo.flags = 0;

    VkResult result = vkCreatePipelineCache(
        device->device,
        &cacheInfo,
        nullptr,
        &pipelineCache
    );

    if (result != VK_SUCCESS) {
        throw std::runtime_error("Failed to create pipeline cache");
    }
}

void GraphicsPipelineNode::CreatePipelineLayout() {
    // Destroy old layout if exists (happens during recompile)
    if (pipelineLayout != VK_NULL_HANDLE && device != VK_NULL_HANDLE) {
        NODE_LOG_DEBUG("GraphicsPipelineNode: Destroying old pipeline layout before recompile");
        vkDestroyPipelineLayout(device->device, pipelineLayout, nullptr);
        pipelineLayout = VK_NULL_HANDLE;
    }

    // NOTE: descriptorSetLayout is already set in CompileImpl (either manual or auto-generated)
    // We use the class member 'descriptorSetLayout' which was set during compilation

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.pNext = nullptr;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &descriptorSetLayout;  // Use the compiled descriptor layout
    layoutInfo.pushConstantRangeCount = 0;
    layoutInfo.pPushConstantRanges = nullptr;

    VkResult result = vkCreatePipelineLayout(
        device->device,
        &layoutInfo,
        nullptr,
        &pipelineLayout
    );

    if (result != VK_SUCCESS) {
        throw std::runtime_error("Failed to create pipeline layout");
    }
}

void GraphicsPipelineNode::BuildShaderStages(std::shared_ptr<ShaderManagement::ShaderDataBundle> bundle) {
    NODE_LOG_INFO("GraphicsPipelineNode: Building shader stages from reflection");

    // Get ShaderModuleCacher from main cacher
    auto& mainCacher = GetOwningGraph()->GetMainCacher();

    // Get cacher (should already be registered by ShaderLibraryNode)
    auto shaderModuleCacher = mainCacher.GetCacher<
        CashSystem::ShaderModuleCacher,
        CashSystem::ShaderModuleWrapper,
        CashSystem::ShaderModuleCreateParams
    >(typeid(CashSystem::ShaderModuleWrapper), device);

    if (!shaderModuleCacher) {
        throw std::runtime_error("GraphicsPipelineNode: ShaderModuleCacher not available");
    }

    // Clear previous stages
    shaderStageInfos.clear();
    shaderModules.clear();

    // Build VkPipelineShaderStageCreateInfo from reflection data
    for (const auto& stage : bundle->program.stages) {
        // Convert ShaderStage enum directly to VkShaderStageFlagBits (they're the same values)
        VkShaderStageFlagBits vkStage = ShaderManagement::ToVulkanStage(stage.stage);
        const char* stageName = ShaderManagement::ShaderStageName(stage.stage);

        // Get shader module from cacher
        auto shaderModule = shaderModuleCacher->GetOrCreateFromSpirv(
            stage.spirvCode,
            stage.entryPoint,
            {},  // no macros
            vkStage,
            std::string(bundle->program.name) + "_" + stageName
        );

        // Store module in map for lifetime management
        shaderModules[stage.stage] = shaderModule;

        // Build VkPipelineShaderStageCreateInfo
        VkPipelineShaderStageCreateInfo stageInfo{};
        stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stageInfo.stage = vkStage;
        stageInfo.module = shaderModule->shaderModule;
        stageInfo.pName = stage.entryPoint.c_str();
        shaderStageInfos.push_back(stageInfo);

        NODE_LOG_INFO("GraphicsPipelineNode: " + std::string(stageName) + " shader module: " +
                     std::to_string(reinterpret_cast<uint64_t>(shaderModule->shaderModule)));
    }

    NODE_LOG_INFO("GraphicsPipelineNode: Built " + std::to_string(shaderStageInfos.size()) + " shader stages");
}

void GraphicsPipelineNode::BuildVertexInputsFromReflection(
    std::shared_ptr<ShaderManagement::ShaderDataBundle> bundle,
    std::vector<VkVertexInputBindingDescription>& outBindings,
    std::vector<VkVertexInputAttributeDescription>& outAttributes)
{
    outBindings.clear();
    outAttributes.clear();

    // Check if reflection data is available
    bool hasReflection = (bundle && bundle->reflectionData && !bundle->GetVertexInputs().empty());

    NODE_LOG_DEBUG("BuildVertexInputsFromReflection: bundle=" + std::string(bundle ? "valid" : "null") +
                   " reflectionData=" + std::string(bundle && bundle->reflectionData ? "valid" : "null") +
                   " vertexInputCount=" + std::to_string(bundle ? bundle->GetVertexInputs().size() : 0) +
                   " hasReflection=" + std::string(hasReflection ? "true" : "false"));

    if (hasReflection) {
        // Build from reflection
        const auto& vertexInputs = bundle->GetVertexInputs();

        for (const auto& input : vertexInputs) {
            VkVertexInputAttributeDescription attr{};
            attr.location = input.location;
            attr.binding = 0;
            attr.format = input.format;
            attr.offset = 0;  // Will be computed below
            outAttributes.push_back(attr);

            NODE_LOG_DEBUG("GraphicsPipelineNode: Vertex input location=" + std::to_string(input.location) +
                          " name=" + input.name + " format=" + std::to_string(input.format));
        }

        // Sort by location
        std::sort(outAttributes.begin(), outAttributes.end(),
            [](const auto& a, const auto& b) { return a.location < b.location; });

        // Compute stride and offsets
        uint32_t currentOffset = 0;
        for (auto& attr : outAttributes) {
            attr.offset = currentOffset;

            uint32_t size = 0;
            switch (attr.format) {
                case VK_FORMAT_R32_SFLOAT: size = 4; break;
                case VK_FORMAT_R32G32_SFLOAT: size = 8; break;
                case VK_FORMAT_R32G32B32_SFLOAT: size = 12; break;
                case VK_FORMAT_R32G32B32A32_SFLOAT: size = 16; break;
                default:
                    NODE_LOG_WARNING("GraphicsPipelineNode: Unknown vertex format " + std::to_string(attr.format));
                    size = 16;
            }
            currentOffset += size;
        }

        VkVertexInputBindingDescription binding{};
        binding.binding = 0;
        binding.stride = currentOffset;
        binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        outBindings.push_back(binding);

        NODE_LOG_INFO("GraphicsPipelineNode: Built vertex input from reflection: " +
                     std::to_string(outAttributes.size()) + " attributes, stride=" +
                     std::to_string(currentOffset) + " bytes");
    } else {
        // Hardcoded fallback for current Draw shader (vec4 pos + vec2 uv)
        NODE_LOG_WARNING("GraphicsPipelineNode: No vertex input reflection - using hardcoded fallback (vec4 pos + vec2 uv)");

        VkVertexInputBindingDescription binding{};
        binding.binding = 0;
        binding.stride = sizeof(float) * 6;
        binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        outBindings.push_back(binding);

        VkVertexInputAttributeDescription posAttr{};
        posAttr.location = 0;
        posAttr.binding = 0;
        posAttr.format = VK_FORMAT_R32G32B32A32_SFLOAT;
        posAttr.offset = 0;
        outAttributes.push_back(posAttr);

        VkVertexInputAttributeDescription uvAttr{};
        uvAttr.location = 1;
        uvAttr.binding = 0;
        uvAttr.format = VK_FORMAT_R32G32_SFLOAT;
        uvAttr.offset = sizeof(float) * 4;
        outAttributes.push_back(uvAttr);
    }
}

void GraphicsPipelineNode::BuildDynamicStateInfo(VkPipelineDynamicStateCreateInfo& outState) {
    static VkDynamicState dynamicStates[] = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };
    outState = CreateDynamicStateInfo(dynamicStates, 2);
}

void GraphicsPipelineNode::BuildVertexInputState(VkPipelineVertexInputStateCreateInfo& outState) {
    if (!enableVertexInput) {
        outState = CreateVertexInputState(nullptr, 0, nullptr, 0);
        return;
    }

    // Binding 0: Vertex buffer with interleaved pos (vec4) + UV (vec2)
    VkVertexInputBindingDescription binding{};
    binding.binding = 0;
    binding.stride = sizeof(float) * 6;  // vec4 + vec2
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    // Attributes: position (location=0, vec4) + UV (location=1, vec2)
    VkVertexInputAttributeDescription attributes[2];
    attributes[0] = {0, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 0};
    attributes[1] = {1, 0, VK_FORMAT_R32G32_SFLOAT, sizeof(float) * 4};

    outState = CreateVertexInputState(&binding, 1, attributes, 2);
}

void GraphicsPipelineNode::BuildInputAssemblyState(VkPipelineInputAssemblyStateCreateInfo& outState) {
    outState = CreateInputAssemblyState(topology, VK_FALSE);
}

void GraphicsPipelineNode::BuildRasterizationState(VkPipelineRasterizationStateCreateInfo& outState) {
    outState = CreateRasterizationState(polygonMode, cullMode, frontFace, 1.0f);
}

void GraphicsPipelineNode::BuildMultisampleState(VkPipelineMultisampleStateCreateInfo& outState) {
    outState = CreateMultisampleState(VK_SAMPLE_COUNT_1_BIT);
}

void GraphicsPipelineNode::BuildDepthStencilState(VkPipelineDepthStencilStateCreateInfo& outState) {
    outState = CreateDepthStencilState(
        enableDepthTest ? VK_TRUE : VK_FALSE,
        enableDepthWrite ? VK_TRUE : VK_FALSE,
        VK_COMPARE_OP_LESS_OR_EQUAL
    );
}

void GraphicsPipelineNode::BuildColorBlendState(VkPipelineColorBlendStateCreateInfo& outState) {
    static VkPipelineColorBlendAttachmentState attachment = CreateColorBlendAttachment(VK_FALSE);
    outState = CreateColorBlendState(&attachment, 1);
}

void GraphicsPipelineNode::BuildViewportState(VkPipelineViewportStateCreateInfo& outState) {
    VkPipelineViewportStateCreateInfo state{};
    state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    state.viewportCount = 1;
    state.pViewports = nullptr;  // Dynamic
    state.scissorCount = 1;
    state.pScissors = nullptr;   // Dynamic
    outState = state;
}

void GraphicsPipelineNode::CreatePipeline(TypedCompileContext& ctx) {
    VkRenderPass renderPass = ctx.In(GraphicsPipelineNodeConfig::RENDER_PASS);

    // Build all pipeline state structures
    VkPipelineDynamicStateCreateInfo dynamicState;
    BuildDynamicStateInfo(dynamicState);

    VkPipelineVertexInputStateCreateInfo vertexInputState;
    BuildVertexInputState(vertexInputState);

    VkPipelineInputAssemblyStateCreateInfo inputAssembly;
    BuildInputAssemblyState(inputAssembly);

    VkPipelineRasterizationStateCreateInfo rasterization;
    BuildRasterizationState(rasterization);

    VkPipelineMultisampleStateCreateInfo multisample;
    BuildMultisampleState(multisample);

    VkPipelineDepthStencilStateCreateInfo depthStencil;
    BuildDepthStencilState(depthStencil);

    VkPipelineColorBlendStateCreateInfo colorBlend;
    BuildColorBlendState(colorBlend);

    VkPipelineViewportStateCreateInfo viewportState;
    BuildViewportState(viewportState);

    // Assemble graphics pipeline create info
    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = static_cast<uint32_t>(shaderStageInfos.size());
    pipelineInfo.pStages = shaderStageInfos.data();
    pipelineInfo.pVertexInputState = &vertexInputState;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pTessellationState = nullptr;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterization;
    pipelineInfo.pMultisampleState = &multisample;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlend;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = pipelineLayout;
    pipelineInfo.renderPass = renderPass;
    pipelineInfo.subpass = 0;
    pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;
    pipelineInfo.basePipelineIndex = -1;

    VkResult result = vkCreateGraphicsPipelines(
        device->device, pipelineCache, 1, &pipelineInfo, nullptr, &pipeline
    );

    if (result != VK_SUCCESS) {
        throw std::runtime_error("Failed to create graphics pipeline");
    }

    ctx.Out(GraphicsPipelineNodeConfig::PIPELINE, pipeline);
    ctx.Out(GraphicsPipelineNodeConfig::PIPELINE_LAYOUT, pipelineLayout);
    ctx.Out(GraphicsPipelineNodeConfig::PIPELINE_CACHE, pipelineCache);
    ctx.Out(GraphicsPipelineNodeConfig::VULKAN_DEVICE_OUT, device);
}

void GraphicsPipelineNode::CreatePipelineWithCache(TypedCompileContext& ctx) {
    // Get MainCacher from owning graph
    auto& mainCacher = GetOwningGraph()->GetMainCacher();

    // Register PipelineCacher (idempotent - safe to call multiple times)
    if (!mainCacher.IsRegistered(typeid(CashSystem::PipelineWrapper))) {
        mainCacher.RegisterCacher<
            CashSystem::PipelineCacher,
            CashSystem::PipelineWrapper,
            CashSystem::PipelineCreateParams
        >(
            typeid(CashSystem::PipelineWrapper),
            "Pipeline",
            true  // device-dependent
        );
        NODE_LOG_DEBUG("GraphicsPipelineNode: Registered PipelineCacher");
    }

    // Cache the cacher reference for use throughout node lifetime
    pipelineCacher = mainCacher.GetCacher<
        CashSystem::PipelineCacher,
        CashSystem::PipelineWrapper,
        CashSystem::PipelineCreateParams
    >(typeid(CashSystem::PipelineWrapper), device);

    if (pipelineCacher) {
        NODE_LOG_INFO("GraphicsPipelineNode: Pipeline cache ready - attempting cached pipeline creation");

        VkRenderPass renderPass = ctx.In(GraphicsPipelineNodeConfig::RENDER_PASS);

        // Build vertex input descriptions from reflection data
        std::vector<VkVertexInputBindingDescription> vertexBindings;
        std::vector<VkVertexInputAttributeDescription> vertexAttributes;

        NODE_LOG_DEBUG("GraphicsPipelineNode: enableVertexInput=" + std::string(enableVertexInput ? "true" : "false"));
        if (enableVertexInput) {
            NODE_LOG_DEBUG("GraphicsPipelineNode: Calling BuildVertexInputsFromReflection...");
            BuildVertexInputsFromReflection(currentShaderBundle, vertexBindings, vertexAttributes);
            NODE_LOG_DEBUG("GraphicsPipelineNode: BuildVertexInputsFromReflection complete: " +
                          std::to_string(vertexBindings.size()) + " bindings, " +
                          std::to_string(vertexAttributes.size()) + " attributes");

            // Log details
            for (const auto& binding : vertexBindings) {
                NODE_LOG_DEBUG("  Binding " + std::to_string(binding.binding) + ": stride=" + std::to_string(binding.stride) +
                              " inputRate=" + std::to_string(binding.inputRate));
            }
            for (const auto& attr : vertexAttributes) {
                NODE_LOG_DEBUG("  Attribute location=" + std::to_string(attr.location) + " binding=" + std::to_string(attr.binding) +
                              " format=" + std::to_string(attr.format) + " offset=" + std::to_string(attr.offset));
            }
        }

        // Setup pipeline create params for cacher
        CashSystem::PipelineCreateParams params;

        // Pass dynamic shader stages to cacher (supports all 14 stage types)
        params.shaderStages = shaderStageInfos;

        // Derive shader keys from bundle metadata (data-driven!)
        std::string programName = currentShaderBundle->GetProgramName();
        params.vertexShaderKey = programName;  // Use program name as base key
        params.fragmentShaderKey = programName;  // Legacy fields - same as program name

        // Build stage-specific keys for logging/debugging
        std::string stageKeys;
        for (const auto& [stage, module] : shaderModules) {
            if (!stageKeys.empty()) stageKeys += "+";
            stageKeys += ShaderManagement::ShaderStageName(stage);
        }
        NODE_LOG_DEBUG("GraphicsPipelineNode: Shader stages: " + stageKeys);

        // Pipeline layout - Use the descriptor set layout (auto-generated or manual from CompileImpl)
        params.descriptorSetLayout = this->descriptorSetLayout;  // Already set in CompileImpl
        params.pushConstantRanges = this->pushConstantRanges;  // Phase 5: Use push constants from reflection
        // Note: PipelineCacher will internally use PipelineLayoutCacher to create/cache the layout

        params.renderPass = renderPass;  // Local variable from ctx.In() above

        // Use semantic keys instead of handle addresses for stable caching across recompiles
        params.layoutKey = "main_pipeline_layout";
        params.renderPassKey = "main_render_pass";
        params.enableDepthTest = this->enableDepthTest;
        params.enableDepthWrite = this->enableDepthWrite;
        params.cullMode = this->cullMode;
        params.polygonMode = this->polygonMode;
        params.topology = this->topology;
        params.vertexBindings = vertexBindings;  // Local variable built from reflection above
        params.vertexAttributes = vertexAttributes;  // Local variable built from reflection above

        NODE_LOG_DEBUG("GraphicsPipelineNode: Pipeline params: depth=" + std::string(this->enableDepthTest ? "true" : "false") +
                      " depthWrite=" + std::string(this->enableDepthWrite ? "true" : "false") + " cull=" + std::to_string(this->cullMode) +
                      " polyMode=" + std::to_string(this->polygonMode) + " topo=" + std::to_string(this->topology));

        try {
            // Use cacher to get or create pipeline
            cachedPipelineWrapper = pipelineCacher->GetOrCreate(params);

            if (cachedPipelineWrapper && cachedPipelineWrapper->pipeline != VK_NULL_HANDLE) {
                pipeline = cachedPipelineWrapper->pipeline;
                pipelineCache = cachedPipelineWrapper->cache;
                pipelineLayout = cachedPipelineWrapper->pipelineLayoutWrapper->layout;  // Get shared layout
                NODE_LOG_INFO("GraphicsPipelineNode: Pipeline retrieved from cacher (cache hit or newly created)");
                return;
            }
        } catch (const std::exception& e) {
            NODE_LOG_ERROR("GraphicsPipelineNode: Cacher failed: " + std::string(e.what()));
            // Fall through to manual creation
        }
    }

    // Fallback: Create pipeline manually if cacher unavailable or failed
    NODE_LOG_WARNING("GraphicsPipelineNode: Creating pipeline without cacher");
    CreatePipeline(ctx);
}

} // namespace Vixen::RenderGraph
