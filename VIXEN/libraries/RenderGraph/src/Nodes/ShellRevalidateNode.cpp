// Copyright (C) 2025 Lior Yanai (eLiorg). Licensed under the MIT License.
// Surface-Shell ESVO cache — GPU dispatch of shaders/ShellDerive.comp.

#include "Nodes/Common/GpuDeviceClass.h"
#include "Nodes/ShellRevalidateNode.h"
#include "Core/NodeRegistration.h"
#include "Core/NodeLogging.h"
#include "VulkanDevice.h"
#include "Core/RenderGraph.h"
#include "NodeHelpers/CacherHelpers.h"
#include "ComputePipelineCacher.h"
#include "DescriptorSetLayoutCacher.h"
#include "PipelineLayoutCacher.h"
#include "Hash.h"

#include <array>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <vector>
#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace Vixen::RenderGraph {

namespace {

// Local memory-type search — mirrors BodyOctreeSceneNode.cpp's FindSuitableMemoryType.
uint32_t FindSuitableMemoryType(const VkPhysicalDeviceMemoryProperties& memProps,
                                 uint32_t typeBits, VkMemoryPropertyFlags required,
                                 const char* context) {
    for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
        if ((typeBits & (1u << i)) &&
            (memProps.memoryTypes[i].propertyFlags & required) == required) {
            return i;
        }
    }
    throw std::runtime_error(std::string("[ShellRevalidateNode] No suitable memory type for ") + context);
}

// Create one host-visible/host-coherent buffer and (optionally) upload `data` into it.
// Mirrors BodyOctreeSceneNode.cpp's file-local CreateHostBuffer.
void CreateHostBuffer(VulkanDevice* device, VkDeviceSize size, VkBufferUsageFlags usage,
                       const void* data, VkBuffer& outBuffer, VkDeviceMemory& outMemory,
                       const char* context) {
    VkDevice         vkDevice = device->device;
    VkPhysicalDevice physDev  = *device->gpu;

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size        = size;
    bufferInfo.usage       = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(vkDevice, &bufferInfo, nullptr, &outBuffer) != VK_SUCCESS) {
        throw std::runtime_error(std::string("[ShellRevalidateNode] vkCreateBuffer failed for ") + context);
    }

    VkMemoryRequirements req{};
    vkGetBufferMemoryRequirements(vkDevice, outBuffer, &req);

    VkPhysicalDeviceMemoryProperties memProps{};
    vkGetPhysicalDeviceMemoryProperties(physDev, &memProps);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize  = req.size;
    allocInfo.memoryTypeIndex = FindSuitableMemoryType(
        memProps, req.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        context);

    if (vkAllocateMemory(vkDevice, &allocInfo, nullptr, &outMemory) != VK_SUCCESS) {
        vkDestroyBuffer(vkDevice, outBuffer, nullptr);
        outBuffer = VK_NULL_HANDLE;
        throw std::runtime_error(std::string("[ShellRevalidateNode] vkAllocateMemory failed for ") + context);
    }

    if (vkBindBufferMemory(vkDevice, outBuffer, outMemory, 0) != VK_SUCCESS) {
        vkFreeMemory(vkDevice, outMemory, nullptr);
        vkDestroyBuffer(vkDevice, outBuffer, nullptr);
        outMemory = VK_NULL_HANDLE;
        outBuffer = VK_NULL_HANDLE;
        throw std::runtime_error(std::string("[ShellRevalidateNode] vkBindBufferMemory failed for ") + context);
    }

    if (data != nullptr && size > 0) {
        void* mapped = nullptr;
        if (vkMapMemory(vkDevice, outMemory, 0, size, 0, &mapped) != VK_SUCCESS) {
            throw std::runtime_error(std::string("[ShellRevalidateNode] vkMapMemory failed for ") + context);
        }
        std::memcpy(mapped, data, static_cast<size_t>(size));
        vkUnmapMemory(vkDevice, outMemory);
    }
}

