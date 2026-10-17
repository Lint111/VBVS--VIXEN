#pragma once

#include "Data/Core/ResourceConfig.h"
#include "Data/CameraData.h"
#include "IRenderTarget.h"

namespace Vixen::RenderGraph {

CONSTEXPR_NODE_CONFIG(ReconstructionHistoryStateNodeConfig, 3, 5, SlotArrayMode::Single) {
    INPUT_SLOT(CAMERA, const CameraData&, 0, SlotNullability::Required,
               SlotRole::Execute, SlotMutability::ReadOnly, SlotScope::NodeLevel);
    INPUT_SLOT(INTERNAL_TARGET, Vixen::Vulkan::Resources::IRenderTarget*, 1,
               SlotNullability::Required, SlotRole::Dependency | SlotRole::Execute,
               SlotMutability::ReadOnly, SlotScope::NodeLevel);
    INPUT_SLOT(OUTPUT_TARGET, Vixen::Vulkan::Resources::IRenderTarget*, 2,
               SlotNullability::Required, SlotRole::Dependency | SlotRole::Execute,
               SlotMutability::ReadOnly, SlotScope::NodeLevel);
    OUTPUT_SLOT(RESET, uint32_t, 0, SlotNullability::Required, SlotMutability::WriteOnly);
    OUTPUT_SLOT(VALID, uint32_t, 1, SlotNullability::Required, SlotMutability::WriteOnly);
    OUTPUT_SLOT(FRAME_INDEX, uint32_t, 2, SlotNullability::Required, SlotMutability::WriteOnly);
    OUTPUT_SLOT(WRITE_INDEX, uint32_t, 3, SlotNullability::Required, SlotMutability::WriteOnly);
    OUTPUT_SLOT(RESET_REASONS, uint32_t, 4, SlotNullability::Required, SlotMutability::WriteOnly);

    ReconstructionHistoryStateNodeConfig() {
        HandleDescriptor camera{"CameraData"}, target{"IRenderTarget*"}, scalar{"uint32_t"};
        INIT_INPUT_DESC(CAMERA, "camera", ResourceLifetime::Persistent, camera);
        INIT_INPUT_DESC(INTERNAL_TARGET, "internal_target", ResourceLifetime::Persistent, target);
        INIT_INPUT_DESC(OUTPUT_TARGET, "output_target", ResourceLifetime::Persistent, target);
        INIT_OUTPUT_DESC(RESET, "reset", ResourceLifetime::Transient, scalar);
        INIT_OUTPUT_DESC(VALID, "valid", ResourceLifetime::Transient, scalar);
        INIT_OUTPUT_DESC(FRAME_INDEX, "frame_index", ResourceLifetime::Transient, scalar);
        INIT_OUTPUT_DESC(WRITE_INDEX, "write_index", ResourceLifetime::Transient, scalar);
        INIT_OUTPUT_DESC(RESET_REASONS, "reset_reasons", ResourceLifetime::Transient, scalar);
    }
};

} // namespace Vixen::RenderGraph
