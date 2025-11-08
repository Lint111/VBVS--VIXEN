#pragma once
#include "Data/Core/ResourceConfig.h"
#include "VulkanResources/VulkanDevice.h"

namespace Vixen::RenderGraph {

// Type alias for VulkanDevice pointer
using VulkanDevicePtr = Vixen::Vulkan::Resources::VulkanDevice*;

// Compile-time slot counts
namespace CameraNodeCounts {
    static constexpr size_t INPUTS = 3;
    static constexpr size_t OUTPUTS = 2;
    static constexpr SlotArrayMode ARRAY_MODE = SlotArrayMode::Single;
}

/**
 * @brief Configuration for CameraNode
 *
 * Manages camera uniform buffer for raymarching compute shaders.
 * Updates per-frame with inverse matrices and position.
 *
 * Inputs: 3 (VULKAN_DEVICE_IN, SWAPCHAIN_PUBLIC, IMAGE_INDEX)
 * Outputs: 2 (CAMERA_BUFFER, CAMERA_BUFFER_SIZE)
 */
CONSTEXPR_NODE_CONFIG(CameraNodeConfig,
                      CameraNodeCounts::INPUTS,
                      CameraNodeCounts::OUTPUTS,
                      CameraNodeCounts::ARRAY_MODE) {
    // ===== INPUTS (3) =====
    INPUT_SLOT(VULKAN_DEVICE_IN, VulkanDevicePtr, 0,
        SlotNullability::Required,
        SlotRole::Dependency,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    INPUT_SLOT(SWAPCHAIN_PUBLIC, SwapChainPublicVariables*, 1,
        SlotNullability::Required,
        SlotRole::Dependency,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    INPUT_SLOT(IMAGE_INDEX, uint32_t, 2,
        SlotNullability::Required,
        SlotRole::Execute,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    // ===== OUTPUTS (2) =====
    OUTPUT_SLOT(CAMERA_BUFFER, VkBuffer, 0,
        SlotNullability::Required,
        SlotMutability::WriteOnly);

    OUTPUT_SLOT(CAMERA_BUFFER_SIZE, uint64_t, 1,
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

    // Per-frame resources (ring buffer)
    static constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 4;

    // Constructor for runtime descriptor initialization
    CameraNodeConfig() {
        // Initialize input descriptors
        HandleDescriptor vulkanDeviceDesc{"VulkanDevice*"};
        INIT_INPUT_DESC(VULKAN_DEVICE_IN, "vulkan_device", ResourceLifetime::Persistent, vulkanDeviceDesc);

        HandleDescriptor swapchainDesc{"SwapChainPublicVariables*"};
        INIT_INPUT_DESC(SWAPCHAIN_PUBLIC, "swapchain_public", ResourceLifetime::Persistent, swapchainDesc);

        HandleDescriptor imageIndexDesc{"uint32_t"};
        INIT_INPUT_DESC(IMAGE_INDEX, "image_index", ResourceLifetime::Transient, imageIndexDesc);

        // Initialize output descriptors
        BufferDescriptor cameraBufferDesc{};
        cameraBufferDesc.size = 144;  // sizeof(CameraData)
        cameraBufferDesc.usage = ResourceUsage::UniformBuffer;
        INIT_OUTPUT_DESC(CAMERA_BUFFER, "camera_buffer", ResourceLifetime::Persistent, cameraBufferDesc);

        HandleDescriptor sizeDesc{"uint64_t"};
        INIT_OUTPUT_DESC(CAMERA_BUFFER_SIZE, "camera_buffer_size", ResourceLifetime::Persistent, sizeDesc);
    }

    // Compile-time validations
    static_assert(INPUT_COUNT == CameraNodeCounts::INPUTS, "Input count mismatch");
    static_assert(OUTPUT_COUNT == CameraNodeCounts::OUTPUTS, "Output count mismatch");
    static_assert(ARRAY_MODE == CameraNodeCounts::ARRAY_MODE, "Array mode mismatch");

    static_assert(VULKAN_DEVICE_IN_Slot::index == 0, "VULKAN_DEVICE_IN must be at index 0");
    static_assert(SWAPCHAIN_PUBLIC_Slot::index == 1, "SWAPCHAIN_PUBLIC must be at index 1");
    static_assert(IMAGE_INDEX_Slot::index == 2, "IMAGE_INDEX must be at index 2");
    static_assert(CAMERA_BUFFER_Slot::index == 0, "CAMERA_BUFFER must be at index 0");
    static_assert(CAMERA_BUFFER_SIZE_Slot::index == 1, "CAMERA_BUFFER_SIZE must be at index 1");

    // Type validations
    static_assert(std::is_same_v<VULKAN_DEVICE_IN_Slot::Type, VulkanDevicePtr>);
    static_assert(std::is_same_v<SWAPCHAIN_PUBLIC_Slot::Type, SwapChainPublicVariables*>);
    static_assert(std::is_same_v<IMAGE_INDEX_Slot::Type, uint32_t>);
    static_assert(std::is_same_v<CAMERA_BUFFER_Slot::Type, VkBuffer>);
    static_assert(std::is_same_v<CAMERA_BUFFER_SIZE_Slot::Type, uint64_t>);
};

// Global compile-time validations
static_assert(CameraNodeConfig::INPUT_COUNT == CameraNodeCounts::INPUTS);
static_assert(CameraNodeConfig::OUTPUT_COUNT == CameraNodeCounts::OUTPUTS);

} // namespace Vixen::RenderGraph
