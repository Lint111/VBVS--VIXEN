#include "Nodes/ComputeDispatchNode.h"
#include "Data/Nodes/ComputeDispatchNodeConfig.h"
#include "VulkanDevice.h"
#include "Core/ComputePerformanceLogger.h"
#include "Core/GPUPerformanceLogger.h"
#include "VulkanSwapChain.h"  // For SwapChainPublicVariables
#include "ShaderDataBundle.h"
#include "Debug/IDebugCapture.h"  // For debug capture passthrough
#include "Core/NodeLogging.h"
#include <stdexcept>
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
    NODE_LOG_INFO("[ComputeDispatchNode] Constructor called for " + instanceName);
}

// ============================================================================
// SETUP
// ============================================================================

void ComputeDispatchNode::SetupImpl(TypedSetupContext& ctx) {
    // Graph-scope initialization only (no input access)
    NODE_LOG_INFO("[ComputeDispatchNode::SetupImpl] Graph-scope initialization");

    // Create specialized performance logger (disabled by default)
    perfLogger_ = std::make_shared<ComputePerformanceLogger>(instanceName);
    perfLogger_->SetEnabled(false);  // Enable manually when needed for debugging

    // Register to node logger hierarchy for shared ownership
    if (nodeLogger) {
        nodeLogger->AddChild(perfLogger_);
    }
}

// ============================================================================
// COMPILE
// ============================================================================

void ComputeDispatchNode::CompileImpl(TypedCompileContext& ctx) {
    NODE_LOG_INFO("[ComputeDispatchNode::CompileImpl] Allocating per-image command buffers");

    // Access device input (compile-time dependency)
    VulkanDevice* devicePtr = ctx.In(ComputeDispatchNodeConfig::VULKAN_DEVICE_IN);
    if (!devicePtr) {
        throw std::runtime_error("[ComputeDispatchNode::CompileImpl] Vulkan device input is null");
    }

    SetDevice(devicePtr);
    vulkanDevice = devicePtr;

    // Get inputs
    commandPool = ctx.In(ComputeDispatchNodeConfig::COMMAND_POOL);
    SwapChainPublicVariables* swapchainInfo = ctx.In(ComputeDispatchNodeConfig::SWAPCHAIN_INFO);

    if (commandPool == VK_NULL_HANDLE) {
        throw std::runtime_error("[ComputeDispatchNode::CompileImpl] Command pool is null/invalid");
    }

    if (!swapchainInfo) {
        throw std::runtime_error("[ComputeDispatchNode::CompileImpl] SwapChain info is null");
    }

    uint32_t imageCount = swapchainInfo->swapChainImageCount;
    NODE_LOG_INFO("[ComputeDispatchNode::CompileImpl] Allocating " + std::to_string(imageCount) + " command buffers");

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

    NODE_LOG_INFO("[ComputeDispatchNode::CompileImpl] Allocated " + std::to_string(imageCount) + " command buffers successfully");

    // Create GPU performance logger with per-frame query pools
    // imageCount is typically 2-3 for double/triple buffering
    gpuPerfLogger_ = std::make_shared<GPUPerformanceLogger>(instanceName, vulkanDevice, imageCount);
    gpuPerfLogger_->SetEnabled(true);
    gpuPerfLogger_->SetLogFrequency(120);  // Log every 120 frames (~2 seconds at 60fps)
    gpuPerfLogger_->SetPrintToTerminal(true);

    if (nodeLogger) {
        nodeLogger->AddChild(gpuPerfLogger_);
    }

    if (gpuPerfLogger_->IsTimingSupported()) {
        NODE_LOG_INFO("[ComputeDispatchNode] GPU performance timing enabled");
    } else {
        NODE_LOG_WARNING("[ComputeDispatchNode] GPU timing not supported on this device");
    }
}

// ============================================================================
// EXECUTE
// ============================================================================

