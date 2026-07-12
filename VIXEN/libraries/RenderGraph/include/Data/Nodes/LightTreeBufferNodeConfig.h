#pragma once
#include "Data/Core/ResourceConfig.h"
#include "VulkanDeviceFwd.h"

namespace Vixen::RenderGraph {

using VulkanDevice = Vixen::Vulkan::Resources::VulkanDevice;

namespace LightTreeBufferNodeCounts {
    static constexpr size_t INPUTS  = 2;  // VULKAN_DEVICE_IN, CURRENT_FRAME_INDEX
    static constexpr size_t OUTPUTS = 1;  // LIGHT_TREE_BUFFER
    static constexpr SlotArrayMode ARRAY_MODE = SlotArrayMode::Single;
}

/**
 * @brief Pure constexpr resource configuration for LightTreeBufferNode (Sampled
 * Lighting Inc3 M4).
 *
 * Uploads the CPU-computed mip-cut light-tree (LightTree.h's BuildLightTreeCut output,
 * pushed via LightTreeBufferNode::SetLightTreeCut — mirrors BodyOctreeSceneNode's
 * SetInstances host->node seam) into a ring-buffered SSBO (Vixen::Gpu::LightTreeBuffer,
 * kMaxLightTreeNodes=64 fixed-capacity array), one buffer per frame-in-flight — same
 * PerFrameResources ring pattern as ReservoirConfigNode/ShadowConfigNode.
 *
 * Inputs: 2
 *   - VULKAN_DEVICE_IN     (VulkanDevice*)  Device for allocation
 *   - CURRENT_FRAME_INDEX  (uint32_t)       Ring index (FrameSyncNode)
 * Outputs: 1
 *   - LIGHT_TREE_BUFFER (VkBuffer)  This frame's ring slot, freshly uploaded
 */
CONSTEXPR_NODE_CONFIG(LightTreeBufferNodeConfig,
                      LightTreeBufferNodeCounts::INPUTS,
                      LightTreeBufferNodeCounts::OUTPUTS,
                      LightTreeBufferNodeCounts::ARRAY_MODE) {

    INPUT_SLOT(VULKAN_DEVICE_IN, VulkanDevice*, 0,
        SlotNullability::Required,
        SlotRole::Dependency,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    INPUT_SLOT(CURRENT_FRAME_INDEX, uint32_t, 1,
        SlotNullability::Required,
        SlotRole::Execute,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    OUTPUT_SLOT(LIGHT_TREE_BUFFER, VkBuffer, 0,
        SlotNullability::Required,
        SlotMutability::WriteOnly);

    LightTreeBufferNodeConfig() {
        HandleDescriptor deviceDesc{"VulkanDevice*"};
        INIT_INPUT_DESC(VULKAN_DEVICE_IN, "vulkan_device", ResourceLifetime::Persistent, deviceDesc);

        HandleDescriptor uint32Desc{"uint32_t"};
        INIT_INPUT_DESC(CURRENT_FRAME_INDEX, "current_frame_index", ResourceLifetime::Transient, uint32Desc);

        HandleDescriptor bufferDesc{"VkBuffer"};
        INIT_OUTPUT_DESC(LIGHT_TREE_BUFFER, "light_tree_buffer", ResourceLifetime::Persistent, bufferDesc);
    }

    static_assert(VULKAN_DEVICE_IN_Slot::index == 0, "VULKAN_DEVICE_IN must be at index 0");
    static_assert(!VULKAN_DEVICE_IN_Slot::nullable, "VULKAN_DEVICE_IN must not be nullable");
    static_assert(std::is_same_v<VULKAN_DEVICE_IN_Slot::Type, VulkanDevice*>);
    static_assert(LIGHT_TREE_BUFFER_Slot::index == 0, "LIGHT_TREE_BUFFER must be at index 0");
    static_assert(!LIGHT_TREE_BUFFER_Slot::nullable, "LIGHT_TREE_BUFFER must not be nullable");
    static_assert(std::is_same_v<LIGHT_TREE_BUFFER_Slot::Type, VkBuffer>);

    VALIDATE_NODE_CONFIG(LightTreeBufferNodeConfig, LightTreeBufferNodeCounts);
};

} // namespace Vixen::RenderGraph
