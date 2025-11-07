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
#include <cstring>

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

void GraphicsPipelineNode::SetupImpl(TypedNode<GraphicsPipelineNodeConfig>::TypedSetupContext& ctx) {
    // Graph-scope initialization only (no input access)
    NODE_LOG_DEBUG("GraphicsPipelineNode: Setup (graph-scope initialization)");
}

void GraphicsPipelineNode::CompileImpl(TypedNode<GraphicsPipelineNodeConfig>::TypedCompileContext& ctx) {
    // Access device input (compile-time dependency)
    VulkanDevicePtr devicePtr = In(GraphicsPipelineNodeConfig::VULKAN_DEVICE_IN);

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
    currentShaderBundle = In(GraphicsPipelineNodeConfig::SHADER_DATA_BUNDLE);  // Store for use in helper functions
    VkRenderPass renderPass = In(GraphicsPipelineNodeConfig::RENDER_PASS);
    VkDescriptorSetLayout manualDescriptorSetLayout = In(GraphicsPipelineNodeConfig::DESCRIPTOR_SET_LAYOUT);
    SwapChainPublicVariables* swapchainInfo = In(GraphicsPipelineNodeConfig::SWAPCHAIN_INFO);

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

        VulkanDevicePtr device = GetDevice();
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
    CreatePipelineWithCache();
    
    // Set outputs
    Out(GraphicsPipelineNodeConfig::PIPELINE, pipeline);
    Out(GraphicsPipelineNodeConfig::PIPELINE_LAYOUT, pipelineLayout);
    Out(GraphicsPipelineNodeConfig::PIPELINE_CACHE, pipelineCache);
    Out(GraphicsPipelineNodeConfig::VULKAN_DEVICE_OUT, device);
}

void GraphicsPipelineNode::ExecuteImpl(TypedNode<GraphicsPipelineNodeConfig>::TypedExecuteContext& ctx) {
    // Pipeline creation happens in Compile phase
    // Execute is a no-op for this node
}

void GraphicsPipelineNode::CleanupImpl(TypedNode<GraphicsPipelineNodeConfig>::TypedCleanupContext& ctx) {
    // If we have a cached pipeline wrapper, just release the shared_ptr
    // The cacher owns VkPipeline, VkPipelineLayout, and VkPipelineCache - will destroy when appropriate
    if (cachedPipelineWrapper) {
        std::cout << "[GraphicsPipelineNode::CleanupImpl] Releasing cached pipeline wrapper (cacher owns all resources)" << std::endl;
        cachedPipelineWrapper.reset();
        pipeline = VK_NULL_HANDLE;
        pipelineLayout = VK_NULL_HANDLE;
        pipelineCache = VK_NULL_HANDLE;
    } else {
        // Fallback: cleanup locally-created resources (if created without cacher)
        if (pipeline != VK_NULL_HANDLE && device != VK_NULL_HANDLE) {
            std::cout << "[GraphicsPipelineNode::CleanupImpl] Destroying locally-created pipeline" << std::endl;
            vkDestroyPipeline(device->device, pipeline, nullptr);
            pipeline = VK_NULL_HANDLE;
        }

        if (pipelineLayout != VK_NULL_HANDLE && device != VK_NULL_HANDLE) {
            std::cout << "[GraphicsPipelineNode::CleanupImpl] Destroying locally-created pipeline layout" << std::endl;
            vkDestroyPipelineLayout(device->device, pipelineLayout, nullptr);
            pipelineLayout = VK_NULL_HANDLE;
        }

        if (pipelineCache != VK_NULL_HANDLE && device != VK_NULL_HANDLE) {
            std::cout << "[GraphicsPipelineNode::CleanupImpl] Destroying locally-created pipeline cache" << std::endl;
            vkDestroyPipelineCache(device->device, pipelineCache, nullptr);
            pipelineCache = VK_NULL_HANDLE;
        }
    }
}

