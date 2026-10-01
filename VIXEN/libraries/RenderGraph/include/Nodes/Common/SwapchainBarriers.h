// Copyright (C) 2025 Lior Yanai (eLiorg). Licensed under the MIT License.
#pragma once

#include "VulkanDevice.h"
#include "IRenderTarget.h"
#include <vulkan/vulkan.h>

namespace Vixen::RenderGraph {

// The physical buffer owns its history; every recording node observes the previous writer.
inline VkImageLayout DecideRenderTargetPriorLayoutAndUpdate(
    Vixen::Vulkan::Resources::IRenderTarget& target, uint32_t index, VkImageLayout newLayout) {
    const VkImageLayout prior = target.GetImageLayout(index);
    target.SetImageLayout(index, newLayout);
    return prior;
}

} // namespace Vixen::RenderGraph

namespace Vixen::RenderGraph::SwapchainBarriers {

inline void TransitionImageToGeneralBarrier2(Vixen::Vulkan::Resources::VulkanDevice* device,
                                              VkCommandBuffer cmd, VkImage image,
                                              VkImageLayout oldLayout = VK_IMAGE_LAYOUT_UNDEFINED) {
    VkImageMemoryBarrier2 ib{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    // A known prior layout may have been produced by any graph pass, including a render pass.
    ib.srcStageMask = oldLayout == VK_IMAGE_LAYOUT_UNDEFINED
        ? VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT : VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    ib.srcAccessMask = oldLayout == VK_IMAGE_LAYOUT_UNDEFINED
        ? VK_ACCESS_2_NONE : VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
    ib.oldLayout = oldLayout;
    ib.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    ib.dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
    ib.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    ib.srcQueueFamilyIndex = ib.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    ib.image = image;
    ib.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dep.imageMemoryBarrierCount = 1;
    dep.pImageMemoryBarriers = &ib;
    device->fpCmdPipelineBarrier2(cmd, &dep);
}

// Restore a blit source to the graph's stable storage-image boundary layout.
inline VkImageMemoryBarrier2 MakeRenderTargetPostBlitBarrier(VkImage image) {
    VkImageMemoryBarrier2 b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    b.srcStageMask = VK_PIPELINE_STAGE_2_BLIT_BIT;
    b.srcAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
    b.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    b.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    b.dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
    b.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = image;
    b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    return b;
}

inline void TransitionImageToPresentBarrier2(Vixen::Vulkan::Resources::VulkanDevice* device,
                                              VkCommandBuffer cmd, VkImage image) {
    VkImageMemoryBarrier2 ib{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    ib.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    ib.srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
    ib.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    ib.dstStageMask = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;
    ib.dstAccessMask = VK_ACCESS_2_NONE;
    ib.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    ib.srcQueueFamilyIndex = ib.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    ib.image = image;
    ib.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dep.imageMemoryBarrierCount = 1;
    dep.pImageMemoryBarriers = &ib;
    device->fpCmdPipelineBarrier2(cmd, &dep);
}

// Blit the current source image to one presentation image. Record the actual boundary layouts;
// a downstream render pass will record its own final layout when it executes.
inline void BlitRenderTargetToSwapchain(
    Vixen::Vulkan::Resources::VulkanDevice* device, VkCommandBuffer cmd,
    Vixen::Vulkan::Resources::IRenderTarget* source,
    Vixen::Vulkan::Resources::IRenderTarget* destination, uint32_t destinationIndex,
    bool leaveImageInGeneral) {
    const VkImage sourceImage = source->GetCurrentImage();
    const VkImage destinationImage = destination->GetImage(destinationIndex);
    const VkExtent2D srcExtent = source->GetExtent(), dstExtent = destination->GetExtent();
    VkImageMemoryBarrier2 entry[2]{};
    for (auto& b : entry) {
        b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        b.dstStageMask = VK_PIPELINE_STAGE_2_BLIT_BIT;
    }
    entry[0].image = sourceImage;
    entry[0].oldLayout = source->GetCurrentLayout();
    entry[0].newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    entry[0].srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    entry[0].srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
    entry[0].dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
    entry[1].image = destinationImage;
    entry[1].oldLayout = destination->GetImageLayout(destinationIndex);
    entry[1].newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    // Chain the destination transition to this submit's BLIT-stage WSI acquire wait.
    entry[1].srcStageMask = VK_PIPELINE_STAGE_2_BLIT_BIT;
    entry[1].srcAccessMask = VK_ACCESS_2_NONE;
    entry[1].dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dep.imageMemoryBarrierCount = 2;
    dep.pImageMemoryBarriers = entry;
    device->fpCmdPipelineBarrier2(cmd, &dep);

    VkImageBlit blit{};
    blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    blit.srcOffsets[1] = {static_cast<int32_t>(srcExtent.width), static_cast<int32_t>(srcExtent.height), 1};
    blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    blit.dstOffsets[1] = {static_cast<int32_t>(dstExtent.width), static_cast<int32_t>(dstExtent.height), 1};
    vkCmdBlitImage(cmd, sourceImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   destinationImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);

    VkImageMemoryBarrier2 exit[2]{MakeRenderTargetPostBlitBarrier(sourceImage), entry[1]};
    exit[1].srcStageMask = VK_PIPELINE_STAGE_2_BLIT_BIT;
    exit[1].srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    exit[1].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    const bool finishesForPresent = !leaveImageInGeneral && destination->UsesWsiSynchronization();
    exit[1].newLayout = !finishesForPresent ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    exit[1].dstStageMask = !finishesForPresent
        ? VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT
        : VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;
    exit[1].dstAccessMask = !finishesForPresent
        ? VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT : VK_ACCESS_2_NONE;
    dep.pImageMemoryBarriers = exit;
    device->fpCmdPipelineBarrier2(cmd, &dep);
    source->SetImageLayout(source->GetCurrentIndex(), exit[0].newLayout);
    destination->SetImageLayout(destinationIndex, exit[1].newLayout);
}

} // namespace Vixen::RenderGraph::SwapchainBarriers
