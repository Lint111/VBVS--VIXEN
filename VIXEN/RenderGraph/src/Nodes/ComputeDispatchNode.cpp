#include "Nodes/ComputeDispatchNode.h"
#include "Nodes/ComputeDispatchNodeConfig.h"
#include "VulkanResources/VulkanDevice.h"
#include <stdexcept>
#include <iostream>

namespace Vixen::RenderGraph {

// ============================================================================
// NODETYPE FACTORY
// ============================================================================

std::unique_ptr<NodeInstance> ComputeDispatchNodeType::CreateInstance(
    const std::string& instanceName
) const {
    return std::make_unique<ComputeDispatchNode>(instanceName, const_cast<ComputeDispatchNodeType*>(this));
}

// ============================================================================
// CONSTRUCTOR
// ============================================================================

ComputeDispatchNode::ComputeDispatchNode(
    const std::string& instanceName,
    NodeType* nodeType
) : TypedNode<ComputeDispatchNodeConfig>(instanceName, nodeType)
{
    std::cout << "[ComputeDispatchNode] Constructor called for " << instanceName << std::endl;
}

// ============================================================================
// SETUP
// ============================================================================

void ComputeDispatchNode::SetupImpl(Context& ctx) {
    std::cout << "[ComputeDispatchNode::SetupImpl] Setting device" << std::endl;

    VulkanDevicePtr devicePtr = In(ComputeDispatchNodeConfig::VULKAN_DEVICE_IN);
    if (!devicePtr) {
        throw std::runtime_error("[ComputeDispatchNode::SetupImpl] Vulkan device input is null");
    }

    SetDevice(devicePtr);
}

// ============================================================================
// COMPILE
// ============================================================================

void ComputeDispatchNode::CompileImpl(Context& ctx) {
    std::cout << "[ComputeDispatchNode::CompileImpl] Recording compute dispatch command buffer" << std::endl;

    // 1. Get parameters
    uint32_t dispatchX = GetParameterValue<uint32_t>(ComputeDispatchNodeConfig::DISPATCH_X, 1);
    uint32_t dispatchY = GetParameterValue<uint32_t>(ComputeDispatchNodeConfig::DISPATCH_Y, 1);
    uint32_t dispatchZ = GetParameterValue<uint32_t>(ComputeDispatchNodeConfig::DISPATCH_Z, 1);
    uint32_t pushConstantSize = GetParameterValue<uint32_t>(ComputeDispatchNodeConfig::PUSH_CONSTANT_SIZE, 0);
    uint32_t descriptorSetCount = GetParameterValue<uint32_t>(ComputeDispatchNodeConfig::DESCRIPTOR_SET_COUNT, 0);

    // Validate dispatch dimensions
    if (!ComputeDispatchNodeConfig::ValidateDispatchDimensions(dispatchX, dispatchY, dispatchZ)) {
        throw std::runtime_error("[ComputeDispatchNode::CompileImpl] Invalid dispatch dimensions: " +
                                 std::to_string(dispatchX) + "x" + std::to_string(dispatchY) + "x" + std::to_string(dispatchZ));
    }

    std::cout << "[ComputeDispatchNode::CompileImpl] Dispatch dimensions: "
              << dispatchX << "x" << dispatchY << "x" << dispatchZ << std::endl;

    // 2. Get inputs
    VulkanDevicePtr devicePtr = In(ComputeDispatchNodeConfig::VULKAN_DEVICE_IN);
    VkCommandPool commandPool = In(ComputeDispatchNodeConfig::COMMAND_POOL);
    VkPipeline computePipeline = In(ComputeDispatchNodeConfig::COMPUTE_PIPELINE);
    VkPipelineLayout pipelineLayout = In(ComputeDispatchNodeConfig::PIPELINE_LAYOUT);

    // Optional inputs
    DescriptorSetVector descriptorSets = In(ComputeDispatchNodeConfig::DESCRIPTOR_SETS);
    VkBuffer pushConstantsBuffer = In(ComputeDispatchNodeConfig::PUSH_CONSTANTS);

    // 3. Allocate command buffer
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;

    VkResult result = vkAllocateCommandBuffers(devicePtr->device, &allocInfo, &commandBuffer_);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("[ComputeDispatchNode::CompileImpl] Failed to allocate command buffer: " + std::to_string(result));
    }

