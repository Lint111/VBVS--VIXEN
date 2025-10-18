#include "VulkanPipeline.h"

#include "VulkanApplication.h"
#include "VulkanResources/VulkanDevice.h"
#include "VulkanShader.h"
#include "VulkanDrawable.h"
#include "VulkanRenderer.h"

VulkanPipeline::VulkanPipeline()
{
    appObj = VulkanApplication::GetInstance();
    deviceObj = appObj->deviceObj.get();
    pipelineCache = VK_NULL_HANDLE;
    pipelineLayout = VK_NULL_HANDLE;
}

VulkanPipeline::~VulkanPipeline()
{
    DestroyPipelineCache();
}   

void VulkanPipeline::CreatePipelineCache()
{
    if(pipelineCache != VK_NULL_HANDLE) {
        return;
	}

    VkResult result;
    VkPipelineCacheCreateInfo pipelineCacheInfo = {};
    pipelineCacheInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
    pipelineCacheInfo.pNext = nullptr;
    pipelineCacheInfo.initialDataSize = 0;
    pipelineCacheInfo.pInitialData = nullptr;
    pipelineCacheInfo.flags = 0;

    result = vkCreatePipelineCache(
        deviceObj->device,
        &pipelineCacheInfo,
        nullptr,
        &pipelineCache
    );

    assert(result == VK_SUCCESS);
}

