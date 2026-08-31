#include "Ui/VixenRmlRenderInterface.h"
#include "Ui/UiShaderSpirv.g.h"

#include <RmlUi/Core/Vertex.h>

#include <cstring>
#include <mutex>
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
                                   std::mutex* submitMutex) {
    device_ = device;
    physicalDevice_ = physicalDevice;
    queue_ = queue;
    submitMutex_ = submitMutex;
    queueFamilyIndex_ = queueFamilyIndex;
    memProps_ = memProps;
    commandPool_ = commandPool;
    renderPass_ = renderPass;

    CreatePipeline();

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
    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSize.descriptorCount = 64;
    VkDescriptorPoolCreateInfo pci{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pci.maxSets = 64;
    pci.poolSizeCount = 1;
    pci.pPoolSizes = &poolSize;
    Check(vkCreateDescriptorPool(device_, &pci, nullptr, &descriptorPool_), "descriptor pool");

    // Pipeline layout: the descriptor set + a 16-byte vertex push constant (translation + viewport).
    VkPushConstantRange pcRange{};
    pcRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pcRange.offset = 0;
    pcRange.size = sizeof(float) * 4;
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
    Check(vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &gp, nullptr, &pipeline_), "graphics pipeline");

    vkDestroyShaderModule(device_, vert, nullptr);
    vkDestroyShaderModule(device_, frag, nullptr);
}

VixenRmlRenderInterface::Texture* VixenRmlRenderInterface::CreateTextureRGBA(const uint8_t* rgba, uint32_t width, uint32_t height) {
    const VkDeviceSize bytes = VkDeviceSize(width) * height * 4;

    VkBuffer staging = VK_NULL_HANDLE; VkDeviceMemory stagingMem = VK_NULL_HANDLE;
    CreateBuffer(bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, staging, stagingMem);
    void* mapped = nullptr;
    Check(vkMapMemory(device_, stagingMem, 0, bytes, 0, &mapped), "map staging");
    std::memcpy(mapped, rgba, static_cast<size_t>(bytes));
    vkUnmapMemory(device_, stagingMem);

    auto* tex = new Texture();
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
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;
    {
        // Externally synchronized per Vulkan spec (audit V-M11).
        std::unique_lock<std::mutex> lock;
        if (submitMutex_) lock = std::unique_lock<std::mutex>(*submitMutex_);
        Check(vkQueueSubmit(queue_, 1, &si, VK_NULL_HANDLE), "submit one-shot");
        vkQueueWaitIdle(queue_);
    }
    vkFreeCommandBuffers(device_, commandPool_, 1, &cmd);
    vkDestroyBuffer(device_, staging, nullptr);
    vkFreeMemory(device_, stagingMem, nullptr);

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
    if (t->sampler) vkDestroySampler(device_, t->sampler, nullptr);
    if (t->view) vkDestroyImageView(device_, t->view, nullptr);
    if (t->image) vkDestroyImage(device_, t->image, nullptr);
    if (t->memory) vkFreeMemory(device_, t->memory, nullptr);
    // The descriptor set is freed implicitly when the pool is destroyed in Shutdown (the pool was not
    // created with VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT, so it is not individually freeable);
    // it is cheap and bounded by the 64-set pool, so leaving it to pool teardown is fine.
    delete t;
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

    const float push[4] = {translation.x, translation.y,
                           static_cast<float>(extent_.width), static_cast<float>(extent_.height)};
    vkCmdPushConstants(cmd_, pipelineLayout_, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push), push);

    Texture* tex = texture ? reinterpret_cast<Texture*>(texture) : defaultTexture_;
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
    Texture* tex = CreateTextureRGBA(reinterpret_cast<const uint8_t*>(source.data()),
                                     static_cast<uint32_t>(dims.x), static_cast<uint32_t>(dims.y));
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

    if (pipeline_) vkDestroyPipeline(device_, pipeline_, nullptr);
    if (pipelineLayout_) vkDestroyPipelineLayout(device_, pipelineLayout_, nullptr);
    if (descriptorPool_) vkDestroyDescriptorPool(device_, descriptorPool_, nullptr);
    if (descriptorLayout_) vkDestroyDescriptorSetLayout(device_, descriptorLayout_, nullptr);
    pipeline_ = VK_NULL_HANDLE;
    pipelineLayout_ = VK_NULL_HANDLE;
    descriptorPool_ = VK_NULL_HANDLE;
    descriptorLayout_ = VK_NULL_HANDLE;
    device_ = VK_NULL_HANDLE;
}

} // namespace Vixen::Ui
