// Copyright (C) 2025 Lior Yanai (eLiorg). Licensed under the MIT License.
// Surface-Shell ESVO cache — GPU dispatch of shaders/ShellDerive.comp.

#include "Nodes/ShellRevalidateNode.h"
#include "Core/NodeRegistration.h"
#include "Core/NodeLogging.h"
#include "VulkanDevice.h"

#include <array>
#include <cstring>
#include <fstream>
#include <mutex>
#include <stdexcept>
#include <vector>

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

void ShellRevalidateNode::SetupImpl(TypedSetupContext& /*ctx*/) {
    NODE_LOG_INFO("[ShellRevalidateNode::SetupImpl] Graph-scope initialization");
}

// ============================================================================
// COMPILE
// ============================================================================

void ShellRevalidateNode::CompileImpl(TypedCompileContext& ctx) {
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

    // Bindings 0-3, ALL storage buffers (ShellDerive.comp:35,38,41,44).
    auto bindL = [](uint32_t b) {
        VkDescriptorSetLayoutBinding lb{};
        lb.binding         = b;
        lb.descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        lb.descriptorCount = 1;
        lb.stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;
        return lb;
    };
    const std::array<VkDescriptorSetLayoutBinding, 4> bindings = {
        bindL(0), bindL(1), bindL(2), bindL(3)
    };
    VkDescriptorSetLayoutCreateInfo dslci{};
    dslci.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dslci.bindingCount = static_cast<uint32_t>(bindings.size());
    dslci.pBindings    = bindings.data();
    if (vkCreateDescriptorSetLayout(vulkanDevice_->device, &dslci, nullptr, &descriptorSetLayout_) != VK_SUCCESS) {
        throw std::runtime_error("[ShellRevalidateNode] vkCreateDescriptorSetLayout failed");
    }

    VkPushConstantRange pcr{};
    pcr.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pcr.offset     = 0;
    pcr.size       = sizeof(PushConstants);
    VkPipelineLayoutCreateInfo plci{};
    plci.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plci.setLayoutCount         = 1;
    plci.pSetLayouts            = &descriptorSetLayout_;
    plci.pushConstantRangeCount = 1;
    plci.pPushConstantRanges    = &pcr;
    if (vkCreatePipelineLayout(vulkanDevice_->device, &plci, nullptr, &pipelineLayout_) != VK_SUCCESS) {
        throw std::runtime_error("[ShellRevalidateNode] vkCreatePipelineLayout failed");
    }

    VkComputePipelineCreateInfo cpci{};
    cpci.sType        = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    cpci.stage.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    cpci.stage.stage  = VK_SHADER_STAGE_COMPUTE_BIT;
    cpci.stage.module = shaderModule_;
    cpci.stage.pName  = "main";
    cpci.layout       = pipelineLayout_;
    // Hand-rolled creation site, bypasses ComputePipelineCacher -- not covered by VIXEN_PIPELINE_STATS.
    if (vkCreateComputePipelines(vulkanDevice_->device, VK_NULL_HANDLE, 1, &cpci, nullptr, &pipeline_) != VK_SUCCESS) {
        throw std::runtime_error("[ShellRevalidateNode] vkCreateComputePipelines failed");
    }

    const std::array<VkDescriptorPoolSize, 1> poolSizes = {{
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 4}
    }};
    VkDescriptorPoolCreateInfo dpci{};
    dpci.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    dpci.maxSets       = 1;
    dpci.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    dpci.pPoolSizes    = poolSizes.data();
    if (vkCreateDescriptorPool(vulkanDevice_->device, &dpci, nullptr, &descriptorPool_) != VK_SUCCESS) {
        throw std::runtime_error("[ShellRevalidateNode] vkCreateDescriptorPool failed");
    }

    VkDescriptorSetAllocateInfo dsai{};
    dsai.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    dsai.descriptorPool     = descriptorPool_;
    dsai.descriptorSetCount = 1;
    dsai.pSetLayouts        = &descriptorSetLayout_;
    if (vkAllocateDescriptorSets(vulkanDevice_->device, &dsai, &descriptorSet_) != VK_SUCCESS) {
        throw std::runtime_error("[ShellRevalidateNode] vkAllocateDescriptorSets failed");
    }
}

