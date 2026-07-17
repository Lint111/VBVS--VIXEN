// Copyright (C) 2025 Lior Yanai (eLiorg). Licensed under the MIT License.
#pragma once

#include "VulkanDevice.h"
#include "IRenderTarget.h"
#include <vulkan/vulkan.h>
#include <unordered_map>

namespace Vixen::RenderGraph {

// KI-007 fix (originally ComputeDispatchNode-local; moved here Sampled Lighting Inc3
// M1 so ComputeStageNode's IMAGE_WRITE role and the new BlitNode can reuse it too,
// instead of hand-syncing a second/third divergent copy): given the tracked
// last-known layout for a render-target VkImage handle (absent = never seen ->
// fresh/recreated image, true prior layout UNDEFINED), returns the layout to declare
// as the barrier's oldLayout and updates the map to the new layout the caller is
// about to transition to. Pure/free so it's unit-testable with fake VkImage handles,
// no device needed. Each CALLER owns its own tracking map (a
// std::unordered_map<VkImage,VkImageLayout> member) — this function is stateless
// itself, just the shared decision/update logic.
inline VkImageLayout DecideRenderTargetPriorLayoutAndUpdate(
    std::unordered_map<VkImage, VkImageLayout>& tracked,
    VkImage image,
    VkImageLayout newLayout)
{
    auto it = tracked.find(image);
    const VkImageLayout priorLayout = (it != tracked.end()) ? it->second : VK_IMAGE_LAYOUT_UNDEFINED;
    tracked[image] = newLayout;
    return priorLayout;
}

} // namespace Vixen::RenderGraph

namespace Vixen::RenderGraph::SwapchainBarriers {

// Fallback barrier2: oldLayout → GENERAL (TOP_OF_PIPE/0-or-BLIT → COMPUTE_SHADER/SHADER_STORAGE_WRITE).
// oldLayout defaults to UNDEFINED (WSI acquire / first-use contract). Pass the render target's actual
// prior layout (e.g. TRANSFER_SRC_OPTIMAL after a blit) when the barrier must match what synchronization
// validation actually tracked instead of relying on UNDEFINED's "discard, don't care" escape hatch.
inline void TransitionImageToGeneralBarrier2(Vixen::Vulkan::Resources::VulkanDevice* device,
                                              VkCommandBuffer cmdBuffer, VkImage image,
                                              VkImageLayout oldLayout = VK_IMAGE_LAYOUT_UNDEFINED) {
    VkImageMemoryBarrier2 ib{};
    ib.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) {
        // Coming from BlitRenderTargetToSwapchain's read of this image last frame.
        ib.srcStageMask  = VK_PIPELINE_STAGE_2_BLIT_BIT;
        ib.srcAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
    } else {
        ib.srcStageMask  = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
        ib.srcAccessMask = VK_ACCESS_2_NONE;
    }
    ib.oldLayout           = oldLayout;
    ib.dstStageMask        = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    ib.dstAccessMask       = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
    ib.newLayout           = VK_IMAGE_LAYOUT_GENERAL;
    ib.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    ib.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    ib.image               = image;
    ib.subresourceRange    = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

    VkDependencyInfo dep{};
    dep.sType                   = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dep.imageMemoryBarrierCount = 1;
    dep.pImageMemoryBarriers    = &ib;
    device->fpCmdPipelineBarrier2(cmdBuffer, &dep);
}