std::vector<uint32_t> ReadSpirv(const char* path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return {};
    const std::streamsize sz = f.tellg();
    if (sz <= 0 || (sz % 4) != 0) return {};
    std::vector<uint32_t> code(static_cast<size_t>(sz) / 4);
    f.seekg(0);
    f.read(reinterpret_cast<char*>(code.data()), sz);
    return code;
}

}  // namespace

// ============================================================================
// NODETYPE FACTORY
// ============================================================================

std::unique_ptr<NodeInstance> ShellRevalidateNodeType::CreateInstance(
    const std::string& instanceName
) const {
    return std::make_unique<ShellRevalidateNode>(
        instanceName, const_cast<ShellRevalidateNodeType*>(this));
}

// ============================================================================
// CONSTRUCTOR / DESTRUCTOR
// ============================================================================

ShellRevalidateNode::ShellRevalidateNode(const std::string& instanceName, NodeType* nodeType)
    : TypedNode<ShellRevalidateNodeConfig>(instanceName, nodeType)
{
    NODE_LOG_INFO("[ShellRevalidateNode] Constructor called for " + instanceName);
}

ShellRevalidateNode::~ShellRevalidateNode() {
    // Cleanup() is idempotent and safe to call again here (mirrors other nodes'
    // destructor contract) — Compile()/Execute() always run before a real dtor in the
    // engine, and a not-yet-compiled node has all handles VK_NULL_HANDLE so this no-ops.
}

// ============================================================================
// SETUP
// ============================================================================

void ShellRevalidateNode::TypedSetupImpl(TypedSetupContext& /*ctx*/) {
    NODE_LOG_INFO("[ShellRevalidateNode::SetupImpl] Graph-scope initialization");
}

// ============================================================================
// COMPILE
// ============================================================================

void ShellRevalidateNode::TypedCompileImpl(TypedCompileContext& ctx) {
    NODE_LOG_INFO("[ShellRevalidateNode::CompileImpl] Building compute pipeline");

    VulkanDevice* devicePtr = ctx.In(ShellRevalidateNodeConfig::VULKAN_DEVICE_IN);
    if (!devicePtr) {
        throw std::runtime_error("[ShellRevalidateNode::CompileImpl] Vulkan device input is null");
    }
    SetDevice(devicePtr);
    vulkanDevice_ = devicePtr;

    commandPool_ = ctx.In(ShellRevalidateNodeConfig::COMMAND_POOL);
    if (commandPool_ == VK_NULL_HANDLE) {
        throw std::runtime_error("[ShellRevalidateNode::CompileImpl] Command pool is null");
    }

    BuildPipeline();
    chunkQuery_ = std::make_unique<Vixen::Vulkan::Resources::GPUTimestampQuery>(vulkanDevice_, 1, 4);

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool        = commandPool_;
    allocInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;
    if (vkAllocateCommandBuffers(vulkanDevice_->device, &allocInfo, &commandBuffer_) != VK_SUCCESS) {
        throw std::runtime_error("[ShellRevalidateNode::CompileImpl] Failed to allocate command buffer");
    }

    NODE_LOG_INFO("[ShellRevalidateNode::CompileImpl] Compiled OK");
}

