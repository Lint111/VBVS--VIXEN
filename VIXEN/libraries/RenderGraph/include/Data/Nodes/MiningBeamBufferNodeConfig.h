#pragma once
#include "Data/Core/ResourceConfig.h"
#include "VulkanDeviceFwd.h"

namespace Vixen::RenderGraph {

using VulkanDevice = Vixen::Vulkan::Resources::VulkanDevice;

namespace MiningBeamBufferNodeCounts {
    static constexpr size_t INPUTS = 2;
    static constexpr size_t OUTPUTS = 1;
    static constexpr SlotArrayMode ARRAY_MODE = SlotArrayMode::Single;
}

/** Uploads the runtime mining-beam list to a frame-ringed SSBO. */
CONSTEXPR_NODE_CONFIG(MiningBeamBufferNodeConfig,
                      MiningBeamBufferNodeCounts::INPUTS,
                      MiningBeamBufferNodeCounts::OUTPUTS,
                      MiningBeamBufferNodeCounts::ARRAY_MODE) {

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

    OUTPUT_SLOT(MINING_BEAM_BUFFER, VkBuffer, 0,
        SlotNullability::Required,
        SlotMutability::WriteOnly);

    MiningBeamBufferNodeConfig() {
        HandleDescriptor deviceDesc{"VulkanDevice*"};
        INIT_INPUT_DESC(VULKAN_DEVICE_IN, "vulkan_device", ResourceLifetime::Persistent, deviceDesc);

        HandleDescriptor uint32Desc{"uint32_t"};
        INIT_INPUT_DESC(CURRENT_FRAME_INDEX, "current_frame_index", ResourceLifetime::Transient, uint32Desc);

        HandleDescriptor bufferDesc{"VkBuffer"};
        INIT_OUTPUT_DESC(MINING_BEAM_BUFFER, "mining_beam_buffer", ResourceLifetime::Persistent, bufferDesc);
    }

    static_assert(VULKAN_DEVICE_IN_Slot::index == 0);
    static_assert(!VULKAN_DEVICE_IN_Slot::nullable);
    static_assert(std::is_same_v<VULKAN_DEVICE_IN_Slot::Type, VulkanDevice*>);
    static_assert(CURRENT_FRAME_INDEX_Slot::index == 1);
    static_assert(!CURRENT_FRAME_INDEX_Slot::nullable);
    static_assert(std::is_same_v<CURRENT_FRAME_INDEX_Slot::Type, uint32_t>);
    static_assert(MINING_BEAM_BUFFER_Slot::index == 0);
    static_assert(!MINING_BEAM_BUFFER_Slot::nullable);
    static_assert(std::is_same_v<MINING_BEAM_BUFFER_Slot::Type, VkBuffer>);

    VALIDATE_NODE_CONFIG(MiningBeamBufferNodeConfig, MiningBeamBufferNodeCounts);
};

} // namespace Vixen::RenderGraph
