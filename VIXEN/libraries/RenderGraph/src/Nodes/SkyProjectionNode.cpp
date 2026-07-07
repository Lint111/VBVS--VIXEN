#include "Nodes/SkyProjectionNode.h"
#include "Core/NodeRegistration.h"
#include "Core/NodeLogging.h"
#include "Core/RenderGraph.h"           // GetOwningGraph()->GetFrameSyncSchedule()
#include "Core/FrameSyncSchedule.h"     // SubmitGroup, SyncEdge, FindGroupForNode
#include "VulkanDevice.h"
#include "VulkanSwapChain.h"             // Vixen::Vulkan::Resources::IRenderTarget

#include "ShaderCompiler.h"             // ShaderManagement::ShaderCompiler (runtime GLSL -> SPIR-V)

#include "TierAddress.h"
#include "TierMath.h"
#include "TierDirection.h"
#include "TierMagnitude.h"

#include <glm/glm.hpp>

#include <cstring>
#include <filesystem>
#include <set>
#include <stdexcept>
#include <vector>

namespace Vixen::RenderGraph {

namespace {

void Check(VkResult r, const char* what) {
    if (r != VK_SUCCESS) throw std::runtime_error(std::string("[SkyProjectionNode] ") + what);
}

uint32_t FindMemoryType(const VkPhysicalDeviceMemoryProperties& memProps, uint32_t typeFilter,
                        VkMemoryPropertyFlags required, const char* context) {
    for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
        if ((typeFilter & (1u << i)) &&
            (memProps.memoryTypes[i].propertyFlags & required) == required) {
            return i;
        }
    }
    throw std::runtime_error(std::string("[SkyProjectionNode] No suitable memory type for ") + context);
}

// Locate a shader source file: same possiblePaths convention BuildRenderGraph.cpp uses for
// BodyInstanceRayMarch.comp (VIXEN_SHADER_SOURCE_DIR compile-time define, then relative
// fallbacks) — this new pair lives in the same top-level shaders/ directory.
std::filesystem::path ResolveShaderPath(const std::string& shaderName) {
    std::vector<std::filesystem::path> candidates = {
#ifdef VIXEN_SHADER_SOURCE_DIR
        std::filesystem::path(VIXEN_SHADER_SOURCE_DIR) / shaderName,
#endif
        std::filesystem::path("shaders") / shaderName,
        std::filesystem::path("../shaders") / shaderName,
        std::filesystem::path(shaderName),
    };
    for (const auto& path : candidates) {
        if (std::filesystem::exists(path)) return path;
    }
    return {};
}

VkShaderModule CompileAndCreateModule(VkDevice device, ShaderManagement::ShaderStage stage,
                                      const std::string& shaderName) {
    std::filesystem::path path = ResolveShaderPath(shaderName);
    if (path.empty()) {
        throw std::runtime_error("[SkyProjectionNode] " + shaderName + " not found in shader search paths");
    }

    ShaderManagement::ShaderCompiler compiler;
    ShaderManagement::CompilationOutput out = compiler.CompileFile(stage, path);
    if (!out.success) {
        throw std::runtime_error("[SkyProjectionNode] Failed to compile " + shaderName + ": " + out.GetFullLog());
    }

    VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    ci.codeSize = out.spirv.size() * sizeof(uint32_t);
    ci.pCode = out.spirv.data();
    VkShaderModule module = VK_NULL_HANDLE;
    Check(vkCreateShaderModule(device, &ci, nullptr, &module), "shader module create");
    return module;
}

// Push-constant layout mirroring shaders/SkyProjection.vert's PushConstants block exactly:
// cameraDir(vec3)+fov(float) [16B], cameraUp(vec3)+aspect(float) [16B],
// cameraRight(vec3)+pad(float) [16B] = 48 bytes.
struct PushConstantLayout {
    float cameraDir[3]; float fov;
    float cameraUp[3]; float aspect;
    float cameraRight[3]; float _pad;
};
static_assert(sizeof(PushConstantLayout) == 48, "must match SkyProjection.vert's PushConstants block");

} // namespace

// ====== SkyProjectionNodeType ======

std::unique_ptr<NodeInstance> SkyProjectionNodeType::CreateInstance(const std::string& instanceName) const {
    return std::make_unique<SkyProjectionNode>(instanceName, const_cast<SkyProjectionNodeType*>(this));
}