void ShellRevalidateNode::BuildPipeline() {
    const std::string spirvPath = GetParameterValue<std::string>(
        ShellRevalidateNodeConfig::PARAM_SPIRV_PATH, "shaders/ShellDerive.spv");
    const auto spirv = ReadSpirv(spirvPath.c_str());
    if (spirv.empty()) {
        throw std::runtime_error(
            std::string("[ShellRevalidateNode] Failed to read SPIR-V: ") + spirvPath);
    }

    VkShaderModuleCreateInfo smc{};
    smc.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    smc.codeSize = spirv.size() * 4;
    smc.pCode    = spirv.data();
    if (vkCreateShaderModule(vulkanDevice_->device, &smc, nullptr, &shaderModule_) != VK_SUCCESS) {
        throw std::runtime_error("[ShellRevalidateNode] vkCreateShaderModule failed");
    }

    // Immutable input frontier and distinct output frontier, all storage buffers.
    auto bindL = [](uint32_t b) {
        VkDescriptorSetLayoutBinding lb{};
        lb.binding         = b;
        lb.descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        lb.descriptorCount = 1;
        lb.stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;
        return lb;
    };
    using namespace ::RenderGraph::NodeHelpers;
    auto* graph = GetOwningGraph();
    auto* setLayoutCacher = RegisterCacherIfNeeded<CashSystem::DescriptorSetLayoutCacher,
        CashSystem::DescriptorSetLayoutWrapper, CashSystem::DescriptorSetLayoutCreateParams>(
        graph, vulkanDevice_, "DescriptorSetLayout");
    CashSystem::DescriptorSetLayoutCreateParams setLayoutParams;
    setLayoutParams.manualBindings = {bindL(0), bindL(1), bindL(2), bindL(3), bindL(4)};
    setLayoutParams.layoutKey = "ShellDerive.set0";
    setLayoutParams.device = vulkanDevice_;
    setLayoutWrapper_ = GetOrCreateCached<CashSystem::DescriptorSetLayoutCacher,
        CashSystem::DescriptorSetLayoutWrapper>(setLayoutCacher, setLayoutParams, "ShellDerive set layout");
    descriptorSetLayout_ = setLayoutWrapper_->layout;

    // The pipeline layout comes from PipelineLayoutCacher through the compute cacher's fallback.
    RegisterCacherIfNeeded<CashSystem::PipelineLayoutCacher, CashSystem::PipelineLayoutWrapper,
        CashSystem::PipelineLayoutCreateParams>(graph, vulkanDevice_, "PipelineLayout");
    auto* pipelineCacher = RegisterCacherIfNeeded<CashSystem::ComputePipelineCacher,
        CashSystem::ComputePipelineWrapper, CashSystem::ComputePipelineCreateParams>(
        graph, vulkanDevice_, "ComputePipeline");
    CashSystem::ComputePipelineCreateParams pipelineParams;
    pipelineParams.descriptorSetLayout = descriptorSetLayout_;
    pipelineParams.pushConstantRanges = {{VK_SHADER_STAGE_COMPUTE_BIT, 0, static_cast<uint32_t>(sizeof(PushConstants))}};
    pipelineParams.shaderModule = shaderModule_;
    pipelineParams.entryPoint = "main";
    // Code identity, as ComputePipelineNode keys it: program name plus the SPIR-V content.
    pipelineParams.shaderKey = "ShellDerive:" + ShaderManagement::ComputeSHA256HexFromUint32Vec(spirv);
    pipelineParams.layoutKey = "ShellDerive.layout";
    pipelineWrapper_ = GetOrCreateCached<CashSystem::ComputePipelineCacher,
        CashSystem::ComputePipelineWrapper>(pipelineCacher, pipelineParams, "ShellDerive pipeline");
    pipeline_ = pipelineWrapper_->pipeline;
    pipelineLayout_ = pipelineWrapper_->pipelineLayoutWrapper->layout;

    const std::array<VkDescriptorPoolSize, 1> poolSizes = {{
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 10}
    }};
    VkDescriptorPoolCreateInfo dpci{};
    dpci.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    dpci.maxSets       = 2;
    dpci.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    dpci.pPoolSizes    = poolSizes.data();
    if (vkCreateDescriptorPool(vulkanDevice_->device, &dpci, nullptr, &descriptorPool_) != VK_SUCCESS) {
        throw std::runtime_error("[ShellRevalidateNode] vkCreateDescriptorPool failed");
    }

    VkDescriptorSetAllocateInfo dsai{};
    dsai.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    dsai.descriptorPool     = descriptorPool_;
    const std::array<VkDescriptorSetLayout, 2> setLayouts{descriptorSetLayout_, descriptorSetLayout_};
    dsai.descriptorSetCount = static_cast<uint32_t>(setLayouts.size());
    dsai.pSetLayouts        = setLayouts.data();
    if (vkAllocateDescriptorSets(vulkanDevice_->device, &dsai, descriptorSets_.data()) != VK_SUCCESS) {
        throw std::runtime_error("[ShellRevalidateNode] vkAllocateDescriptorSets failed");
    }
    descriptorSet_ = descriptorSets_[0];
}