void ComputeDispatchNode::ExecuteImpl(TypedExecuteContext& ctx) {
    // Get current image index from SwapChainNode
    uint32_t imageIndex = ctx.In(ComputeDispatchNodeConfig::IMAGE_INDEX);

    // Get current frame-in-flight index from FrameSyncNode
    uint32_t currentFrameIndex = ctx.In(ComputeDispatchNodeConfig::CURRENT_FRAME_INDEX);

    // Get semaphore arrays from FrameSyncNode
    const std::vector<VkSemaphore>& imageAvailableSemaphores = ctx.In(ComputeDispatchNodeConfig::IMAGE_AVAILABLE_SEMAPHORES_ARRAY);
    const std::vector<VkSemaphore>& renderCompleteSemaphores = ctx.In(ComputeDispatchNodeConfig::RENDER_COMPLETE_SEMAPHORES_ARRAY);
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

    // Collect GPU performance results for this frame-in-flight (after fence wait)
    // The fence for this frame index was waited on, so previous frame's results are ready
    if (gpuPerfLogger_) {
        gpuPerfLogger_->CollectResults(currentFrameIndex);
    }

    // Guard against invalid image index
    if (imageIndex == UINT32_MAX || imageIndex >= commandBuffers.size()) {
        NODE_LOG_WARNING("ComputeDispatchNode: Invalid image index - skipping frame");
        return;
    }

    // Detect if inputs changed (mark all command buffers dirty if so)
    VkPipeline currentPipeline = ctx.In(ComputeDispatchNodeConfig::COMPUTE_PIPELINE);
    VkPipelineLayout currentPipelineLayout = ctx.In(ComputeDispatchNodeConfig::PIPELINE_LAYOUT);
    std::vector<VkDescriptorSet> currentDescriptorSets = ctx.In(ComputeDispatchNodeConfig::DESCRIPTOR_SETS);

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

    // Always re-record to update push constants (they change every frame)
    // TODO: Optimize using secondary command buffers or dynamic state
    VkCommandBuffer cmdBuffer = commandBuffers.GetValue(imageIndex);
    RecordComputeCommands(ctx, cmdBuffer, imageIndex, currentFrameIndex, &pushConstants);
    commandBuffers.MarkReady(imageIndex);

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
    ctx.Out(ComputeDispatchNodeConfig::RENDER_COMPLETE_SEMAPHORE, renderCompleteSemaphore);

    // Pass through debug capture for downstream debug reader nodes
    // Debug capture input comes from DescriptorResourceGathererNode
    Debug::IDebugCapture* debugCapture = ctx.In(ComputeDispatchNodeConfig::DEBUG_CAPTURE);
    ctx.Out(ComputeDispatchNodeConfig::DEBUG_CAPTURE_OUT, debugCapture);
    if (debugCapture) {
        static int debugLogCount = 0;
        if (debugLogCount++ < 3) {
            NODE_LOG_INFO("[ComputeDispatchNode] Passing through debug capture: " + debugCapture->GetDebugName());
        }
    }
}

// ============================================================================
// RECORD COMPUTE COMMANDS
// ============================================================================

void ComputeDispatchNode::RecordComputeCommands(Context& ctx, VkCommandBuffer cmdBuffer, uint32_t imageIndex, uint32_t frameIndex, const void* pushConstantData) {
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
    std::vector<VkDescriptorSet> descriptorSets = ctx.In(ComputeDispatchNodeConfig::DESCRIPTOR_SETS);
    SwapChainPublicVariables* swapchainInfo = ctx.In(ComputeDispatchNodeConfig::SWAPCHAIN_INFO);

    // Validate descriptor sets
    if (descriptorSets.empty() || imageIndex >= descriptorSets.size()) {
        throw std::runtime_error("[ComputeDispatchNode::RecordComputeCommands] Invalid descriptor sets for image " + std::to_string(imageIndex));
    }

    // Get dispatch dimensions from swapchain extent (8x8 workgroup size)
    uint32_t dispatchX = (swapchainInfo->Extent.width + 7) / 8;
    uint32_t dispatchY = (swapchainInfo->Extent.height + 7) / 8;
    uint32_t dispatchZ = 1;

    static int logCount = 0;
    if (logCount++ < 3) {
        NODE_LOG_INFO("[ComputeDispatchNode] Dispatch: " + std::to_string(dispatchX) + "x" + std::to_string(dispatchY) + "x" + std::to_string(dispatchZ) +
                      " for swapchain " + std::to_string(swapchainInfo->Extent.width) + "x" + std::to_string(swapchainInfo->Extent.height));
    }

    // Execute recording steps
    VkImage swapchainImage = swapchainInfo->colorBuffers[imageIndex].image;
    VkDescriptorSet descriptorSet = descriptorSets[imageIndex];

    // Begin GPU timing frame (reset queries for this frame)
    if (gpuPerfLogger_) {
        gpuPerfLogger_->BeginFrame(cmdBuffer, frameIndex);
    }

    TransitionImageToGeneral(cmdBuffer, swapchainImage);
    BindComputePipeline(cmdBuffer, pipeline, pipelineLayout, descriptorSet);
    SetPushConstants(ctx, cmdBuffer, pipelineLayout, pushConstantData);

    // Record GPU timestamps around dispatch
    if (gpuPerfLogger_) {
        gpuPerfLogger_->RecordDispatchStart(cmdBuffer, frameIndex);
    }

    // Dispatch compute shader
    vkCmdDispatch(cmdBuffer, dispatchX, dispatchY, dispatchZ);

    // End GPU timing
    if (gpuPerfLogger_) {
        gpuPerfLogger_->RecordDispatchEnd(cmdBuffer, frameIndex, swapchainInfo->Extent.width, swapchainInfo->Extent.height);
    }

    TransitionImageToPresent(cmdBuffer, swapchainImage);

    // End command buffer
    result = vkEndCommandBuffer(cmdBuffer);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("[ComputeDispatchNode::RecordComputeCommands] Failed to end command buffer");
    }

    NODE_LOG_INFO("[ComputeDispatchNode::RecordComputeCommands] Recorded compute commands for image " + std::to_string(imageIndex));
}