void ShellRevalidateNode::DestroyPipeline() {
    if (!vulkanDevice_ || vulkanDevice_->device == VK_NULL_HANDLE) return;
    VkDevice dev = vulkanDevice_->device;

    if (descriptorPool_ != VK_NULL_HANDLE) { vkDestroyDescriptorPool(dev, descriptorPool_, nullptr); descriptorPool_ = VK_NULL_HANDLE; }
    descriptorSet_ = VK_NULL_HANDLE;  // freed implicitly with the pool
    if (pipeline_ != VK_NULL_HANDLE) { vkDestroyPipeline(dev, pipeline_, nullptr); pipeline_ = VK_NULL_HANDLE; }
    if (pipelineLayout_ != VK_NULL_HANDLE) { vkDestroyPipelineLayout(dev, pipelineLayout_, nullptr); pipelineLayout_ = VK_NULL_HANDLE; }
    if (descriptorSetLayout_ != VK_NULL_HANDLE) { vkDestroyDescriptorSetLayout(dev, descriptorSetLayout_, nullptr); descriptorSetLayout_ = VK_NULL_HANDLE; }
    if (shaderModule_ != VK_NULL_HANDLE) { vkDestroyShaderModule(dev, shaderModule_, nullptr); shaderModule_ = VK_NULL_HANDLE; }
}

void ShellRevalidateNode::BuildDescriptorSet(VkBuffer sourcePool, VkBuffer brickLookup,
                                              VkBuffer shellFlags, VkBuffer config) {
    VkDescriptorBufferInfo sourcePoolInfo{sourcePool, 0, VK_WHOLE_SIZE};
    VkDescriptorBufferInfo brickLookupInfo{brickLookup, 0, VK_WHOLE_SIZE};
    VkDescriptorBufferInfo shellFlagsInfo{shellFlags, 0, VK_WHOLE_SIZE};
    VkDescriptorBufferInfo configInfo{config, 0, VK_WHOLE_SIZE};

    auto wB = [&](uint32_t b, VkDescriptorBufferInfo* info) {
        VkWriteDescriptorSet w{};
        w.sType          = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w.dstSet         = descriptorSet_;
        w.dstBinding     = b;
        w.descriptorCount = 1;
        w.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        w.pBufferInfo    = info;
        return w;
    };
    const std::array<VkWriteDescriptorSet, 4> writes = {
        wB(0, &sourcePoolInfo), wB(1, &brickLookupInfo), wB(2, &shellFlagsInfo), wB(3, &configInfo)
    };
    vkUpdateDescriptorSets(vulkanDevice_->device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
}

void ShellRevalidateNode::EnsureShellFlagsBuffer(uint32_t brickCount) {
    const VkDeviceSize needed =
        std::max<VkDeviceSize>(static_cast<VkDeviceSize>(brickCount) * sizeof(uint32_t), 1);
    if (shellFlagsBuffer_ != VK_NULL_HANDLE && shellFlagsCapacity_ >= needed) {
        // Reuse: zero it out (a fresh derive should not see stale FRONTIER/SHELL bits
        // from a previous dispatch at a different brickCount).
        void* mapped = nullptr;
        if (vkMapMemory(vulkanDevice_->device, shellFlagsMemory_, 0, needed, 0, &mapped) == VK_SUCCESS) {
            std::memset(mapped, 0, static_cast<size_t>(needed));
            vkUnmapMemory(vulkanDevice_->device, shellFlagsMemory_);
        }
        return;
    }
    if (shellFlagsBuffer_ != VK_NULL_HANDLE) { vkDestroyBuffer(vulkanDevice_->device, shellFlagsBuffer_, nullptr); shellFlagsBuffer_ = VK_NULL_HANDLE; }
    if (shellFlagsMemory_ != VK_NULL_HANDLE) { vkFreeMemory(vulkanDevice_->device, shellFlagsMemory_, nullptr); shellFlagsMemory_ = VK_NULL_HANDLE; }
    std::vector<uint8_t> zeros(static_cast<size_t>(needed), 0);
    CreateHostBuffer(vulkanDevice_, needed,
                      VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                      zeros.data(), shellFlagsBuffer_, shellFlagsMemory_, "shell flags SSBO");
    shellFlagsCapacity_ = needed;
}

void ShellRevalidateNode::RecordDispatch(VkCommandBuffer cmd, uint32_t brickCount, uint32_t bpa,
                                          uint32_t dilationLayers) {
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(cmd, &beginInfo) != VK_SUCCESS) {
        throw std::runtime_error("[ShellRevalidateNode] vkBeginCommandBuffer failed");
    }

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout_, 0, 1, &descriptorSet_, 0, nullptr);

    const uint32_t groupCount = (brickCount + 63u) / 64u;  // local_size_x = 64 (ShellDerive.comp:31)

    // Mode 0: surface classify (ShellDerive.comp:75-90).
    PushConstants pc0{brickCount, bpa, 0u, 0u};
    vkCmdPushConstants(cmd, pipelineLayout_, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc0), &pc0);
    vkCmdDispatch(cmd, groupCount, 1, 1);

    // A barrier between passes: mode-1 reads shellFlags[] written by the previous
    // dispatch (mode-0's seed, or the prior dilation layer's atomicOr updates).
    auto shellFlagsBarrier = [&]() {
        VkMemoryBarrier mb{};
        mb.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        mb.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        mb.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                              0, 1, &mb, 0, nullptr, 0, nullptr);
    };

    // Mode 1: `dilationLayers` dilation passes (ShellDerive.comp:92-128).
    for (uint32_t layer = 0; layer < dilationLayers; ++layer) {
        shellFlagsBarrier();
        PushConstants pc1{brickCount, bpa, 1u, 0u};
        vkCmdPushConstants(cmd, pipelineLayout_, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc1), &pc1);
        vkCmdDispatch(cmd, groupCount, 1, 1);
    }

    if (vkEndCommandBuffer(cmd) != VK_SUCCESS) {
        throw std::runtime_error("[ShellRevalidateNode] vkEndCommandBuffer failed");
    }
}

