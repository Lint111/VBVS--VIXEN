#include "Nodes/GeometryRenderNode.h"
#include "VulkanResources/VulkanDevice.h"
#include <cstring>

namespace Vixen::RenderGraph {

// ====== GeometryRenderNodeType ======

GeometryRenderNodeType::GeometryRenderNodeType(const std::string& typeName) : NodeType(typeName) {
    pipelineType = PipelineType::Graphics;
    requiredCapabilities = DeviceCapability::Graphics;
    supportsInstancing = true;
    maxInstances = 0; // Unlimited

    // Inputs are opaque references (set via Set methods)

    // No outputs - this node records commands

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

void GeometryRenderNode::Setup() {
    // No setup needed for Phase 1 minimal (stub)
}

void GeometryRenderNode::Compile() {
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

    // TODO Phase 1: Validation disabled - nodes are stubs
    // Validate inputs
    // if (renderPass == VK_NULL_HANDLE) {
    //     throw std::runtime_error("GeometryRenderNode: render pass not set");
    // }
    // ... rest of validation commented out for Phase 1
}

void GeometryRenderNode::Execute(VkCommandBuffer commandBuffer) {
    // Execute is typically used for per-frame command recording
    // For now, this node records commands via RecordDrawCommands()
    // which is called externally with the appropriate framebuffer index
}

void GeometryRenderNode::Cleanup() {
    // No resources to clean up - this node only records commands
}

void GeometryRenderNode::RecordDrawCommands(VkCommandBuffer cmdBuffer, uint32_t framebufferIndex) {
    // Get inputs via typed config API
    VkRenderPass renderPass = In(GeometryRenderNodeConfig::RENDER_PASS);
    VkFramebuffer framebuffer = In(GeometryRenderNodeConfig::FRAMEBUFFERS, framebufferIndex);
    VkPipeline pipeline = In(GeometryRenderNodeConfig::PIPELINE);
    VkPipelineLayout pipelineLayout = In(GeometryRenderNodeConfig::PIPELINE_LAYOUT);
    VkBuffer vertexBuffer = In(GeometryRenderNodeConfig::VERTEX_BUFFER);
    const VkViewport* viewport = In(GeometryRenderNodeConfig::VIEWPORT);
    const VkRect2D* scissor = In(GeometryRenderNodeConfig::SCISSOR);
    uint32_t renderWidth = In(GeometryRenderNodeConfig::RENDER_WIDTH);
    uint32_t renderHeight = In(GeometryRenderNodeConfig::RENDER_HEIGHT);

    // Setup clear values
    VkClearValue clearValues[2];
    clearValues[0] = clearColor;
    clearValues[1] = clearDepthStencil;

    // Begin render pass
    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.pNext = nullptr;
    renderPassInfo.renderPass = renderPass;
    renderPassInfo.framebuffer = framebuffer;
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

    // Bind descriptor sets (get count from input array)
    // TODO: Query actual descriptor set count from inputs
    // For now, assume we have descriptor sets
    {
        // Get first descriptor set to check if available
        VkDescriptorSet firstDescSet = In(GeometryRenderNodeConfig::DESCRIPTOR_SETS, 0);
        if (firstDescSet != VK_NULL_HANDLE && pipelineLayout != VK_NULL_HANDLE) {
            // TODO: Support multiple descriptor sets properly
            vkCmdBindDescriptorSets(
                cmdBuffer,
                VK_PIPELINE_BIND_POINT_GRAPHICS,
                pipelineLayout,
                0,
                1, // For now, bind just the first one
                &firstDescSet,
                0,
                nullptr
            );
        }
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
        VkBuffer indexBuffer = In(GeometryRenderNodeConfig::INDEX_BUFFER);
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
    vkCmdSetViewport(cmdBuffer, 0, 1, viewport);

    // Set scissor
    vkCmdSetScissor(cmdBuffer, 0, 1, scissor);

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
    
    // Store command buffer in output (if needed for graph connections)
    // Out(GeometryRenderNodeConfig::COMMAND_BUFFERS, cmdBuffer, framebufferIndex);
}

} // namespace Vixen::RenderGraph