// ====== SkyProjectionNode ======

SkyProjectionNode::SkyProjectionNode(const std::string& instanceName, NodeType* nodeType)
    : TypedNode<SkyProjectionNodeConfig>(instanceName, nodeType) {}

void SkyProjectionNode::SetupImpl(TypedSetupContext& /*ctx*/) {
    NODE_LOG_DEBUG("[SkyProjectionNode] Setup (graph-scope initialization)");
}

// ============================================================================
// DATA role — synthetic fixture -> SSBO (Task 5)
// ============================================================================

void SkyProjectionNode::BuildSyntheticFixture() {
    using namespace Vixen::SVO;

    // SYNTHETIC TEST FIXTURE — NOT THE PRODUCTION DATA PATH (see SkyProjectionNode.h header).
    // One observer address at a T0-planet-tier leaf, and 3 candidate objects diverging from it
    // at varying tiers/hop-offsets, chosen to land at three distinct screen positions against
    // the default camera preset (BuildRenderGraph.cpp PRESET 1: camera at (64,64,300), looking
    // toward -Z, yaw=0/pitch=0, FOV=45 deg) so a live run can eyeball "roughly centered",
    // "off to one side", and "borderline out of FOV" (hand-computed expected values recorded in
    // the M3 Progress Log before this milestone's live gate was run):
    //   - "centered":     divergent tail composes to (0,0,-t0Leaf) -> direction (0,0,-1), the
    //                     exact -Z direction the default camera (cameraDir=(0,0,-1)) looks down
    //                     -> expected NDC (0,0), dead center.
    //   - "off-to-one-side": local offset (+0.6,0,-1) -> direction (0.5145,0,-0.8575) ->
    //     expected NDC (~0.815, 0), visibly off-center but still in-frame.
    //   - "borderline-out-of-fov": local offset (+1.3,0,-1) -> direction (0.7926,0,-0.6097) ->
    //     expected NDC (~1.765, 0), outside [-1,1] -> off-screen.
    const std::array<TierScaleRange, static_cast<std::size_t>(TierIndex::Count)> tiers = BuildTierScaleTable();
    const double t0Leaf = tiers[static_cast<std::size_t>(TierIndex::T0Planet)].leafCm;

    TierAddress observer{2, 5, 0};  // T0-planet-tier leaf; hops are arbitrary (only used for SharedPrefixLength)
    TierHopFrame observerHop{glm::vec3(1.5f, 1.5f, 1.5f), t0Leaf};  // observer sits at its frame's own origin

    struct Candidate {
        const char* label;
        TierAddress address;
        TierHopFrame hop;         // single divergent hop (siblings of the observer's last hop)
        double intrinsicBrightness;
    };
    const std::vector<Candidate> candidates = {
        {"centered",               TierAddress{2, 5, 1}, TierHopFrame{glm::vec3(1.5f, 1.5f, 0.5f), t0Leaf}, 1.0},
        {"off-to-one-side",        TierAddress{2, 5, 2}, TierHopFrame{glm::vec3(2.1f, 1.5f, 0.5f), t0Leaf}, 0.6},
        {"borderline-out-of-fov",  TierAddress{2, 5, 3}, TierHopFrame{glm::vec3(2.8f, 1.5f, 0.5f), t0Leaf}, 0.9},
    };

    skyPoints_.clear();
    skyPoints_.reserve(candidates.size());
    for (const Candidate& c : candidates) {
        std::array<TierHopFrame, 1> objectTail{c.hop};
        std::array<TierHopFrame, 1> observerTail{observerHop};
        ComposedDirection composed = ComposeLocalDirection(
            observer, std::span<const TierHopFrame>(observerTail),
            c.address, std::span<const TierHopFrame>(objectTail));

        StaleApparentMagnitude mag = ApparentMagnitudeWithStaleness(
            composed.distanceCm, c.intrinsicBrightness, LightDelayStaleness{});

        SkyPointGpu gpuPoint{};
        gpuPoint.direction[0] = composed.direction.x;
        gpuPoint.direction[1] = composed.direction.y;
        gpuPoint.direction[2] = composed.direction.z;
        gpuPoint.magnitude = static_cast<float>(mag.magnitude);
        gpuPoint.appliedDelaySeconds = static_cast<float>(mag.appliedDelaySeconds);
        skyPoints_.push_back(gpuPoint);

        if (!loggedFixture_) {
            NODE_LOG_INFO("[SkyProjectionNode] fixture '" + std::string(c.label) + "': direction=(" +
                          std::to_string(composed.direction.x) + ", " + std::to_string(composed.direction.y) +
                          ", " + std::to_string(composed.direction.z) + ") distanceCm=" +
                          std::to_string(composed.distanceCm) + " magnitude=" + std::to_string(mag.magnitude) +
                          " valid=" + (composed.valid ? "true" : "false"));
        }
    }
    loggedFixture_ = true;

    pointCount_ = static_cast<uint32_t>(skyPoints_.size());
}

