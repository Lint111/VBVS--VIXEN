#pragma once

#include "Data/Core/ResourceConfig.h"
#include "Data/CameraData.h"
#include "VulkanDeviceFwd.h"

namespace Vixen::RenderGraph {

using VulkanDevice = Vixen::Vulkan::Resources::VulkanDevice;

namespace TileBinViewNodeCounts {
    static constexpr size_t INPUTS = 6;
    static constexpr size_t OUTPUTS = 1;
    static constexpr SlotArrayMode ARRAY_MODE = SlotArrayMode::Single;
}

// GB1 (R857): publishes the per-frame camera projection record consumed by
// the generated tile scatter pass. The buffer is ringed because the generated
// pass reads the record asynchronously after this CPU node executes.
CONSTEXPR_NODE_CONFIG(TileBinViewNodeConfig,
                      TileBinViewNodeCounts::INPUTS,
                      TileBinViewNodeCounts::OUTPUTS,
                      TileBinViewNodeCounts::ARRAY_MODE) {
    INPUT_SLOT(VULKAN_DEVICE_IN, VulkanDevice*, 0, SlotNullability::Required,
               SlotRole::Dependency, SlotMutability::ReadOnly, SlotScope::NodeLevel);
    INPUT_SLOT(CURRENT_FRAME_INDEX, uint32_t, 1, SlotNullability::Required,
               SlotRole::Execute, SlotMutability::ReadOnly, SlotScope::NodeLevel);
    INPUT_SLOT(CAMERA_DATA, const CameraData&, 2, SlotNullability::Required,
               SlotRole::Execute, SlotMutability::ReadOnly, SlotScope::NodeLevel);
    INPUT_SLOT(WIDTH, uint32_t, 3, SlotNullability::Required,
               SlotRole::Execute, SlotMutability::ReadOnly, SlotScope::NodeLevel);
    INPUT_SLOT(HEIGHT, uint32_t, 4, SlotNullability::Required,
               SlotRole::Execute, SlotMutability::ReadOnly, SlotScope::NodeLevel);
    INPUT_SLOT(INSTANCE_COUNT, int32_t, 5, SlotNullability::Required,
               SlotRole::Execute, SlotMutability::ReadOnly, SlotScope::NodeLevel);

    OUTPUT_SLOT(VIEW_BUFFER, VkBuffer, 0, SlotNullability::Required,
                SlotMutability::WriteOnly);

    TileBinViewNodeConfig() {
        HandleDescriptor deviceDesc{"VulkanDevice*"};
        INIT_INPUT_DESC(VULKAN_DEVICE_IN, "vulkan_device", ResourceLifetime::Persistent, deviceDesc);
        INIT_INPUT_DESC(CURRENT_FRAME_INDEX, "current_frame_index", ResourceLifetime::Transient,
                        BufferDescription{});
        HandleDescriptor cameraDesc{"CameraData"};
        INIT_INPUT_DESC(CAMERA_DATA, "camera_data", ResourceLifetime::Persistent, cameraDesc);
        INIT_INPUT_DESC(WIDTH, "width", ResourceLifetime::Transient, BufferDescription{});
        INIT_INPUT_DESC(HEIGHT, "height", ResourceLifetime::Transient, BufferDescription{});
        INIT_INPUT_DESC(INSTANCE_COUNT, "instance_count", ResourceLifetime::Transient, BufferDescription{});
        BufferDescription viewDesc{};
        viewDesc.usage = ResourceUsage::StorageBuffer;
        viewDesc.memoryProperties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                     VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        INIT_OUTPUT_DESC(VIEW_BUFFER, "tile_bin_view_buffer", ResourceLifetime::Transient, viewDesc);
    }

    VALIDATE_NODE_CONFIG(TileBinViewNodeConfig, TileBinViewNodeCounts);
};

} // namespace Vixen::RenderGraph