// ============================================================================
// EXECUTE
// ============================================================================

void ShellRevalidateNode::ExecuteImpl(TypedExecuteContext& ctx) {
    VkBuffer sourcePool  = ctx.In(ShellRevalidateNodeConfig::SOURCE_POOL_BUFFER);
    VkBuffer brickLookup = ctx.In(ShellRevalidateNodeConfig::BRICK_LOOKUP_BUFFER);
    VkBuffer config      = ctx.In(ShellRevalidateNodeConfig::CONFIG_BUFFER);
    uint32_t brickCount  = ctx.In(ShellRevalidateNodeConfig::BRICK_COUNT);
    uint32_t bpa         = ctx.In(ShellRevalidateNodeConfig::BRICKS_PER_AXIS);
    uint32_t dilation    = ctx.In(ShellRevalidateNodeConfig::SHELL_DILATION);

    if (sourcePool == VK_NULL_HANDLE || brickLookup == VK_NULL_HANDLE || config == VK_NULL_HANDLE) {
        throw std::runtime_error("[ShellRevalidateNode::ExecuteImpl] A required buffer input is null");
    }

    EnsureShellFlagsBuffer(brickCount);
    BuildDescriptorSet(sourcePool, brickLookup, shellFlagsBuffer_, config);
    RecordDispatch(commandBuffer_, brickCount, bpa, dilation);

    VkSubmitInfo submitInfo{};
    submitInfo.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers    = &commandBuffer_;
    {
        // Externally synchronized per Vulkan spec (audit V-M11).
        std::lock_guard<std::mutex> submitLock(vulkanDevice_->SubmitMutex(vulkanDevice_->queue));
        if (vkQueueSubmit(vulkanDevice_->queue, 1, &submitInfo, VK_NULL_HANDLE) != VK_SUCCESS) {
            throw std::runtime_error("[ShellRevalidateNode::ExecuteImpl] vkQueueSubmit failed");
        }
        vkQueueWaitIdle(vulkanDevice_->queue);
    }

    ctx.Out(ShellRevalidateNodeConfig::SHELL_FLAGS_BUFFER, shellFlagsBuffer_);
}

// ============================================================================
// CLEANUP
// ============================================================================

void ShellRevalidateNode::CleanupImpl(TypedCleanupContext& /*ctx*/) {
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
        shellFlagsCapacity_ = 0;

        DestroyPipeline();
        commandPool_ = VK_NULL_HANDLE;
    }

    NODE_LOG_INFO("[ShellRevalidateNode::CleanupImpl] Cleanup complete");
}

} // namespace Vixen::RenderGraph

// Self-registration: registrar kept in this TU; RenderGraphNodes is whole-archived so it is not stripped.
VIXEN_REGISTER_NODE(Vixen::RenderGraph::ShellRevalidateNodeType);