void SkyProjectionNode::CreateBuffer(Vixen::Vulkan::Resources::VulkanDevice* device) {
    VkDevice vkDevice = device->device;
    VkPhysicalDevice physDev = *device->gpu;

    const VkDeviceSize bufferSize = static_cast<VkDeviceSize>(skyPoints_.size()) * sizeof(SkyPointGpu);

    VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bufferInfo.size = bufferSize;
    bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    Check(vkCreateBuffer(vkDevice, &bufferInfo, nullptr, &buffer_), "vkCreateBuffer (sky points SSBO)");

    VkMemoryRequirements req{};
    vkGetBufferMemoryRequirements(vkDevice, buffer_, &req);

    VkPhysicalDeviceMemoryProperties memProps{};
    vkGetPhysicalDeviceMemoryProperties(physDev, &memProps);

    VkMemoryAllocateInfo allocInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocInfo.allocationSize = req.size;
    allocInfo.memoryTypeIndex = FindMemoryType(
        memProps, req.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        "SkyProjectionNode sky points SSBO");

    if (vkAllocateMemory(vkDevice, &allocInfo, nullptr, &memory_) != VK_SUCCESS) {
        vkDestroyBuffer(vkDevice, buffer_, nullptr);
        buffer_ = VK_NULL_HANDLE;
        throw std::runtime_error("[SkyProjectionNode] vkAllocateMemory failed");
    }
    if (vkBindBufferMemory(vkDevice, buffer_, memory_, 0) != VK_SUCCESS) {
        vkFreeMemory(vkDevice, memory_, nullptr);
        vkDestroyBuffer(vkDevice, buffer_, nullptr);
        memory_ = VK_NULL_HANDLE;
        buffer_ = VK_NULL_HANDLE;
        throw std::runtime_error("[SkyProjectionNode] vkBindBufferMemory failed");
    }

    void* mapped = nullptr;
    Check(vkMapMemory(vkDevice, memory_, 0, bufferSize, 0, &mapped), "vkMapMemory (sky points SSBO)");
    std::memcpy(mapped, skyPoints_.data(), static_cast<size_t>(bufferSize));
    vkUnmapMemory(vkDevice, memory_);

    NODE_LOG_INFO("[SkyProjectionNode] Created sky points SSBO: " + std::to_string(skyPoints_.size()) +
                  " points (" + std::to_string(static_cast<uint64_t>(bufferSize)) + " bytes)");
}

void SkyProjectionNode::DestroyBuffer() {
    if (!GetDevice()) return;
    VkDevice vkDevice = GetDevice()->device;
    if (buffer_ != VK_NULL_HANDLE) { vkDestroyBuffer(vkDevice, buffer_, nullptr); buffer_ = VK_NULL_HANDLE; }
    if (memory_ != VK_NULL_HANDLE) { vkFreeMemory(vkDevice, memory_, nullptr); memory_ = VK_NULL_HANDLE; }
}

// ============================================================================
// DRAW role — pipeline + per-frame submit (Tasks 6-7)
// ============================================================================