// ============================================================================
// HELPER METHODS
// ============================================================================

void ComputeDispatchNode::TransitionImageToGeneral(VkCommandBuffer cmdBuffer, VkImage image) {
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
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
}

void ComputeDispatchNode::BindComputePipeline(VkCommandBuffer cmdBuffer, VkPipeline pipeline, VkPipelineLayout layout, VkDescriptorSet descriptorSet) {
    // Bind compute pipeline
    vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);

    // Bind descriptor set from DescriptorSetNode
    vkCmdBindDescriptorSets(
        cmdBuffer,
        VK_PIPELINE_BIND_POINT_COMPUTE,
        layout,
        0,
        1,
        &descriptorSet,
        0,
        nullptr
    );
}

void ComputeDispatchNode::SetPushConstants(Context& ctx, VkCommandBuffer cmdBuffer, VkPipelineLayout layout, const void* pushConstantData) {
    // Check for push constant data from PushConstantGathererNode
    std::vector<uint8_t> pushConstantDataVec = ctx.In(ComputeDispatchNodeConfig::PUSH_CONSTANT_DATA);
    std::vector<VkPushConstantRange> pushConstantRanges = ctx.In(ComputeDispatchNodeConfig::PUSH_CONSTANT_RANGES);

    // Use gathered push constants if available
    if (!pushConstantDataVec.empty() && !pushConstantRanges.empty()) {
        // Apply each push constant range
        for (const auto& range : pushConstantRanges) {
            if (range.offset + range.size <= pushConstantDataVec.size()) {
                vkCmdPushConstants(
                    cmdBuffer,
                    layout,
                    range.stageFlags,
                    range.offset,
                    range.size,
                    pushConstantDataVec.data() + range.offset
                );

                static int pcLogCount = 0;
                if (pcLogCount++ < 3) {
                    NODE_LOG_INFO("[ComputeDispatchNode] Setting gathered push constants: offset=" +
                                  std::to_string(range.offset) + ", size=" + std::to_string(range.size));
                }
            }
        }
    }
    // Fall back to legacy push constant data if no gatherer connected
    else if (pushConstantData != nullptr) {
        // Get shader bundle to check for push constants
        auto shaderBundle = ctx.In(ComputeDispatchNodeConfig::SHADER_DATA_BUNDLE);

        if (shaderBundle && shaderBundle->reflectionData &&
            !shaderBundle->reflectionData->pushConstants.empty()) {

            // Get first push constant range (we assume single range for now)
            const auto& pc = shaderBundle->reflectionData->pushConstants[0];

            vkCmdPushConstants(
                cmdBuffer,
                layout,
                VK_SHADER_STAGE_COMPUTE_BIT,
                pc.offset,
                pc.size,
                pushConstantData
            );

            static int pcLogCount = 0;
            if (pcLogCount++ < 3) {
                NODE_LOG_INFO("[ComputeDispatchNode] Setting legacy push constants: offset=" +
                              std::to_string(pc.offset) + ", size=" + std::to_string(pc.size));
            }
        }
    }
}

void ComputeDispatchNode::TransitionImageToPresent(VkCommandBuffer cmdBuffer, VkImage image) {
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = 0;
    barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    vkCmdPipelineBarrier(
        cmdBuffer,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
        0,
        0, nullptr,
        0, nullptr,
        1, &barrier
    );
}

// ============================================================================
// CLEANUP
// ============================================================================

void ComputeDispatchNode::CleanupImpl(TypedCleanupContext& ctx) {
    NODE_LOG_INFO("[ComputeDispatchNode::CleanupImpl] Cleaning up resources");

    // shared_ptr handles cleanup automatically:
    // - Node drops reference when perfLogger_ destroyed
    // - Parent (nodeLogger) keeps it alive until log extraction
    // - No manual RemoveChild needed
    perfLogger_.reset();
    gpuPerfLogger_.reset();

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

        // Reset command pool to avoid using stale handle during recompilation
        commandPool = VK_NULL_HANDLE;
    }

    NODE_LOG_INFO("[ComputeDispatchNode::CleanupImpl] Cleanup complete");
}

} // namespace Vixen::RenderGraph