void GraphicsPipelineNode::CreatePipelineCache() {
    // Only create if not already created
    if (pipelineCache != VK_NULL_HANDLE) {
        std::cout << "[GraphicsPipelineNode] Pipeline cache already exists, reusing" << std::endl;
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
        std::cout << "[GraphicsPipelineNode] Destroying old pipeline layout before recompile" << std::endl;
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

    std::cout << "[BuildVertexInputsFromReflection] bundle=" << (bundle ? "valid" : "null")
              << " reflectionData=" << (bundle && bundle->reflectionData ? "valid" : "null")
              << " vertexInputCount=" << (bundle ? bundle->GetVertexInputs().size() : 0)
              << " hasReflection=" << hasReflection << std::endl;

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
        std::cout << "[GraphicsPipelineNode::BuildVertexInputsFromReflection] No vertex input reflection - using hardcoded fallback (vec4 pos + vec2 uv)" << std::endl;
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

void GraphicsPipelineNode::CreatePipeline() {
    VkRenderPass renderPass = In(GraphicsPipelineNodeConfig::RENDER_PASS);
    
    // Dynamic state
    VkDynamicState dynamicStates[] = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };

    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.pNext = nullptr;
    dynamicState.dynamicStateCount = 2;
    dynamicState.pDynamicStates = dynamicStates;

    // Vertex input state
    VkPipelineVertexInputStateCreateInfo vertexInputState{};
    vertexInputState.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputState.pNext = nullptr;
    vertexInputState.flags = 0;

    // Define vertex input binding and attributes matching shader expectations
    VkVertexInputBindingDescription vertexBinding = {};
    VkVertexInputAttributeDescription vertexAttributes[2] = {};
    
    if (enableVertexInput) {
        // Binding 0: Vertex buffer with interleaved pos (vec4) + UV (vec2)
        // Stride = 4 floats (pos) + 2 floats (UV) = 24 bytes
        vertexBinding.binding = 0;
        vertexBinding.stride = sizeof(float) * 6; // vec4 + vec2
        vertexBinding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        
        // Attribute 0: Position (location = 0, vec4, offset = 0)
        vertexAttributes[0].location = 0;
        vertexAttributes[0].binding = 0;
        vertexAttributes[0].format = VK_FORMAT_R32G32B32A32_SFLOAT;
        vertexAttributes[0].offset = 0;
        
        // Attribute 1: UV coords (location = 1, vec2, offset = 16 bytes)
        vertexAttributes[1].location = 1;
        vertexAttributes[1].binding = 0;
        vertexAttributes[1].format = VK_FORMAT_R32G32_SFLOAT;
        vertexAttributes[1].offset = sizeof(float) * 4;
        
        vertexInputState.vertexBindingDescriptionCount = 1;
        vertexInputState.pVertexBindingDescriptions = &vertexBinding;
        vertexInputState.vertexAttributeDescriptionCount = 2;
        vertexInputState.pVertexAttributeDescriptions = vertexAttributes;
    } else {
        vertexInputState.vertexBindingDescriptionCount = 0;
        vertexInputState.pVertexBindingDescriptions = nullptr;
        vertexInputState.vertexAttributeDescriptionCount = 0;
        vertexInputState.pVertexAttributeDescriptions = nullptr;
    }

    // Input assembly state
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.pNext = nullptr;
    inputAssembly.flags = 0;
    inputAssembly.topology = topology;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    // Rasterization state
    VkPipelineRasterizationStateCreateInfo rasterization{};
    rasterization.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterization.pNext = nullptr;
    rasterization.flags = 0;
    rasterization.depthClampEnable = VK_FALSE;
    rasterization.rasterizerDiscardEnable = VK_FALSE;
    rasterization.polygonMode = polygonMode;
    rasterization.cullMode = cullMode;
    rasterization.frontFace = frontFace;
    rasterization.depthBiasEnable = VK_FALSE;
    rasterization.depthBiasConstantFactor = 0.0f;
    rasterization.depthBiasClamp = 0.0f;
    rasterization.depthBiasSlopeFactor = 0.0f;
    rasterization.lineWidth = 1.0f;

    // Multisample state
    VkPipelineMultisampleStateCreateInfo multisample{};
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.pNext = nullptr;
    multisample.flags = 0;
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    multisample.sampleShadingEnable = VK_FALSE;
    multisample.minSampleShading = 1.0f;
    multisample.pSampleMask = nullptr;
    multisample.alphaToCoverageEnable = VK_FALSE;
    multisample.alphaToOneEnable = VK_FALSE;

    // Depth stencil state
    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.pNext = nullptr;
    depthStencil.flags = 0;
    depthStencil.depthTestEnable = enableDepthTest ? VK_TRUE : VK_FALSE;
    depthStencil.depthWriteEnable = enableDepthWrite ? VK_TRUE : VK_FALSE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;
    depthStencil.front = {};
    depthStencil.back = {};
    depthStencil.minDepthBounds = 0.0f;
    depthStencil.maxDepthBounds = 1.0f;

    // Color blend state
    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.blendEnable = VK_FALSE;
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
                                          VK_COLOR_COMPONENT_G_BIT |
                                          VK_COLOR_COMPONENT_B_BIT |
                                          VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
    colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
    colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

    VkPipelineColorBlendStateCreateInfo colorBlend{};
    colorBlend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlend.pNext = nullptr;
    colorBlend.flags = 0;
    colorBlend.logicOpEnable = VK_FALSE;
    colorBlend.logicOp = VK_LOGIC_OP_COPY;
    colorBlend.attachmentCount = 1;
    colorBlend.pAttachments = &colorBlendAttachment;
    colorBlend.blendConstants[0] = 1.0f;
    colorBlend.blendConstants[1] = 1.0f;
    colorBlend.blendConstants[2] = 1.0f;
    colorBlend.blendConstants[3] = 1.0f;

    // Viewport state
    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.pNext = nullptr;
    viewportState.flags = 0;
    viewportState.viewportCount = 1;
    viewportState.pViewports = nullptr; // Dynamic
    viewportState.scissorCount = 1;
    viewportState.pScissors = nullptr; // Dynamic

    // Graphics pipeline create info
    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.pNext = nullptr;
    pipelineInfo.flags = 0;
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
        device->device,
        pipelineCache,
        1,
        &pipelineInfo,
        nullptr,
        &pipeline
    );

    if (result != VK_SUCCESS) {
        throw std::runtime_error("Failed to create graphics pipeline");
    }
    
    // Output pipeline resources and device
    Out(GraphicsPipelineNodeConfig::PIPELINE, pipeline);
    Out(GraphicsPipelineNodeConfig::PIPELINE_LAYOUT, pipelineLayout);
    Out(GraphicsPipelineNodeConfig::PIPELINE_CACHE, pipelineCache);
    Out(GraphicsPipelineNodeConfig::VULKAN_DEVICE_OUT, device);
}

