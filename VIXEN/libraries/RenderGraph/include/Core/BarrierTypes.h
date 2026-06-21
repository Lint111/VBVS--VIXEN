// Copyright (C) 2025 Lior Yanai (eLiorg). Licensed under the MIT License.
#pragma once

#include <vulkan/vulkan.h>
#include <cstdint>

namespace Vixen::RenderGraph {

/// Concrete Vulkan sync semantics of one resource access. `layout` is ignored for buffers.
struct AccessInfo {
    VkPipelineStageFlags2 stage  = VK_PIPELINE_STAGE_2_NONE;
    VkAccessFlags2        access = VK_ACCESS_2_NONE;
    VkImageLayout         layout = VK_IMAGE_LAYOUT_UNDEFINED;
};

/// Declarative per-pass access kind. Distinct from creation-time ResourceUsage flags
/// (ResourceTypes.h): this describes how a *pass* touches a resource, for sync.
enum class AccessKind : uint8_t {
    None = 0,
    ComputeStorageRead,
    ComputeStorageWrite,
    ComputeStorageReadWrite,
    ComputeSampledRead,
    FragmentSampledRead,
    VertexStorageRead,
    ColorAttachmentWrite,
    DepthAttachmentReadWrite,
    IndirectRead,
    TransferRead,
    TransferWrite,
    PresentSrc,
};

/// Single source of truth: AccessKind -> {stage, access, layout}.
[[nodiscard]] constexpr AccessInfo ResolveAccess(AccessKind kind) {
    switch (kind) {
    case AccessKind::ComputeStorageRead:
        return {VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
                VK_IMAGE_LAYOUT_GENERAL};
    case AccessKind::ComputeStorageWrite:
        return {VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                VK_IMAGE_LAYOUT_GENERAL};
    case AccessKind::ComputeStorageReadWrite:
        return {VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                VK_IMAGE_LAYOUT_GENERAL};
    case AccessKind::ComputeSampledRead:
        return {VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    case AccessKind::FragmentSampledRead:
        return {VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    case AccessKind::VertexStorageRead:
        return {VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
                VK_IMAGE_LAYOUT_UNDEFINED};
    case AccessKind::ColorAttachmentWrite:
        return {VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    case AccessKind::DepthAttachmentReadWrite:
        return {VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    case AccessKind::IndirectRead:
        return {VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT, VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT,
                VK_IMAGE_LAYOUT_UNDEFINED};
    case AccessKind::TransferRead:
        return {VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_READ_BIT,
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL};
    case AccessKind::TransferWrite:
        return {VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL};
    case AccessKind::PresentSrc:
        return {VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT, VK_ACCESS_2_NONE,
                VK_IMAGE_LAYOUT_PRESENT_SRC_KHR};
    case AccessKind::None:
    default:
        return {VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE, VK_IMAGE_LAYOUT_UNDEFINED};
    }
}

[[nodiscard]] constexpr bool AccessWrites(AccessKind k) {
    switch (k) {
    case AccessKind::ComputeStorageWrite:
    case AccessKind::ComputeStorageReadWrite:
    case AccessKind::ColorAttachmentWrite:
    case AccessKind::DepthAttachmentReadWrite:
    case AccessKind::TransferWrite:
        return true;
    default:
        return false;
    }
}

[[nodiscard]] constexpr bool AccessReads(AccessKind k) {
    switch (k) {
    case AccessKind::ComputeStorageRead:
    case AccessKind::ComputeStorageReadWrite:
    case AccessKind::ComputeSampledRead:
    case AccessKind::FragmentSampledRead:
    case AccessKind::VertexStorageRead:
    case AccessKind::DepthAttachmentReadWrite:
    case AccessKind::IndirectRead:
    case AccessKind::TransferRead:
        return true;
    default:
        return false;
    }
}

} // namespace Vixen::RenderGraph