void SkyProjectionNode::CreatePipeline(Vixen::Vulkan::Resources::VulkanDevice* device, VkRenderPass renderPass) {
    device_ = device->device;
    queue_ = device->queue;
    fpQueueSubmit2_ = device->fpQueueSubmit2;
    renderPass_ = renderPass;

    // Descriptor set layout: one SSBO at binding 0, vertex stage (matches SkyProjection.vert).
    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    VkDescriptorSetLayoutCreateInfo dlci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    dlci.bindingCount = 1;
    dlci.pBindings = &binding;
    Check(vkCreateDescriptorSetLayout(device_, &dlci, nullptr, &descriptorLayout_), "descriptor set layout");

    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSize.descriptorCount = 1;
    VkDescriptorPoolCreateInfo pci{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pci.maxSets = 1;
    pci.poolSizeCount = 1;
    pci.pPoolSizes = &poolSize;
    Check(vkCreateDescriptorPool(device_, &pci, nullptr, &descriptorPool_), "descriptor pool");

    VkDescriptorSetAllocateInfo dsai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    dsai.descriptorPool = descriptorPool_;
    dsai.descriptorSetCount = 1;
    dsai.pSetLayouts = &descriptorLayout_;
    Check(vkAllocateDescriptorSets(device_, &dsai, &descriptorSet_), "alloc descriptor set");

    VkDescriptorBufferInfo dbi{};
    dbi.buffer = buffer_;
    dbi.offset = 0;
    dbi.range = VK_WHOLE_SIZE;
    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = descriptorSet_;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    write.pBufferInfo = &dbi;
    vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);

    VkPushConstantRange pcRange{};
    pcRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pcRange.offset = 0;
    pcRange.size = sizeof(PushConstantLayout);
    VkPipelineLayoutCreateInfo plci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    plci.setLayoutCount = 1;
    plci.pSetLayouts = &descriptorLayout_;
    plci.pushConstantRangeCount = 1;
    plci.pPushConstantRanges = &pcRange;
    Check(vkCreatePipelineLayout(device_, &plci, nullptr, &pipelineLayout_), "pipeline layout");

    VkShaderModule vert = CompileAndCreateModule(device_, ShaderManagement::ShaderStage::Vertex, "SkyProjection.vert");
    VkShaderModule frag = CompileAndCreateModule(device_, ShaderManagement::ShaderStage::Fragment, "SkyProjection.frag");

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vert;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = frag;
    stages[1].pName = "main";

    // No vertex input attributes — gl_VertexIndex reads the SSBO directly (mirrors
    // GraphicsPipelineNode's ENABLE_VERTEX_INPUT=false empty-vertex-input path).
    VkPipelineVertexInputStateCreateInfo vin{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};

    VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ia.topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST;

    VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vp.viewportCount = 1;
    vp.scissorCount = 1;  // dynamic — set per frame

    VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // Straight (non-premultiplied) alpha blend: soft circular falloff (SkyProjection.frag)
    // composites over the existing voxel/UI content underneath this Load-op pass.
    VkPipelineColorBlendAttachmentState blend{};
    blend.blendEnable = VK_TRUE;
    blend.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    blend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blend.colorBlendOp = VK_BLEND_OP_ADD;
    blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blend.alphaBlendOp = VK_BLEND_OP_ADD;
    blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                           VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
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
    gp.renderPass = renderPass_;  // consumed, not owned — color-only Load-op pass, no depth
    gp.subpass = 0;
    Check(vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &gp, nullptr, &pipeline_), "graphics pipeline");

    vkDestroyShaderModule(device_, vert, nullptr);
    vkDestroyShaderModule(device_, frag, nullptr);
}

void SkyProjectionNode::DestroyPipeline() {
    if (device_ == VK_NULL_HANDLE) return;
    if (pipeline_) { vkDestroyPipeline(device_, pipeline_, nullptr); pipeline_ = VK_NULL_HANDLE; }
    if (pipelineLayout_) { vkDestroyPipelineLayout(device_, pipelineLayout_, nullptr); pipelineLayout_ = VK_NULL_HANDLE; }
    if (descriptorPool_) { vkDestroyDescriptorPool(device_, descriptorPool_, nullptr); descriptorPool_ = VK_NULL_HANDLE; }
    if (descriptorLayout_) { vkDestroyDescriptorSetLayout(device_, descriptorLayout_, nullptr); descriptorLayout_ = VK_NULL_HANDLE; }
    descriptorSet_ = VK_NULL_HANDLE;  // freed implicitly with the pool
}

