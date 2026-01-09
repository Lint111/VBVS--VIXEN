#include "Nodes/GeometryRenderNode.h"
#include "VulkanDevice.h"
#include "VulkanSwapChain.h"
#include <cstring>
#include "Core/NodeLogging.h"
#include "Core/TaskProfiles/SimpleTaskProfile.h"  // Sprint 6.5: Profile integration

namespace Vixen::RenderGraph {

// ====== GeometryRenderNodeType ======

std::unique_ptr<NodeInstance> GeometryRenderNodeType::CreateInstance(
    const std::string& instanceName
) const {
    return std::make_unique<GeometryRenderNode>(
        instanceName,
        const_cast<GeometryRenderNodeType*>(this)
    );
}

// ====== GeometryRenderNode ======

GeometryRenderNode::GeometryRenderNode(
    const std::string& instanceName,
    NodeType* nodeType
)
    : TypedNode<GeometryRenderNodeConfig>(instanceName, nodeType)
{
    // Initialize clear values
    clearColor.color.float32[0] = 0.0f;
    clearColor.color.float32[1] = 0.0f;
    clearColor.color.float32[2] = 0.0f;
    clearColor.color.float32[3] = 1.0f;

    clearDepthStencil.depthStencil.depth = 1.0f;
    clearDepthStencil.depthStencil.stencil = 0;
}

void GeometryRenderNode::SetupImpl(TypedSetupContext& ctx) {
    // Graph-scope initialization only (no input access)
    NODE_LOG_DEBUG("GeometryRenderNode: Setup (graph-scope initialization)");
}

void GeometryRenderNode::CompileImpl(TypedCompileContext& ctx) {
    // Access device and command pool inputs (compile-time dependencies)
    VulkanDevice* vulkanDevice = ctx.In(GeometryRenderNodeConfig::VULKAN_DEVICE);
    if (!vulkanDevice) {
        throw std::runtime_error("GeometryRenderNode: VulkanDevice input is null");
    }
    SetDevice(vulkanDevice);

    commandPool = ctx.In(GeometryRenderNodeConfig::COMMAND_POOL);
    if (commandPool == VK_NULL_HANDLE) {
        throw std::runtime_error("GeometryRenderNode: CommandPool input is null");
    }

    // Get parameters
    vertexCount = GetParameterValue<uint32_t>(GeometryRenderNodeConfig::VERTEX_COUNT, 0);
    instanceCount = GetParameterValue<uint32_t>(GeometryRenderNodeConfig::INSTANCE_COUNT, 1);
    firstVertex = GetParameterValue<uint32_t>(GeometryRenderNodeConfig::FIRST_VERTEX, 0);
    firstInstance = GetParameterValue<uint32_t>(GeometryRenderNodeConfig::FIRST_INSTANCE, 0);
    useIndexBuffer = GetParameterValue<bool>(GeometryRenderNodeConfig::USE_INDEX_BUFFER, false);
    indexCount = GetParameterValue<uint32_t>(GeometryRenderNodeConfig::INDEX_COUNT, 0);

    // Get clear color
    clearColor.color.float32[0] = GetParameterValue<float>(GeometryRenderNodeConfig::CLEAR_COLOR_R, 0.0f);
    clearColor.color.float32[1] = GetParameterValue<float>(GeometryRenderNodeConfig::CLEAR_COLOR_G, 0.0f);
    clearColor.color.float32[2] = GetParameterValue<float>(GeometryRenderNodeConfig::CLEAR_COLOR_B, 0.0f);
    clearColor.color.float32[3] = GetParameterValue<float>(GeometryRenderNodeConfig::CLEAR_COLOR_A, 1.0f);

    // Get clear depth/stencil
    clearDepthStencil.depthStencil.depth = GetParameterValue<float>(GeometryRenderNodeConfig::CLEAR_DEPTH, 1.0f);
    clearDepthStencil.depthStencil.stencil = GetParameterValue<uint32_t>(GeometryRenderNodeConfig::CLEAR_STENCIL, 0);

    // Allocate command buffers (one per framebuffer/swapchain image)
    // Get actual image count from swapchain
    SwapChainPublicVariables* swapchainInfo = ctx.In(GeometryRenderNodeConfig::SWAPCHAIN_INFO);
    if (!swapchainInfo) {
        throw std::runtime_error("GeometryRenderNode: SwapChain info is null during Compile");
    }

    uint32_t imageCount = swapchainInfo->swapChainImageCount;
    NODE_LOG_INFO("[GeometryRenderNode::Compile] Swapchain has " + std::to_string(imageCount) + " images, allocating command buffers");
    commandBuffers.resize(imageCount);

    // Allocate raw command buffers
    std::vector<VkCommandBuffer> rawCommandBuffers(imageCount);
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = imageCount;

    VkResult result = vkAllocateCommandBuffers(vulkanDevice->device, &allocInfo, rawCommandBuffers.data());
    if (result != VK_SUCCESS) {
        throw std::runtime_error("GeometryRenderNode: Failed to allocate command buffers");
    }

    // Phase 0.3: Store in stateful container (all start as Dirty - need initial recording)
    for (uint32_t i = 0; i < imageCount; i++) {
        commandBuffers[i] = rawCommandBuffers[i];
        commandBuffers.MarkDirty(i);
    }

    // Phase 0.2: Semaphores now managed by FrameSyncNode (per-flight pattern)
    // No need to create per-swapchain-image semaphores anymore

    // Create GPU performance logger using centralized GPUQueryManager from VulkanDevice
    // Sprint 6.3 Phase 0: All nodes share the same query manager to prevent slot conflicts
    auto* queryMgrPtr = static_cast<GPUQueryManager*>(vulkanDevice->GetQueryManager());
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
            NODE_LOG_INFO("[GeometryRenderNode] GPU performance timing enabled");
        } else {
            NODE_LOG_WARNING("[GeometryRenderNode] GPU timing not supported on this device");
        }
    } else {
        NODE_LOG_WARNING("[GeometryRenderNode] GPUQueryManager not available from VulkanDevice");
    }

    // Sprint 6.5: Register GPU task profile for cost estimation and learning
    std::string profileId = GetInstanceName() + "_gpu_render";
    gpuProfile_ = GetOrCreateProfile<SimpleTaskProfile>(profileId, profileId, "graphics");
    if (gpuProfile_) {
        RegisterPhaseProfile(VirtualTaskPhase::Execute, gpuProfile_);
        NODE_LOG_INFO("[GeometryRenderNode] Registered GPU profile: " + profileId);
    }
}

