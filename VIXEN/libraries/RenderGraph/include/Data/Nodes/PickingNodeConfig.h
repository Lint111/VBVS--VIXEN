#pragma once
#include "Data/Core/ResourceConfig.h"
#include "Data/InputState.h"
#include "Data/CameraData.h"

// Forward declaration for the CPU-side entity voxel world (non-wrapper pointer
// type — only consumed as a plain pointer for CPU-side spatial queries, so no
// conversion_type detection is needed; a forward decl is sufficient).
namespace Vixen::GaiaVoxel {
    class GaiaVoxelWorld;
}

namespace Vixen::RenderGraph {

// Compile-time slot counts
namespace PickingNodeCounts {
    static constexpr size_t INPUTS = 5;   // INPUT_STATE, CAMERA_DATA, VOXEL_WORLD, VIEWPORT_WIDTH, VIEWPORT_HEIGHT
    static constexpr size_t OUTPUTS = 1;   // LAST_PICK_HIT (status)
    static constexpr SlotArrayMode ARRAY_MODE = SlotArrayMode::Single;
}

/**
 * @brief Configuration for PickingNode (AR#35) — node type ID 125.
 *
 * CPU click-picking sink: on a left-click press edge it unprojects the cursor to
 * a world ray (ComputePickRay), marches the voxel world to the first solid voxel,
 * logs the hit/miss and publishes a PickResultEvent on the message bus.
 *
 * Inputs (5, all per-frame / Execute role, ReadOnly):
 *   - INPUT_STATE     (InputState*)              cursor + mouse buttons
 *   - CAMERA_DATA     (const CameraData&)        invProjection/invView/cameraPos (matches CameraNode's output slot type)
 *   - VOXEL_WORLD     (GaiaVoxelWorld*)          CPU voxel world for spatial queries (VoxelGridNode slot 10)
 *   - VIEWPORT_WIDTH  (uint32_t)                 swapchain width  (WindowNode WIDTH_OUT)
 *   - VIEWPORT_HEIGHT (uint32_t)                 swapchain height (WindowNode HEIGHT_OUT)
 *
 * Outputs (1):
 *   - LAST_PICK_HIT   (uint32_t)                 1 if the most recent pick hit a voxel, else 0.
 *
 * NOTE on output count: the graph's topological sort (GraphTopology::TopologicalSort)
 * visits ALL added nodes, so a pure 0-output sink WOULD still execute, and the config
 * macros (VALIDATE_NODE_CONFIG only checks count equality) permit OUTPUTS == 0. We
 * nonetheless expose LAST_PICK_HIT so the pick result is a first-class graph value a
 * future node (e.g. selection highlight) can consume directly — consistent with every
 * other node carrying at least one output. The authoritative result still flows via the
 * PickResultEvent on the bus.
 */
CONSTEXPR_NODE_CONFIG(PickingNodeConfig,
                      PickingNodeCounts::INPUTS,
                      PickingNodeCounts::OUTPUTS,
                      PickingNodeCounts::ARRAY_MODE) {
    // ===== INPUTS (5) =====
    // All per-frame inputs use SlotRole::Execute + ReadOnly (mirrors how CameraNode
    // declares INPUT_STATE / IMAGE_INDEX) so they are read during Execute, not Compile.
    INPUT_SLOT(INPUT_STATE, InputStatePtr, 0,
        SlotNullability::Required,
        SlotRole::Execute,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    // CameraData carried by reference — same slot type CameraNode OUTPUTS (const CameraData&),
    // so the direct connection rule matches type-for-type.
    INPUT_SLOT(CAMERA_DATA, const CameraData&, 1,
        SlotNullability::Required,
        SlotRole::Execute,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    INPUT_SLOT(VOXEL_WORLD, Vixen::GaiaVoxel::GaiaVoxelWorld*, 2,
        SlotNullability::Required,
        SlotRole::Execute,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    INPUT_SLOT(VIEWPORT_WIDTH, uint32_t, 3,
        SlotNullability::Required,
        SlotRole::Execute,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    INPUT_SLOT(VIEWPORT_HEIGHT, uint32_t, 4,
        SlotNullability::Required,
        SlotRole::Execute,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    // ===== OUTPUTS (1) =====
    OUTPUT_SLOT(LAST_PICK_HIT, uint32_t, 0,
        SlotNullability::Optional,
        SlotMutability::WriteOnly);

    // Constructor for runtime descriptor initialization
    PickingNodeConfig() {
        HandleDescriptor inputStateDesc{"InputState*"};
        INIT_INPUT_DESC(INPUT_STATE, "input_state", ResourceLifetime::Persistent, inputStateDesc);

        HandleDescriptor cameraDataDesc{"CameraDataPtr"};
        INIT_INPUT_DESC(CAMERA_DATA, "camera_data", ResourceLifetime::Persistent, cameraDataDesc);

        HandleDescriptor voxelWorldDesc{"Vixen::GaiaVoxel::GaiaVoxelWorld*"};
        INIT_INPUT_DESC(VOXEL_WORLD, "voxel_world", ResourceLifetime::Persistent, voxelWorldDesc);

        HandleDescriptor viewportWidthDesc{"uint32_t"};
        INIT_INPUT_DESC(VIEWPORT_WIDTH, "viewport_width", ResourceLifetime::Transient, viewportWidthDesc);

        HandleDescriptor viewportHeightDesc{"uint32_t"};
        INIT_INPUT_DESC(VIEWPORT_HEIGHT, "viewport_height", ResourceLifetime::Transient, viewportHeightDesc);

        HandleDescriptor lastPickHitDesc{"uint32_t"};
        INIT_OUTPUT_DESC(LAST_PICK_HIT, "last_pick_hit", ResourceLifetime::Transient, lastPickHitDesc);
    }

    // Automated config validation
    VALIDATE_NODE_CONFIG(PickingNodeConfig, PickingNodeCounts);

    // Slot index validations
    static_assert(INPUT_STATE_Slot::index == 0, "INPUT_STATE must be at index 0");
    static_assert(CAMERA_DATA_Slot::index == 1, "CAMERA_DATA must be at index 1");
    static_assert(VOXEL_WORLD_Slot::index == 2, "VOXEL_WORLD must be at index 2");
    static_assert(VIEWPORT_WIDTH_Slot::index == 3, "VIEWPORT_WIDTH must be at index 3");
    static_assert(VIEWPORT_HEIGHT_Slot::index == 4, "VIEWPORT_HEIGHT must be at index 4");
    static_assert(LAST_PICK_HIT_Slot::index == 0, "LAST_PICK_HIT must be at index 0");

    // Type validations
    static_assert(std::is_same_v<INPUT_STATE_Slot::Type, InputStatePtr>);
    static_assert(std::is_same_v<CAMERA_DATA_Slot::Type, const CameraData&>);
    static_assert(std::is_same_v<VOXEL_WORLD_Slot::Type, Vixen::GaiaVoxel::GaiaVoxelWorld*>);
    static_assert(std::is_same_v<VIEWPORT_WIDTH_Slot::Type, uint32_t>);
    static_assert(std::is_same_v<VIEWPORT_HEIGHT_Slot::Type, uint32_t>);
    static_assert(std::is_same_v<LAST_PICK_HIT_Slot::Type, uint32_t>);
};

} // namespace Vixen::RenderGraph