bool VulkanPipeline::CreatePipeline(VulkanDrawable *drawableObj, VulkanShader *shaderObj, Config config, VkPipeline* pipelineHandle)
{
    if(!drawableObj || !shaderObj || !shaderObj->initialized) {
        std::string nullArgs = "";
        if(!drawableObj) nullArgs += " drawableObj ";
        if(!shaderObj) nullArgs += " shaderObj ";
        if(shaderObj && !shaderObj->initialized) nullArgs += " shaderObj not initialized ";
        std::cerr << "CreatePipeline: invalid arguments (null): " << nullArgs << std::endl;
        return false;
    }

    if(shaderObj->shaderStages == nullptr || shaderObj->stagesCount == 0) {
        std::cerr << "CreatePipeline: shader stages not initialized" << std::endl;
        return false;
    }

    if(appObj->renderObj->renderPass == VK_NULL_HANDLE) {
        std::cerr << "CreatePipeline: renderPass is VK_NULL_HANDLE" << std::endl;
        return false;
    }

    if(pipelineCache == VK_NULL_HANDLE) {
        std::cerr << "CreatePipeline: pipelineCache is VK_NULL_HANDLE, creating one now." << std::endl;
        CreatePipelineCache();
        if(pipelineCache == VK_NULL_HANDLE) {
            std::cerr << "CreatePipeline: failed to create pipeline cache" << std::endl;
            return false;
        }
    }

    VkResult result;

    // Initialize the dynamic state info as Empty
    VkDynamicState dynamicStateEnables[2];
    uint32_t dynamicCount = 0;
    memset(dynamicStateEnables, 0, sizeof(dynamicStateEnables));
    dynamicStateEnables[dynamicCount++] = VK_DYNAMIC_STATE_VIEWPORT;
    dynamicStateEnables[dynamicCount++] = VK_DYNAMIC_STATE_SCISSOR;

    VkPipelineDynamicStateCreateInfo dynamicState = {};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.pNext = nullptr;
    dynamicState.pDynamicStates = dynamicStateEnables;
    dynamicState.dynamicStateCount = dynamicCount;

    VkPipelineVertexInputStateCreateInfo vertexInputStateInfo = {};
    vertexInputStateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputStateInfo.pNext = nullptr;
    vertexInputStateInfo.flags = 0;
    if(config.enableVertexInput) {
        vertexInputStateInfo.vertexBindingDescriptionCount = 1; // viIpBind is a single struct
        vertexInputStateInfo.pVertexBindingDescriptions = &drawableObj->viIpBind;
        vertexInputStateInfo.vertexAttributeDescriptionCount = 2; // viIpAttr is an array of 2
        vertexInputStateInfo.pVertexAttributeDescriptions = drawableObj->viIpAttr;

        // Validate that vertex buffer was created (stride > 0 indicates initialization)
        if(drawableObj->viIpBind.stride == 0) {
            std::cerr << "CreatePipeline: vertex input enabled but vertex buffer not created (stride is 0)" << std::endl;
            std::cerr << "  Make sure CreateVertexBuffer() is called before CreatePipeline()" << std::endl;
            return false;
        }
    } else {
        vertexInputStateInfo.vertexBindingDescriptionCount = 0;
        vertexInputStateInfo.pVertexBindingDescriptions = nullptr;
        vertexInputStateInfo.vertexAttributeDescriptionCount = 0;
        vertexInputStateInfo.pVertexAttributeDescriptions = nullptr;
    }

    VkPipelineInputAssemblyStateCreateInfo inputAssemblyInfo = {};
    inputAssemblyInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssemblyInfo.pNext = nullptr;
    inputAssemblyInfo.flags = 0;
    inputAssemblyInfo.primitiveRestartEnable = VK_FALSE;
    inputAssemblyInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineRasterizationStateCreateInfo rasterStateInfo = {};
    rasterStateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterStateInfo.pNext = nullptr;
    rasterStateInfo.flags = 0;
    rasterStateInfo.polygonMode = VK_POLYGON_MODE_FILL;
    rasterStateInfo.cullMode = VK_CULL_MODE_BACK_BIT;
    rasterStateInfo.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterStateInfo.depthClampEnable = VK_FALSE;  // Depth clamping requires depthClamp feature
    rasterStateInfo.rasterizerDiscardEnable = VK_FALSE;
    rasterStateInfo.depthBiasEnable = VK_FALSE;
    rasterStateInfo.depthBiasConstantFactor = 0.0f;
    rasterStateInfo.depthBiasClamp = 0.0f;
    rasterStateInfo.depthBiasSlopeFactor = 0.0f;
    rasterStateInfo.lineWidth = 1.0f;

    VkPipelineColorBlendAttachmentState colorBlendAttachmentStateInfo[1] = {};
    colorBlendAttachmentStateInfo[0].colorWriteMask = 0xF;
    colorBlendAttachmentStateInfo[0].blendEnable = VK_FALSE;

    // Define color and alpha blending operations.
    colorBlendAttachmentStateInfo[0].alphaBlendOp = VK_BLEND_OP_ADD;
    colorBlendAttachmentStateInfo[0].colorBlendOp = VK_BLEND_OP_ADD;

    // Set the source and destination color/alpha blend factors.
    colorBlendAttachmentStateInfo[0].srcColorBlendFactor = VK_BLEND_FACTOR_ZERO;
    colorBlendAttachmentStateInfo[0].dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
    colorBlendAttachmentStateInfo[0].srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    colorBlendAttachmentStateInfo[0].dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;

    VkPipelineColorBlendStateCreateInfo colorBlendStateInfo = {};
    colorBlendStateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlendStateInfo.flags = 0;
    colorBlendStateInfo.pNext = nullptr;
    colorBlendStateInfo.attachmentCount = 1;
    colorBlendStateInfo.pAttachments = colorBlendAttachmentStateInfo;
    colorBlendStateInfo.logicOpEnable = VK_FALSE;
    colorBlendStateInfo.blendConstants[0] = 1.0f;
    colorBlendStateInfo.blendConstants[1] = 1.0f;
    colorBlendStateInfo.blendConstants[2] = 1.0f;
    colorBlendStateInfo.blendConstants[3] = 1.0f;

    VkPipelineViewportStateCreateInfo viewportStateInfo = {};
    viewportStateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportStateInfo.pNext = nullptr;
    viewportStateInfo.flags = 0;

    // Number of viewports must be equal to number of scissors
    viewportStateInfo.viewportCount = NUMBER_OF_VIEWPORTS;
    viewportStateInfo.scissorCount = NUMBER_OF_SCISSORS;
    viewportStateInfo.pScissors = nullptr;
    viewportStateInfo.pViewports = nullptr;

    VkPipelineDepthStencilStateCreateInfo depthStencilStateInfo = {};
    depthStencilStateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencilStateInfo.pNext = nullptr;
    depthStencilStateInfo.flags = 0;
    depthStencilStateInfo.depthTestEnable = config.enableDepthTest;
    depthStencilStateInfo.depthWriteEnable = config.enableDepthWrite;
    depthStencilStateInfo.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    depthStencilStateInfo.depthBoundsTestEnable = VK_FALSE;
    depthStencilStateInfo.stencilTestEnable = VK_FALSE;
    depthStencilStateInfo.back.passOp = VK_STENCIL_OP_KEEP;
    depthStencilStateInfo.back.failOp = VK_STENCIL_OP_KEEP;
    depthStencilStateInfo.back.compareOp = VK_COMPARE_OP_ALWAYS;
    depthStencilStateInfo.back.compareMask = 0;
    depthStencilStateInfo.back.reference = 0;
    depthStencilStateInfo.back.depthFailOp = VK_STENCIL_OP_KEEP;
    depthStencilStateInfo.back.writeMask = 0;
    depthStencilStateInfo.minDepthBounds = 0;
    depthStencilStateInfo.maxDepthBounds = 0;
    depthStencilStateInfo.stencilTestEnable = VK_FALSE;
    depthStencilStateInfo.front = depthStencilStateInfo.back;

    VkPipelineMultisampleStateCreateInfo multisampleStateInfo = {};
    multisampleStateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampleStateInfo.pNext = nullptr;
    multisampleStateInfo.flags = 0;
    multisampleStateInfo.rasterizationSamples = NUM_SAMPLES;


    // Use the drawable's pipeline layout (which includes descriptor set layouts)
    VkPipelineLayout drawablePipelineLayout = drawableObj->pipelineLayout;
    if (drawablePipelineLayout == VK_NULL_HANDLE) {
        std::cerr << "CreatePipeline: drawable pipelineLayout is VK_NULL_HANDLE" << std::endl;
        return false;
    }

    // Populate the VkGraphicsPipelineCreateInfo structure to specify
    // programmable stages, fixed-function pipeline stages render
    // pass, sub-passes and pipeline layouts
    VkGraphicsPipelineCreateInfo pipelineInfo = {};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.pVertexInputState = &vertexInputStateInfo;
    pipelineInfo.pInputAssemblyState = &inputAssemblyInfo;
    pipelineInfo.pRasterizationState = &rasterStateInfo;
    pipelineInfo.pColorBlendState = &colorBlendStateInfo;
    pipelineInfo.pTessellationState = nullptr;
    pipelineInfo.pMultisampleState = &multisampleStateInfo;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.pViewportState = &viewportStateInfo;
    pipelineInfo.pDepthStencilState = &depthStencilStateInfo;
    pipelineInfo.pStages = shaderObj->shaderStages;
    pipelineInfo.stageCount = 2;
    pipelineInfo.renderPass = drawableObj->GetRenderer()->renderPass;
    pipelineInfo.subpass = 0;
    pipelineInfo.layout = drawablePipelineLayout;

    // Create the pipilne using the meta-data store in the
    // VkGraphicsPipelineCreateInfo object
    result = vkCreateGraphicsPipelines(
        deviceObj->device,
        pipelineCache,
        1,
        &pipelineInfo,
        nullptr,
        pipelineHandle
    );

    if(result != VK_SUCCESS) {
        std::cerr << "CreatePipeline: failed to create graphics pipeline" << std::endl;
        return false;
    }

    return (result == VK_SUCCESS);
}

void VulkanPipeline::DestroyPipelineCache()
{
    if(pipelineCache != VK_NULL_HANDLE) {
        vkDestroyPipelineCache(deviceObj->device, pipelineCache, nullptr);
        pipelineCache = VK_NULL_HANDLE;
    }
    // Note: pipelineLayout is owned by VulkanDrawable, not destroyed here
}
