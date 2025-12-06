#include "Nodes/TraceRaysNode.h"
#include "VulkanDevice.h"
#include "VulkanSwapChain.h"
#include "Core/NodeLogging.h"
#include <array>

namespace Vixen::RenderGraph {

// ============================================================================
// NODE TYPE FACTORY
// ============================================================================

std::unique_ptr<NodeInstance> TraceRaysNodeType::CreateInstance(
    const std::string& instanceName
) const {
    return std::unique_ptr<NodeInstance>(
        new TraceRaysNode(instanceName, const_cast<TraceRaysNodeType*>(this))
    );
}

// ============================================================================
// TRACE RAYS NODE IMPLEMENTATION
// ============================================================================

TraceRaysNode::TraceRaysNode(
    const std::string& instanceName,
    NodeType* nodeType
) : TypedNode<TraceRaysNodeConfig>(instanceName, nodeType)
{
    NODE_LOG_INFO("TraceRaysNode constructor (Phase K)");
}

void TraceRaysNode::SetupImpl(TypedSetupContext& ctx) {
    NODE_LOG_DEBUG("[TraceRaysNode::SetupImpl] ENTERED");

    width_ = GetParameterValue<uint32_t>(TraceRaysNodeConfig::PARAM_WIDTH, 1920u);
    height_ = GetParameterValue<uint32_t>(TraceRaysNodeConfig::PARAM_HEIGHT, 1080u);
    depth_ = GetParameterValue<uint32_t>(TraceRaysNodeConfig::PARAM_DEPTH, 1u);

    NODE_LOG_INFO("TraceRays setup: dimensions=" +
                  std::to_string(width_) + "x" + std::to_string(height_) + "x" + std::to_string(depth_));

    NODE_LOG_DEBUG("[TraceRaysNode::SetupImpl] COMPLETED");
}

void TraceRaysNode::CompileImpl(TypedCompileContext& ctx) {
    NODE_LOG_DEBUG("[TraceRaysNode::CompileImpl] ENTERED");
    NODE_LOG_INFO("=== TraceRaysNode::CompileImpl START ===");

    // Get device
    VulkanDevice* devicePtr = ctx.In(TraceRaysNodeConfig::VULKAN_DEVICE_IN);
    if (!devicePtr) {
        throw std::runtime_error("[TraceRaysNode] VULKAN_DEVICE_IN is null");
    }
    SetDevice(devicePtr);
    vulkanDevice_ = devicePtr;

    // Get command pool
    commandPool_ = ctx.In(TraceRaysNodeConfig::COMMAND_POOL);
    if (commandPool_ == VK_NULL_HANDLE) {
        throw std::runtime_error("[TraceRaysNode] COMMAND_POOL is null");
    }

    // Get pipeline data
    pipelineData_ = ctx.In(TraceRaysNodeConfig::RT_PIPELINE_DATA);
    if (!pipelineData_ || !pipelineData_->IsValid()) {
        throw std::runtime_error("[TraceRaysNode] RT_PIPELINE_DATA is null or invalid");
    }

    // Get acceleration structure data
    accelData_ = ctx.In(TraceRaysNodeConfig::ACCELERATION_STRUCTURE_DATA);
    if (!accelData_ || !accelData_->IsValid()) {
        throw std::runtime_error("[TraceRaysNode] ACCELERATION_STRUCTURE_DATA is null or invalid");
    }

    // Load RTX functions
    if (!LoadRTXFunctions()) {
        throw std::runtime_error("[TraceRaysNode] Failed to load RTX functions");
    }

    // Create descriptor resources
    if (!CreateDescriptorResources()) {
        throw std::runtime_error("[TraceRaysNode] Failed to create descriptor resources");
    }

    // Allocate command buffers
    if (!AllocateCommandBuffers()) {
        throw std::runtime_error("[TraceRaysNode] Failed to allocate command buffers");
    }

    NODE_LOG_INFO("=== TraceRaysNode::CompileImpl COMPLETE ===");
    NODE_LOG_DEBUG("[TraceRaysNode::CompileImpl] COMPLETED");
}

void TraceRaysNode::ExecuteImpl(TypedExecuteContext& ctx) {
    // Get frame info
    SwapChainPublicVariables* swapchainInfo = ctx.In(TraceRaysNodeConfig::SWAPCHAIN_INFO);
    uint32_t imageIndex = ctx.In(TraceRaysNodeConfig::IMAGE_INDEX);
    uint32_t currentFrame = ctx.In(TraceRaysNodeConfig::CURRENT_FRAME_INDEX);
    VkFence inFlightFence = ctx.In(TraceRaysNodeConfig::IN_FLIGHT_FENCE);

    // Get semaphore arrays
    const std::vector<VkSemaphore>& imageAvailableSemaphores =
        ctx.In(TraceRaysNodeConfig::IMAGE_AVAILABLE_SEMAPHORES_ARRAY);
    const std::vector<VkSemaphore>& renderCompleteSemaphores =
        ctx.In(TraceRaysNodeConfig::RENDER_COMPLETE_SEMAPHORES_ARRAY);

    // Get push constants (optional)
    std::vector<uint8_t> pushConstData = ctx.In(TraceRaysNodeConfig::PUSH_CONSTANT_DATA);

    VkDevice device = vulkanDevice_->device;

    // Wait for previous frame to complete
    vkWaitForFences(device, 1, &inFlightFence, VK_TRUE, UINT64_MAX);
    vkResetFences(device, 1, &inFlightFence);

    // Get command buffer for this frame
    VkCommandBuffer cmdBuffer = commandBuffers_[currentFrame];
    vkResetCommandBuffer(cmdBuffer, 0);

    // Begin command buffer
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(cmdBuffer, &beginInfo);

    // Get output image
    VkImage outputImage = swapchainInfo->colorBuffers[imageIndex].image;
    VkImageView outputImageView = swapchainInfo->colorBuffers[imageIndex].view;

    // Transition image to GENERAL for ray tracing output
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = outputImage;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;

    vkCmdPipelineBarrier(
        cmdBuffer,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
        0,
        0, nullptr,
        0, nullptr,
        1, &barrier
    );

    // Update descriptor set with current image view
    UpdateDescriptorSet(outputImageView);

    // Bind ray tracing pipeline
    vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, pipelineData_->pipeline);

