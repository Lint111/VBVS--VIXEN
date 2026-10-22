#include "Ui/VixenRmlRenderInterface.h"
#include "Ui/UiShaderSpirv.g.h"
#include "Text/Abi.h"
#include "Text/AtlasFormat.h"
#include "Text/GlyphAtlas.h"
#include "Text/TextShaderSpirv.g.h"

#include <RmlUi/Core/Vertex.h>

#include <cstring>
#include <cstddef>
#include <span>
#include <stdexcept>

namespace Vixen::Ui {
namespace {

void Check(VkResult r, const char* what) {
    if (r != VK_SUCCESS) throw std::runtime_error(std::string("VixenRmlRenderInterface: ") + what);
}

VkShaderModule MakeShaderModule(VkDevice device, const uint32_t* code, size_t sizeBytes) {
    VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    ci.codeSize = sizeBytes;
    ci.pCode = code;
    VkShaderModule m = VK_NULL_HANDLE;
    Check(vkCreateShaderModule(device, &ci, nullptr, &m), "shader module");
    return m;
}

} // namespace

uint32_t VixenRmlRenderInterface::FindMemoryType(uint32_t typeBits, VkMemoryPropertyFlags props) const {
    for (uint32_t i = 0; i < memProps_.memoryTypeCount; ++i) {
        if ((typeBits & (1u << i)) && (memProps_.memoryTypes[i].propertyFlags & props) == props)
            return i;
    }
    throw std::runtime_error("VixenRmlRenderInterface: no suitable memory type");
}

void VixenRmlRenderInterface::CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                                           VkMemoryPropertyFlags props, VkBuffer& outBuf, VkDeviceMemory& outMem) {
    VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bi.size = size;
    bi.usage = usage;
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    Check(vkCreateBuffer(device_, &bi, nullptr, &outBuf), "create buffer");

    VkMemoryRequirements req{};
    vkGetBufferMemoryRequirements(device_, outBuf, &req);
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = FindMemoryType(req.memoryTypeBits, props);
    Check(vkAllocateMemory(device_, &ai, nullptr, &outMem), "allocate buffer memory");
    Check(vkBindBufferMemory(device_, outBuf, outMem, 0), "bind buffer memory");
}

void VixenRmlRenderInterface::Init(VkDevice device, VkPhysicalDevice physicalDevice, VkQueue queue,
                                   uint32_t queueFamilyIndex, const VkPhysicalDeviceMemoryProperties& memProps,
                                   VkCommandPool commandPool, VkRenderPass renderPass,
                                   OneShotSubmit submitOneShot,
                                   VkPipelineCache pipelineCache) {
    device_ = device;
    pipelineCache_ = pipelineCache;
    physicalDevice_ = physicalDevice;
    queue_ = queue;
    submitOneShot_ = std::move(submitOneShot);
    queueFamilyIndex_ = queueFamilyIndex;
    memProps_ = memProps;
    commandPool_ = commandPool;
    renderPass_ = renderPass;

    CreatePipeline();
    CreateTextPipeline();

    // 1x1 opaque-white default texture so untextured geometry reuses the textured pipeline.
    const uint8_t white[4] = {255, 255, 255, 255};
    defaultTexture_ = CreateTextureRGBA(white, 1, 1);
}

