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

    // Get acceleration structure data (still needed for SBT configuration)
    accelData_ = ctx.In(TraceRaysNodeConfig::ACCELERATION_STRUCTURE_DATA);
    if (!accelData_ || !accelData_->IsValid()) {
        throw std::runtime_error("[TraceRaysNode] ACCELERATION_STRUCTURE_DATA is null or invalid");
    }

    // Get swapchain info for command buffer allocation count (matches ComputeDispatchNode)
    SwapChainPublicVariables* swapchainInfo = ctx.In(TraceRaysNodeConfig::SWAPCHAIN_INFO);
    if (!swapchainInfo) {
        throw std::runtime_error("[TraceRaysNode] SWAPCHAIN_INFO is null");
    }
    swapChainImageCount_ = swapchainInfo->swapChainImageCount;
    NODE_LOG_INFO("[TraceRaysNode] Swapchain image count: " + std::to_string(swapChainImageCount_));

    // Load RTX functions
    if (!LoadRTXFunctions()) {
        throw std::runtime_error("[TraceRaysNode] Failed to load RTX functions");
    }

    // NOTE: Descriptor resources are now managed by DescriptorSetNode
    // We receive descriptor sets via DESCRIPTOR_SETS input in ExecuteImpl
    // This follows the same pattern as ComputeDispatchNode

    // Allocate command buffers
    if (!AllocateCommandBuffers()) {
        throw std::runtime_error("[TraceRaysNode] Failed to allocate command buffers");
    }

    // Create GPU performance logger using centralized GPUQueryManager from VulkanDevice
    // Sprint 6.3 Phase 0: All nodes share the same query manager to prevent slot conflicts
    auto* queryMgrPtr = static_cast<GPUQueryManager*>(vulkanDevice_->GetQueryManager());
    if (queryMgrPtr) {
        // Wrap raw pointer in shared_ptr with no-op deleter (VulkanDevice owns the manager)
        auto queryManager = std::shared_ptr<GPUQueryManager>(queryMgrPtr, [](GPUQueryManager*){});

        gpuPerfLogger_ = std::make_shared<GPUPerformanceLogger>(instanceName, queryManager);
        gpuPerfLogger_->SetEnabled(true);
        gpuPerfLogger_->SetLogFrequency(120);
        gpuPerfLogger_->SetPrintToTerminal(false);

        if (nodeLogger) {
            nodeLogger->AddChild(gpuPerfLogger_);
        }

        if (gpuPerfLogger_->IsTimingSupported()) {
            NODE_LOG_INFO("[TraceRaysNode] GPU performance timing enabled");
        } else {
            NODE_LOG_WARNING("[TraceRaysNode] GPU timing not supported on this device");
        }
    } else {
        NODE_LOG_WARNING("[TraceRaysNode] GPUQueryManager not available from VulkanDevice");
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

    // Get descriptor sets from DescriptorSetNode (following ComputeDispatchNode pattern)
    const std::vector<VkDescriptorSet>& descriptorSets = ctx.In(TraceRaysNodeConfig::DESCRIPTOR_SETS);
    if (descriptorSets.empty()) {
        NODE_LOG_ERROR("[TraceRaysNode] No descriptor sets received from graph");
        return;
    }

    // Validate descriptor set for this image
    if (imageIndex >= descriptorSets.size()) {
        NODE_LOG_ERROR("[TraceRaysNode] Invalid descriptor set index: " +
                       std::to_string(imageIndex) + " >= " + std::to_string(descriptorSets.size()));
        return;
    }

    VkDescriptorSet descriptorSet = descriptorSets[imageIndex];
    if (descriptorSet == VK_NULL_HANDLE) {
        NODE_LOG_ERROR("[TraceRaysNode] Descriptor set is null for image " + std::to_string(imageIndex));
        return;
    }

    static int logCounter = 0;
    if (logCounter++ < 5) {
        NODE_LOG_INFO("[TraceRaysNode] Using descriptor set from graph for image " +
                      std::to_string(imageIndex) + " (frame " + std::to_string(currentFrame) + ")");
    }

    VkDevice device = vulkanDevice_->device;

    // Reset fence before submitting (fence was already waited on by FrameSyncNode)
    // This matches ComputeDispatchNode pattern exactly
    vkResetFences(device, 1, &inFlightFence);

    // Collect GPU performance results for this frame-in-flight (after fence wait)
    if (gpuPerfLogger_) {
        gpuPerfLogger_->CollectResults(currentFrame);
    }

    // Guard against invalid image index (matches ComputeDispatchNode)
    if (imageIndex == UINT32_MAX || imageIndex >= commandBuffers_.size()) {
        NODE_LOG_WARNING("[TraceRaysNode] Invalid image index - skipping frame");
        return;
    }

    // Get command buffer for this IMAGE (not frame) - matches ComputeDispatchNode pattern
    VkCommandBuffer cmdBuffer = commandBuffers_[imageIndex];

    // Begin command buffer (matches ComputeDispatchNode pattern - no special flags)
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = 0;  // Match ComputeDispatchNode - no ONE_TIME_SUBMIT

    vkBeginCommandBuffer(cmdBuffer, &beginInfo);

    // Get output image for layout transitions
    VkImage outputImage = swapchainInfo->colorBuffers[imageIndex].image;

    // Transition image to GENERAL for ray tracing output
    // After present, swapchain images are in PRESENT_SRC_KHR layout.
    // We use UNDEFINED to discard previous contents (works for any source layout).
    // Note: First frame image might be in UNDEFINED, but UNDEFINED->GENERAL is valid.
    // If validation complains about layout mismatch, the real issue is elsewhere.
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;  // Discard previous contents
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

    // Bind ray tracing pipeline
    vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, pipelineData_->pipeline);

    // Bind descriptor set from graph (DescriptorSetNode handles creation/updates)
    vkCmdBindDescriptorSets(
        cmdBuffer,
        VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR,
        pipelineData_->pipelineLayout,
        0,
        1,
        &descriptorSet,
        0,
        nullptr
    );

    // Push constants - get stage flags from pipeline data (set by RayTracingPipelineNode)
    if (!pushConstData.empty() && pipelineData_->pushConstantStages != 0) {
        vkCmdPushConstants(
            cmdBuffer,
            pipelineData_->pipelineLayout,
            pipelineData_->pushConstantStages,
            pipelineData_->pushConstantOffset,
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

    // Begin GPU timing frame (reset queries for this frame) - CRITICAL for timing to work
    if (gpuPerfLogger_) {
        gpuPerfLogger_->BeginFrame(cmdBuffer, currentFrame);
    }

    // Record GPU timing start
    if (gpuPerfLogger_) {
        gpuPerfLogger_->RecordDispatchStart(cmdBuffer, currentFrame);
    }

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

    // Record GPU timing end
    if (gpuPerfLogger_) {
        gpuPerfLogger_->RecordDispatchEnd(cmdBuffer, currentFrame, width_, height_);
    }

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

    // Release GPU resources (QueryPools) while device is still valid.
    // GPU resources (QueryPools) will be automatically released by GPUQueryManager destructor
    // Logger object stays alive for parent log extraction.

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
// DESCRIPTOR RESOURCES - REMOVED
// ============================================================================
// NOTE: Descriptor pool/set creation is now handled by DescriptorSetNode.
// TraceRaysNode receives descriptor sets via DESCRIPTOR_SETS input slot.
// This follows the same pattern as ComputeDispatchNode for consistency.

bool TraceRaysNode::AllocateCommandBuffers() {
    VkDevice device = vulkanDevice_->device;

    // Allocate one command buffer per swapchain image (matches ComputeDispatchNode pattern)
    // swapChainImageCount_ is set during CompileImpl from SWAPCHAIN_INFO
    commandBuffers_.resize(swapChainImageCount_);

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = commandPool_;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = swapChainImageCount_;

    if (vkAllocateCommandBuffers(device, &allocInfo, commandBuffers_.data()) != VK_SUCCESS) {
        NODE_LOG_ERROR("Failed to allocate command buffers");
        return false;
    }

    NODE_LOG_INFO("Allocated " + std::to_string(swapChainImageCount_) + " command buffers (one per swapchain image)");
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

    // NOTE: Descriptor pool/set cleanup is handled by DescriptorSetNode
    // TraceRaysNode no longer owns descriptor resources

    NODE_LOG_INFO("Destroyed TraceRaysNode resources");
}

} // namespace Vixen::RenderGraph
