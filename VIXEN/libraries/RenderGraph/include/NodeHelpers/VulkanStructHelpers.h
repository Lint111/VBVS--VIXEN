#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include <array>
#include <string>
#include <stdexcept>

namespace RenderGraph::NodeHelpers {

/// ============ Pipeline Structure Builders ============

inline VkPipelineDynamicStateCreateInfo CreateDynamicStateInfo(
    const VkDynamicState* states,
    uint32_t stateCount
) {
    VkPipelineDynamicStateCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    info.dynamicStateCount = stateCount;
    info.pDynamicStates = states;
    return info;
}

inline VkPipelineVertexInputStateCreateInfo CreateVertexInputState(
    const VkVertexInputBindingDescription* bindings,
    uint32_t bindingCount,
    const VkVertexInputAttributeDescription* attributes,
    uint32_t attributeCount
) {
    VkPipelineVertexInputStateCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    info.vertexBindingDescriptionCount = bindingCount;
    info.pVertexBindingDescriptions = bindings;
    info.vertexAttributeDescriptionCount = attributeCount;
    info.pVertexAttributeDescriptions = attributes;
    return info;
}

inline VkPipelineInputAssemblyStateCreateInfo CreateInputAssemblyState(
    VkPrimitiveTopology topology,
    VkBool32 primitiveRestartEnable = VK_FALSE
) {
    VkPipelineInputAssemblyStateCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    info.topology = topology;
    info.primitiveRestartEnable = primitiveRestartEnable;
    return info;
}

inline VkPipelineRasterizationStateCreateInfo CreateRasterizationState(
    VkPolygonMode polygonMode,
    VkCullModeFlags cullMode,
    VkFrontFace frontFace,
    float lineWidth = 1.0f
) {
    VkPipelineRasterizationStateCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    info.polygonMode = polygonMode;
    info.cullMode = cullMode;
    info.frontFace = frontFace;
    info.lineWidth = lineWidth;
    info.depthClampEnable = VK_FALSE;
    info.rasterizerDiscardEnable = VK_FALSE;
    return info;
}

inline VkPipelineMultisampleStateCreateInfo CreateMultisampleState(
    VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT
) {
    VkPipelineMultisampleStateCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    info.rasterizationSamples = samples;
    info.sampleShadingEnable = VK_FALSE;
    info.minSampleShading = 1.0f;
    info.pSampleMask = nullptr;
    info.alphaToCoverageEnable = VK_FALSE;
    info.alphaToOneEnable = VK_FALSE;
    return info;
}

inline VkPipelineDepthStencilStateCreateInfo CreateDepthStencilState(
    VkBool32 depthTestEnable,
    VkBool32 depthWriteEnable,
    VkCompareOp depthCompareOp = VK_COMPARE_OP_LESS
) {
    VkPipelineDepthStencilStateCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    info.depthTestEnable = depthTestEnable;
    info.depthWriteEnable = depthWriteEnable;
    info.depthCompareOp = depthCompareOp;
    info.depthBoundsTestEnable = VK_FALSE;
    info.stencilTestEnable = VK_FALSE;
    return info;
}

inline VkPipelineColorBlendAttachmentState CreateColorBlendAttachment(
    VkBool32 blendEnable = VK_FALSE
) {
    VkPipelineColorBlendAttachmentState state{};
    state.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT |
        VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT |
        VK_COLOR_COMPONENT_A_BIT;
    state.blendEnable = blendEnable;
    return state;
}

/// Build a color-blend attachment state from a named blend mode (AR#32).
///
/// This is the single source of truth for the `BLEND_MODE` GraphicsPipelineNode
/// parameter — both the cacher path (PipelineCacher) and the manual fallback path
/// consume the value the node computes once via this function. All modes write all
/// color channels. Throws on an unknown mode, matching the Parse* convention in
/// EnumParsers.h (no silent fallthrough).
///
/// Modes:
/// - "None"               opaque, blending disabled (default; preserves prior behavior)
/// - "Alpha"              straight (non-premultiplied) alpha "over": src*a + dst*(1-a)
/// - "PremultipliedAlpha" alpha "over" for src already multiplied by its alpha: src + dst*(1-a)
/// - "Additive"           alpha-weighted additive (glow/fire/particles): src*a + dst
/// - "Multiply"           modulate/darken: src*dst
inline VkPipelineColorBlendAttachmentState MakeColorBlendAttachment(const std::string& mode) {
    VkPipelineColorBlendAttachmentState state{};
    state.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT |
        VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT |
        VK_COLOR_COMPONENT_A_BIT;
    state.colorBlendOp = VK_BLEND_OP_ADD;
    state.alphaBlendOp = VK_BLEND_OP_ADD;

    if (mode == "None") {
        state.blendEnable = VK_FALSE;
        return state;
    }

    state.blendEnable = VK_TRUE;
    if (mode == "Alpha") {
        state.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        state.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        state.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        state.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    } else if (mode == "PremultipliedAlpha") {
        state.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
        state.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        state.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        state.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    } else if (mode == "Additive") {
        state.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        state.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
        state.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        state.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    } else if (mode == "Multiply") {
        state.srcColorBlendFactor = VK_BLEND_FACTOR_DST_COLOR;
        state.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
        state.srcAlphaBlendFactor = VK_BLEND_FACTOR_DST_ALPHA;
        state.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    } else {
        throw std::runtime_error("Unknown blend mode: " + mode);
    }
    return state;
}

inline VkPipelineColorBlendStateCreateInfo CreateColorBlendState(
    const VkPipelineColorBlendAttachmentState* attachments,
    uint32_t attachmentCount,
    const std::array<float, 4>& blendConstants = {1.0f, 1.0f, 1.0f, 1.0f}
) {
    VkPipelineColorBlendStateCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    info.logicOpEnable = VK_FALSE;
    info.logicOp = VK_LOGIC_OP_COPY;
    info.attachmentCount = attachmentCount;
    info.pAttachments = attachments;
    info.blendConstants[0] = blendConstants[0];
    info.blendConstants[1] = blendConstants[1];
    info.blendConstants[2] = blendConstants[2];
    info.blendConstants[3] = blendConstants[3];
    return info;
}

/// ============ Render Pass Structure Builders ============

inline VkAttachmentDescription CreateAttachmentDescription(
    VkFormat format,
    VkAttachmentLoadOp loadOp,
    VkAttachmentStoreOp storeOp,
    VkImageLayout initialLayout,
    VkImageLayout finalLayout,
    VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT
) {
    VkAttachmentDescription desc{};
    desc.format = format;
    desc.samples = samples;
    desc.loadOp = loadOp;
    desc.storeOp = storeOp;
    desc.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    desc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    desc.initialLayout = initialLayout;
    desc.finalLayout = finalLayout;
    return desc;
}

inline VkAttachmentReference CreateAttachmentReference(
    uint32_t attachment,
    VkImageLayout layout
) {
    VkAttachmentReference ref{};
    ref.attachment = attachment;
    ref.layout = layout;
    return ref;
}

inline VkSubpassDescription CreateSubpassDescription(
    const VkAttachmentReference* colorAttachments,
    uint32_t colorAttachmentCount,
    const VkAttachmentReference* depthAttachment = nullptr
) {
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = colorAttachmentCount;
    subpass.pColorAttachments = colorAttachments;
    subpass.pDepthStencilAttachment = depthAttachment;
    return subpass;
}

inline VkSubpassDependency CreateSubpassDependency(
    uint32_t srcSubpass,
    uint32_t dstSubpass,
    VkPipelineStageFlags srcStageMask,
    VkPipelineStageFlags dstStageMask,
    VkAccessFlags srcAccessMask,
    VkAccessFlags dstAccessMask
) {
    VkSubpassDependency dep{};
    dep.srcSubpass = srcSubpass;
    dep.dstSubpass = dstSubpass;
    dep.srcStageMask = srcStageMask;
    dep.dstStageMask = dstStageMask;
    dep.srcAccessMask = srcAccessMask;
    dep.dstAccessMask = dstAccessMask;
    return dep;
}

/// ============ Framebuffer Structure Builders ============

inline VkFramebufferCreateInfo CreateFramebufferInfo(
    VkRenderPass renderPass,
    const VkImageView* attachments,
    uint32_t attachmentCount,
    uint32_t width,
    uint32_t height,
    uint32_t layers = 1
) {
    VkFramebufferCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    info.renderPass = renderPass;
    info.attachmentCount = attachmentCount;
    info.pAttachments = attachments;
    info.width = width;
    info.height = height;
    info.layers = layers;
    return info;
}

/// ============ Image Structure Builders ============

inline VkImageCreateInfo CreateImageInfo(
    VkImageType imageType,
    VkFormat format,
    VkExtent3D extent,
    uint32_t mipLevels,
    uint32_t arrayLayers,
    VkImageUsageFlags usage,
    VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT
) {
    VkImageCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    info.imageType = imageType;
    info.format = format;
    info.extent = extent;
    info.mipLevels = mipLevels;
    info.arrayLayers = arrayLayers;
    info.samples = samples;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage = usage;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    return info;
}

inline VkImageViewCreateInfo CreateImageViewInfo(
    VkImage image,
    VkImageViewType viewType,
    VkFormat format,
    VkImageAspectFlags aspectMask,
    uint32_t mipLevels = 1,
    uint32_t arrayLayers = 1
) {
    VkImageViewCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    info.image = image;
    info.viewType = viewType;
    info.format = format;
    info.subresourceRange.aspectMask = aspectMask;
    info.subresourceRange.baseMipLevel = 0;
    info.subresourceRange.levelCount = mipLevels;
    info.subresourceRange.baseArrayLayer = 0;
    info.subresourceRange.layerCount = arrayLayers;
    return info;
}

/// ============ Buffer Structure Builders ============

inline VkBufferCreateInfo CreateBufferInfo(
    VkDeviceSize size,
    VkBufferUsageFlags usage,
    VkSharingMode sharingMode = VK_SHARING_MODE_EXCLUSIVE
) {
    VkBufferCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    info.size = size;
    info.usage = usage;
    info.sharingMode = sharingMode;
    return info;
}

} // namespace RenderGraph::NodeHelpers