void GeometryRenderNode::ExecuteImpl(TypedExecuteContext& ctx) {
    // Get current image index from SwapChainNode
    uint32_t imageIndex = ctx.In(GeometryRenderNodeConfig::IMAGE_INDEX);

    // Phase 0.5: Get current frame-in-flight index from FrameSyncNode
    uint32_t currentFrameIndex = ctx.In(GeometryRenderNodeConfig::CURRENT_FRAME_INDEX);

    // Phase 0.5: Get semaphore arrays from FrameSyncNode
    const std::vector<VkSemaphore>& imageAvailableSemaphores = ctx.In(GeometryRenderNodeConfig::IMAGE_AVAILABLE_SEMAPHORES_ARRAY);
    const std::vector<VkSemaphore>& renderCompleteSemaphores = ctx.In(GeometryRenderNodeConfig::RENDER_COMPLETE_SEMAPHORES_ARRAY);
    VkFence inFlightFence = ctx.In(GeometryRenderNodeConfig::IN_FLIGHT_FENCE);

    // Phase 0.6: CORRECT per Vulkan guide - Two-tier indexing
    // https://docs.vulkan.org/guide/latest/swapchain_semaphore_reuse.html
    //
    // - imageAvailable: Indexed by FRAME index (per-flight) - tracks CPU-GPU pacing
    // - renderComplete: Indexed by IMAGE index (per-image) - tracks presentation engine usage
    //
    // This prevents "semaphore still in use by swapchain" errors because each image
    // gets its own renderComplete semaphore that won't be reused until that specific
    // image is acquired again.
    VkSemaphore imageAvailableSemaphore = imageAvailableSemaphores[currentFrameIndex];
    VkSemaphore renderCompleteSemaphore = renderCompleteSemaphores[imageIndex];

    static int logCounter = 0;
    if (logCounter++ < 20) {
        NODE_LOG_INFO("Frame " + std::to_string(currentFrameIndex) + ", Image " + std::to_string(imageIndex) +
                      ": using imageAvailable[" + std::to_string(currentFrameIndex) + "], renderComplete[" + std::to_string(imageIndex) + "]");
    }

    // Phase 0.4: Reset fence before submitting (fence was already waited on by FrameSyncNode)
    vkResetFences(device->device, 1, &inFlightFence);

    // Collect GPU performance results for this frame-in-flight (after fence wait)
    if (gpuPerfLogger_) {
        gpuPerfLogger_->CollectResults(currentFrameIndex);

        // Sprint 6.5: Feed GPU timing to task profile for cost learning
        if (gpuProfile_) {
            auto sample = gpuProfile_->Sample();
            float gpuTimeMs = gpuPerfLogger_->GetLastDispatchMs();
            if (gpuTimeMs > 0.0f) {
                uint64_t gpuTimeNs = static_cast<uint64_t>(gpuTimeMs * 1'000'000.0f);
                sample.Finalize(gpuTimeNs);
            } else {
                sample.Cancel();  // No valid measurement
            }
        }
    }

    // Guard against invalid image index (swapchain out of date)
    if (imageIndex == UINT32_MAX || imageIndex >= commandBuffers.size()) {
        NODE_LOG_WARNING("GeometryRenderNode: Invalid image index - skipping frame");
        return;
    }

    // Phase 0.3: Detect if inputs changed (mark all command buffers dirty if so)
    VkRenderPass currentRenderPass = ctx.In(GeometryRenderNodeConfig::RENDER_PASS);
    VkPipeline currentPipeline = ctx.In(GeometryRenderNodeConfig::PIPELINE);
    VkBuffer currentVertexBuffer = ctx.In(GeometryRenderNodeConfig::VERTEX_BUFFER);
    std::vector<VkDescriptorSet> descriptorSets = ctx.In(GeometryRenderNodeConfig::DESCRIPTOR_SETS);
    VkDescriptorSet currentDescriptorSet = (descriptorSets.size() > 0) ? descriptorSets[0] : VK_NULL_HANDLE;

    if (currentRenderPass != lastRenderPass ||
        currentPipeline != lastPipeline ||
        currentVertexBuffer != lastVertexBuffer ||
        currentDescriptorSet != lastDescriptorSet) {
        // Inputs changed - mark all command buffers dirty
        commandBuffers.MarkAllDirty();

        lastRenderPass = currentRenderPass;
        lastPipeline = currentPipeline;
        lastVertexBuffer = currentVertexBuffer;
        lastDescriptorSet = currentDescriptorSet;
    }

    // Phase 0.3: Only re-record if dirty
    // Note: When GPU performance logging is enabled, we always re-record to capture timestamps
    VkCommandBuffer cmdBuffer = commandBuffers.GetValue(imageIndex);
    bool needsRecording = commandBuffers.IsDirty(imageIndex) ||
                          (gpuPerfLogger_ && gpuPerfLogger_->IsEnabled());
    if (needsRecording) {
        RecordDrawCommands(ctx, cmdBuffer, imageIndex, currentFrameIndex);
        commandBuffers.MarkReady(imageIndex);
    }

    // Submit command buffer to graphics queue
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

    // Wait for image to be available before writing to it
    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = &imageAvailableSemaphore;
    submitInfo.pWaitDstStageMask = &waitStage;

    // Submit command buffer
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmdBuffer;

    // Phase 0.2: Signal the per-flight render complete semaphore from FrameSyncNode
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = &renderCompleteSemaphore;

    // Phase 0.2: Signal fence when GPU completes this frame (CPU-GPU sync)
    VkResult result = vkQueueSubmit(device->queue, 1, &submitInfo, inFlightFence);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("GeometryRenderNode: Failed to submit command buffer");
    }

    // Output the same render complete semaphore for PresentNode (pass-through)
    ctx.Out(GeometryRenderNodeConfig::RENDER_COMPLETE_SEMAPHORE, renderCompleteSemaphore);
}