    // Bind descriptor set
    vkCmdBindDescriptorSets(
        cmdBuffer,
        VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR,
        pipelineData_->pipelineLayout,
        0,
        1,
        &descriptorSet_,
        0,
        nullptr
    );

    // Push constants
    if (!pushConstData.empty()) {
        vkCmdPushConstants(
            cmdBuffer,
            pipelineData_->pipelineLayout,
            VK_SHADER_STAGE_RAYGEN_BIT_KHR,
            0,
            static_cast<uint32_t>(pushConstData.size()),
            pushConstData.data()
        );
    }

    // Setup SBT regions
    VkStridedDeviceAddressRegionKHR raygenRegion{};
    raygenRegion.deviceAddress = pipelineData_->sbt.raygenRegion.deviceAddress;
    raygenRegion.stride = pipelineData_->sbt.raygenRegion.stride;
    raygenRegion.size = pipelineData_->sbt.raygenRegion.size;

    VkStridedDeviceAddressRegionKHR missRegion{};
    missRegion.deviceAddress = pipelineData_->sbt.missRegion.deviceAddress;
    missRegion.stride = pipelineData_->sbt.missRegion.stride;
    missRegion.size = pipelineData_->sbt.missRegion.size;

    VkStridedDeviceAddressRegionKHR hitRegion{};
    hitRegion.deviceAddress = pipelineData_->sbt.hitRegion.deviceAddress;
    hitRegion.stride = pipelineData_->sbt.hitRegion.stride;
    hitRegion.size = pipelineData_->sbt.hitRegion.size;

    VkStridedDeviceAddressRegionKHR callableRegion{};

    // Dispatch rays
    vkCmdTraceRaysKHR_(
        cmdBuffer,
        &raygenRegion,
        &missRegion,
        &hitRegion,
        &callableRegion,
        width_,
        height_,
        depth_
    );

    // Transition image to PRESENT_SRC
    barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = 0;

    vkCmdPipelineBarrier(
        cmdBuffer,
        VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
        VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
        0,
        0, nullptr,
        0, nullptr,
        1, &barrier
    );

    vkEndCommandBuffer(cmdBuffer);

    // Submit command buffer
    VkSemaphore waitSemaphores[] = { imageAvailableSemaphores[currentFrame] };
    VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR };
    VkSemaphore signalSemaphores[] = { renderCompleteSemaphores[imageIndex] };

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmdBuffer;
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;

    VkResult result = vkQueueSubmit(vulkanDevice_->queue, 1, &submitInfo, inFlightFence);
    if (result != VK_SUCCESS) {
        NODE_LOG_ERROR("vkQueueSubmit failed: " + std::to_string(result));
    }

    // Output command buffer and semaphore
    ctx.Out(TraceRaysNodeConfig::COMMAND_BUFFER, cmdBuffer);
    ctx.Out(TraceRaysNodeConfig::RENDER_COMPLETE_SEMAPHORE, signalSemaphores[0]);
}

