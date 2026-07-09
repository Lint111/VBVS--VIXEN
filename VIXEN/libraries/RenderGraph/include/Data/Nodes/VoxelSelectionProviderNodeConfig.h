#pragma once
#include "Data/Core/ResourceConfig.h"
#include "Data/InputState.h"
#include "Selection/SelectionCandidate.h"
#include "VulkanDeviceFwd.h"
#include <vulkan/vulkan.h>

namespace Vixen::RenderGraph {

// Type alias for VulkanDevice (use VulkanDevice* explicitly in slots)
using VulkanDevice = Vixen::Vulkan::Resources::VulkanDevice;

// Compile-time slot counts
namespace VoxelSelectionProviderNodeCounts {
    static constexpr size_t INPUTS = 7;   // INPUT_STATE, ID_IMAGE, VULKAN_DEVICE, COMMAND_POOL, CURRENT_FRAME_INDEX, VIEWPORT_WIDTH, VIEWPORT_HEIGHT
    static constexpr size_t OUTPUTS = 1;  // CANDIDATE (SelectionCandidate)
    static constexpr SlotArrayMode ARRAY_MODE = SlotArrayMode::Single;
}

/**
 * @brief Configuration for VoxelSelectionProviderNode (SEL-P2 — providers are nodes).
 *
 * The voxel-domain selection PROVIDER, now a first-class graph node (was the C++
 * VoxelSelectionProvider object the coordinator owned). On a left-click down-edge it
 * copies the crosshair texel of PickIdTargetNode's pick-ID image (binding-9 target),
 * decodes the packed pickID (brick<<10 | voxel), and emits a SelectionCandidate on its
 * CANDIDATE output. The SelectionCoordinatorNode gathers this candidate (and any other
 * provider nodes' candidates) through its MultiConnect PROVIDER_CANDIDATES slot and
 * priority-resolves. Off the click edge — or on a miss — it emits {hit=false}.
 *
 * The GPU readback (one-shot fenced vkCmdCopyImageToBuffer + host-visible staging
 * buffer) was MOVED here from the old C++ provider, not rewritten. The device is taken
 * from the base NodeInstance (SetDevice in CompileImpl from VULKAN_DEVICE; GetDevice()
 * elsewhere) per the device convention — no private device member.
 *
 * Inputs (7):
 *   - INPUT_STATE         (InputState*, Execute)       mouse buttons (left-click edge)
 *   - ID_IMAGE            (VkImage, Dependency)        current-frame pick-ID image (PickIdTargetNode.ID_IMAGE)
 *   - VULKAN_DEVICE       (VulkanDevice*, Dependency)  device for the one-shot copy + staging buffer
 *   - COMMAND_POOL        (VkCommandPool, Dependency)  pool for the one-shot readback command buffer
 *   - CURRENT_FRAME_INDEX (uint32_t, Execute)          frame-in-flight index (diagnostics / future ring use)
 *   - VIEWPORT_WIDTH      (uint32_t, Execute)          pick-ID image width  (M4: RenderTargetNode WIDTH_OUT — the
 *                                                       RENDER extent, matching PickIdTargetNode's sizing) — center offset
 *   - VIEWPORT_HEIGHT     (uint32_t, Execute)          pick-ID image height (M4: RenderTargetNode HEIGHT_OUT) — center offset
 *
 * Outputs (1):
 *   - CANDIDATE (SelectionCandidate)   this provider's per-click result (hit/miss + id/priority).
 *
 * Param:
 *   - PARAM_PRIORITY (int, default 0 = world layer). Stamped on the candidate; the coordinator
 *     resolves by max priority (UI providers register higher to occlude the world).
 */
CONSTEXPR_NODE_CONFIG(VoxelSelectionProviderNodeConfig,
                      VoxelSelectionProviderNodeCounts::INPUTS,
                      VoxelSelectionProviderNodeCounts::OUTPUTS,
                      VoxelSelectionProviderNodeCounts::ARRAY_MODE) {

    // ===== PARAMETER NAMES =====
    /// Provider layer priority (int). Higher occludes lower. Default 0 = world layer.
    static constexpr const char* PARAM_PRIORITY = "priority";

    // ===== INPUTS (7) =====
    // Per-frame inputs (InputState, frame index, viewport) use SlotRole::Execute + ReadOnly.
    INPUT_SLOT(INPUT_STATE, InputStatePtr, 0,
        SlotNullability::Required,
        SlotRole::Execute,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    // ID_IMAGE: per-pixel pick-ID VkImage from PickIdTargetNode (binding-9 target). The ring is
    // stable for the cached scene's lifetime, so consume it as a Dependency (cached in CompileImpl);
    // the single-texel copy happens only on a click edge.
    INPUT_SLOT(ID_IMAGE, VkImage, 1,
        SlotNullability::Required,
        SlotRole::Dependency,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    // Device for the one-shot copy submit (device->queue) + the host-visible staging buffer.
    INPUT_SLOT(VULKAN_DEVICE, VulkanDevice*, 2,
        SlotNullability::Required,
        SlotRole::Dependency,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    // Command pool for the one-shot readback command buffer (CommandPoolNode).
    INPUT_SLOT(COMMAND_POOL, VkCommandPool, 3,
        SlotNullability::Required,
        SlotRole::Dependency,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    // Frame-in-flight index (FrameSyncNode) — Execute role. Kept for diagnostics / a possible
    // previous-completed-frame fallback; the copy targets the configured ID_IMAGE.
    INPUT_SLOT(CURRENT_FRAME_INDEX, uint32_t, 4,
        SlotNullability::Required,
        SlotRole::Execute,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    INPUT_SLOT(VIEWPORT_WIDTH, uint32_t, 5,
        SlotNullability::Required,
        SlotRole::Execute,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    INPUT_SLOT(VIEWPORT_HEIGHT, uint32_t, 6,
        SlotNullability::Required,
        SlotRole::Execute,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    // ===== OUTPUTS (1) =====
    // CANDIDATE is emitted every Execute (hit=false off the click edge) so the coordinator's
    // accumulation slot always has a fresh value from this source. Optional nullability mirrors
    // other value outputs; the coordinator treats an absent candidate as a miss.
    OUTPUT_SLOT(CANDIDATE, SelectionCandidate, 0,
        SlotNullability::Optional,
        SlotMutability::WriteOnly);

    // Constructor for runtime descriptor initialization
    VoxelSelectionProviderNodeConfig() {
        HandleDescriptor inputStateDesc{"InputState*"};
        INIT_INPUT_DESC(INPUT_STATE, "input_state", ResourceLifetime::Persistent, inputStateDesc);

        HandleDescriptor idImageDesc{"VkImage"};
        INIT_INPUT_DESC(ID_IMAGE, "id_image", ResourceLifetime::Persistent, idImageDesc);

        HandleDescriptor deviceDesc{"VulkanDevice*"};
        INIT_INPUT_DESC(VULKAN_DEVICE, "vulkan_device", ResourceLifetime::Persistent, deviceDesc);

        HandleDescriptor commandPoolDesc{"VkCommandPool"};
        INIT_INPUT_DESC(COMMAND_POOL, "command_pool", ResourceLifetime::Persistent, commandPoolDesc);

        HandleDescriptor uint32Desc{"uint32_t"};
        INIT_INPUT_DESC(CURRENT_FRAME_INDEX, "current_frame_index", ResourceLifetime::Transient, uint32Desc);
        INIT_INPUT_DESC(VIEWPORT_WIDTH,  "viewport_width",  ResourceLifetime::Transient, uint32Desc);
        INIT_INPUT_DESC(VIEWPORT_HEIGHT, "viewport_height", ResourceLifetime::Transient, uint32Desc);

        HandleDescriptor candidateDesc{"SelectionCandidate"};
        INIT_OUTPUT_DESC(CANDIDATE, "candidate", ResourceLifetime::Transient, candidateDesc);
    }

    // Automated config validation
    VALIDATE_NODE_CONFIG(VoxelSelectionProviderNodeConfig, VoxelSelectionProviderNodeCounts);

    // Slot index validations
    static_assert(INPUT_STATE_Slot::index == 0, "INPUT_STATE must be at index 0");
    static_assert(ID_IMAGE_Slot::index == 1, "ID_IMAGE must be at index 1");
    static_assert(VULKAN_DEVICE_Slot::index == 2, "VULKAN_DEVICE must be at index 2");
    static_assert(COMMAND_POOL_Slot::index == 3, "COMMAND_POOL must be at index 3");
    static_assert(CURRENT_FRAME_INDEX_Slot::index == 4, "CURRENT_FRAME_INDEX must be at index 4");
    static_assert(VIEWPORT_WIDTH_Slot::index == 5, "VIEWPORT_WIDTH must be at index 5");
    static_assert(VIEWPORT_HEIGHT_Slot::index == 6, "VIEWPORT_HEIGHT must be at index 6");
    static_assert(CANDIDATE_Slot::index == 0, "CANDIDATE must be at index 0");

    // Type validations
    static_assert(std::is_same_v<INPUT_STATE_Slot::Type, InputStatePtr>);
    static_assert(std::is_same_v<ID_IMAGE_Slot::Type, VkImage>);
    static_assert(std::is_same_v<VULKAN_DEVICE_Slot::Type, VulkanDevice*>);
    static_assert(std::is_same_v<COMMAND_POOL_Slot::Type, VkCommandPool>);
    static_assert(std::is_same_v<CURRENT_FRAME_INDEX_Slot::Type, uint32_t>);
    static_assert(std::is_same_v<VIEWPORT_WIDTH_Slot::Type, uint32_t>);
    static_assert(std::is_same_v<VIEWPORT_HEIGHT_Slot::Type, uint32_t>);
    static_assert(std::is_same_v<CANDIDATE_Slot::Type, SelectionCandidate>);
};

} // namespace Vixen::RenderGraph