void GeometryRenderNode::CleanupImpl(TypedCleanupContext& ctx) {
    // GPU resources (QueryPools) will be automatically released by GPUQueryManager destructor

    // Free command buffers
    if (!commandBuffers.empty() && commandPool != VK_NULL_HANDLE && device) {
        // Extract raw command buffer handles for vkFreeCommandBuffers
        std::vector<VkCommandBuffer> rawHandles;
        rawHandles.reserve(commandBuffers.size());
        for (size_t i = 0; i < commandBuffers.size(); i++) {
            rawHandles.push_back(commandBuffers.GetValue(i));
        }

        vkFreeCommandBuffers(
            device->device,
            commandPool,
            static_cast<uint32_t>(rawHandles.size()),
            rawHandles.data()
        );
        commandBuffers.clear();
    }

    // Phase 0.2: Semaphores now managed by FrameSyncNode - no cleanup needed here
}

void GeometryRenderNode::RecordDrawCommands(Context& ctx, VkCommandBuffer cmdBuffer, uint32_t framebufferIndex, uint32_t frameIndex) {
    // Begin command buffer
    BeginCommandBuffer(cmdBuffer);

    // Get inputs
    VkRenderPass renderPass = ctx.In(GeometryRenderNodeConfig::RENDER_PASS);
    VkPipeline pipeline = ctx.In(GeometryRenderNodeConfig::PIPELINE);
    VkPipelineLayout pipelineLayout = ctx.In(GeometryRenderNodeConfig::PIPELINE_LAYOUT);
    VkBuffer vertexBuffer = ctx.In(GeometryRenderNodeConfig::VERTEX_BUFFER);
    SwapChainPublicVariables* swapchainInfo = ctx.In(GeometryRenderNodeConfig::SWAPCHAIN_INFO);
    std::vector<VkDescriptorSet> descriptorSets = ctx.In(GeometryRenderNodeConfig::DESCRIPTOR_SETS);

    // Validate inputs
    ValidateInputs(ctx, renderPass, pipeline, pipelineLayout, vertexBuffer, swapchainInfo);

    // Get framebuffer
    std::vector<VkFramebuffer> framebuffers = ctx.In(GeometryRenderNodeConfig::FRAMEBUFFERS);
    if (framebufferIndex >= framebuffers.size()) {
        std::string errorMsg = "Framebuffer index " + std::to_string(framebufferIndex) +
                               " out of bounds (size " + std::to_string(framebuffers.size()) + ")";
        NODE_LOG_ERROR(errorMsg);
        throw std::runtime_error(errorMsg);
    }
    VkFramebuffer currentFramebuffer = framebuffers[framebufferIndex];

    // Begin GPU timing frame (reset queries for this frame) - CRITICAL for timing to work
    if (gpuPerfLogger_) {
        gpuPerfLogger_->BeginFrame(cmdBuffer, frameIndex);
    }

    // Record GPU timing start (before render pass for accurate timing)
    if (gpuPerfLogger_) {
        gpuPerfLogger_->RecordDispatchStart(cmdBuffer, frameIndex);
    }

    // Begin render pass with clear
    BeginRenderPassWithClear(cmdBuffer, renderPass, currentFramebuffer,
                            swapchainInfo->Extent.width, swapchainInfo->Extent.height);

    // Bind pipeline and descriptors
    BindPipelineAndDescriptors(cmdBuffer, pipeline, pipelineLayout, descriptorSets);

    // Set push constants (camera data for ray marching)
    SetPushConstants(ctx, cmdBuffer, pipelineLayout);

    // Bind buffers
    BindVertexAndIndexBuffers(cmdBuffer, ctx, vertexBuffer);

    // Set viewport and scissor
    SetViewportAndScissor(cmdBuffer, swapchainInfo->Extent);

    // Draw
    RecordDrawCall(cmdBuffer, ctx);

    // End render pass
    vkCmdEndRenderPass(cmdBuffer);

    // Record GPU timing end (after render pass)
    if (gpuPerfLogger_) {
        gpuPerfLogger_->RecordDispatchEnd(cmdBuffer, frameIndex,
            swapchainInfo->Extent.width, swapchainInfo->Extent.height);
    }

    // End command buffer
    EndCommandBuffer(cmdBuffer);
}