void TraceRaysNode::CleanupImpl(TypedCleanupContext& ctx) {
    NODE_LOG_INFO("TraceRaysNode cleanup");
    DestroyResources();
}

// ============================================================================
// RTX FUNCTION LOADING
// ============================================================================

bool TraceRaysNode::LoadRTXFunctions() {
    VkDevice device = vulkanDevice_->device;

    vkCmdTraceRaysKHR_ =
        (PFN_vkCmdTraceRaysKHR)vkGetDeviceProcAddr(device, "vkCmdTraceRaysKHR");

    if (!vkCmdTraceRaysKHR_) {
        NODE_LOG_ERROR("Failed to load vkCmdTraceRaysKHR");
        return false;
    }

    return true;
}

// ============================================================================
// DESCRIPTOR RESOURCES
// ============================================================================

bool TraceRaysNode::CreateDescriptorResources() {
    VkDevice device = vulkanDevice_->device;

    // Create descriptor pool
    std::array<VkDescriptorPoolSize, 3> poolSizes{};

    poolSizes[0].type = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
    poolSizes[0].descriptorCount = 1;

    poolSizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    poolSizes[1].descriptorCount = 1;

    poolSizes[2].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSizes[2].descriptorCount = 2;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    poolInfo.maxSets = 1;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();

    if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool_) != VK_SUCCESS) {
        NODE_LOG_ERROR("Failed to create descriptor pool");
        return false;
    }

    // Allocate descriptor set
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool_;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &pipelineData_->descriptorSetLayout;

    if (vkAllocateDescriptorSets(device, &allocInfo, &descriptorSet_) != VK_SUCCESS) {
        NODE_LOG_ERROR("Failed to allocate descriptor set");
        return false;
    }

    NODE_LOG_INFO("Created descriptor resources for TraceRaysNode");
    return true;
}

void TraceRaysNode::UpdateDescriptorSet(VkImageView outputImage) {
    VkDevice device = vulkanDevice_->device;

    // Acceleration structure write
    VkWriteDescriptorSetAccelerationStructureKHR accelWrite{};
    accelWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
    accelWrite.accelerationStructureCount = 1;
    accelWrite.pAccelerationStructures = &accelData_->tlas;

    VkWriteDescriptorSet accelDescWrite{};
    accelDescWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    accelDescWrite.pNext = &accelWrite;
    accelDescWrite.dstSet = descriptorSet_;
    accelDescWrite.dstBinding = 0;
    accelDescWrite.descriptorCount = 1;
    accelDescWrite.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;

    // Output image write
    VkDescriptorImageInfo imageInfo{};
    imageInfo.imageView = outputImage;
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

    VkWriteDescriptorSet imageWrite{};
    imageWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    imageWrite.dstSet = descriptorSet_;
    imageWrite.dstBinding = 1;
    imageWrite.descriptorCount = 1;
    imageWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    imageWrite.pImageInfo = &imageInfo;

    std::array<VkWriteDescriptorSet, 2> writes = { accelDescWrite, imageWrite };
    vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()),
                           writes.data(), 0, nullptr);
}

bool TraceRaysNode::AllocateCommandBuffers() {
    VkDevice device = vulkanDevice_->device;

    commandBuffers_.resize(framesInFlight_);

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = commandPool_;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = framesInFlight_;

    if (vkAllocateCommandBuffers(device, &allocInfo, commandBuffers_.data()) != VK_SUCCESS) {
        NODE_LOG_ERROR("Failed to allocate command buffers");
        return false;
    }

    NODE_LOG_INFO("Allocated " + std::to_string(framesInFlight_) + " command buffers");
    return true;
}

void TraceRaysNode::DestroyResources() {
    if (!vulkanDevice_) {
        return;
    }

    VkDevice device = vulkanDevice_->device;

    // Free command buffers
    if (!commandBuffers_.empty() && commandPool_ != VK_NULL_HANDLE) {
        vkFreeCommandBuffers(device, commandPool_,
                             static_cast<uint32_t>(commandBuffers_.size()),
                             commandBuffers_.data());
        commandBuffers_.clear();
    }

    // Destroy descriptor pool (also frees descriptor set)
    if (descriptorPool_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device, descriptorPool_, nullptr);
        descriptorPool_ = VK_NULL_HANDLE;
        descriptorSet_ = VK_NULL_HANDLE;
    }

    NODE_LOG_INFO("Destroyed TraceRaysNode resources");
}

} // namespace Vixen::RenderGraph