void SkyProjectionNode::CompileImpl(TypedCompileContext& ctx) {
    Vixen::Vulkan::Resources::VulkanDevice* device = ctx.In(SkyProjectionNodeConfig::VULKAN_DEVICE_IN);
    if (!device) throw std::runtime_error("[SkyProjectionNode] VULKAN_DEVICE_IN is null");
    SetDevice(device);

    commandPool_ = ctx.In(SkyProjectionNodeConfig::COMMAND_POOL);
    if (commandPool_ == VK_NULL_HANDLE) throw std::runtime_error("[SkyProjectionNode] COMMAND_POOL is null");

    VkRenderPass renderPass = ctx.In(SkyProjectionNodeConfig::RENDER_PASS);
    if (renderPass == VK_NULL_HANDLE) throw std::runtime_error("[SkyProjectionNode] RENDER_PASS is null");

    // FR-7-style persistence: build the fixture + buffer once; both survive recompile.
    if (buffer_ == VK_NULL_HANDLE) {
        BuildSyntheticFixture();
        CreateBuffer(device);
    } else {
        NODE_LOG_INFO("[SkyProjectionNode] Reusing persistent sky points SSBO across recompile");
    }

    ctx.Out(SkyProjectionNodeConfig::SKY_POINTS_BUFFER, buffer_);
    ctx.Out(SkyProjectionNodeConfig::SKY_POINT_COUNT, pointCount_);

    // Pipeline creation needs the render pass; a byte-identical color-format render pass
    // (e.g. after a resize rebuild upstream) keeps this pipeline valid without rebuilding it —
    // the same reasoning UIRenderNode's own pipeline construction relies on.
    if (pipeline_ == VK_NULL_HANDLE) {
        CreatePipeline(device, renderPass);
    } else {
        renderPass_ = renderPass;
    }

    Vixen::Vulkan::Resources::IRenderTarget* swapchainInfo = ctx.In(SkyProjectionNodeConfig::SWAPCHAIN_INFO);
    if (!swapchainInfo) throw std::runtime_error("[SkyProjectionNode] SWAPCHAIN_INFO is null");
    const uint32_t imageCount = swapchainInfo->GetImageCount();

    const bool rebuildSync = (imageCount != syncImageCount_) || commandBuffers_.empty();
    if (rebuildSync && imageCount > 0) {
        if (!commandBuffers_.empty() && commandPool_ != VK_NULL_HANDLE) {
            vkFreeCommandBuffers(device_, commandPool_, static_cast<uint32_t>(commandBuffers_.size()),
                                 commandBuffers_.data());
        }
        commandBuffers_.resize(imageCount);
        VkCommandBufferAllocateInfo cbai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        cbai.commandPool = commandPool_;
        cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cbai.commandBufferCount = imageCount;
        Check(vkAllocateCommandBuffers(device_, &cbai, commandBuffers_.data()), "alloc command buffers");
        syncImageCount_ = imageCount;
    }

    NODE_LOG_INFO("[SkyProjectionNode] Compile complete: " + std::to_string(pointCount_) +
                  " sky points, " + std::to_string(imageCount) + " framebuffers");
}

void SkyProjectionNode::RecordFrame(VkCommandBuffer cmd, VkFramebuffer framebuffer, VkExtent2D extent,
                                    const CameraData& camera, uint32_t /*frameIndex*/) {
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    vkBeginCommandBuffer(cmd, &bi);

    // Load-op pass: clearValueCount=0 — this pass preserves whatever the voxel compute already
    // wrote (PARAM_COLOR_LOAD_OP=Load on the RenderPassNode this pipeline is built against), so
    // there is no clear value to provide at all (distinct from GeometryRenderNode's always-2
    // clear values, which assumes a fresh Clear-op pass — wrong shape for a composite layer).
    VkRenderPassBeginInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    rp.renderPass = renderPass_;
    rp.framebuffer = framebuffer;
    rp.renderArea.offset = {0, 0};
    rp.renderArea.extent = extent;
    rp.clearValueCount = 0;
    rp.pClearValues = nullptr;
    vkCmdBeginRenderPass(cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);

    VkViewport viewport{0.0f, 0.0f, static_cast<float>(extent.width), static_cast<float>(extent.height), 0.0f, 1.0f};
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    VkRect2D scissor{{0, 0}, extent};
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout_, 0, 1, &descriptorSet_, 0, nullptr);

    PushConstantLayout push{};
    push.cameraDir[0] = camera.cameraDir.x; push.cameraDir[1] = camera.cameraDir.y; push.cameraDir[2] = camera.cameraDir.z;
    push.fov = camera.fov;
    push.cameraUp[0] = camera.cameraUp.x; push.cameraUp[1] = camera.cameraUp.y; push.cameraUp[2] = camera.cameraUp.z;
    push.aspect = camera.aspect;
    push.cameraRight[0] = camera.cameraRight.x; push.cameraRight[1] = camera.cameraRight.y; push.cameraRight[2] = camera.cameraRight.z;
    push._pad = 0.0f;
    vkCmdPushConstants(cmd, pipelineLayout_, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push), &push);

    if (pointCount_ > 0) {
        vkCmdDraw(cmd, pointCount_, 1, 0, 0);
    }

    vkCmdEndRenderPass(cmd);
    vkEndCommandBuffer(cmd);
}