// Baked-Perf M6 Task 6.2 (audit E3): return a blit's render-target source from
// TRANSFER_SRC_OPTIMAL (where the blit left it) back to GENERAL — the stable cross-node
// boundary layout every ComputeStageNode IMAGE_WRITE producer/consumer already assumes.
// Before this fix, BlitRenderTargetToSwapchain left the render target in TRANSFER_SRC_OPTIMAL
// across the frame boundary while SpatialReuseNode's next write (ComputeStageNode's private
// imageWriteLayouts_ map, which only ever stores GENERAL) declared oldLayout=GENERAL — a
// stale-layout mismatch against what the driver/validation layer actually tracked (the real
// per-frame spec violation this milestone fixes). Returning to GENERAL here means BOTH the
// blit's own layoutTracking map AND ComputeStageNode's imageWriteLayouts_ agree on the
// image's state without sharing a map: GENERAL is the one layout every producer of this
// image already writes into DecideRenderTargetPriorLayoutAndUpdate's tracked state.
inline VkImageMemoryBarrier2 MakeRenderTargetPostBlitBarrier(VkImage image) {
    VkImageMemoryBarrier2 barrier{};
    barrier.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    barrier.srcStageMask        = VK_PIPELINE_STAGE_2_BLIT_BIT;
    barrier.srcAccessMask       = VK_ACCESS_2_TRANSFER_READ_BIT;
    barrier.oldLayout           = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.dstStageMask        = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    barrier.dstAccessMask       = VK_ACCESS_2_SHADER_STORAGE_READ_BIT |
                                  VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
    barrier.newLayout           = VK_IMAGE_LAYOUT_GENERAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image               = image;
    barrier.subresourceRange    = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    return barrier;
}

// Explicit GENERAL → PRESENT_SRC_KHR transition for the voxel-only (!leaveImageInGeneral) path.
inline void TransitionImageToPresentBarrier2(Vixen::Vulkan::Resources::VulkanDevice* device,
                                              VkCommandBuffer cmdBuffer, VkImage image) {
    VkImageMemoryBarrier2 ib{};
    ib.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    ib.srcStageMask        = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    ib.srcAccessMask       = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
    ib.oldLayout           = VK_IMAGE_LAYOUT_GENERAL;
    ib.dstStageMask        = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;
    ib.dstAccessMask       = VK_ACCESS_2_NONE;
    ib.newLayout           = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    ib.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    ib.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    ib.image               = image;
    ib.subresourceRange    = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

    VkDependencyInfo dep{};
    dep.sType                   = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dep.imageMemoryBarrierCount = 1;
    dep.pImageMemoryBarriers    = &ib;
    device->fpCmdPipelineBarrier2(cmdBuffer, &dep);
}