void GeometryRenderNode::BeginCommandBuffer(VkCommandBuffer cmdBuffer) {
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = 0;
    beginInfo.pInheritanceInfo = nullptr;

    VkResult result = vkBeginCommandBuffer(cmdBuffer, &beginInfo);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("GeometryRenderNode: Failed to begin command buffer");
    }
}

void GeometryRenderNode::ValidateInputs(
    Context& ctx,
    VkRenderPass renderPass,
    VkPipeline pipeline,
    VkPipelineLayout pipelineLayout,
    VkBuffer vertexBuffer,
    SwapChainPublicVariables* swapchainInfo
) {
    if (renderPass == VK_NULL_HANDLE) {
        std::string errorMsg = "GeometryRenderNode: RenderPass is VK_NULL_HANDLE";
        NODE_LOG_ERROR(errorMsg);
        throw std::runtime_error(errorMsg);
    }
    if (pipeline == VK_NULL_HANDLE) {
        std::string errorMsg = "GeometryRenderNode: Pipeline is VK_NULL_HANDLE";
        NODE_LOG_ERROR(errorMsg);
        throw std::runtime_error(errorMsg);
    }
    if (pipelineLayout == VK_NULL_HANDLE) {
        std::string errorMsg = "GeometryRenderNode: Pipeline layout is VK_NULL_HANDLE";
        NODE_LOG_ERROR(errorMsg);
        throw std::runtime_error(errorMsg);
    }
    // Note: vertexBuffer can be VK_NULL_HANDLE for fullscreen passes (no vertex input)
    if (swapchainInfo == nullptr) {
        std::string errorMsg = "GeometryRenderNode: SwapChain info is null";
        NODE_LOG_ERROR(errorMsg);
        throw std::runtime_error(errorMsg);
    }
}

