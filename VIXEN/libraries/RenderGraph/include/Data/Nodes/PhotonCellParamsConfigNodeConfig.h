// Copyright (C) 2025 Lior Yanai (eLiorg). Licensed under the MIT License.
#pragma once

#include "Data/Core/ResourceConfig.h"
#include "VulkanDeviceFwd.h"

namespace Vixen::RenderGraph {

using VulkanDevice = Vixen::Vulkan::Resources::VulkanDevice;

namespace PhotonCellParamsConfigNodeCounts {
    static constexpr size_t INPUTS = 2;
    static constexpr size_t OUTPUTS = 1;
    static constexpr SlotArrayMode ARRAY_MODE = SlotArrayMode::Single;
}

/**
 * @brief Ring-buffered 48-byte photon-cell parameters.
 *
 * PreTick writes the current generation and owner knobs into the frame slot;
 * Execute re-emits that slot for the deposit/fold descriptors.  The block is
 * intentionally engine-side and is not a new codegen schema vocabulary.
 */
CONSTEXPR_NODE_CONFIG(PhotonCellParamsConfigNodeConfig,
                      PhotonCellParamsConfigNodeCounts::INPUTS,
                      PhotonCellParamsConfigNodeCounts::OUTPUTS,
                      PhotonCellParamsConfigNodeCounts::ARRAY_MODE) {
    INPUT_SLOT(VULKAN_DEVICE_IN, VulkanDevice*, 0,
        SlotNullability::Required, SlotRole::Dependency,
        SlotMutability::ReadOnly, SlotScope::NodeLevel);

    INPUT_SLOT(CURRENT_FRAME_INDEX, uint32_t, 1,
        SlotNullability::Required, SlotRole::Execute,
        SlotMutability::ReadOnly, SlotScope::NodeLevel);

    OUTPUT_SLOT(PHOTON_CELL_PARAMS_BUFFER, VkBuffer, 0,
        SlotNullability::Required, SlotMutability::WriteOnly);

    PhotonCellParamsConfigNodeConfig() {
        HandleDescriptor deviceDesc{"VulkanDevice*"};
        INIT_INPUT_DESC(VULKAN_DEVICE_IN, "vulkan_device",
                        ResourceLifetime::Persistent, deviceDesc);
        INIT_INPUT_DESC(CURRENT_FRAME_INDEX, "current_frame_index",
                        ResourceLifetime::Transient, BufferDescription{});

        BufferDescription paramsDesc{};
        paramsDesc.usage = ResourceUsage::StorageBuffer;
        paramsDesc.memoryProperties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                      VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        INIT_OUTPUT_DESC(PHOTON_CELL_PARAMS_BUFFER, "photon_cell_params_buffer",
                         ResourceLifetime::Transient, paramsDesc);
    }

    static_assert(VULKAN_DEVICE_IN_Slot::index == 0);
    static_assert(CURRENT_FRAME_INDEX_Slot::index == 1);
    static_assert(PHOTON_CELL_PARAMS_BUFFER_Slot::index == 0);
    static_assert(std::is_same_v<VULKAN_DEVICE_IN_Slot::Type, VulkanDevice*>);
    static_assert(std::is_same_v<CURRENT_FRAME_INDEX_Slot::Type, uint32_t>);
    static_assert(std::is_same_v<PHOTON_CELL_PARAMS_BUFFER_Slot::Type, VkBuffer>);

    VALIDATE_NODE_CONFIG(PhotonCellParamsConfigNodeConfig,
                         PhotonCellParamsConfigNodeCounts);
};

} // namespace Vixen::RenderGraph