// Parse helper methods
VkCullModeFlags GraphicsPipelineNode::ParseCullMode(const std::string& mode) {
    if (mode == "None") return VK_CULL_MODE_NONE;
    if (mode == "Front") return VK_CULL_MODE_FRONT_BIT;
    if (mode == "Back") return VK_CULL_MODE_BACK_BIT;
    if (mode == "FrontAndBack") return VK_CULL_MODE_FRONT_AND_BACK;
    return VK_CULL_MODE_BACK_BIT; // Default
}

VkPolygonMode GraphicsPipelineNode::ParsePolygonMode(const std::string& mode) {
    if (mode == "Fill") return VK_POLYGON_MODE_FILL;
    if (mode == "Line") return VK_POLYGON_MODE_LINE;
    if (mode == "Point") return VK_POLYGON_MODE_POINT;
    return VK_POLYGON_MODE_FILL; // Default
}

VkPrimitiveTopology GraphicsPipelineNode::ParseTopology(const std::string& topo) {
    if (topo == "PointList") return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
    if (topo == "LineList") return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
    if (topo == "LineStrip") return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
    if (topo == "TriangleList") return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    if (topo == "TriangleStrip") return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
    if (topo == "TriangleFan") return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN;
    return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST; // Default
}

VkFrontFace GraphicsPipelineNode::ParseFrontFace(const std::string& face) {
    if (face == "Clockwise") return VK_FRONT_FACE_CLOCKWISE;
    if (face == "CounterClockwise") return VK_FRONT_FACE_COUNTER_CLOCKWISE;
    return VK_FRONT_FACE_COUNTER_CLOCKWISE; // Default
}

void GraphicsPipelineNode::CreatePipelineWithCache() {
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

        VkRenderPass renderPass = In(GraphicsPipelineNodeConfig::RENDER_PASS);

        // Build vertex input descriptions from reflection data
        std::vector<VkVertexInputBindingDescription> vertexBindings;
        std::vector<VkVertexInputAttributeDescription> vertexAttributes;

        std::cout << "[GraphicsPipelineNode] enableVertexInput=" << enableVertexInput << std::endl;
        if (enableVertexInput) {
            std::cout << "[GraphicsPipelineNode] Calling BuildVertexInputsFromReflection..." << std::endl;
            BuildVertexInputsFromReflection(currentShaderBundle, vertexBindings, vertexAttributes);
            std::cout << "[GraphicsPipelineNode] BuildVertexInputsFromReflection complete: "
                      << vertexBindings.size() << " bindings, "
                      << vertexAttributes.size() << " attributes" << std::endl;

            // Log details
            for (const auto& binding : vertexBindings) {
                std::cout << "  Binding " << binding.binding << ": stride=" << binding.stride
                          << " inputRate=" << binding.inputRate << std::endl;
            }
            for (const auto& attr : vertexAttributes) {
                std::cout << "  Attribute location=" << attr.location << " binding=" << attr.binding
                          << " format=" << attr.format << " offset=" << attr.offset << std::endl;
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
        params.descriptorSetLayout = descriptorSetLayout;  // Already set in CompileImpl
        params.pushConstantRanges = pushConstantRanges;  // Phase 5: Use push constants from reflection
        // Note: PipelineCacher will internally use PipelineLayoutCacher to create/cache the layout

        params.renderPass = renderPass;

        // Use semantic keys instead of handle addresses for stable caching across recompiles
        params.layoutKey = "main_pipeline_layout";
        params.renderPassKey = "main_render_pass";
        params.enableDepthTest = enableDepthTest;
        params.enableDepthWrite = enableDepthWrite;
        params.cullMode = cullMode;
        params.polygonMode = polygonMode;
        params.topology = topology;
        params.vertexBindings = vertexBindings;
        params.vertexAttributes = vertexAttributes;

        std::cout << "[GraphicsPipelineNode] Pipeline params: depth=" << enableDepthTest
                  << " depthWrite=" << enableDepthWrite << " cull=" << cullMode
                  << " polyMode=" << polygonMode << " topo=" << topology << std::endl;

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
    CreatePipeline();
}

} // namespace Vixen::RenderGraph