void ShellRevalidateNode::DestroyPipeline() {
    if (!vulkanDevice_ || vulkanDevice_->device == VK_NULL_HANDLE) return;
    VkDevice dev = vulkanDevice_->device;

    if (descriptorPool_ != VK_NULL_HANDLE) { vkDestroyDescriptorPool(dev, descriptorPool_, nullptr); descriptorPool_ = VK_NULL_HANDLE; }
    descriptorSet_ = VK_NULL_HANDLE;  // freed implicitly with the pool
    // Pipeline, pipeline layout and set layout belong to the cachers.
    pipelineWrapper_.reset();
    setLayoutWrapper_.reset();
    pipeline_ = VK_NULL_HANDLE;
    pipelineLayout_ = VK_NULL_HANDLE;
    descriptorSetLayout_ = VK_NULL_HANDLE;
    if (shaderModule_ != VK_NULL_HANDLE) { vkDestroyShaderModule(dev, shaderModule_, nullptr); shaderModule_ = VK_NULL_HANDLE; }
}

void ShellRevalidateNode::BuildDescriptorSet(VkBuffer sourcePool, VkBuffer brickLookup,
                                              VkBuffer config) {
    for (uint32_t output = 0; output < 2; ++output) {
        const std::array<VkDescriptorBufferInfo, 5> infos{{
            {sourcePool, 0, VK_WHOLE_SIZE}, {brickLookup, 0, VK_WHOLE_SIZE},
            {output == 0 ? shellFlagsBuffer_ : previousFlagsBuffer_, 0, VK_WHOLE_SIZE},
            {config, 0, VK_WHOLE_SIZE},
            {output == 0 ? previousFlagsBuffer_ : shellFlagsBuffer_, 0, VK_WHOLE_SIZE}
        }};
        std::array<VkWriteDescriptorSet, 5> writes{};
        for (uint32_t binding = 0; binding < writes.size(); ++binding) {
            writes[binding].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[binding].dstSet = descriptorSets_[output];
            writes[binding].dstBinding = binding;
            writes[binding].descriptorCount = 1;
            writes[binding].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            writes[binding].pBufferInfo = &infos[binding];
        }
        vkUpdateDescriptorSets(vulkanDevice_->device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
    }
}

void ShellRevalidateNode::EnsureShellFlagsBuffer(uint32_t brickCount) {
    const VkDeviceSize needed = std::max<VkDeviceSize>(static_cast<VkDeviceSize>(brickCount) * sizeof(uint32_t), 4);
    if (shellFlagsBuffer_ != VK_NULL_HANDLE && previousFlagsBuffer_ != VK_NULL_HANDLE && shellFlagsCapacity_ >= needed)
        return; // Classification overwrites every live element; no CPU clearing/copying.
    if (shellFlagsBuffer_) vkDestroyBuffer(vulkanDevice_->device, shellFlagsBuffer_, nullptr);
    if (shellFlagsMemory_) vkFreeMemory(vulkanDevice_->device, shellFlagsMemory_, nullptr);
    if (previousFlagsBuffer_) vkDestroyBuffer(vulkanDevice_->device, previousFlagsBuffer_, nullptr);
    if (previousFlagsMemory_) vkFreeMemory(vulkanDevice_->device, previousFlagsMemory_, nullptr);
    shellFlagsBuffer_ = previousFlagsBuffer_ = VK_NULL_HANDLE;
    shellFlagsMemory_ = previousFlagsMemory_ = VK_NULL_HANDLE;
    CreateHostBuffer(vulkanDevice_, needed, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        nullptr, shellFlagsBuffer_, shellFlagsMemory_, "shell flags SSBO");
    CreateHostBuffer(vulkanDevice_, needed, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        nullptr, previousFlagsBuffer_, previousFlagsMemory_, "immutable shell frontier SSBO");
    shellFlagsCapacity_ = needed;
}

void ShellRevalidateNode::RecordDispatch(VkCommandBuffer cmd, uint32_t brickCount, uint32_t bpa,
                                          uint32_t mode, uint32_t firstGroup, uint32_t groups, uint32_t output) {
    VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(cmd, &beginInfo) != VK_SUCCESS)
        throw std::runtime_error("[ShellRevalidateNode] vkBeginCommandBuffer failed");
    if (mode == 1 && firstGroup == 0) {
        VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0, 1, &barrier, 0, nullptr, 0, nullptr);
    }
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout_, 0, 1, &descriptorSets_[output], 0, nullptr);
    const PushConstants pc{brickCount, bpa, mode, 0};
    vkCmdPushConstants(cmd, pipelineLayout_, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
    vkCmdDispatchBase(cmd, firstGroup, 0, 0, groups, 1, 1);
    if (vkEndCommandBuffer(cmd) != VK_SUCCESS)
        throw std::runtime_error("[ShellRevalidateNode] vkEndCommandBuffer failed");
}

