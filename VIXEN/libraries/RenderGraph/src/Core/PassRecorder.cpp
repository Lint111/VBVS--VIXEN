// Copyright (C) 2025 Lior Yanai (eLiorg). Licensed under the MIT License.

#include "Core/PassRecorder.h"

#include <stdexcept>
#include <variant>

namespace Vixen::RenderGraph {

// ============================================================================
// ReplayGroupBarriers
// ============================================================================

/// Replay one group's baked barriers as a single vkCmdPipelineBarrier2.
/// For P4 each non-image GroupBarrier becomes a global VkMemoryBarrier2
/// (conservative: covers all buffer hazards, no handle resolution needed).
/// Image barriers are a deliberate P5 no-op stub — `GroupBarrier.resource`
/// is a node-local identity (often a sentinel) and cannot be resolved to a
/// VkImage without the swapchain correlation work deferred to P5.
void ReplayGroupBarriers(VkCommandBuffer cmd, const std::vector<GroupBarrier>& barriers) {
    if (barriers.empty()) return;

    std::vector<VkMemoryBarrier2> memBarriers;
    // P5: baked image barriers
    // std::vector<VkImageMemoryBarrier2> imageBarriers;  // reserved for P5

    for (const GroupBarrier& b : barriers) {
        if (b.isImage) {
            // P5: baked image barriers — skip in P4
            continue;
        }
        VkMemoryBarrier2 mb{};
        mb.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
        mb.srcStageMask  = b.src.stage;
        mb.srcAccessMask = b.src.access;
        mb.dstStageMask  = b.dst.stage;
        mb.dstAccessMask = b.dst.access;
        memBarriers.push_back(mb);
    }

    if (memBarriers.empty()) return;

    VkDependencyInfo dep{};
    dep.sType              = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dep.memoryBarrierCount = static_cast<uint32_t>(memBarriers.size());
    dep.pMemoryBarriers    = memBarriers.data();
    vkCmdPipelineBarrier2(cmd, &dep);
}

// ============================================================================
// RecordOneStep — compute arm
// ============================================================================

static void RecordOneStep(VkCommandBuffer cmd, const ComputePassStep& step, uint32_t imageIndex) {
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, step.pipeline);

    // descriptorSets holds one set PER SWAPCHAIN IMAGE (parallel to RenderPassStep::framebuffers).
    // Bind exactly the set for the current image at firstSet — binding the whole vector would map
    // per-image sets onto consecutive set indices the pipeline layout does not declare.
    if (imageIndex < step.descriptorSets.size()) {
        VkDescriptorSet set = step.descriptorSets[imageIndex];
        vkCmdBindDescriptorSets(
            cmd,
            VK_PIPELINE_BIND_POINT_COMPUTE,
            step.layout,
            step.firstSet,
            1,
            &set,
            0, nullptr
        );
    }

    if (step.pushConstants) {
        const PushConstantData& pc = *step.pushConstants;
        vkCmdPushConstants(
            cmd,
            step.layout,
            pc.stageFlags,
            pc.offset,
            static_cast<uint32_t>(pc.data.size()),
            pc.data.data()
        );
    }

    vkCmdDispatch(cmd, step.workGroupCount.x, step.workGroupCount.y, step.workGroupCount.z);
}

// ============================================================================
// RecordOneStep — render-pass arm (Task 4)
// ============================================================================

static void RecordOneStep(VkCommandBuffer cmd, const RenderPassStep& step, uint32_t imageIndex) {
    // Guard framebuffers[imageIndex] against out-of-range
    if (imageIndex >= step.framebuffers.size()) {
        throw std::runtime_error("[PassRecorder] imageIndex " + std::to_string(imageIndex) +
                                 " out of bounds (framebuffers.size()=" +
                                 std::to_string(step.framebuffers.size()) + ")");
    }

    // Begin render pass
    VkRenderPassBeginInfo rpBegin{};
    rpBegin.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpBegin.renderPass        = step.renderPass;
    rpBegin.framebuffer       = step.framebuffers[imageIndex];
    rpBegin.renderArea.offset = {0, 0};
    rpBegin.renderArea.extent = step.renderArea;
    rpBegin.clearValueCount   = static_cast<uint32_t>(step.clearValues.size());
    rpBegin.pClearValues      = step.clearValues.empty() ? nullptr : step.clearValues.data();

    vkCmdBeginRenderPass(cmd, &rpBegin, VK_SUBPASS_CONTENTS_INLINE);

    // Bind graphics pipeline
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, step.pipeline);

    // Bind descriptor sets — one set PER SWAPCHAIN IMAGE (parallel to framebuffers above).
    // Bind exactly the set for the current image at firstSet (see compute arm note).
    if (imageIndex < step.descriptorSets.size()) {
        VkDescriptorSet set = step.descriptorSets[imageIndex];
        vkCmdBindDescriptorSets(
            cmd,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            step.layout,
            step.firstSet,
            1,
            &set,
            0, nullptr
        );
    }

    // Push constants (if any)
    if (step.pushConstants) {
        const PushConstantData& pc = *step.pushConstants;
        vkCmdPushConstants(
            cmd,
            step.layout,
            pc.stageFlags,
            pc.offset,
            static_cast<uint32_t>(pc.data.size()),
            pc.data.data()
        );
    }

    // Set viewport and scissor from step.renderArea (lifted from GeometryRenderNode::SetViewportAndScissor)
    VkViewport viewport{};
    viewport.x        = 0.0f;
    viewport.y        = 0.0f;
    viewport.width    = static_cast<float>(step.renderArea.width);
    viewport.height   = static_cast<float>(step.renderArea.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = step.renderArea;

    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    // Bind vertex buffer (optional — fullscreen triangle draws skip this)
    if (step.vertexBuffer) {
        VkDeviceSize offsets[] = {0};
        vkCmdBindVertexBuffers(cmd, 0, 1, &(*step.vertexBuffer), offsets);
    }

    vkCmdDraw(cmd, step.vertexCount, step.instanceCount, 0, 0);

    vkCmdEndRenderPass(cmd);
}

// ============================================================================
// RecordPassGroup
// ============================================================================

void RecordPassGroup(VkCommandBuffer cmd, const std::vector<PassStep>& passes,
                     const FrameSyncSchedule& schedule, uint32_t imageIndex) {
    for (uint32_t i = 0; i < static_cast<uint32_t>(passes.size()); ++i) {
        if (i < static_cast<uint32_t>(schedule.groups.size())) {
            ReplayGroupBarriers(cmd, schedule.groups[i].entryBarriers);
        }
        std::visit([&](const auto& step) { RecordOneStep(cmd, step, imageIndex); }, passes[i]);
    }
}

} // namespace Vixen::RenderGraph
