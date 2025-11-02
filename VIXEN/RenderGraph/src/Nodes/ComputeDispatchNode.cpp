#include "Nodes/ComputeDispatchNode.h"
#include "Nodes/ComputeDispatchNodeConfig.h"
#include "VulkanResources/VulkanDevice.h"
#include "Core/ComputePerformanceLogger.h"
#include "VulkanSwapChain.h"  // For SwapChainPublicVariables
#include <stdexcept>
#include <iostream>
#include <chrono>

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
    vulkanDevice = devicePtr;

#if VIXEN_DEBUG_BUILD
    // Create specialized performance logger and register to node logger
    perfLogger_ = std::make_unique<ComputePerformanceLogger>(instanceName);
    if (nodeLogger) {
        nodeLogger->AddChild(perfLogger_.get());
    }
#endif
}

// ============================================================================
// COMPILE
// ============================================================================

void ComputeDispatchNode::CompileImpl(Context& ctx) {
    std::cout << "[ComputeDispatchNode::CompileImpl] Allocating per-image command buffers" << std::endl;

    // Get inputs
    commandPool = In(ComputeDispatchNodeConfig::COMMAND_POOL);
    SwapChainPublicVariables* swapchainInfo = In(ComputeDispatchNodeConfig::SWAPCHAIN_INFO);

    if (!swapchainInfo) {
        throw std::runtime_error("[ComputeDispatchNode::CompileImpl] SwapChain info is null");
    }

    uint32_t imageCount = swapchainInfo->swapChainImageCount;
    std::cout << "[ComputeDispatchNode::CompileImpl] Allocating " << imageCount << " command buffers" << std::endl;

    // Allocate command buffers (one per swapchain image)
    commandBuffers.resize(imageCount);

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = imageCount;

    std::vector<VkCommandBuffer> cmdBuffers(imageCount);
    VkResult result = vkAllocateCommandBuffers(vulkanDevice->device, &allocInfo, cmdBuffers.data());
    if (result != VK_SUCCESS) {
        throw std::runtime_error("[ComputeDispatchNode::CompileImpl] Failed to allocate command buffers: " + std::to_string(result));
    }

    // Store command buffers in stateful container
    for (uint32_t i = 0; i < imageCount; ++i) {
        commandBuffers[i] = cmdBuffers[i];
        commandBuffers.MarkDirty(i);  // Initial state: needs recording
    }

    std::cout << "[ComputeDispatchNode::CompileImpl] Allocated " << imageCount << " command buffers successfully" << std::endl;
}

// ============================================================================
// EXECUTE
// ============================================================================

