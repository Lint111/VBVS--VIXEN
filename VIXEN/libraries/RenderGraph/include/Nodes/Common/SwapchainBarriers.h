// Copyright (C) 2025 Lior Yanai (eLiorg). Licensed under the MIT License.
#pragma once

#include "VulkanDevice.h"
#include <vulkan/vulkan.h>

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

} // namespace Vixen::RenderGraph::SwapchainBarriers