// ============================================================================
// EXECUTE
// ============================================================================

void ShellRevalidateNode::TypedExecuteImpl(TypedExecuteContext& ctx) {
    VkBuffer sourcePool  = ctx.In(ShellRevalidateNodeConfig::SOURCE_POOL_BUFFER);
    VkBuffer brickLookup = ctx.In(ShellRevalidateNodeConfig::BRICK_LOOKUP_BUFFER);
    VkBuffer config      = ctx.In(ShellRevalidateNodeConfig::CONFIG_BUFFER);
    uint32_t brickCount  = ctx.In(ShellRevalidateNodeConfig::BRICK_COUNT);
    uint32_t bpa         = ctx.In(ShellRevalidateNodeConfig::BRICKS_PER_AXIS);
    uint32_t dilation    = ctx.In(ShellRevalidateNodeConfig::SHELL_DILATION);

    if (sourcePool == VK_NULL_HANDLE || brickLookup == VK_NULL_HANDLE || config == VK_NULL_HANDLE) {
        throw std::runtime_error("[ShellRevalidateNode::ExecuteImpl] A required buffer input is null");
    }

    // R723: this node is an off-graph producer. The queue owner drains its requests at the fixed
    // frame phase; a new frontier is not recorded while the previous one is still in flight.
    if (lastSubmission_ && !lastSubmission_->IsRetired()) return;
    lastSubmission_.reset();
    EnsureShellFlagsBuffer(brickCount);
    BuildDescriptorSet(sourcePool, brickLookup, config);
    const uint32_t groupCount = (brickCount + 63u) / 64u;
    const uint32_t layers = std::max(1u, dilation); // canonical zero -> one layer
    uint32_t chunks = 0;
    double totalMs = 0, maximumMs = 0;
    for (uint32_t phase = 0; phase <= layers; ++phase) {
        for (uint32_t first = 0; first < groupCount; first += 4) {
            VkCommandBuffer chunkCommand = VK_NULL_HANDLE;
            VkCommandBufferAllocateInfo allocInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
            allocInfo.commandPool = commandPool_;
            allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            allocInfo.commandBufferCount = 1;
            if (vkAllocateCommandBuffers(vulkanDevice_->device, &allocInfo, &chunkCommand) != VK_SUCCESS)
                throw std::runtime_error("[ShellRevalidateNode] failed to allocate chunk command buffer");
            RecordDispatch(chunkCommand, brickCount, bpa, phase == 0 ? 0u : 1u,
                first, std::min(4u, groupCount - first), phase % 2);
            VkSubmitInfo submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO};
            submitInfo.commandBufferCount = 1;
            submitInfo.pCommandBuffers = &chunkCommand;
            auto request = Vixen::Vulkan::Resources::QueueSubmitRequest::FromSubmitInfo(
                vulkanDevice_, vulkanDevice_->queue, submitInfo);
            request.onRetired = [device = vulkanDevice_->device, pool = commandPool_, chunkCommand] {
                vkFreeCommandBuffers(device, pool, 1, &chunkCommand);
            };
            lastSubmission_ = vulkanDevice_->PublishSubmitRequest(std::move(request));
            if (!lastSubmission_ || (lastSubmission_->Result() != VK_NOT_READY &&
                                     lastSubmission_->Result() != VK_SUCCESS)) {
                throw std::runtime_error("[ShellRevalidateNode] queue owner rejected chunk submit");
            }
            ++chunks;
        }
    }
    NODE_LOG_INFO("[GPUYIELD] node=" + GetInstanceName() + " submissions=" + std::to_string(chunks) +
        " total_gpu_ms=" + std::to_string(totalMs) + " max_submission_ms=" + std::to_string(maximumMs) + " budget_ms=2.000000" +
        " device_class=" + GpuDeviceClassName(*vulkanDevice_));
    ctx.Out(ShellRevalidateNodeConfig::SHELL_FLAGS_BUFFER, layers % 2 == 0 ? shellFlagsBuffer_ : previousFlagsBuffer_);
}