// M4 (render-scale decoupling): blits an offscreen render target (already written by
// a compute pass, still GENERAL) up/down to the swapchain extent. Handles the full
// GENERAL->TRANSFER_SRC / swapchain ?->TRANSFER_DST / blit / ->GENERAL-or-PRESENT_SRC
// barrier sequence:
//   render target:  GENERAL (compute write)      -> TRANSFER_SRC_OPTIMAL -> GENERAL
//   swapchain:       ?  (WSI acquire / prior frame's real last layout) -> TRANSFER_DST_OPTIMAL
//   vkCmdBlitImage
//   swapchain:       TRANSFER_DST_OPTIMAL -> GENERAL (composite/UI) or PRESENT_SRC_KHR (voxel-only)
// Baked-Perf M6 Task 6.2 (audit E3): the render target is returned to GENERAL before this
// function returns (MakeRenderTargetPostBlitBarrier above) — GENERAL is the stable cross-node
// boundary layout, so the NEXT write (by whichever ComputeStageNode IMAGE_WRITE producer
// produces it) can declare oldLayout=GENERAL without needing to share this function's private
// `layoutTracking` map with that producer's own imageWriteLayouts_ map. Before this fix the
// render target was left in TRANSFER_SRC_OPTIMAL across the frame boundary while the next
// writer's own private map (which only ever stores GENERAL) declared a stale oldLayout=GENERAL —
// a genuine spec violation the validation layer would catch as a layout mismatch.
//
// Originally a ComputeDispatchNode-private method (`BlitRenderTargetToSwapchain`);
// extracted here Sampled Lighting Inc3 M1 (KI-018) so the new presentation-only
// BlitNode can call the SAME logic instead of a second, divergent copy.
// `layoutTracking` is the CALLER's own std::unordered_map<VkImage,VkImageLayout>
// member (one per node instance, exactly like ComputeDispatchNode's own
// renderTargetImageLayouts_ before this extraction) — tracks BOTH the render-target
// and swapchain image handles this function touches, keyed together in one map since
// each handle is only ever the subject of ONE of the two roles.
inline void BlitRenderTargetToSwapchain(
    Vixen::Vulkan::Resources::VulkanDevice* device,
    std::unordered_map<VkImage, VkImageLayout>& layoutTracking,
    VkCommandBuffer cmdBuffer,
    Vixen::Vulkan::Resources::IRenderTarget* renderTarget,
    VkImage swapchainImage,
    VkExtent2D swapchainExtent,
    bool leaveImageInGeneral)
{
    VkImage renderTargetImage = renderTarget->GetCurrentImage();
    VkExtent2D srcExtent = renderTarget->GetExtent();

    // Swapchain-side counterpart to the KI-007 fix: the entry barrier below used to hardcode
    // oldLayout=UNDEFINED for the swapchain image on EVERY frame, but that's only true for a
    // swapchain image's true first use. On the leaveImageInGeneral path, the downstream UI render
    // pass (PARAM_INITIAL_LAYOUT=General, PARAM_FINAL_LAYOUT=PresentSrc) moves this SAME image
    // handle GENERAL->PRESENT_SRC_KHR and then vkQueuePresentKHR leaves it there — so the NEXT time
    // this ring slot's image index comes back around, its real layout is PRESENT_SRC_KHR, not
    // UNDEFINED. Declaring UNDEFINED anyway produced VUID-vkCmdDraw-None-09600 at the UI render
    // pass's first draw (the render pass's initialLayout=General assertion was already false by
    // the time the pass began), the root cause of the render-view flicker (KI-009).
    const VkImageLayout swapchainPriorLayout = Vixen::RenderGraph::DecideRenderTargetPriorLayoutAndUpdate(
        layoutTracking, swapchainImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    // --- Entry barriers: render target GENERAL->TRANSFER_SRC, swapchain ?->TRANSFER_DST ---
    VkImageMemoryBarrier2 entryBarriers[2]{};

    entryBarriers[0].sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    entryBarriers[0].srcStageMask        = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    entryBarriers[0].srcAccessMask       = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
    entryBarriers[0].oldLayout           = VK_IMAGE_LAYOUT_GENERAL;
    entryBarriers[0].dstStageMask        = VK_PIPELINE_STAGE_2_BLIT_BIT;
    entryBarriers[0].dstAccessMask       = VK_ACCESS_2_TRANSFER_READ_BIT;
    entryBarriers[0].newLayout           = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    entryBarriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    entryBarriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    entryBarriers[0].image               = renderTargetImage;
    entryBarriers[0].subresourceRange    = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

    entryBarriers[1].sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    // srcStageMask must match (or be synchronized-after) the acquire semaphore's wait stage
    // on THIS command buffer's own submit so this barrier actually chains an execution
    // dependency off that wait. TOP_OF_PIPE_BIT (the pre-KI-007 value) is a no-op source that
    // doesn't synchronize with anything, which is only harmless when oldLayout is a true
    // first-use UNDEFINED (nothing to wait for) — once the swapchain-tracking fix above
    // declares a real prior layout (PRESENT_SRC_KHR from a previous frame's present), this
    // must correctly wait on the acquire, else validation reports SYNC-HAZARD-WRITE-AFTER-READ
    // against vkAcquireNextImageKHR. Baked-Perf M6 Task 6.1 (audit E2): BLIT_BIT, not
    // COMPUTE_SHADER_BIT — this function's OWN command buffer either belongs to BlitNode
    // (whose acquire wait, when it owns one, is declared at BLIT_BIT — see BlitNode::
    // ExecuteImpl) or ComputeDispatchNode's voxel-only path (whose compute dispatch is
    // already CPU-recorded strictly before this function runs in the SAME command buffer,
    // so BLIT_BIT here still correctly comes after that dispatch in submission order
    // regardless of the acquire wait's own stage there).
    entryBarriers[1].srcStageMask        = VK_PIPELINE_STAGE_2_BLIT_BIT;
    entryBarriers[1].srcAccessMask       = VK_ACCESS_2_NONE;
    entryBarriers[1].oldLayout           = swapchainPriorLayout;
    entryBarriers[1].dstStageMask        = VK_PIPELINE_STAGE_2_BLIT_BIT;
    entryBarriers[1].dstAccessMask       = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    entryBarriers[1].newLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    entryBarriers[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    entryBarriers[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    entryBarriers[1].image               = swapchainImage;
    entryBarriers[1].subresourceRange    = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

    VkDependencyInfo entryDep{};
    entryDep.sType                   = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    entryDep.imageMemoryBarrierCount = 2;
    entryDep.pImageMemoryBarriers    = entryBarriers;
    device->fpCmdPipelineBarrier2(cmdBuffer, &entryDep);

    // KI-007: the render target's compute write (by whichever pass produced it) already
    // transitioned renderTargetImage to GENERAL before this function runs (guaranteeing
    // entryBarriers[0]'s hardcoded oldLayout=GENERAL above is correct), and this barrier just
    // moved it to TRANSFER_SRC_OPTIMAL — record that so the NEXT write to this same handle
    // (a future frame) declares the correct oldLayout instead of guessing.
    layoutTracking[renderTargetImage] = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

    // --- Blit (LINEAR filter — upscales/downscales src extent to dst extent) ---
    VkImageBlit blit{};
    blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    blit.srcOffsets[0]  = {0, 0, 0};
    blit.srcOffsets[1]  = {static_cast<int32_t>(srcExtent.width), static_cast<int32_t>(srcExtent.height), 1};
    blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    blit.dstOffsets[0]  = {0, 0, 0};
    blit.dstOffsets[1]  = {static_cast<int32_t>(swapchainExtent.width), static_cast<int32_t>(swapchainExtent.height), 1};

    vkCmdBlitImage(cmdBuffer,
                   renderTargetImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   swapchainImage,    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                   1, &blit, VK_FILTER_LINEAR);

    // --- Exit barriers: restore the render-target source to GENERAL (Task 6.2 / audit E3) and
    // transition swapchain TRANSFER_DST -> today's contract (GENERAL for UI, else PRESENT) ---
    VkImageMemoryBarrier2 exitBarriers[2]{MakeRenderTargetPostBlitBarrier(renderTargetImage), {}};
    VkImageMemoryBarrier2& exitBarrier = exitBarriers[1];
    exitBarrier.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    exitBarrier.srcStageMask        = VK_PIPELINE_STAGE_2_BLIT_BIT;
    exitBarrier.srcAccessMask       = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    exitBarrier.oldLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    exitBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    exitBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    exitBarrier.image               = swapchainImage;
    exitBarrier.subresourceRange    = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

    if (leaveImageInGeneral) {
        // Downstream UI render pass LOADs from GENERAL and owns the ->PRESENT_SRC transition.
        exitBarrier.dstStageMask  = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        exitBarrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT;
        exitBarrier.newLayout     = VK_IMAGE_LAYOUT_GENERAL;
    } else {
        exitBarrier.dstStageMask  = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;
        exitBarrier.dstAccessMask = VK_ACCESS_2_NONE;
        exitBarrier.newLayout     = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    }

    // Either way, this ring slot's image ends the frame at PRESENT_SRC_KHR by the time it's reused:
    // on the leaveImageInGeneral path this function hands it to the UI render pass in GENERAL, but
    // that pass's own finalLayout=PresentSrc (BuildRenderGraph.cpp) plus the present call moves it
    // there before this same image index comes back around. Track that real end state (not the
    // intermediate GENERAL this function leaves it in) so next frame's entry barrier above declares
    // the correct oldLayout instead of hardcoding UNDEFINED.
    layoutTracking[swapchainImage] = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    // Task 6.2: keep this function's own history honest too, though the real cross-node
    // contract is that GENERAL is what every consumer of renderTargetImage already expects
    // (ComputeStageNode's imageWriteLayouts_ map independently arrives at the same GENERAL
    // state for its own next write — the two maps no longer need to agree by coincidence).
    layoutTracking[renderTargetImage] = VK_IMAGE_LAYOUT_GENERAL;

    VkDependencyInfo exitDep{};
    exitDep.sType                   = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    exitDep.imageMemoryBarrierCount = 2;
    exitDep.pImageMemoryBarriers    = exitBarriers;
    device->fpCmdPipelineBarrier2(cmdBuffer, &exitDep);
}

} // namespace Vixen::RenderGraph::SwapchainBarriers
