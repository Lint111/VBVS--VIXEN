#include "Nodes/ProxyRasterStageNode.h"

#include "Core/NodeRegistration.h"
#include "Core/NodeLogging.h"
#include "Core/RenderGraph.h"
#include "IRenderTarget.h"
#include "VulkanDevice.h"

#include <algorithm>
#include <bit>
#include <limits>
#include <mutex>
#include <set>
#include <stdexcept>

namespace Vixen::RenderGraph {

namespace {
constexpr uint32_t kCommandBufferRingDepth = 4u;
constexpr uint32_t kProxyCubeVertexCount = 36u;
constexpr uint32_t kProxyCandidateLimit = 192u;

struct alignas(16) ProxyRasterPushConstants {
    glm::mat4 viewProj;
    glm::vec4 cameraPosFov;
    glm::vec4 cameraDirAspect;
    glm::vec4 cameraUpWidth;
    glm::vec4 cameraRightHeight;
};
static_assert(sizeof(ProxyRasterPushConstants) == 128,
              "B2 raster push block must fit Vulkan's guaranteed 128-byte range");
} // namespace

std::unique_ptr<NodeInstance> ProxyRasterStageNodeType::CreateInstance(
    const std::string& instanceName) const {
    return std::make_unique<ProxyRasterStageNode>(
        instanceName, const_cast<ProxyRasterStageNodeType*>(this));
}

ProxyRasterStageNode::ProxyRasterStageNode(const std::string& instanceName,
                                           NodeType* nodeType)
    : TypedNode<ProxyRasterStageNodeConfig>(instanceName, nodeType) {}

void ProxyRasterStageNode::CompileImpl(TypedCompileContext& ctx) {
    VulkanDevice* device = ctx.In(ProxyRasterStageNodeConfig::VULKAN_DEVICE);
    if (!device || device->device == VK_NULL_HANDLE) {
        throw std::runtime_error("[ProxyRasterStageNode] Vulkan device is null");
    }
    SetDevice(device);
    const bool forceComputeWriter = GetParameterValue<bool>(
        ProxyRasterStageNodeConfig::PARAM_FORCE_COMPUTE_WRITER, false);
    useFragmentWriter_ = !forceComputeWriter && device->HasCapability(
        "DeviceFeature:fragmentStoresAndAtomics");

    const VkPipeline selectedPipeline = useFragmentWriter_
        ? ctx.In(ProxyRasterStageNodeConfig::PIPELINE)
        : ctx.In(ProxyRasterStageNodeConfig::COMPUTE_PIPELINE);
    if (selectedPipeline == VK_NULL_HANDLE) {
        throw std::runtime_error(
            std::string("[ProxyRasterStageNode] selected ") +
            (useFragmentWriter_ ? "fragment" : "compute") +
            " writer pipeline is null");
    }
    NODE_LOG_INFO(std::string("B2 proxy interval writer: ") +
                  (useFragmentWriter_ ? "fragment-store" : "compute") +
                  (forceComputeWriter ? " (forced parity/capture path)" :
                                        " (capability-selected)"));
    commandPool_ = ctx.In(ProxyRasterStageNodeConfig::COMMAND_POOL);
    if (commandPool_ == VK_NULL_HANDLE) {
        throw std::runtime_error("[ProxyRasterStageNode] command pool is null");
    }

    commandBuffers_.resize(kCommandBufferRingDepth);
    std::vector<VkCommandBuffer> buffers(kCommandBufferRingDepth);
    VkCommandBufferAllocateInfo allocate{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    allocate.commandPool = commandPool_;
    allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocate.commandBufferCount = kCommandBufferRingDepth;
    const VkResult result = vkAllocateCommandBuffers(
        device->device, &allocate, buffers.data());
    if (result != VK_SUCCESS) {
        throw std::runtime_error("[ProxyRasterStageNode] vkAllocateCommandBuffers failed: " +
                                 std::to_string(result));
    }
    for (uint32_t i = 0u; i < kCommandBufferRingDepth; ++i) {
        commandBuffers_[i] = buffers[i];
        commandBuffers_.MarkDirty(i);
    }

    const std::vector<VkBuffer> writes =
        ctx.In(ProxyRasterStageNodeConfig::BUFFER_WRITE_ARRAY);
    ctx.Out(ProxyRasterStageNodeConfig::BUFFER_OUT,
            writes.empty() ? VK_NULL_HANDLE : writes.front());
    ctx.Out(ProxyRasterStageNodeConfig::VULKAN_DEVICE_OUT, device);
}

void ProxyRasterStageNode::ExecuteImpl(TypedExecuteContext& ctx) {
    const uint32_t frameIndex = ctx.In(ProxyRasterStageNodeConfig::CURRENT_FRAME_INDEX);
    if (frameIndex >= commandBuffers_.size()) {
        throw std::runtime_error("[ProxyRasterStageNode] frame index outside command-buffer ring");
    }

    VkCommandBuffer commandBuffer = commandBuffers_.GetValue(frameIndex);
    RecordCommands(ctx, commandBuffer);
    commandBuffers_.MarkReady(frameIndex);

    VkSemaphore timeline = ctx.In(ProxyRasterStageNodeConfig::TIMELINE_SEMAPHORE_IN);
    const uint64_t frameBase = ctx.In(ProxyRasterStageNodeConfig::TIMELINE_FRAME_BASE_IN);
    std::vector<VkSemaphoreSubmitInfo> waits;
    std::vector<VkSemaphoreSubmitInfo> signals;

    if (timeline != VK_NULL_HANDLE) {
        const FrameSyncSchedule& schedule = GetOwningGraph()->GetFrameSyncSchedule();
        if (const SubmitGroup* group = FindGroupForNode(schedule, this)) {
            for (uint32_t edgeIndex : group->waitEdges) {
                VkSemaphoreSubmitInfo wait{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
                wait.semaphore = timeline;
                wait.value = schedule.edges[edgeIndex].timelineOffset + frameBase;
                wait.stageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT |
                                 VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT |
                                 VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
                waits.push_back(wait);
            }

            std::set<uint64_t> signalValues;
            for (uint32_t edgeIndex : group->signalEdges) {
                signalValues.insert(schedule.edges[edgeIndex].timelineOffset + frameBase);
            }
            for (uint64_t value : signalValues) {
                VkSemaphoreSubmitInfo signal{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
                signal.semaphore = timeline;
                signal.value = value;
                signal.stageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT |
                                   VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT |
                                   VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
                signals.push_back(signal);
            }
        }
    }

    VkCommandBufferSubmitInfo commandInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
    commandInfo.commandBuffer = commandBuffer;
    VkSubmitInfo2 submit{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
    submit.waitSemaphoreInfoCount = static_cast<uint32_t>(waits.size());
    submit.pWaitSemaphoreInfos = waits.data();
    submit.commandBufferInfoCount = 1u;
    submit.pCommandBufferInfos = &commandInfo;
    submit.signalSemaphoreInfoCount = static_cast<uint32_t>(signals.size());
    submit.pSignalSemaphoreInfos = signals.data();

    VkResult result = VK_SUCCESS;
    {
        std::lock_guard<std::mutex> lock(GetDevice()->SubmitMutex(GetDevice()->queue));
        result = GetDevice()->fpQueueSubmit2(GetDevice()->queue, 1u, &submit, VK_NULL_HANDLE);
    }
    if (result != VK_SUCCESS) {
        throw std::runtime_error("[ProxyRasterStageNode] vkQueueSubmit2 failed: " +
                                 std::to_string(result));
    }

    const std::vector<VkBuffer> writes =
        ctx.In(ProxyRasterStageNodeConfig::BUFFER_WRITE_ARRAY);
    ctx.Out(ProxyRasterStageNodeConfig::BUFFER_OUT,
            writes.empty() ? VK_NULL_HANDLE : writes.front());
    ctx.Out(ProxyRasterStageNodeConfig::VULKAN_DEVICE_OUT, GetDevice());
}

void ProxyRasterStageNode::RecordCommands(TypedExecuteContext& ctx,
                                          VkCommandBuffer commandBuffer) {
    const std::vector<VkBuffer> writes =
        ctx.In(ProxyRasterStageNodeConfig::BUFFER_WRITE_ARRAY);
    if (writes.empty() || writes.front() == VK_NULL_HANDLE) {
        throw std::runtime_error("[ProxyRasterStageNode] candidate buffer is null");
    }
    VkBuffer candidateBuffer = writes.front();

    auto* target = ctx.In(ProxyRasterStageNodeConfig::COLOR_TARGET);
    const std::vector<VkFramebuffer> framebuffers =
        ctx.In(ProxyRasterStageNodeConfig::FRAMEBUFFERS);
    if (!target || target->GetCurrentIndex() >= framebuffers.size()) {
        throw std::runtime_error("[ProxyRasterStageNode] invalid color target/framebuffer ring");
    }

    const uint32_t imageIndex = target->GetCurrentIndex();
    const std::vector<VkDescriptorSet> descriptorSets = useFragmentWriter_
        ? ctx.In(ProxyRasterStageNodeConfig::DESCRIPTOR_SETS)
        : ctx.In(ProxyRasterStageNodeConfig::COMPUTE_DESCRIPTOR_SETS);
    if (imageIndex >= descriptorSets.size()) {
        throw std::runtime_error("[ProxyRasterStageNode] selected descriptor-set ring is too small");
    }

    if (vkResetCommandBuffer(commandBuffer, 0u) != VK_SUCCESS) {
        throw std::runtime_error("[ProxyRasterStageNode] vkResetCommandBuffer failed");
    }
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    if (vkBeginCommandBuffer(commandBuffer, &begin) != VK_SUCCESS) {
        throw std::runtime_error("[ProxyRasterStageNode] vkBeginCommandBuffer failed");
    }

    // The candidate buffer is intentionally one 32-byte record per pixel, not
    // multiplied by frames-in-flight. Both B2 submits use the same Vulkan queue,
    // so this barrier makes the new clear wait for any prior frame's march read
    // before reusing the buffer.
    VkBufferMemoryBarrier2 recycleBarrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2};
    recycleBarrier.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    recycleBarrier.srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT;
    recycleBarrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
    recycleBarrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    recycleBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    recycleBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    recycleBarrier.buffer = candidateBuffer;
    recycleBarrier.offset = 0u;
    recycleBarrier.size = VK_WHOLE_SIZE;
    VkDependencyInfo recycleDependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    recycleDependency.bufferMemoryBarrierCount = 1u;
    recycleDependency.pBufferMemoryBarriers = &recycleBarrier;
    GetDevice()->fpCmdPipelineBarrier2(commandBuffer, &recycleDependency);

    // One zero fill initializes all eight words/pixel. The mask is the validity
    // bit; entryKey/exitBits are decoded only when at least one mask bit is set.
    vkCmdFillBuffer(commandBuffer, candidateBuffer, 0u, VK_WHOLE_SIZE, 0u);
    VkBufferMemoryBarrier2 fillBarrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2};
    fillBarrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
    fillBarrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    fillBarrier.dstStageMask = useFragmentWriter_
        ? VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT
        : VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    fillBarrier.dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT |
                                VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
    fillBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    fillBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    fillBarrier.buffer = candidateBuffer;
    fillBarrier.offset = 0u;
    fillBarrier.size = VK_WHOLE_SIZE;
    VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dependency.bufferMemoryBarrierCount = 1u;
    dependency.pBufferMemoryBarriers = &fillBarrier;
    GetDevice()->fpCmdPipelineBarrier2(commandBuffer, &dependency);

    const VkExtent2D extent = target->GetExtent();
    const CameraData& camera = ctx.In(ProxyRasterStageNodeConfig::CAMERA_DATA);
    const uint32_t proxyCount = ctx.In(ProxyRasterStageNodeConfig::PROXY_AABB_COUNT);
    if (extent.width > std::numeric_limits<uint16_t>::max() ||
        extent.height > std::numeric_limits<uint16_t>::max()) {
        throw std::runtime_error("[ProxyRasterStageNode] render extent exceeds packed 16-bit dimensions");
    }
    const uint32_t packedDimensions = extent.width | (extent.height << 16u);
    ProxyRasterPushConstants push{};
    push.viewProj = ctx.In(ProxyRasterStageNodeConfig::CURRENT_VIEW_PROJ);
    push.cameraPosFov = glm::vec4(camera.cameraPos, camera.fov);
    push.cameraDirAspect = glm::vec4(camera.cameraDir, camera.aspect);
    push.cameraUpWidth = glm::vec4(camera.cameraUp, static_cast<float>(proxyCount));
    push.cameraRightHeight = glm::vec4(camera.cameraRight,
                                      std::bit_cast<float>(packedDimensions));
    const uint32_t bodyCount = std::min(
        static_cast<uint32_t>(std::max(ctx.In(ProxyRasterStageNodeConfig::INSTANCE_COUNT), 0)),
        kProxyCandidateLimit);

    if (useFragmentWriter_) {
        VkClearValue clear{};
        VkRenderPassBeginInfo renderBegin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
        renderBegin.renderPass = ctx.In(ProxyRasterStageNodeConfig::RENDER_PASS);
        renderBegin.framebuffer = framebuffers[target->GetCurrentIndex()];
        renderBegin.renderArea.offset = {0, 0};
        renderBegin.renderArea.extent = extent;
        renderBegin.clearValueCount = 1u;
        renderBegin.pClearValues = &clear;
        vkCmdBeginRenderPass(commandBuffer, &renderBegin, VK_SUBPASS_CONTENTS_INLINE);

        VkViewport viewport{};
        viewport.width = static_cast<float>(extent.width);
        viewport.height = static_cast<float>(extent.height);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        VkRect2D scissor{{0, 0}, extent};
        vkCmdSetViewport(commandBuffer, 0u, 1u, &viewport);
        vkCmdSetScissor(commandBuffer, 0u, 1u, &scissor);

        const VkPipeline pipeline = ctx.In(ProxyRasterStageNodeConfig::PIPELINE);
        const VkPipelineLayout layout = ctx.In(ProxyRasterStageNodeConfig::PIPELINE_LAYOUT);
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                layout, 0u, 1u, &descriptorSets[imageIndex], 0u, nullptr);
        vkCmdPushConstants(commandBuffer, layout,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0u, sizeof(push), &push);

        const uint64_t drawInstanceCount = static_cast<uint64_t>(proxyCount) * bodyCount;
        if (drawInstanceCount > std::numeric_limits<uint32_t>::max()) {
            throw std::runtime_error(
                "[ProxyRasterStageNode] proxy/body draw product exceeds uint32_t");
        }
        vkCmdDraw(commandBuffer, kProxyCubeVertexCount,
                  static_cast<uint32_t>(drawInstanceCount), 0u, 0u);
        vkCmdEndRenderPass(commandBuffer);
    } else if (bodyCount > 0u && proxyCount > 0u) {
        const VkPipeline pipeline = ctx.In(ProxyRasterStageNodeConfig::COMPUTE_PIPELINE);
        const VkPipelineLayout layout =
            ctx.In(ProxyRasterStageNodeConfig::COMPUTE_PIPELINE_LAYOUT);
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                                layout, 0u, 1u, &descriptorSets[imageIndex], 0u, nullptr);
        vkCmdPushConstants(commandBuffer, layout, VK_SHADER_STAGE_COMPUTE_BIT,
                           0u, sizeof(push), &push);
        vkCmdDispatch(commandBuffer,
                      (extent.width + 7u) / 8u,
                      (extent.height + 7u) / 8u,
                      bodyCount);
    }
    if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
        throw std::runtime_error("[ProxyRasterStageNode] vkEndCommandBuffer failed");
    }
}

void ProxyRasterStageNode::CleanupImpl(TypedCleanupContext&) {
    if (GetDevice() && GetDevice()->device != VK_NULL_HANDLE &&
        commandPool_ != VK_NULL_HANDLE && !commandBuffers_.empty()) {
        std::vector<VkCommandBuffer> buffers;
        buffers.reserve(commandBuffers_.size());
        for (size_t i = 0; i < commandBuffers_.size(); ++i) {
            buffers.push_back(commandBuffers_.GetValue(i));
        }
        vkFreeCommandBuffers(GetDevice()->device, commandPool_,
                             static_cast<uint32_t>(buffers.size()), buffers.data());
        commandBuffers_.clear();
    }
    commandPool_ = VK_NULL_HANDLE;
}

} // namespace Vixen::RenderGraph

VIXEN_REGISTER_NODE(Vixen::RenderGraph::ProxyRasterStageNodeType);