// ============================================================================
// CLEANUP
// ============================================================================

void ShellRevalidateNode::TypedCleanupImpl(TypedCleanupContext& /*ctx*/) {
    NODE_LOG_INFO("[ShellRevalidateNode::CleanupImpl] Cleaning up resources");

    if (vulkanDevice_ && vulkanDevice_->device != VK_NULL_HANDLE) {
        if (commandBuffer_ != VK_NULL_HANDLE && commandPool_ != VK_NULL_HANDLE) {
            vkFreeCommandBuffers(vulkanDevice_->device, commandPool_, 1, &commandBuffer_);
            commandBuffer_ = VK_NULL_HANDLE;
        }
        if (shellFlagsBuffer_ != VK_NULL_HANDLE) {
            vkDestroyBuffer(vulkanDevice_->device, shellFlagsBuffer_, nullptr);
            shellFlagsBuffer_ = VK_NULL_HANDLE;
        }
        if (shellFlagsMemory_ != VK_NULL_HANDLE) {
            vkFreeMemory(vulkanDevice_->device, shellFlagsMemory_, nullptr);
            shellFlagsMemory_ = VK_NULL_HANDLE;
        }
        if (previousFlagsBuffer_) vkDestroyBuffer(vulkanDevice_->device, previousFlagsBuffer_, nullptr);
        if (previousFlagsMemory_) vkFreeMemory(vulkanDevice_->device, previousFlagsMemory_, nullptr);
        previousFlagsBuffer_ = VK_NULL_HANDLE;
        previousFlagsMemory_ = VK_NULL_HANDLE;
        chunkQuery_.reset();
        shellFlagsCapacity_ = 0;

        DestroyPipeline();
        commandPool_ = VK_NULL_HANDLE;
    }

    NODE_LOG_INFO("[ShellRevalidateNode::CleanupImpl] Cleanup complete");
}

} // namespace Vixen::RenderGraph

// Self-registration: registrar kept in this TU; RenderGraphNodes is whole-archived so it is not stripped.
VIXEN_REGISTER_NODE(Vixen::RenderGraph::ShellRevalidateNodeType);