void SkyProjectionNode::ExecuteImpl(TypedExecuteContext& ctx) {
    const uint32_t imageIndex = ctx.In(SkyProjectionNodeConfig::IMAGE_INDEX);
    const uint32_t currentFrameIndex = ctx.In(SkyProjectionNodeConfig::CURRENT_FRAME_INDEX);
    const std::vector<VkFramebuffer>& framebuffers = ctx.In(SkyProjectionNodeConfig::FRAMEBUFFERS);
    Vixen::Vulkan::Resources::IRenderTarget* swapchainInfo = ctx.In(SkyProjectionNodeConfig::SWAPCHAIN_INFO);
    const CameraData& camera = ctx.In(SkyProjectionNodeConfig::CAMERA_DATA);

    if (imageIndex == UINT32_MAX || imageIndex >= commandBuffers_.size() ||
        imageIndex >= framebuffers.size() || !swapchainInfo) {
        return;
    }

    VkCommandBuffer cmd = commandBuffers_[imageIndex];
    RecordFrame(cmd, framebuffers[imageIndex], swapchainInfo->GetExtent(), camera, currentFrameIndex);

    VkCommandBufferSubmitInfo cmdInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
    cmdInfo.commandBuffer = cmd;

    std::vector<VkSemaphoreSubmitInfo> waits, signals;

    // Standalone-only path: if IMAGE_AVAILABLE_SEMAPHORES_ARRAY is actually connected (this node
    // is the first submit, no upstream compute producer wired), wait the binary WSI acquire. In
    // the live composite pipeline this input is left unconnected — ordering vs. the upstream
    // compute is carried solely by the timeline waitEdge below (mirrors UIRenderNode's P5b M3
    // convention exactly: composite mode drops the binary handoff wait entirely).
    const std::vector<VkSemaphore>& imageAvailable = ctx.In(SkyProjectionNodeConfig::IMAGE_AVAILABLE_SEMAPHORES_ARRAY);
    if (!imageAvailable.empty() && currentFrameIndex < imageAvailable.size()) {
        VkSemaphoreSubmitInfo binaryWait{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
        binaryWait.semaphore = imageAvailable[currentFrameIndex];
        binaryWait.value = 0;
        binaryWait.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        waits.push_back(binaryWait);
    }

    VkSemaphore timelineSem = ctx.In(SkyProjectionNodeConfig::TIMELINE_SEMAPHORE_IN);
    uint64_t frameBase = ctx.In(SkyProjectionNodeConfig::TIMELINE_FRAME_BASE_IN);
    if (timelineSem != VK_NULL_HANDLE) {
        const FrameSyncSchedule& sched = GetOwningGraph()->GetFrameSyncSchedule();
        if (const SubmitGroup* grp = FindGroupForNode(sched, this)) {
            // Timeline WAITS (this node is a consumer of the upstream compute's write).
            for (uint32_t idx : grp->waitEdges) {
                VkSemaphoreSubmitInfo twait{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
                twait.semaphore = timelineSem;
                twait.value = sched.edges[idx].timelineOffset + frameBase;
                twait.stageMask = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT |
                                  VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
                waits.push_back(twait);
            }

            // Timeline SIGNALS (this node is ALSO a producer the downstream UI composite pass
            // waits on — unlike UIRenderNode, which is last and has no timeline consumer, this
            // node sits in the MIDDLE of the chain and must signal its own completion value, or
            // the scheduler's baked waitEdge on the UI side never resolves (a live-gate-caught
            // bug: without this, vkQueuePresentKHR hangs/errors waiting a timeline value nothing
            // ever signals — VUID-vkQueuePresentKHR-pWaitSemaphores-03268). Mirrors
            // ComputeDispatchNode's own signalEdges loop exactly, including the dedup (all of a
            // producer's signalEdges share one timelineOffset == this node's own groupId).
            std::set<uint64_t> distinctSignalValues;
            for (uint32_t idx : grp->signalEdges) {
                distinctSignalValues.insert(sched.edges[idx].timelineOffset + frameBase);
            }
            for (uint64_t value : distinctSignalValues) {
                VkSemaphoreSubmitInfo tsig{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
                tsig.semaphore = timelineSem;
                tsig.value = value;
                tsig.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
                signals.push_back(tsig);
            }
        }
    }

    // No binary "composite complete" signal: this node's only downstream consumer
    // (UIRenderNode's COMPOSITE_WAIT_SEMAPHORE input) never actually waits it (a vestigial slot,
    // per UIRenderNode's own P5b M3 convention — see this node's header doc comment for the
    // live-gate-caught VUID-vkQueueSubmit2-semaphore-03868 this fixes: an owned-but-never-waited
    // binary semaphore signalled every frame double-signals). Ordering is carried entirely by
    // the timeline signals/waits above.

    VkSubmitInfo2 si{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
    si.waitSemaphoreInfoCount = static_cast<uint32_t>(waits.size());
    si.pWaitSemaphoreInfos = waits.data();
    si.commandBufferInfoCount = 1;
    si.pCommandBufferInfos = &cmdInfo;
    si.signalSemaphoreInfoCount = static_cast<uint32_t>(signals.size());
    si.pSignalSemaphoreInfos = signals.data();

    // This pass does NOT own the frame fence (unlike UIRenderNode, which is last in the chain
    // and resets+signals it) — it submits with VK_NULL_HANDLE, mirroring ComputeDispatchNode's
    // `leaveImageInGeneral ? VK_NULL_HANDLE : inFlightFence` composite-mode convention exactly
    // (this node reads IN_FLIGHT_FENCE per its config's doc comment, but never as its own submit
    // fence — the downstream UI composite pass owns it).
    fpQueueSubmit2_(queue_, 1, &si, VK_NULL_HANDLE);

    // Topology-only passthrough (see header doc comment): nothing ever waits this value at
    // runtime (mirrors UIRenderNode's own COMPOSITE_WAIT_SEMAPHORE input being permanently
    // unread) — VK_NULL_HANDLE is correct and honest, not a placeholder standing in for a real
    // semaphore.
    ctx.Out(SkyProjectionNodeConfig::RENDER_COMPLETE_SEMAPHORE, static_cast<VkSemaphore>(VK_NULL_HANDLE));
}

void SkyProjectionNode::CleanupImpl(TypedCleanupContext& ctx) {
    if (ctx.reason == CleanupReason::Recompile) {
        NODE_LOG_INFO("[SkyProjectionNode] Cleanup (recompile) - keeping persistent resources");
        return;
    }

    NODE_LOG_INFO("[SkyProjectionNode] Cleanup (final teardown) - destroying resources");
    if (device_ != VK_NULL_HANDLE) {
        if (!commandBuffers_.empty() && commandPool_ != VK_NULL_HANDLE) {
            vkFreeCommandBuffers(device_, commandPool_, static_cast<uint32_t>(commandBuffers_.size()),
                                 commandBuffers_.data());
        }
        commandBuffers_.clear();
        syncImageCount_ = 0;
    }
    DestroyPipeline();
    DestroyBuffer();
    device_ = VK_NULL_HANDLE;
}

} // namespace Vixen::RenderGraph

// Self-registration: registrar kept in this TU; RenderGraphNodes is whole-archived so it is not stripped.
VIXEN_REGISTER_NODE(Vixen::RenderGraph::SkyProjectionNodeType);