    std::cout << "[ComputeDispatchNode::CompileImpl] Allocated command buffer: "
              << reinterpret_cast<uint64_t>(commandBuffer_) << std::endl;

    // 4. Begin recording
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = 0;  // Can be resubmitted without re-recording

    result = vkBeginCommandBuffer(commandBuffer_, &beginInfo);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("[ComputeDispatchNode::CompileImpl] Failed to begin command buffer: " + std::to_string(result));
    }

    // 5. Bind compute pipeline
    vkCmdBindPipeline(commandBuffer_, VK_PIPELINE_BIND_POINT_COMPUTE, computePipeline);
    std::cout << "[ComputeDispatchNode::CompileImpl] Bound compute pipeline: "
              << reinterpret_cast<uint64_t>(computePipeline) << std::endl;

    // 6. Bind descriptor sets (if provided)
    if (descriptorSetCount > 0 && !descriptorSets.empty()) {
        if (descriptorSets.size() < descriptorSetCount) {
            throw std::runtime_error("[ComputeDispatchNode::CompileImpl] Descriptor set count mismatch: expected " +
                                     std::to_string(descriptorSetCount) + ", got " + std::to_string(descriptorSets.size()));
        }

        vkCmdBindDescriptorSets(
            commandBuffer_,
            VK_PIPELINE_BIND_POINT_COMPUTE,
            pipelineLayout,
            0,  // firstSet
            descriptorSetCount,
            descriptorSets.data(),
            0,  // dynamicOffsetCount
            nullptr  // pDynamicOffsets
        );

        std::cout << "[ComputeDispatchNode::CompileImpl] Bound " << descriptorSetCount << " descriptor sets" << std::endl;
    }

    // 7. Push constants (if provided)
    if (pushConstantSize > 0 && pushConstantsBuffer != VK_NULL_HANDLE) {
        // TODO: Map buffer and push constants
        // For now, assume push constants are provided via mapped buffer
        // In practice, this might need a different input type (raw bytes)
        std::cout << "[ComputeDispatchNode::CompileImpl] WARNING: Push constants not yet implemented" << std::endl;
    }

    // 8. Dispatch compute shader
    vkCmdDispatch(commandBuffer_, dispatchX, dispatchY, dispatchZ);
    std::cout << "[ComputeDispatchNode::CompileImpl] Dispatched compute: "
              << dispatchX << "x" << dispatchY << "x" << dispatchZ << std::endl;

    // 9. End recording
    result = vkEndCommandBuffer(commandBuffer_);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("[ComputeDispatchNode::CompileImpl] Failed to end command buffer: " + std::to_string(result));
    }

    // 10. Set outputs
    Out(ComputeDispatchNodeConfig::COMMAND_BUFFER, commandBuffer_);
    Out(ComputeDispatchNodeConfig::VULKAN_DEVICE_OUT, devicePtr);

    std::cout << "[ComputeDispatchNode::CompileImpl] Compute dispatch command buffer recorded successfully" << std::endl;
}

// ============================================================================
// EXECUTE
// ============================================================================

void ComputeDispatchNode::ExecuteImpl(Context& ctx) {
    // No-op: Command buffer submission handled by graph execution system
    // The recorded command buffer is submitted by downstream nodes or graph orchestration
}

// ============================================================================
// CLEANUP
// ============================================================================

void ComputeDispatchNode::CleanupImpl() {
    std::cout << "[ComputeDispatchNode::CleanupImpl] Freeing command buffer" << std::endl;

    if (commandBuffer_ != VK_NULL_HANDLE) {
        VulkanDevicePtr devicePtr = In(ComputeDispatchNodeConfig::VULKAN_DEVICE_IN);
        VkCommandPool commandPool = In(ComputeDispatchNodeConfig::COMMAND_POOL);

        if (devicePtr && commandPool != VK_NULL_HANDLE) {
            vkFreeCommandBuffers(devicePtr->device, commandPool, 1, &commandBuffer_);
            std::cout << "[ComputeDispatchNode::CleanupImpl] Command buffer freed" << std::endl;
        }

        commandBuffer_ = VK_NULL_HANDLE;
    }
}

} // namespace Vixen::RenderGraph