void ComputeDispatchNode::ExecuteImpl(Context& ctx) {
    // Get current image index from SwapChainNode
    uint32_t imageIndex = ctx.In(ComputeDispatchNodeConfig::IMAGE_INDEX);

    // Get current frame-in-flight index from FrameSyncNode
    uint32_t currentFrameIndex = ctx.In(ComputeDispatchNodeConfig::CURRENT_FRAME_INDEX);

    // Get semaphore arrays from FrameSyncNode
    const VkSemaphore* imageAvailableSemaphores = ctx.In(ComputeDispatchNodeConfig::IMAGE_AVAILABLE_SEMAPHORES_ARRAY);
    const VkSemaphore* renderCompleteSemaphores = ctx.In(ComputeDispatchNodeConfig::RENDER_COMPLETE_SEMAPHORES_ARRAY);
    VkFence inFlightFence = ctx.In(ComputeDispatchNodeConfig::IN_FLIGHT_FENCE);

    // Two-tier indexing: imageAvailable by frame, renderComplete by image
    VkSemaphore imageAvailableSemaphore = imageAvailableSemaphores[currentFrameIndex];
    VkSemaphore renderCompleteSemaphore = renderCompleteSemaphores[imageIndex];

    static int logCounter = 0;
    if (logCounter++ < 20) {
        NODE_LOG_INFO("Compute Frame " + std::to_string(currentFrameIndex) + ", Image " + std::to_string(imageIndex));
    }

    // Phase 0.4: Reset fence before submitting (fence was already waited on by FrameSyncNode)
    vkResetFences(vulkanDevice->device, 1, &inFlightFence);

    // Guard against invalid image index
    if (imageIndex == UINT32_MAX || imageIndex >= commandBuffers.size()) {
        NODE_LOG_WARNING("ComputeDispatchNode: Invalid image index - skipping frame");
        return;
    }

    // Detect if inputs changed (mark all command buffers dirty if so)
    VkPipeline currentPipeline = ctx.In(ComputeDispatchNodeConfig::COMPUTE_PIPELINE);
    VkPipelineLayout currentPipelineLayout = ctx.In(ComputeDispatchNodeConfig::PIPELINE_LAYOUT);
    DescriptorSetVector currentDescriptorSets = ctx.In(ComputeDispatchNodeConfig::DESCRIPTOR_SETS);

    if (currentPipeline != lastPipeline ||
        currentPipelineLayout != lastPipelineLayout ||
        currentDescriptorSets != lastDescriptorSets) {
        // Inputs changed - mark all command buffers dirty
        commandBuffers.MarkAllDirty();

        lastPipeline = currentPipeline;
        lastPipelineLayout = currentPipelineLayout;
        lastDescriptorSets = currentDescriptorSets;
    }

    // Calculate push constants (time updates every frame)
    struct PushConstants {
        float time;
        uint32_t frame;
        uint32_t padding[2];
    } pushConstants;

    static auto startTime = std::chrono::high_resolution_clock::now();
    auto currentTime = std::chrono::high_resolution_clock::now();
    float elapsedTime = std::chrono::duration<float>(currentTime - startTime).count();

    pushConstants.time = elapsedTime;
    pushConstants.frame = static_cast<uint32_t>(elapsedTime * 60.0f);

    // Only re-record if dirty
    VkCommandBuffer cmdBuffer = commandBuffers.GetValue(imageIndex);
    if (commandBuffers.IsDirty(imageIndex)) {
        RecordComputeCommands(ctx, cmdBuffer, imageIndex, &pushConstants);
        commandBuffers.MarkReady(imageIndex);
    } else {
        // Command buffer already recorded, just update push constants
        VkPipelineLayout pipelineLayout = ctx.In(ComputeDispatchNodeConfig::PIPELINE_LAYOUT);
        vkCmdPushConstants(cmdBuffer, pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0,
                          sizeof(float) + sizeof(uint32_t), &pushConstants);
    }

    // Submit command buffer to compute queue
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

    // Wait for image to be available before writing to it
    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = &imageAvailableSemaphore;
    submitInfo.pWaitDstStageMask = &waitStage;

    // Submit command buffer
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmdBuffer;

    // Signal render complete semaphore (will be consumed by Present)
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = &renderCompleteSemaphore;

    // Submit to graphics queue (assume compute = graphics for now)
    VkResult result = vkQueueSubmit(vulkanDevice->queue, 1, &submitInfo, inFlightFence);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("[ComputeDispatchNode::ExecuteImpl] Failed to submit command buffer: " + std::to_string(result));
    }

    // Output semaphore for Present to wait on
    Out(ComputeDispatchNodeConfig::RENDER_COMPLETE_SEMAPHORE, renderCompleteSemaphore);
}

// ============================================================================
// RECORD COMPUTE COMMANDS
// ============================================================================

