#include "Nodes/GraphicsPipelineNode.h"
#include "VulkanResources/VulkanDevice.h"
#include <cstring>

namespace Vixen::RenderGraph {

// ====== GraphicsPipelineNodeType ======

GraphicsPipelineNodeType::GraphicsPipelineNodeType() {
    typeId = 108; // Unique ID
    typeName = "GraphicsPipeline";
    pipelineType = PipelineType::Graphics;
    requiredCapabilities = DeviceCapability::Graphics;
    supportsInstancing = true;
    maxInstances = 0; // Unlimited

    // Inputs are opaque references (set via Set methods)

    // Outputs are opaque (accessed via Get methods)

    // Workload metrics
    workloadMetrics.estimatedMemoryFootprint = 8192; // Pipeline state
    workloadMetrics.estimatedComputeCost = 0.5f; // Pipeline compilation
    workloadMetrics.estimatedBandwidthCost = 0.0f;
    workloadMetrics.canRunInParallel = true;
}

std::unique_ptr<NodeInstance> GraphicsPipelineNodeType::CreateInstance(
    const std::string& instanceName,
    Vixen::Vulkan::Resources::VulkanDevice* device
) const {
    return std::make_unique<GraphicsPipelineNode>(
        instanceName,
        const_cast<GraphicsPipelineNodeType*>(this),
        device
    );
}

// ====== GraphicsPipelineNode ======

GraphicsPipelineNode::GraphicsPipelineNode(
    const std::string& instanceName,
    NodeType* nodeType,
    Vixen::Vulkan::Resources::VulkanDevice* device
)
    : NodeInstance(instanceName, nodeType, device)
{
}

GraphicsPipelineNode::~GraphicsPipelineNode() {
    Cleanup();
}

void GraphicsPipelineNode::Setup() {
    // Create pipeline cache
    CreatePipelineCache();
}

void GraphicsPipelineNode::Compile() {
    // Get parameters
    enableDepthTest = GetParameterValue<bool>("enableDepthTest", true);
    enableDepthWrite = GetParameterValue<bool>("enableDepthWrite", true);
    enableVertexInput = GetParameterValue<bool>("enableVertexInput", true);

    std::string cullModeStr = GetParameterValue<std::string>("cullMode", "Back");
    std::string polygonModeStr = GetParameterValue<std::string>("polygonMode", "Fill");
    std::string topologyStr = GetParameterValue<std::string>("topology", "TriangleList");
    std::string frontFaceStr = GetParameterValue<std::string>("frontFace", "CounterClockwise");

    cullMode = ParseCullMode(cullModeStr);
    polygonMode = ParsePolygonMode(polygonModeStr);
    topology = ParseTopology(topologyStr);
    frontFace = ParseFrontFace(frontFaceStr);

    // Validate inputs
    if (shaderStages == nullptr || shaderStageCount == 0) {
        throw std::runtime_error("GraphicsPipelineNode: shader stages not set");
    }
    if (renderPass == VK_NULL_HANDLE) {
        throw std::runtime_error("GraphicsPipelineNode: render pass not set");
    }
    if (descriptorSetLayout == VK_NULL_HANDLE) {
        throw std::runtime_error("GraphicsPipelineNode: descriptor set layout not set");
    }

    // Create pipeline layout
    CreatePipelineLayout();

    // Create graphics pipeline
    CreatePipeline();
}

void GraphicsPipelineNode::Execute(VkCommandBuffer commandBuffer) {
    // Pipeline creation happens in Compile phase
    // Execute is a no-op for this node
}

void GraphicsPipelineNode::Cleanup() {
    VkDevice vkDevice = device->device;

    if (pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(vkDevice, pipeline, nullptr);
        pipeline = VK_NULL_HANDLE;
    }

    if (pipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(vkDevice, pipelineLayout, nullptr);
        pipelineLayout = VK_NULL_HANDLE;
    }

    if (pipelineCache != VK_NULL_HANDLE) {
        vkDestroyPipelineCache(vkDevice, pipelineCache, nullptr);
        pipelineCache = VK_NULL_HANDLE;
    }
}

void GraphicsPipelineNode::CreatePipelineCache() {
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
    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.pNext = nullptr;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &descriptorSetLayout;
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

void GraphicsPipelineNode::CreatePipeline() {
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

    if (enableVertexInput && vertexBinding != nullptr) {
        vertexInputState.vertexBindingDescriptionCount = 1;
        vertexInputState.pVertexBindingDescriptions = vertexBinding;
        vertexInputState.vertexAttributeDescriptionCount = vertexAttributeCount;
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
    pipelineInfo.stageCount = shaderStageCount;
    pipelineInfo.pStages = shaderStages;
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
}

// Setter methods for input references
void GraphicsPipelineNode::SetShaderStages(const VkPipelineShaderStageCreateInfo* stages, uint32_t count) {
    shaderStages = stages;
    shaderStageCount = count;
}

void GraphicsPipelineNode::SetRenderPass(VkRenderPass pass) {
    renderPass = pass;
}

void GraphicsPipelineNode::SetVertexInput(
    const VkVertexInputBindingDescription* binding,
    const VkVertexInputAttributeDescription* attributes,
    uint32_t attributeCount
) {
    vertexBinding = binding;
    vertexAttributes = attributes;
    vertexAttributeCount = attributeCount;
}

void GraphicsPipelineNode::SetDescriptorSetLayout(VkDescriptorSetLayout layout) {
    descriptorSetLayout = layout;
}

void GraphicsPipelineNode::SetViewport(const VkViewport& vp) {
    viewport = vp;
}

void GraphicsPipelineNode::SetScissor(const VkRect2D& sc) {
    scissor = sc;
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

} // namespace Vixen::RenderGraph
