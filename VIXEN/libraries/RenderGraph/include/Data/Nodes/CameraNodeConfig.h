#pragma once
#include "Data/Core/ResourceConfig.h"
#include "VulkanDeviceFwd.h"
#include "Data/InputState.h"
#include "Data/CameraData.h"

namespace Vixen::RenderGraph {

// Type alias for VulkanDevice pointer
using VulkanDevice = Vixen::Vulkan::Resources::VulkanDevice;

// Type alias for CameraData pointer (defined in ResourceVariant.h)
using CameraDataPtr = const CameraData*;

// Compile-time slot counts
namespace CameraNodeCounts {
    static constexpr size_t INPUTS = 4;  // Added INPUT_STATE
    static constexpr size_t OUTPUTS = 3;  // CameraData struct + PREV_VIEW_PROJ (Sampled Lighting Inc2 M3) + CURRENT_VIEW_PROJ (Recipe Bucketing Inc2 M1)
    static constexpr SlotArrayMode ARRAY_MODE = SlotArrayMode::Single;
}

/**
 * @brief Configuration for CameraNode
 *
 * Manages camera data for raymarching compute shaders.
 * Outputs a CameraData struct that can be used for push constants or uniform buffers.
 *
 * Inputs: 4 (VULKAN_DEVICE_IN, SWAPCHAIN_PUBLIC, IMAGE_INDEX, INPUT_STATE)
 * Outputs: 3 (CAMERA_DATA, PREV_VIEW_PROJ, CURRENT_VIEW_PROJ)
 */
CONSTEXPR_NODE_CONFIG(CameraNodeConfig,
                      CameraNodeCounts::INPUTS,
                      CameraNodeCounts::OUTPUTS,
                      CameraNodeCounts::ARRAY_MODE) {
    // ===== INPUTS (4) =====
    // Use generic INPUT_SLOT; lifetime (Persistent) is declared in INIT_INPUT_DESC
    INPUT_SLOT(VULKAN_DEVICE_IN, VulkanDevice*, 0,
        SlotNullability::Required,
        SlotRole::Dependency,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    INPUT_SLOT(SWAPCHAIN_PUBLIC, Vixen::Vulkan::Resources::IRenderTarget*, 1,
        SlotNullability::Required,
        SlotRole::Dependency,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    INPUT_SLOT(IMAGE_INDEX, uint32_t, 2,
        SlotNullability::Required,
        SlotRole::Execute,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    INPUT_SLOT(INPUT_STATE, InputStatePtr, 3,
        SlotNullability::Required,
        SlotRole::Execute,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    // ===== OUTPUTS (3) =====
    // Use generic OUTPUT_SLOT; lifetime (Persistent) is declared in INIT_OUTPUT_DESC
    OUTPUT_SLOT(CAMERA_DATA, const CameraData&, 0,
        SlotNullability::Required,
        SlotMutability::WriteOnly);

    // Sampled Lighting Inc2 M3: LAST frame's view*projection matrix (world -> prev-frame
    // clip space), retained by CameraNode (see ExecuteImpl/CompileImpl — compute-current-
    // then-store-previous each frame). A separate output slot rather than a new CameraData
    // field: CameraData's layout is frozen (it doubles as the push-constant source — see
    // that struct's own "MUST match shader layout exactly" header), and this mat4 is not a
    // push-constant field anyway (see PrevCameraConfigNode, binding 21). Read every Execute
    // by PrevCameraConfigNode to upload into its own ring buffer.
    OUTPUT_SLOT(PREV_VIEW_PROJ, const glm::mat4&, 1,
        SlotNullability::Required,
        SlotMutability::WriteOnly);

    // Recipe Bucketing Inc2 M1: THIS frame's view*projection matrix (world -> current-frame
    // clip space), a sibling of PREV_VIEW_PROJ using the exact same OUTPUT_SLOT/INIT_OUTPUT_DESC
    // pattern. No new computation -- CameraNode already computes `projection * view` every
    // Compile/Execute (see prevViewProj's own assignment sites), this just exposes that same
    // value under a new slot before it becomes "previous" next frame. Consumed by the recipe
    // instance-bucketing pre-pass to project world-space bound spheres to screen space.
    OUTPUT_SLOT(CURRENT_VIEW_PROJ, const glm::mat4&, 2,
        SlotNullability::Required,
        SlotMutability::WriteOnly);

    // ===== PARAMETERS =====
    static constexpr const char* PARAM_FOV = "fov";
    static constexpr const char* PARAM_NEAR_PLANE = "near";
    static constexpr const char* PARAM_FAR_PLANE = "far";
    static constexpr const char* PARAM_CAMERA_X = "camera_x";
    static constexpr const char* PARAM_CAMERA_Y = "camera_y";
    static constexpr const char* PARAM_CAMERA_Z = "camera_z";
    static constexpr const char* PARAM_YAW = "yaw";
    static constexpr const char* PARAM_PITCH = "pitch";
    static constexpr const char* PARAM_GRID_RESOLUTION = "grid_resolution";
    // Orbit-model pose requests: the render camera IS an orbit camera (position derived from
    // orbitCenter + yaw/pitch/orbitDistance every Execute — camera_x/y/z only seed the pre-orbit
    // state). These let a host/console re-anchor the orbit (e.g. click-to-fly to a body).
    // Defaults match the main app's Cornell-box demo scene (orbitCenter=(5,5,5), 10^3 world); a
    // consumer whose rendered geometry sits elsewhere (e.g. vixen_editor's object-centered
    // documents) must set these explicitly or the orbit camera frames empty space.
    static constexpr const char* PARAM_ORBIT_CENTER_X = "orbit_center_x";
    static constexpr const char* PARAM_ORBIT_CENTER_Y = "orbit_center_y";
    static constexpr const char* PARAM_ORBIT_CENTER_Z = "orbit_center_z";
    static constexpr const char* PARAM_ORBIT_DISTANCE = "orbit_distance";
    // Tiered-ESVO Inc3 M8: genuine look-target, independent of orbitCenter. When ANY of these
    // three is present, UpdateCameraData aims `forward` at (lookTarget - cameraPosition) instead
    // of (orbitCenter - cameraPosition) — breaking the "camera always looks at orbitCenter"
    // constraint that CameraNode.h's SetPitchForTest comment (and the Inc3 M6/M7 investigations)
    // documented as missing. Unset (the common case, every pre-M8 scene) leaves lookTarget_
    // exactly equal to orbitCenter every frame, so UpdateCameraData's forward computation is
    // byte-identical to pre-M8.
    static constexpr const char* PARAM_LOOK_TARGET_X = "look_target_x";
    static constexpr const char* PARAM_LOOK_TARGET_Y = "look_target_y";
    static constexpr const char* PARAM_LOOK_TARGET_Z = "look_target_z";
    // Forces reapply of every present pose param this SetupImpl even when its value is unchanged
    // from lastApplied (the applyIfChanged change-tracking normally skips a same-value write).
    // Field bug 2026-07-03: a console reset to a pose already equal to the stored value (e.g.
    // `lookcam 0 0` when yaw/pitch are already 0) was a silent no-op. The host bumps this on every
    // console pose write; CameraNode treats any change in its value as "reapply everything present".
    static constexpr const char* PARAM_POSE_SEQ = "pose_seq";

    // Per-frame resources (ring buffer)
    static constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 4;

    // Constructor for runtime descriptor initialization
    CameraNodeConfig() {
        // Initialize input descriptors
        HandleDescriptor vulkanDeviceDesc{"VulkanDevice*"};
        INIT_INPUT_DESC(VULKAN_DEVICE_IN, "vulkan_device", ResourceLifetime::Persistent, vulkanDeviceDesc);

        HandleDescriptor swapchainDesc{"IRenderTarget*"};
        INIT_INPUT_DESC(SWAPCHAIN_PUBLIC, "swapchain_public", ResourceLifetime::Persistent, swapchainDesc);

        HandleDescriptor imageIndexDesc{"uint32_t"};
        INIT_INPUT_DESC(IMAGE_INDEX, "image_index", ResourceLifetime::Transient, imageIndexDesc);

        HandleDescriptor inputStateDesc{"InputState*"};
        INIT_INPUT_DESC(INPUT_STATE, "input_state", ResourceLifetime::Persistent, inputStateDesc);

        // Initialize output descriptor
        HandleDescriptor cameraDataDesc{"CameraDataPtr"};
        INIT_OUTPUT_DESC(CAMERA_DATA, "camera_data", ResourceLifetime::Persistent, cameraDataDesc);

        // Sampled Lighting Inc2 M3: prev-frame view*proj, re-published every Execute
        // (Persistent — the CameraNode instance is stable; the matrix it refers to updates
        // fresh each frame, same convention as CAMERA_DATA itself).
        HandleDescriptor prevViewProjDesc{"glm::mat4"};
        INIT_OUTPUT_DESC(PREV_VIEW_PROJ, "prev_view_proj", ResourceLifetime::Persistent, prevViewProjDesc);

        // Recipe Bucketing Inc2 M1: current-frame view*proj, re-published every Execute
        // alongside PREV_VIEW_PROJ (same Persistent convention -- see that slot's comment).
        HandleDescriptor currentViewProjDesc{"glm::mat4"};
        INIT_OUTPUT_DESC(CURRENT_VIEW_PROJ, "current_view_proj", ResourceLifetime::Persistent, currentViewProjDesc);
    }

    // Automated config validation
    VALIDATE_NODE_CONFIG(CameraNodeConfig, CameraNodeCounts);

    // Slot index validations
    static_assert(VULKAN_DEVICE_IN_Slot::index == 0, "VULKAN_DEVICE_IN must be at index 0");
    static_assert(SWAPCHAIN_PUBLIC_Slot::index == 1, "SWAPCHAIN_PUBLIC must be at index 1");
    static_assert(IMAGE_INDEX_Slot::index == 2, "IMAGE_INDEX must be at index 2");
    static_assert(INPUT_STATE_Slot::index == 3, "INPUT_STATE must be at index 3");
    static_assert(CAMERA_DATA_Slot::index == 0, "CAMERA_DATA must be at index 0");
    static_assert(PREV_VIEW_PROJ_Slot::index == 1, "PREV_VIEW_PROJ must be at index 1");
    static_assert(CURRENT_VIEW_PROJ_Slot::index == 2, "CURRENT_VIEW_PROJ must be at index 2");

    // Type validations
    static_assert(std::is_same_v<VULKAN_DEVICE_IN_Slot::Type, VulkanDevice*>);
    static_assert(std::is_same_v<SWAPCHAIN_PUBLIC_Slot::Type, Vixen::Vulkan::Resources::IRenderTarget*>);
    static_assert(std::is_same_v<IMAGE_INDEX_Slot::Type, uint32_t>);
    static_assert(std::is_same_v<INPUT_STATE_Slot::Type, InputStatePtr>);
    static_assert(std::is_same_v<CAMERA_DATA_Slot::Type, const CameraData&>);
    static_assert(std::is_same_v<PREV_VIEW_PROJ_Slot::Type, const glm::mat4&>);
    static_assert(std::is_same_v<CURRENT_VIEW_PROJ_Slot::Type, const glm::mat4&>);
};

} // namespace Vixen::RenderGraph