void VixenRmlRenderInterface::CreatePipeline() {
    // Descriptor set layout: one combined image sampler at binding 0, fragment stage.
    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo dlci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    dlci.bindingCount = 1;
    dlci.pBindings = &binding;
    Check(vkCreateDescriptorSetLayout(device_, &dlci, nullptr, &descriptorLayout_), "descriptor set layout");

    // Descriptor pool: enough for the font atlas, the default texture, and any generated textures.
    VkDescriptorPoolSize poolSizes[2]{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[0].descriptorCount = 256;
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSizes[1].descriptorCount = 768;
    VkDescriptorPoolCreateInfo pci{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pci.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    pci.maxSets = 256;
    pci.poolSizeCount = 2;
    pci.pPoolSizes = poolSizes;
    Check(vkCreateDescriptorPool(device_, &pci, nullptr, &descriptorPool_), "descriptor pool");

    // Pipeline layout: the descriptor set + a 16-byte vertex push constant (translation + viewport).
    VkPushConstantRange pcRange{};
    pcRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pcRange.offset = 0;
    pcRange.size = sizeof(float) * 4 + sizeof(uint32_t);
    VkPipelineLayoutCreateInfo plci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    plci.setLayoutCount = 1;
    plci.pSetLayouts = &descriptorLayout_;
    plci.pushConstantRangeCount = 1;
    plci.pPushConstantRanges = &pcRange;
    Check(vkCreatePipelineLayout(device_, &plci, nullptr, &pipelineLayout_), "pipeline layout");

    VkShaderModule vert = MakeShaderModule(device_, kUiVertSpirv, sizeof(kUiVertSpirv));
    VkShaderModule frag = MakeShaderModule(device_, kUiFragSpirv, sizeof(kUiFragSpirv));
    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vert;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = frag;
    stages[1].pName = "main";

    // Vertex input matches Rml::Vertex { Vector2f position; ColourbPremultiplied colour; Vector2f tex_coord; }.
    VkVertexInputBindingDescription bindDesc{};
    bindDesc.binding = 0;
    bindDesc.stride = sizeof(Rml::Vertex);
    bindDesc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    VkVertexInputAttributeDescription attrs[3]{};
    attrs[0].location = 0; attrs[0].binding = 0; attrs[0].format = VK_FORMAT_R32G32_SFLOAT;  attrs[0].offset = offsetof(Rml::Vertex, position);
    attrs[1].location = 1; attrs[1].binding = 0; attrs[1].format = VK_FORMAT_R8G8B8A8_UNORM; attrs[1].offset = offsetof(Rml::Vertex, colour);
    attrs[2].location = 2; attrs[2].binding = 0; attrs[2].format = VK_FORMAT_R32G32_SFLOAT;  attrs[2].offset = offsetof(Rml::Vertex, tex_coord);
    VkPipelineVertexInputStateCreateInfo vin{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vin.vertexBindingDescriptionCount = 1;
    vin.pVertexBindingDescriptions = &bindDesc;
    vin.vertexAttributeDescriptionCount = 3;
    vin.pVertexAttributeDescriptions = attrs;

    VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vp.viewportCount = 1;
    vp.scissorCount = 1;  // dynamic — values set per frame

    VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // Premultiplied-alpha blending (RmlUi vertex colour is premultiplied).
    VkPipelineColorBlendAttachmentState blend{};
    blend.blendEnable = VK_TRUE;
    blend.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
    blend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blend.colorBlendOp = VK_BLEND_OP_ADD;
    blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blend.alphaBlendOp = VK_BLEND_OP_ADD;
    blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    cb.attachmentCount = 1;
    cb.pAttachments = &blend;

    VkDynamicState dyn[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    ds.dynamicStateCount = 2;
    ds.pDynamicStates = dyn;

    VkGraphicsPipelineCreateInfo gp{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    gp.stageCount = 2;
    gp.pStages = stages;
    gp.pVertexInputState = &vin;
    gp.pInputAssemblyState = &ia;
    gp.pViewportState = &vp;
    gp.pRasterizationState = &rs;
    gp.pMultisampleState = &ms;
    gp.pColorBlendState = &cb;
    gp.pDynamicState = &ds;
    gp.layout = pipelineLayout_;
    gp.renderPass = renderPass_;  // UI-owned, color-only → no depth-stencil state needed
    gp.subpass = 0;
    Check(vkCreateGraphicsPipelines(device_, pipelineCache_, 1, &gp, nullptr, &pipeline_), "graphics pipeline");

    vkDestroyShaderModule(device_, vert, nullptr);
    vkDestroyShaderModule(device_, frag, nullptr);
}

void VixenRmlRenderInterface::CreateTextPipeline() {
    VkDescriptorSetLayoutBinding bindings[4]{};
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    for (uint32_t i = 1; i < 4; ++i) {
        bindings[i].binding = i;
        bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[i].descriptorCount = 1;
        bindings[i].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    }
    VkDescriptorSetLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    layoutInfo.bindingCount = 4;
    layoutInfo.pBindings = bindings;
    Check(vkCreateDescriptorSetLayout(device_, &layoutInfo, nullptr, &textDescriptorLayout_),
          "text descriptor set layout");

    VkPushConstantRange push{};
    push.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    push.offset = 0;
    push.size = sizeof(float) * 4;
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &textDescriptorLayout_;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &push;
    Check(vkCreatePipelineLayout(device_, &pipelineLayoutInfo, nullptr, &textPipelineLayout_),
          "text pipeline layout");

    VkShaderModule vert = MakeShaderModule(device_, Vixen::Text::kTextVertSpirv,
                                            sizeof(Vixen::Text::kTextVertSpirv));
    VkShaderModule frag = MakeShaderModule(device_, Vixen::Text::kTextFragSpirv,
                                            sizeof(Vixen::Text::kTextFragSpirv));
    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vert;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = frag;
    stages[1].pName = "main";

    VkPipelineVertexInputStateCreateInfo vertexInput{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo viewport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewport.viewportCount = 1;
    viewport.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = VK_CULL_MODE_NONE;
    raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo multisample{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineColorBlendAttachmentState blend{};
    blend.blendEnable = VK_TRUE;
    blend.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
    blend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blend.colorBlendOp = VK_BLEND_OP_ADD;
    blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blend.alphaBlendOp = VK_BLEND_OP_ADD;
    blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                           VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo colorBlend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    colorBlend.attachmentCount = 1;
    colorBlend.pAttachments = &blend;
    VkDynamicState dynamicStates[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamic.dynamicStateCount = 2;
    dynamic.pDynamicStates = dynamicStates;

    VkGraphicsPipelineCreateInfo pipelineInfo{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = stages;
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &assembly;
    pipelineInfo.pViewportState = &viewport;
    pipelineInfo.pRasterizationState = &raster;
    pipelineInfo.pMultisampleState = &multisample;
    pipelineInfo.pColorBlendState = &colorBlend;
    pipelineInfo.pDynamicState = &dynamic;
    pipelineInfo.layout = textPipelineLayout_;
    pipelineInfo.renderPass = renderPass_;
    Check(vkCreateGraphicsPipelines(device_, pipelineCache_, 1, &pipelineInfo, nullptr, &textPipeline_),
          "text graphics pipeline");
    vkDestroyShaderModule(device_, vert, nullptr);
    vkDestroyShaderModule(device_, frag, nullptr);
}

VixenRmlRenderInterface::Texture* VixenRmlRenderInterface::CreateTextureRGBA(const uint8_t* rgba, uint32_t width, uint32_t height,
                                                                              bool isMsdf) {
    const VkDeviceSize bytes = VkDeviceSize(width) * height * 4;

    VkBuffer staging = VK_NULL_HANDLE; VkDeviceMemory stagingMem = VK_NULL_HANDLE;
    CreateBuffer(bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, staging, stagingMem);
    void* mapped = nullptr;
    Check(vkMapMemory(device_, stagingMem, 0, bytes, 0, &mapped), "map staging");
    std::memcpy(mapped, rgba, static_cast<size_t>(bytes));
    vkUnmapMemory(device_, stagingMem);

    auto* tex = new Texture();
    tex->isMsdf = isMsdf;
    VkImageCreateInfo ici{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ici.imageType = VK_IMAGE_TYPE_2D;
    ici.format = VK_FORMAT_R8G8B8A8_UNORM;
    ici.extent = {width, height, 1};
    ici.mipLevels = 1;
    ici.arrayLayers = 1;
    ici.samples = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling = VK_IMAGE_TILING_OPTIMAL;
    ici.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    ici.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    Check(vkCreateImage(device_, &ici, nullptr, &tex->image), "create image");

    VkMemoryRequirements req{};
    vkGetImageMemoryRequirements(device_, tex->image, &req);
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = FindMemoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    Check(vkAllocateMemory(device_, &ai, nullptr, &tex->memory), "allocate image memory");
    Check(vkBindImageMemory(device_, tex->image, tex->memory, 0), "bind image memory");

    // One-shot command buffer: transition → copy → transition to shader-read.
    VkCommandBufferAllocateInfo cbai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cbai.commandPool = commandPool_;
    cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbai.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    Check(vkAllocateCommandBuffers(device_, &cbai, &cmd), "alloc one-shot cmd");
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &bi);

    VkImageMemoryBarrier toDst{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    toDst.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    toDst.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toDst.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toDst.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toDst.image = tex->image;
    toDst.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    toDst.srcAccessMask = 0;
    toDst.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                         0, nullptr, 0, nullptr, 1, &toDst);

    VkBufferImageCopy copy{};
    copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    copy.imageExtent = {width, height, 1};
    vkCmdCopyBufferToImage(cmd, staging, tex->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);

    VkImageMemoryBarrier toRead = toDst;
    toRead.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toRead.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    toRead.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    toRead.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
                         0, nullptr, 0, nullptr, 1, &toRead);

    vkEndCommandBuffer(cmd);
    // R715: the upload rides the owning node's submission stream (queue order plus the barriers
    // above order it before the UI draw that samples it); the staging buffer is released once the
    // queue timeline reaches it instead of after a submit plus wait idle.
    const VkDevice device = device_;
    const bool submitted = submitOneShot_ && submitOneShot_(commandPool_, cmd, [device, staging, stagingMem] {
        vkDestroyBuffer(device, staging, nullptr);
        vkFreeMemory(device, stagingMem, nullptr);
    });
    if (!submitted) Check(VK_ERROR_INITIALIZATION_FAILED, "submit one-shot");

    VkImageViewCreateInfo vci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vci.image = tex->image;
    vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vci.format = VK_FORMAT_R8G8B8A8_UNORM;
    vci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    Check(vkCreateImageView(device_, &vci, nullptr, &tex->view), "image view");

    VkSamplerCreateInfo sci{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    sci.magFilter = VK_FILTER_LINEAR;
    sci.minFilter = VK_FILTER_LINEAR;
    sci.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    Check(vkCreateSampler(device_, &sci, nullptr, &tex->sampler), "sampler");

    VkDescriptorSetAllocateInfo dsai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    dsai.descriptorPool = descriptorPool_;
    dsai.descriptorSetCount = 1;
    dsai.pSetLayouts = &descriptorLayout_;
    Check(vkAllocateDescriptorSets(device_, &dsai, &tex->set), "alloc descriptor set");
    VkDescriptorImageInfo dii{};
    dii.sampler = tex->sampler;
    dii.imageView = tex->view;
    dii.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    w.dstSet = tex->set;
    w.dstBinding = 0;
    w.descriptorCount = 1;
    w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    w.pImageInfo = &dii;
    vkUpdateDescriptorSets(device_, 1, &w, 0, nullptr);

    textures_.push_back(tex);
    return tex;
}

void VixenRmlRenderInterface::DestroyGeometry(Geometry* g) {
    if (!g) return;
    if (g->vbuf) vkDestroyBuffer(device_, g->vbuf, nullptr);
    if (g->vmem) vkFreeMemory(device_, g->vmem, nullptr);
    if (g->ibuf) vkDestroyBuffer(device_, g->ibuf, nullptr);
    if (g->imem) vkFreeMemory(device_, g->imem, nullptr);
    delete g;
}

void VixenRmlRenderInterface::DestroyTexture(Texture* t) {
    if (!t) return;
    if (t->set) vkFreeDescriptorSets(device_, descriptorPool_, 1, &t->set);
    if (t->sampler) vkDestroySampler(device_, t->sampler, nullptr);
    if (t->view) vkDestroyImageView(device_, t->view, nullptr);
    if (t->image) vkDestroyImage(device_, t->image, nullptr);
    if (t->memory) vkFreeMemory(device_, t->memory, nullptr);
    delete t;
}

void VixenRmlRenderInterface::DestroyTextDrawResources(TextDrawResources* resources) {
    if (!resources) return;
    if (resources->descriptorSet)
        vkFreeDescriptorSets(device_, descriptorPool_, 1, &resources->descriptorSet);
    if (resources->glyphBuffer) vkDestroyBuffer(device_, resources->glyphBuffer, nullptr);
    if (resources->glyphMemory) vkFreeMemory(device_, resources->glyphMemory, nullptr);
    if (resources->styleBuffer) vkDestroyBuffer(device_, resources->styleBuffer, nullptr);
    if (resources->styleMemory) vkFreeMemory(device_, resources->styleMemory, nullptr);
    if (resources->animationBuffer) vkDestroyBuffer(device_, resources->animationBuffer, nullptr);
    if (resources->animationMemory) vkFreeMemory(device_, resources->animationMemory, nullptr);
    delete resources;
}

void VixenRmlRenderInterface::BeginFrame(VkCommandBuffer cmd, VkExtent2D extent) {
    cmd_ = cmd;
    extent_ = extent;
    scissorEnabled_ = false;

    // Advance the frame clock, then reclaim any deferred-released resource old enough that no in-flight
    // frame can still reference it (age >= kDeferFrames). Bias LATE: a resource freed a frame too early
    // is a use-after-free. Iterate-and-erase in place; the lists are tiny (HUD geometry only).
    ++frameCounter_;
    for (auto it = pendingGeometryDeletes_.begin(); it != pendingGeometryDeletes_.end();) {
        if (frameCounter_ - it->first >= kDeferFrames) {
            DestroyGeometry(it->second);
            it = pendingGeometryDeletes_.erase(it);
        } else {
            ++it;
        }
    }
    for (auto it = pendingTextureDeletes_.begin(); it != pendingTextureDeletes_.end();) {
        if (frameCounter_ - it->first >= kDeferFrames) {
            DestroyTexture(it->second);
            it = pendingTextureDeletes_.erase(it);
        } else {
            ++it;
        }
    }
    for (auto it = pendingTextDrawDeletes_.begin(); it != pendingTextDrawDeletes_.end();) {
        if (frameCounter_ - it->first >= kDeferFrames) {
            DestroyTextDrawResources(it->second);
            it = pendingTextDrawDeletes_.erase(it);
        } else {
            ++it;
        }
    }
}

void VixenRmlRenderInterface::DrawTextInstances(const Vixen::Text::GlyphAtlas& atlas,
                                       std::span<const Vixen::Text::GlyphInstance> glyphs,
                                       std::span<const Vixen::Text::SpanStyle> styles,
                                       std::span<const Vixen::Text::SpanAnim> animations,
                                       float timeSeconds) {
    if (cmd_ == VK_NULL_HANDLE || textPipeline_ == VK_NULL_HANDLE || glyphs.empty() ||
        styles.empty() || animations.empty() || extent_.width == 0 || extent_.height == 0)
        return;

    AtlasTexture& cached = atlasTextures_[&atlas];
    const uint32_t atlasVersion = atlas.Version();
    if (!cached.texture || cached.version != atlasVersion) {
        if (cached.texture)
            ReleaseTexture(reinterpret_cast<Rml::TextureHandle>(cached.texture));
        const std::span<const std::uint8_t> pixels = atlas.Pixels();
        cached.texture = CreateTextureRGBA(pixels.data(), atlas.PageDimension(), atlas.PageDimension(), true);
        cached.version = atlasVersion;
    }

    auto* resources = new TextDrawResources();
    auto upload = [&](std::span<const std::byte> bytes, VkBuffer& buffer, VkDeviceMemory& memory) {
        CreateBuffer(bytes.size(), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                     buffer, memory);
        void* mapped = nullptr;
        Check(vkMapMemory(device_, memory, 0, bytes.size(), 0, &mapped), "map text storage buffer");
        std::memcpy(mapped, bytes.data(), bytes.size());
        vkUnmapMemory(device_, memory);
    };
    upload(std::as_bytes(glyphs), resources->glyphBuffer, resources->glyphMemory);
    upload(std::as_bytes(styles), resources->styleBuffer, resources->styleMemory);
    upload(std::as_bytes(animations), resources->animationBuffer, resources->animationMemory);

    VkDescriptorSetAllocateInfo allocate{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    allocate.descriptorPool = descriptorPool_;
    allocate.descriptorSetCount = 1;
    allocate.pSetLayouts = &textDescriptorLayout_;
    Check(vkAllocateDescriptorSets(device_, &allocate, &resources->descriptorSet),
          "allocate text descriptor set");
    VkDescriptorImageInfo image{};
    image.sampler = cached.texture->sampler;
    image.imageView = cached.texture->view;
    image.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkDescriptorBufferInfo buffers[3] = {
        {resources->glyphBuffer, 0, VK_WHOLE_SIZE},
        {resources->styleBuffer, 0, VK_WHOLE_SIZE},
        {resources->animationBuffer, 0, VK_WHOLE_SIZE},
    };
    VkWriteDescriptorSet writes[4]{};
    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = resources->descriptorSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorCount = 1;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[0].pImageInfo = &image;
    for (uint32_t i = 0; i < 3; ++i) {
        writes[i + 1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i + 1].dstSet = resources->descriptorSet;
        writes[i + 1].dstBinding = i + 1;
        writes[i + 1].descriptorCount = 1;
        writes[i + 1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[i + 1].pBufferInfo = &buffers[i];
    }
    vkUpdateDescriptorSets(device_, 4, writes, 0, nullptr);

    vkCmdBindPipeline(cmd_, VK_PIPELINE_BIND_POINT_GRAPHICS, textPipeline_);
    VkViewport viewport{0.0f, 0.0f, static_cast<float>(extent_.width),
                        static_cast<float>(extent_.height), 0.0f, 1.0f};
    vkCmdSetViewport(cmd_, 0, 1, &viewport);
    const VkRect2D scissor{{0, 0}, extent_};
    vkCmdSetScissor(cmd_, 0, 1, &scissor);
    const float push[4] = {static_cast<float>(extent_.width), static_cast<float>(extent_.height),
                           timeSeconds, 0.0f};
    vkCmdPushConstants(cmd_, textPipelineLayout_, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push), push);
    vkCmdBindDescriptorSets(cmd_, VK_PIPELINE_BIND_POINT_GRAPHICS, textPipelineLayout_,
                            0, 1, &resources->descriptorSet, 0, nullptr);
    vkCmdDraw(cmd_, 6, static_cast<uint32_t>(glyphs.size()), 0, 0);
    pendingTextDrawDeletes_.emplace_back(frameCounter_, resources);
}

Rml::CompiledGeometryHandle VixenRmlRenderInterface::CompileGeometry(Rml::Span<const Rml::Vertex> vertices,
                                                                     Rml::Span<const int> indices) {
    auto* g = new Geometry();
    g->indexCount = static_cast<uint32_t>(indices.size());

    const VkDeviceSize vbytes = vertices.size() * sizeof(Rml::Vertex);
    CreateBuffer(vbytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, g->vbuf, g->vmem);
    void* vmap = nullptr;
    vkMapMemory(device_, g->vmem, 0, vbytes, 0, &vmap);
    std::memcpy(vmap, vertices.data(), static_cast<size_t>(vbytes));
    vkUnmapMemory(device_, g->vmem);

    const VkDeviceSize ibytes = indices.size() * sizeof(int);
    CreateBuffer(ibytes, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, g->ibuf, g->imem);
    void* imap = nullptr;
    vkMapMemory(device_, g->imem, 0, ibytes, 0, &imap);
    std::memcpy(imap, indices.data(), static_cast<size_t>(ibytes));
    vkUnmapMemory(device_, g->imem);

    geometries_.push_back(g);
    return reinterpret_cast<Rml::CompiledGeometryHandle>(g);
}

void VixenRmlRenderInterface::RenderGeometry(Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation,
                                             Rml::TextureHandle texture) {
    if (cmd_ == VK_NULL_HANDLE) return;
    auto* g = reinterpret_cast<Geometry*>(geometry);

    vkCmdBindPipeline(cmd_, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);

    VkViewport viewport{0.0f, 0.0f, static_cast<float>(extent_.width), static_cast<float>(extent_.height), 0.0f, 1.0f};
    vkCmdSetViewport(cmd_, 0, 1, &viewport);
    VkRect2D scissor = scissorEnabled_ ? scissor_ : VkRect2D{{0, 0}, extent_};
    vkCmdSetScissor(cmd_, 0, 1, &scissor);

    Texture* tex = texture ? reinterpret_cast<Texture*>(texture) : defaultTexture_;
    struct UiPushConstants {
        float translation[2];
        float viewport[2];
        uint32_t isMsdf;
    };
    const UiPushConstants push{{translation.x, translation.y},
                               {static_cast<float>(extent_.width), static_cast<float>(extent_.height)},
                               tex->isMsdf ? 1u : 0u};
    vkCmdPushConstants(cmd_, pipelineLayout_, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(push), &push);

    vkCmdBindDescriptorSets(cmd_, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout_, 0, 1, &tex->set, 0, nullptr);

    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd_, 0, 1, &g->vbuf, &offset);
    vkCmdBindIndexBuffer(cmd_, g->ibuf, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(cmd_, g->indexCount, 1, 0, 0, 0);
}

void VixenRmlRenderInterface::ReleaseGeometry(Rml::CompiledGeometryHandle geometry) {
    auto* g = reinterpret_cast<Geometry*>(geometry);
    if (!g) return;
    // Hand ownership from geometries_ to the deferred-delete queue: erase it here (so Shutdown won't
    // double-free) and free it later in BeginFrame once it is past every in-flight frame. RmlUi may
    // call Release while the buffer is still bound in a frame the GPU hasn't finished — freeing now
    // would be a use-after-free.
    for (auto it = geometries_.begin(); it != geometries_.end(); ++it) {
        if (*it == g) { geometries_.erase(it); break; }
    }
    pendingGeometryDeletes_.emplace_back(frameCounter_, g);
}

Rml::TextureHandle VixenRmlRenderInterface::LoadTexture(Rml::Vector2i& /*dims*/, const Rml::String& /*source*/) {
    return 0;  // S0 demo has no <img> sources; image loading lands in a later slice.
}

Rml::TextureHandle VixenRmlRenderInterface::GenerateTexture(Rml::Span<const Rml::byte> source, Rml::Vector2i dims) {
    const auto* bytes = reinterpret_cast<const uint8_t*>(source.data());
    const bool isMsdf = dims.x > 0 && dims.y > 0 && source.size() >= sizeof(Vixen::Text::kAtlasFormatMarker) &&
                        std::memcmp(bytes, Vixen::Text::kAtlasFormatMarker, sizeof(Vixen::Text::kAtlasFormatMarker)) == 0;
    Texture* tex = CreateTextureRGBA(reinterpret_cast<const uint8_t*>(source.data()),
                                     static_cast<uint32_t>(dims.x), static_cast<uint32_t>(dims.y), isMsdf);
    return reinterpret_cast<Rml::TextureHandle>(tex);
}

void VixenRmlRenderInterface::ReleaseTexture(Rml::TextureHandle texture) {
    auto* t = reinterpret_cast<Texture*>(texture);
    if (!t) return;
    // Same deferred hand-off as ReleaseGeometry: erase from textures_ so Shutdown won't double-free,
    // then free in BeginFrame once no in-flight frame can still sample it.
    for (auto it = textures_.begin(); it != textures_.end(); ++it) {
        if (*it == t) { textures_.erase(it); break; }
    }
    pendingTextureDeletes_.emplace_back(frameCounter_, t);
}

void VixenRmlRenderInterface::EnableScissorRegion(bool enable) {
    scissorEnabled_ = enable;
}

void VixenRmlRenderInterface::SetScissorRegion(Rml::Rectanglei region) {
    scissor_.offset = {region.Left(), region.Top()};
    scissor_.extent = {static_cast<uint32_t>(region.Width()), static_cast<uint32_t>(region.Height())};
}

void VixenRmlRenderInterface::Shutdown() {
    if (device_ == VK_NULL_HANDLE) return;
    // Caller vkDeviceWaitIdle's first, so everything still owned — live AND deferred-but-not-yet-freed —
    // is safe to destroy now. Drain both the live lists and the pending-delete queues via the same
    // per-struct helpers the deferred path uses, so teardown is identical no matter which path frees it.

    for (Texture* t : textures_) DestroyTexture(t);
    textures_.clear();
    defaultTexture_ = nullptr;  // owned via textures_ above
    for (auto& [releaseFrame, t] : pendingTextureDeletes_) { (void)releaseFrame; DestroyTexture(t); }
    pendingTextureDeletes_.clear();

    for (Geometry* g : geometries_) DestroyGeometry(g);
    geometries_.clear();
    for (auto& [releaseFrame, g] : pendingGeometryDeletes_) { (void)releaseFrame; DestroyGeometry(g); }
    pendingGeometryDeletes_.clear();
    for (auto& [releaseFrame, resources] : pendingTextDrawDeletes_) {
        (void)releaseFrame;
        DestroyTextDrawResources(resources);
    }
    pendingTextDrawDeletes_.clear();
    atlasTextures_.clear();

    if (pipeline_) vkDestroyPipeline(device_, pipeline_, nullptr);
    if (textPipeline_) vkDestroyPipeline(device_, textPipeline_, nullptr);
    if (pipelineLayout_) vkDestroyPipelineLayout(device_, pipelineLayout_, nullptr);
    if (textPipelineLayout_) vkDestroyPipelineLayout(device_, textPipelineLayout_, nullptr);
    if (descriptorPool_) vkDestroyDescriptorPool(device_, descriptorPool_, nullptr);
    if (descriptorLayout_) vkDestroyDescriptorSetLayout(device_, descriptorLayout_, nullptr);
    if (textDescriptorLayout_) vkDestroyDescriptorSetLayout(device_, textDescriptorLayout_, nullptr);
    pipeline_ = VK_NULL_HANDLE;
    textPipeline_ = VK_NULL_HANDLE;
    pipelineLayout_ = VK_NULL_HANDLE;
    textPipelineLayout_ = VK_NULL_HANDLE;
    descriptorPool_ = VK_NULL_HANDLE;
    descriptorLayout_ = VK_NULL_HANDLE;
    textDescriptorLayout_ = VK_NULL_HANDLE;
    device_ = VK_NULL_HANDLE;
    pipelineCache_ = VK_NULL_HANDLE;
}

} // namespace Vixen::Ui
