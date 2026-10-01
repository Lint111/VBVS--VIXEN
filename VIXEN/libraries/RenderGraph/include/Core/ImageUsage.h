#pragma once
#include "Core/BarrierTypes.h"

namespace Vixen::RenderGraph {

// Creation requirements derived from the same per-consumer declarations used for scheduling.
constexpr VkImageUsageFlags ImageUsageForAccess(AccessKind kind) {
    const auto access = ResolveAccess(kind).access;
    VkImageUsageFlags flags = 0;
    if (access & (VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT))
        flags |= VK_IMAGE_USAGE_STORAGE_BIT;
    if (access & VK_ACCESS_2_SHADER_SAMPLED_READ_BIT) flags |= VK_IMAGE_USAGE_SAMPLED_BIT;
    if (access & (VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT))
        flags |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    if (access & (VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT))
        flags |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    if (access & VK_ACCESS_2_TRANSFER_READ_BIT) flags |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    if (access & VK_ACCESS_2_TRANSFER_WRITE_BIT) flags |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    return flags;
}

constexpr VkImageUsageFlags ImageUsageForDescriptor(VkDescriptorType type) {
    switch (type) {
    case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE: return VK_IMAGE_USAGE_STORAGE_BIT;
    case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
    case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER: return VK_IMAGE_USAGE_SAMPLED_BIT;
    case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT: return VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT;
    default: return 0;
    }
}

} // namespace Vixen::RenderGraph
