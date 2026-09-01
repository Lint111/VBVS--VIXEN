#pragma once

#include "Core/BarrierTypes.h"
#include "Data/CameraData.h"
#include "Data/Core/ResourceConfig.h"
#include "VulkanDeviceFwd.h"

#include <glm/glm.hpp>

namespace Vixen::Vulkan::Resources { struct IRenderTarget; }

namespace Vixen::RenderGraph {

using VulkanDevice = Vixen::Vulkan::Resources::VulkanDevice;

namespace ProxyRasterStageNodeCounts {
inline constexpr size_t INPUTS = 19;
inline constexpr size_t OUTPUTS = 2;
inline constexpr SlotArrayMode ARRAY_MODE = SlotArrayMode::Array;
}

/**
 * B2 capability-selected intermediate submit. It clears the per-pixel proxy
 * interval buffer, then uses either the fragment-store raster writer or its
 * compute-writer twin before signaling baked timeline edges. It owns no WSI
 * semaphore or fence.
 */
CONSTEXPR_NODE_CONFIG(ProxyRasterStageNodeConfig,
                      ProxyRasterStageNodeCounts::INPUTS,
                      ProxyRasterStageNodeCounts::OUTPUTS,
                      ProxyRasterStageNodeCounts::ARRAY_MODE) {
    static constexpr const char* PARAM_FORCE_COMPUTE_WRITER = "forceComputeWriter";
    INPUT_SLOT(RENDER_PASS, VkRenderPass, 0,
        SlotNullability::Required, SlotRole::Dependency,
        SlotMutability::ReadOnly, SlotScope::NodeLevel);
    INPUT_SLOT(FRAMEBUFFERS, std::vector<VkFramebuffer>, 1,
        SlotNullability::Required, SlotRole::Dependency,
        SlotMutability::ReadOnly, SlotScope::NodeLevel);
    INPUT_SLOT(PIPELINE, VkPipeline, 2,
        SlotNullability::Required, SlotRole::Dependency,
        SlotMutability::ReadOnly, SlotScope::NodeLevel);
    INPUT_SLOT(PIPELINE_LAYOUT, VkPipelineLayout, 3,
        SlotNullability::Required, SlotRole::Dependency,
        SlotMutability::ReadOnly, SlotScope::NodeLevel);
    INPUT_SLOT(DESCRIPTOR_SETS, const std::vector<VkDescriptorSet>&, 4,
        SlotNullability::Required, SlotRole::Dependency,
        SlotMutability::ReadOnly, SlotScope::NodeLevel);
    INPUT_SLOT_SYNC(COLOR_TARGET, Vixen::Vulkan::Resources::IRenderTarget*, 5,
        SlotNullability::Required, SlotRole::Execute,
        SlotMutability::ReadWrite, SlotScope::NodeLevel,
        ::Vixen::RenderGraph::AccessKind::ColorAttachmentWrite);
    INPUT_SLOT(COMMAND_POOL, VkCommandPool, 6,
        SlotNullability::Required, SlotRole::Dependency,
        SlotMutability::ReadOnly, SlotScope::NodeLevel);
    INPUT_SLOT(VULKAN_DEVICE, VulkanDevice*, 7,
        SlotNullability::Required, SlotRole::Dependency,
        SlotMutability::ReadOnly, SlotScope::NodeLevel);
    INPUT_SLOT(CURRENT_FRAME_INDEX, uint32_t, 8,
        SlotNullability::Required, SlotRole::Execute,
        SlotMutability::ReadOnly, SlotScope::NodeLevel);
    INPUT_SLOT(TIMELINE_SEMAPHORE_IN, VkSemaphore, 9,
        SlotNullability::Optional, SlotRole::Execute,
        SlotMutability::ReadOnly, SlotScope::NodeLevel);
    INPUT_SLOT(TIMELINE_FRAME_BASE_IN, uint64_t, 10,
        SlotNullability::Optional, SlotRole::Execute,
        SlotMutability::ReadOnly, SlotScope::NodeLevel);
    INPUT_SLOT(CAMERA_DATA, const CameraData&, 11,
        SlotNullability::Required, SlotRole::Execute,
        SlotMutability::ReadOnly, SlotScope::NodeLevel);
    INPUT_SLOT(CURRENT_VIEW_PROJ, const glm::mat4&, 12,
        SlotNullability::Required, SlotRole::Execute,
        SlotMutability::ReadOnly, SlotScope::NodeLevel);
    INPUT_SLOT(PROXY_AABB_COUNT, uint32_t, 13,
        SlotNullability::Required, SlotRole::Execute,
        SlotMutability::ReadOnly, SlotScope::NodeLevel);
    INPUT_SLOT(INSTANCE_COUNT, int32_t, 14,
        SlotNullability::Required, SlotRole::Execute,
        SlotMutability::ReadOnly, SlotScope::NodeLevel);
    INPUT_SLOT_SYNC(BUFFER_WRITE_ARRAY, std::vector<VkBuffer>, 15,
        SlotNullability::Required, SlotRole::Execute,
        SlotMutability::ReadWrite, SlotScope::NodeLevel,
        ::Vixen::RenderGraph::AccessKind::ShaderStorageWrite);
    INPUT_SLOT(COMPUTE_PIPELINE, VkPipeline, 16,
        SlotNullability::Required, SlotRole::Dependency,
        SlotMutability::ReadOnly, SlotScope::NodeLevel);
    INPUT_SLOT(COMPUTE_PIPELINE_LAYOUT, VkPipelineLayout, 17,
        SlotNullability::Required, SlotRole::Dependency,
        SlotMutability::ReadOnly, SlotScope::NodeLevel);
    INPUT_SLOT(COMPUTE_DESCRIPTOR_SETS, const std::vector<VkDescriptorSet>&, 18,
        SlotNullability::Required, SlotRole::Dependency,
        SlotMutability::ReadOnly, SlotScope::NodeLevel);

    OUTPUT_SLOT(BUFFER_OUT, VkBuffer, 0,
        SlotNullability::Optional, SlotMutability::WriteOnly);
    OUTPUT_SLOT(VULKAN_DEVICE_OUT, VulkanDevice*, 1,
        SlotNullability::Required, SlotMutability::WriteOnly);

    ProxyRasterStageNodeConfig() {
        HandleDescriptor renderPassDesc{"VkRenderPass"};
        INIT_INPUT_DESC(RENDER_PASS, "render_pass", ResourceLifetime::Persistent, renderPassDesc);
        HandleDescriptor framebuffersDesc{"std::vector<VkFramebuffer>"};
        INIT_INPUT_DESC(FRAMEBUFFERS, "framebuffers", ResourceLifetime::Transient, framebuffersDesc);
        HandleDescriptor pipelineDesc{"VkPipeline"};
        INIT_INPUT_DESC(PIPELINE, "pipeline", ResourceLifetime::Persistent, pipelineDesc);
        HandleDescriptor layoutDesc{"VkPipelineLayout"};
        INIT_INPUT_DESC(PIPELINE_LAYOUT, "pipeline_layout", ResourceLifetime::Persistent, layoutDesc);
        HandleDescriptor descriptorSetsDesc{"std::vector<VkDescriptorSet>"};
        INIT_INPUT_DESC(DESCRIPTOR_SETS, "descriptor_sets", ResourceLifetime::Persistent, descriptorSetsDesc);
        HandleDescriptor renderTargetDesc{"IRenderTarget*"};
        INIT_INPUT_DESC(COLOR_TARGET, "color_target", ResourceLifetime::Persistent, renderTargetDesc);
        HandleDescriptor commandPoolDesc{"VkCommandPool"};
        INIT_INPUT_DESC(COMMAND_POOL, "command_pool", ResourceLifetime::Persistent, commandPoolDesc);
        HandleDescriptor deviceDesc{"VulkanDevice*"};
        INIT_INPUT_DESC(VULKAN_DEVICE, "vulkan_device", ResourceLifetime::Persistent, deviceDesc);
        HandleDescriptor uintDesc{"uint32_t"};
        INIT_INPUT_DESC(CURRENT_FRAME_INDEX, "current_frame_index", ResourceLifetime::Transient, uintDesc);
        HandleDescriptor semaphoreDesc{"VkSemaphore"};
        INIT_INPUT_DESC(TIMELINE_SEMAPHORE_IN, "timeline_semaphore_in", ResourceLifetime::Persistent, semaphoreDesc);
        HandleDescriptor uint64Desc{"uint64_t"};
        INIT_INPUT_DESC(TIMELINE_FRAME_BASE_IN, "timeline_frame_base_in", ResourceLifetime::Transient, uint64Desc);
        HandleDescriptor cameraDesc{"CameraData"};
        INIT_INPUT_DESC(CAMERA_DATA, "camera_data", ResourceLifetime::Persistent, cameraDesc);
        HandleDescriptor matrixDesc{"glm::mat4"};
        INIT_INPUT_DESC(CURRENT_VIEW_PROJ, "current_view_proj", ResourceLifetime::Persistent, matrixDesc);
        INIT_INPUT_DESC(PROXY_AABB_COUNT, "proxy_aabb_count", ResourceLifetime::Transient, uintDesc);
        HandleDescriptor intDesc{"int32_t"};
        INIT_INPUT_DESC(INSTANCE_COUNT, "instance_count", ResourceLifetime::Transient, intDesc);
        HandleDescriptor bufferArrayDesc{"std::vector<VkBuffer>"};
        INIT_INPUT_DESC(BUFFER_WRITE_ARRAY, "buffer_write_array", ResourceLifetime::Transient, bufferArrayDesc);
        INIT_INPUT_DESC(COMPUTE_PIPELINE, "compute_pipeline", ResourceLifetime::Persistent, pipelineDesc);
        INIT_INPUT_DESC(COMPUTE_PIPELINE_LAYOUT, "compute_pipeline_layout", ResourceLifetime::Persistent, layoutDesc);
        INIT_INPUT_DESC(COMPUTE_DESCRIPTOR_SETS, "compute_descriptor_sets", ResourceLifetime::Persistent, descriptorSetsDesc);

        HandleDescriptor bufferDesc{"VkBuffer"};
        INIT_OUTPUT_DESC(BUFFER_OUT, "buffer_out", ResourceLifetime::Transient, bufferDesc);
        INIT_OUTPUT_DESC(VULKAN_DEVICE_OUT, "vulkan_device_out", ResourceLifetime::Persistent, deviceDesc);
    }

    VALIDATE_NODE_CONFIG(ProxyRasterStageNodeConfig, ProxyRasterStageNodeCounts);
    static_assert(BUFFER_WRITE_ARRAY_Slot::accessKind == AccessKind::ShaderStorageWrite);
    static_assert(COLOR_TARGET_Slot::accessKind == AccessKind::ColorAttachmentWrite);
};

} // namespace Vixen::RenderGraph