void GeometryRenderNode::BeginRenderPassWithClear(
    VkCommandBuffer cmdBuffer,
    VkRenderPass renderPass,
    VkFramebuffer framebuffer,
    uint32_t width,
    uint32_t height
) {
    VkClearValue clearValues[2];
    clearValues[0] = clearColor;
    clearValues[1] = clearDepthStencil;

    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.pNext = nullptr;
    renderPassInfo.renderPass = renderPass;
    renderPassInfo.framebuffer = framebuffer;
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = {width, height};
    renderPassInfo.clearValueCount = 2;
    renderPassInfo.pClearValues = clearValues;

    vkCmdBeginRenderPass(cmdBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
}

void GeometryRenderNode::BindPipelineAndDescriptors(
    VkCommandBuffer cmdBuffer,
    VkPipeline pipeline,
    VkPipelineLayout pipelineLayout,
    const std::vector<VkDescriptorSet>& descriptorSets
) {
    // Bind pipeline
    vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

    // Bind descriptor sets
    if (descriptorSets.size() == 0) {
        NODE_LOG_WARNING("[GeometryRenderNode] WARNING: No descriptor sets provided, rendering may fail!");
        return;
    }

    if (descriptorSets[0] != VK_NULL_HANDLE && pipelineLayout != VK_NULL_HANDLE) {
        vkCmdBindDescriptorSets(
            cmdBuffer,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            pipelineLayout,
            0,
            1,
            &descriptorSets[0],
            0,
            nullptr
        );
    } else {
        NODE_LOG_WARNING("[GeometryRenderNode] WARNING: Descriptor set is NULL, rendering may fail!");
    }
}

void GeometryRenderNode::SetPushConstants(Context& ctx, VkCommandBuffer cmdBuffer, VkPipelineLayout pipelineLayout) {
    // Get push constant data from PushConstantGathererNode
    std::vector<uint8_t> pushConstantData = ctx.In(GeometryRenderNodeConfig::PUSH_CONSTANT_DATA);
    std::vector<VkPushConstantRange> pushConstantRanges = ctx.In(GeometryRenderNodeConfig::PUSH_CONSTANT_RANGES);

    if (pushConstantData.empty() || pushConstantRanges.empty()) {
        // No push constants provided - this is OK for shaders that don't use them
        return;
    }

    // Apply each push constant range
    for (const auto& range : pushConstantRanges) {
        if (range.offset + range.size <= pushConstantData.size()) {
            vkCmdPushConstants(
                cmdBuffer,
                pipelineLayout,
                range.stageFlags,
                range.offset,
                range.size,
                pushConstantData.data() + range.offset
            );
            NODE_LOG_DEBUG("[GeometryRenderNode] Set push constants: offset=" + std::to_string(range.offset) +
                          ", size=" + std::to_string(range.size) + ", stages=" + std::to_string(range.stageFlags));
        } else {
            NODE_LOG_WARNING("[GeometryRenderNode] WARNING: Push constant range exceeds data size!");
        }
    }
}

void GeometryRenderNode::BindVertexAndIndexBuffers(
    VkCommandBuffer cmdBuffer,
    Context& ctx,
    VkBuffer vertexBuffer
) {
    // Bind vertex buffer (skip for fullscreen passes with no vertex input)
    if (vertexBuffer != VK_NULL_HANDLE) {
        VkDeviceSize offsets[] = {0};
        vkCmdBindVertexBuffers(cmdBuffer, 0, 1, &vertexBuffer, offsets);
    }

    // Bind index buffer if enabled
    if (useIndexBuffer) {
        VkBuffer indexBuffer = ctx.In(GeometryRenderNodeConfig::INDEX_BUFFER);
        if (indexBuffer != VK_NULL_HANDLE) {
            vkCmdBindIndexBuffer(cmdBuffer, indexBuffer, 0, VK_INDEX_TYPE_UINT32);
        }
    }
}

void GeometryRenderNode::SetViewportAndScissor(
    VkCommandBuffer cmdBuffer,
    const VkExtent2D& extent
) {
    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(extent.width);
    viewport.height = static_cast<float>(extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = extent;

    vkCmdSetViewport(cmdBuffer, 0, 1, &viewport);
    vkCmdSetScissor(cmdBuffer, 0, 1, &scissor);
}

void GeometryRenderNode::RecordDrawCall(VkCommandBuffer cmdBuffer, Context& ctx) {
    if (useIndexBuffer) {
        vkCmdDrawIndexed(cmdBuffer, indexCount, instanceCount, 0, 0, firstInstance);
    } else {
        vkCmdDraw(cmdBuffer, vertexCount, instanceCount, firstVertex, firstInstance);
    }
}

void GeometryRenderNode::EndCommandBuffer(VkCommandBuffer cmdBuffer) {
    VkResult result = vkEndCommandBuffer(cmdBuffer);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("GeometryRenderNode: Failed to end command buffer");
    }
}

} // namespace Vixen::RenderGraph