void ComputeDispatchNode::RecordComputeCommands(Context& ctx, VkCommandBuffer cmdBuffer, uint32_t imageIndex, const void* pushConstantData) {
    // Begin command buffer recording
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = 0;

    VkResult result = vkBeginCommandBuffer(cmdBuffer, &beginInfo);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("[ComputeDispatchNode::RecordComputeCommands] Failed to begin command buffer");
    }

    // Get inputs
    VkPipeline pipeline = ctx.In(ComputeDispatchNodeConfig::COMPUTE_PIPELINE);
    VkPipelineLayout pipelineLayout = ctx.In(ComputeDispatchNodeConfig::PIPELINE_LAYOUT);
    DescriptorSetVector descriptorSets = ctx.In(ComputeDispatchNodeConfig::DESCRIPTOR_SETS);
    SwapChainPublicVariables* swapchainInfo = ctx.In(ComputeDispatchNodeConfig::SWAPCHAIN_INFO);

    // Get dispatch dimensions from swapchain extent (8x8 workgroup size)
    uint32_t dispatchX = (swapchainInfo->Extent.width + 7) / 8;
    uint32_t dispatchY = (swapchainInfo->Extent.height + 7) / 8;
    uint32_t dispatchZ = 1;

    static int logCount = 0;
    if (logCount++ < 3) {
        std::cout << "[ComputeDispatchNode] Dispatch: " << dispatchX << "x" << dispatchY << "x" << dispatchZ
                  << " for swapchain " << swapchainInfo->Extent.width << "x" << swapchainInfo->Extent.height << std::endl;
    }

    // Validate descriptor sets
    if (descriptorSets.empty() || imageIndex >= descriptorSets.size()) {
        throw std::runtime_error("[ComputeDispatchNode::RecordComputeCommands] Invalid descriptor sets for image " + std::to_string(imageIndex));
    }

    // Transition swapchain image: UNDEFINED -> GENERAL
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = swapchainInfo->colorBuffers[imageIndex].image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    vkCmdPipelineBarrier(
        cmdBuffer,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0,
        0, nullptr,
        0, nullptr,
        1, &barrier
    );

    // Bind compute pipeline
    vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);

    // Bind descriptor set from DescriptorSetNode
    VkDescriptorSet descriptorSet = descriptorSets[imageIndex];
    vkCmdBindDescriptorSets(
        cmdBuffer,
        VK_PIPELINE_BIND_POINT_COMPUTE,
        pipelineLayout,
        0,
        1,
        &descriptorSet,
        0,
        nullptr
    );

    // Set push constants from caller (updated every frame in ExecuteImpl)
    vkCmdPushConstants(
        cmdBuffer,
        pipelineLayout,
        VK_SHADER_STAGE_COMPUTE_BIT,
        0,  // offset
        sizeof(float) + sizeof(uint32_t),  // size (8 bytes: float + uint)
        pushConstantData
    );

    // Dispatch compute shader
    vkCmdDispatch(cmdBuffer, dispatchX, dispatchY, dispatchZ);

    // Transition swapchain image: GENERAL -> PRESENT_SRC
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = 0;
    barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    vkCmdPipelineBarrier(
        cmdBuffer,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
        0,
        0, nullptr,
        0, nullptr,
        1, &barrier
    );

    // End command buffer
    result = vkEndCommandBuffer(cmdBuffer);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("[ComputeDispatchNode::RecordComputeCommands] Failed to end command buffer");
    }

    std::cout << "[ComputeDispatchNode::RecordComputeCommands] Recorded compute commands for image " << imageIndex << std::endl;
}

// ============================================================================
// CLEANUP
// ============================================================================

void ComputeDispatchNode::CleanupImpl() {
    std::cout << "[ComputeDispatchNode::CleanupImpl] Cleaning up resources" << std::endl;

    if (vulkanDevice && vulkanDevice->device != VK_NULL_HANDLE) {
        // Free command buffers
        if (!commandBuffers.empty() && commandPool != VK_NULL_HANDLE) {
            std::vector<VkCommandBuffer> rawHandles;
            rawHandles.reserve(commandBuffers.size());
            for (size_t i = 0; i < commandBuffers.size(); ++i) {
                rawHandles.push_back(commandBuffers.GetValue(i));
            }

            vkFreeCommandBuffers(
                vulkanDevice->device,
                commandPool,
                static_cast<uint32_t>(rawHandles.size()),
                rawHandles.data()
            );
            commandBuffers.clear();
        }
    }

    std::cout << "[ComputeDispatchNode::CleanupImpl] Cleanup complete" << std::endl;
}

} // namespace Vixen::RenderGraph
