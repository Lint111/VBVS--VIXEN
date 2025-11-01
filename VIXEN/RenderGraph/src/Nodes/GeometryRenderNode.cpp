#include "Nodes/GeometryRenderNode.h"
#include "VulkanResources/VulkanDevice.h"
#include "VulkanSwapChain.h"
#include <cstring>
#include "Core/NodeLogging.h"

namespace Vixen::RenderGraph {

// ====== GeometryRenderNodeType ======

GeometryRenderNodeType::GeometryRenderNodeType(const std::string& typeName) : NodeType(typeName) {
    pipelineType = PipelineType::Graphics;
    requiredCapabilities = DeviceCapability::Graphics;
    supportsInstancing = true;
    maxInstances = 0; // Unlimited

    // Populate schemas from Config
    GeometryRenderNodeConfig config;
    inputSchema = config.GetInputVector();
    outputSchema = config.GetOutputVector();

    // Workload metrics
    workloadMetrics.estimatedMemoryFootprint = 1024; // Command recording
    workloadMetrics.estimatedComputeCost = 1.0f; // Actual rendering work
    workloadMetrics.estimatedBandwidthCost = 1.0f;
    workloadMetrics.canRunInParallel = false; // Command recording is sequential per queue
}

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

GeometryRenderNode::~GeometryRenderNode() {
    Cleanup();
}

void GeometryRenderNode::SetupImpl() {
    // Get device and command pool from inputs
    vulkanDevice = In(GeometryRenderNodeConfig::VULKAN_DEVICE);
    if (!vulkanDevice) {
        throw std::runtime_error("GeometryRenderNode: VulkanDevice input is null");
    }

    commandPool = In(GeometryRenderNodeConfig::COMMAND_POOL);
    if (commandPool == VK_NULL_HANDLE) {
        throw std::runtime_error("GeometryRenderNode: CommandPool input is null");
    }
}

void GeometryRenderNode::CompileImpl() {
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
    SwapChainPublicVariables* swapchainInfo = In(GeometryRenderNodeConfig::SWAPCHAIN_INFO);
    if (!swapchainInfo) {
        throw std::runtime_error("GeometryRenderNode: SwapChain info is null during Compile");
    }

    uint32_t imageCount = swapchainInfo->swapChainImageCount;
    std::cout << "[GeometryRenderNode::Compile] Swapchain has " << imageCount << " images, allocating command buffers" << std::endl;
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
}

void GeometryRenderNode::ExecuteImpl() {
    // Get current image index from SwapChainNode
    uint32_t imageIndex = In(GeometryRenderNodeConfig::IMAGE_INDEX, NodeInstance::SlotRole::ExecuteOnly);

    // Phase 0.5: Get current frame-in-flight index from FrameSyncNode
    uint32_t currentFrameIndex = In(GeometryRenderNodeConfig::CURRENT_FRAME_INDEX, NodeInstance::SlotRole::ExecuteOnly);

    // Phase 0.5: Get semaphore arrays from FrameSyncNode
    const VkSemaphore* imageAvailableSemaphores = In(GeometryRenderNodeConfig::IMAGE_AVAILABLE_SEMAPHORES_ARRAY, NodeInstance::SlotRole::ExecuteOnly);
    const VkSemaphore* renderCompleteSemaphores = In(GeometryRenderNodeConfig::RENDER_COMPLETE_SEMAPHORES_ARRAY, NodeInstance::SlotRole::ExecuteOnly);
    VkFence inFlightFence = In(GeometryRenderNodeConfig::IN_FLIGHT_FENCE, NodeInstance::SlotRole::ExecuteOnly);

    // Phase 0.5: CRITICAL - Index semaphores correctly:
    // - imageAvailable: Indexed by FRAME index (per-flight) - matches SwapChainNode's acquire semaphore
    // - renderComplete: Indexed by IMAGE index (per-image) - for presentation
    VkSemaphore imageAvailableSemaphore = imageAvailableSemaphores[currentFrameIndex];
    VkSemaphore renderCompleteSemaphore = renderCompleteSemaphores[imageIndex];

    // Phase 0.4: Reset fence before submitting (fence was already waited on by FrameSyncNode)
    vkResetFences(vulkanDevice->device, 1, &inFlightFence);

    // Guard against invalid image index (swapchain out of date)
    if (imageIndex == UINT32_MAX || imageIndex >= commandBuffers.size()) {
        NODE_LOG_WARNING("GeometryRenderNode: Invalid image index - skipping frame");
        return;
    }

    // Phase 0.3: Detect if inputs changed (mark all command buffers dirty if so)
    VkRenderPass currentRenderPass = In(GeometryRenderNodeConfig::RENDER_PASS, NodeInstance::SlotRole::ExecuteOnly);
    VkPipeline currentPipeline = In(GeometryRenderNodeConfig::PIPELINE, NodeInstance::SlotRole::ExecuteOnly);
    VkBuffer currentVertexBuffer = In(GeometryRenderNodeConfig::VERTEX_BUFFER, NodeInstance::SlotRole::ExecuteOnly);
    std::vector<VkDescriptorSet> descriptorSets = In(GeometryRenderNodeConfig::DESCRIPTOR_SETS, NodeInstance::SlotRole::ExecuteOnly);
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
    VkCommandBuffer cmdBuffer = commandBuffers.GetValue(imageIndex);
    if (commandBuffers.IsDirty(imageIndex)) {
        RecordDrawCommands(cmdBuffer, imageIndex);
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
    VkResult result = vkQueueSubmit(vulkanDevice->queue, 1, &submitInfo, inFlightFence);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("GeometryRenderNode: Failed to submit command buffer");
    }

    // Output the same render complete semaphore for PresentNode (pass-through)
    Out(GeometryRenderNodeConfig::RENDER_COMPLETE_SEMAPHORE, renderCompleteSemaphore);
}

void GeometryRenderNode::CleanupImpl() {
    // Free command buffers
    if (!commandBuffers.empty() && commandPool != VK_NULL_HANDLE && vulkanDevice) {
        // Extract raw command buffer handles for vkFreeCommandBuffers
        std::vector<VkCommandBuffer> rawHandles;
        rawHandles.reserve(commandBuffers.size());
        for (size_t i = 0; i < commandBuffers.size(); i++) {
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

    // Phase 0.2: Semaphores now managed by FrameSyncNode - no cleanup needed here
}

void GeometryRenderNode::RecordDrawCommands(VkCommandBuffer cmdBuffer, uint32_t framebufferIndex) {
    // Begin command buffer recording
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = 0;
    beginInfo.pInheritanceInfo = nullptr;
    
    VkResult result = vkBeginCommandBuffer(cmdBuffer, &beginInfo);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("GeometryRenderNode: Failed to begin command buffer");
    }
    
    // Get inputs via typed config API
    VkRenderPass renderPass = In(GeometryRenderNodeConfig::RENDER_PASS, NodeInstance::SlotRole::ExecuteOnly);
    std::vector<VkFramebuffer> framebuffers = In(GeometryRenderNodeConfig::FRAMEBUFFERS, NodeInstance::SlotRole::ExecuteOnly);
    if(framebufferIndex >= framebuffers.size()) {
        std::string errorMsg = "Framebuffer index " + std::to_string(framebufferIndex) + " out of bounds (size " + std::to_string(framebuffers.size()) + ")";
        NODE_LOG_ERROR(errorMsg);
        throw std::runtime_error(errorMsg);
    }

    VkFramebuffer currentFramebuffer = framebuffers[framebufferIndex];


    VkPipeline pipeline = In(GeometryRenderNodeConfig::PIPELINE, NodeInstance::SlotRole::ExecuteOnly);
    VkPipelineLayout pipelineLayout = In(GeometryRenderNodeConfig::PIPELINE_LAYOUT, NodeInstance::SlotRole::ExecuteOnly);
    VkBuffer vertexBuffer = In(GeometryRenderNodeConfig::VERTEX_BUFFER, NodeInstance::SlotRole::ExecuteOnly);
    SwapChainPublicVariables* swapchainInfo = In(GeometryRenderNodeConfig::SWAPCHAIN_INFO, NodeInstance::SlotRole::ExecuteOnly);
    

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
    if (swapchainInfo == nullptr) {
        std::string errorMsg = "GeometryRenderNode: SwapChain info is null";
        NODE_LOG_ERROR(errorMsg);
        throw std::runtime_error(errorMsg);
    }
    if (vertexBuffer == VK_NULL_HANDLE) {
        std::string errorMsg = "GeometryRenderNode: Vertex buffer is VK_NULL_HANDLE";
        NODE_LOG_ERROR(errorMsg);
        throw std::runtime_error(errorMsg);
    }
    if (pipelineLayout == VK_NULL_HANDLE) {
        std::string errorMsg = "GeometryRenderNode: Pipeline layout is VK_NULL_HANDLE";
        NODE_LOG_ERROR(errorMsg);
        throw std::runtime_error(errorMsg);
    }

    // Extract viewport and scissor from swapchain info
    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(swapchainInfo->Extent.width);
    viewport.height = static_cast<float>(swapchainInfo->Extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = swapchainInfo->Extent;

    uint32_t renderWidth = swapchainInfo->Extent.width;
    uint32_t renderHeight = swapchainInfo->Extent.height;

    // Setup clear values
    VkClearValue clearValues[2];
    clearValues[0] = clearColor;
    clearValues[1] = clearDepthStencil;

    // Begin render pass
    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.pNext = nullptr;
    renderPassInfo.renderPass = renderPass;
    renderPassInfo.framebuffer = currentFramebuffer;
    renderPassInfo.renderArea.offset.x = 0;
    renderPassInfo.renderArea.offset.y = 0;
    renderPassInfo.renderArea.extent.width = renderWidth;
    renderPassInfo.renderArea.extent.height = renderHeight;
    renderPassInfo.clearValueCount = 2;
    renderPassInfo.pClearValues = clearValues;

    vkCmdBeginRenderPass(
        cmdBuffer,
        &renderPassInfo,
        VK_SUBPASS_CONTENTS_INLINE
    );

    // Bind pipeline
    vkCmdBindPipeline(
        cmdBuffer,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        pipeline
    );

    // Bind descriptor sets
    std::vector<VkDescriptorSet> descriptorSets = In(GeometryRenderNodeConfig::DESCRIPTOR_SETS, NodeInstance::SlotRole::ExecuteOnly);

    if(descriptorSets.size() == 0) {
        std::string errorMsg = "[GeometryRenderNode] WARNING: No descriptor sets provided, rendering may fail!";
        NODE_LOG_WARNING(errorMsg);
        std::cout << errorMsg << std::endl;
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
        std::string errorMsg = "[GeometryRenderNode] WARNING: Descriptor set is NULL, rendering may fail!";
        NODE_LOG_WARNING(errorMsg);
        std::cout << errorMsg << std::endl;
    }
    

    // Bind vertex buffer
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(
        cmdBuffer,
        0,
        1,
        &vertexBuffer,
        offsets
    );

    // Bind index buffer (if using indexed rendering)
    if (useIndexBuffer) {
    VkBuffer indexBuffer = In(GeometryRenderNodeConfig::INDEX_BUFFER, NodeInstance::SlotRole::ExecuteOnly);
        if (indexBuffer != VK_NULL_HANDLE) {
            vkCmdBindIndexBuffer(
                cmdBuffer,
                indexBuffer,
                0,
                VK_INDEX_TYPE_UINT32
            );
        }
    }

    // Set viewport
    vkCmdSetViewport(cmdBuffer, 0, 1, &viewport);

    // Set scissor
    vkCmdSetScissor(cmdBuffer, 0, 1, &scissor);

    // Draw
    if (useIndexBuffer) {
        vkCmdDrawIndexed(
            cmdBuffer,
            indexCount,
            instanceCount,
            0,
            0,
            firstInstance
        );
    } else {
        vkCmdDraw(
            cmdBuffer,
            vertexCount,
            instanceCount,
            firstVertex,
            firstInstance
        );
    }

    // End render pass
    vkCmdEndRenderPass(cmdBuffer);
    
    // End command buffer recording
    result = vkEndCommandBuffer(cmdBuffer);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("GeometryRenderNode: Failed to end command buffer");
    }
}

} // namespace Vixen::RenderGraph
