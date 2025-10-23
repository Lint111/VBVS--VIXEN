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
    : NodeInstance(instanceName, nodeType)
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
    vertexCount = GetParameterValue<uint32_t>("vertexCount", 0);
    instanceCount = GetParameterValue<uint32_t>("instanceCount", 1);
    firstVertex = GetParameterValue<uint32_t>("firstVertex", 0);
    firstInstance = GetParameterValue<uint32_t>("firstInstance", 0);
    useIndexBuffer = GetParameterValue<bool>("useIndexBuffer", false);
    indexCount = GetParameterValue<uint32_t>("indexCount", 0);

    // Get clear color
    clearColor.color.float32[0] = GetParameterValue<float>("clearColorR", 0.0f);
    clearColor.color.float32[1] = GetParameterValue<float>("clearColorG", 0.0f);
    clearColor.color.float32[2] = GetParameterValue<float>("clearColorB", 0.0f);
    clearColor.color.float32[3] = GetParameterValue<float>("clearColorA", 1.0f);

    // Get clear depth/stencil
    clearDepthStencil.depthStencil.depth = GetParameterValue<float>("clearDepth", 1.0f);
    clearDepthStencil.depthStencil.stencil = GetParameterValue<uint32_t>("clearStencil", 0);

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
    if (framebufferIndex >= framebuffers.size()) {
        throw std::runtime_error("GeometryRenderNode: invalid framebuffer index");
    }

    // Setup clear values
    VkClearValue clearValues[2];
    clearValues[0] = clearColor;
    clearValues[1] = clearDepthStencil;

    // Begin render pass
    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.pNext = nullptr;
    renderPassInfo.renderPass = renderPass;
    renderPassInfo.framebuffer = framebuffers[framebufferIndex];
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

    // Bind descriptor sets (if any)
    if (!descriptorSets.empty() && pipelineLayout != VK_NULL_HANDLE) {
        vkCmdBindDescriptorSets(
            cmdBuffer,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            pipelineLayout,
            0,
            static_cast<uint32_t>(descriptorSets.size()),
            descriptorSets.data(),
            0,
            nullptr
        );
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
        vkCmdBindIndexBuffer(
            cmdBuffer,
            indexBuffer,
            0,
            VK_INDEX_TYPE_UINT32
        );
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
}

// Setter methods for input references
void GeometryRenderNode::SetRenderPass(VkRenderPass pass) {
    renderPass = pass;
}

void GeometryRenderNode::SetFramebuffers(const std::vector<VkFramebuffer>& fbs) {
    framebuffers = fbs;
}

void GeometryRenderNode::SetPipeline(VkPipeline pipe, VkPipelineLayout layout) {
    pipeline = pipe;
    pipelineLayout = layout;
}

void GeometryRenderNode::SetDescriptorSets(const std::vector<VkDescriptorSet>& sets) {
    descriptorSets = sets;
}

void GeometryRenderNode::SetVertexBuffer(VkBuffer buffer) {
    vertexBuffer = buffer;
}

void GeometryRenderNode::SetIndexBuffer(VkBuffer buffer) {
    indexBuffer = buffer;
}

void GeometryRenderNode::SetViewport(const VkViewport& vp) {
    viewport = vp;
}

void GeometryRenderNode::SetScissor(const VkRect2D& sc) {
    scissor = sc;
}

void GeometryRenderNode::SetRenderArea(uint32_t width, uint32_t height) {
    renderWidth = width;
    renderHeight = height;
}

} // namespace Vixen::RenderGraph
