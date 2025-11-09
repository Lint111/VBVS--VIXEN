#include "Nodes/CameraNode.h"
#include "VulkanResources/VulkanDevice.h"
#include "VulkanSwapChain.h"
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include <cstring>

namespace Vixen::RenderGraph {

// ============================================================================
// NODE TYPE FACTORY
// ============================================================================

std::unique_ptr<NodeInstance> CameraNodeType::CreateInstance(
    const std::string& instanceName
) const {
    return std::make_unique<CameraNode>(instanceName, const_cast<CameraNodeType*>(this));
}

// ============================================================================
// CAMERA NODE IMPLEMENTATION
// ============================================================================

CameraNode::CameraNode(
    const std::string& instanceName,
    NodeType* nodeType
) : TypedNode<CameraNodeConfig>(instanceName, nodeType)
{
    NODE_LOG_INFO("CameraNode constructor");
}

void CameraNode::SetupImpl(TypedSetupContext& ctx) {
    NODE_LOG_INFO("CameraNode setup");

    // Read parameters
    fov = GetParameterValue<float>(CameraNodeConfig::PARAM_FOV, 45.0f);
    nearPlane = GetParameterValue<float>(CameraNodeConfig::PARAM_NEAR_PLANE, 0.1f);
    farPlane = GetParameterValue<float>(CameraNodeConfig::PARAM_FAR_PLANE, 1000.0f);

    cameraPosition.x = GetParameterValue<float>(CameraNodeConfig::PARAM_CAMERA_X, 0.0f);
    cameraPosition.y = GetParameterValue<float>(CameraNodeConfig::PARAM_CAMERA_Y, 0.0f);
    cameraPosition.z = GetParameterValue<float>(CameraNodeConfig::PARAM_CAMERA_Z, 3.0f);

    yaw = GetParameterValue<float>(CameraNodeConfig::PARAM_YAW, 0.0f);
    pitch = GetParameterValue<float>(CameraNodeConfig::PARAM_PITCH, 0.0f);

    gridResolution = GetParameterValue<uint32_t>(CameraNodeConfig::PARAM_GRID_RESOLUTION, 128u);
}

void CameraNode::CompileImpl(TypedCompileContext& ctx) {
    NODE_LOG_INFO("CameraNode compile");

    // Get device
    VulkanDevicePtr devicePtr = ctx.In(CameraNodeConfig::VULKAN_DEVICE_IN);
    if (!devicePtr) {
        throw std::runtime_error("[CameraNode] VULKAN_DEVICE_IN is null");
    }

    SetDevice(devicePtr);
    vulkanDevice = devicePtr;

    // Get swapchain info for initial aspect ratio
    SwapChainPublicVariables* swapchainInfo = ctx.In(CameraNodeConfig::SWAPCHAIN_PUBLIC);
    if (!swapchainInfo) {
        throw std::runtime_error("[CameraNode] SWAPCHAIN_PUBLIC is null");
    }

    // Create a SINGLE camera UBO shared by all frames
    // This is safe because frame-in-flight synchronization ensures the GPU
    // has finished reading frame N before the CPU updates it for frame N+3
    // All descriptor sets will point to this one buffer
    NODE_LOG_INFO("Creating single shared camera UBO");

    perFrameResources.Initialize(vulkanDevice, 1);  // Only 1 buffer

    VkDeviceSize bufferSize = sizeof(CameraData);
    perFrameResources.CreateUniformBuffer(0, bufferSize);

    // Initialize with valid camera data
    float aspectRatio = static_cast<float>(swapchainInfo->Extent.width) /
                        static_cast<float>(swapchainInfo->Extent.height);
    UpdateCameraMatrices(0, 0, aspectRatio);

    // Output the buffer (same for all descriptor sets)
    ctx.Out(CameraNodeConfig::CAMERA_BUFFER, perFrameResources.GetUniformBuffer(0));
    ctx.Out(CameraNodeConfig::CAMERA_BUFFER_SIZE, static_cast<uint64_t>(bufferSize));

    NODE_LOG_INFO("Created shared camera UBO successfully");
}

void CameraNode::ExecuteImpl(TypedExecuteContext& ctx) {
    // Get swapchain info for aspect ratio
    SwapChainPublicVariables* swapchainInfo = ctx.In(CameraNodeConfig::SWAPCHAIN_PUBLIC);
    if (!swapchainInfo) {
        return;
    }

    float aspectRatio = static_cast<float>(swapchainInfo->Extent.width) /
                        static_cast<float>(swapchainInfo->Extent.height);

    // Update the shared camera UBO for this frame
    // Always use buffer index 0 (the only buffer)
    // Frame-in-flight sync ensures GPU finished reading before we write
    UpdateCameraMatrices(0, 0, aspectRatio);
}

void CameraNode::UpdateCameraMatrices(uint32_t frameIndex, uint32_t imageIndex, float aspectRatio) {
    void* mappedPtr = perFrameResources.GetUniformBufferMapped(imageIndex);
    if (!mappedPtr) {
        return;
    }

    // Create projection matrix
    glm::mat4 projection = glm::perspective(
        glm::radians(fov),
        aspectRatio,
        nearPlane,
        farPlane
    );

    // Create view matrix from camera position and orientation
    // Standard convention: yaw=0, pitch=0 looks toward -Z (forward)
    glm::vec3 forward;
    forward.x = cos(pitch) * sin(yaw);
    forward.y = sin(pitch);
    forward.z = -cos(pitch) * cos(yaw);  // Negative Z for forward
    forward = glm::normalize(forward);

    glm::vec3 target = cameraPosition + forward;
    glm::mat4 view = glm::lookAt(cameraPosition, target, glm::vec3(0.0f, 1.0f, 0.0f));

    // Compute inverse matrices
    glm::mat4 invProjection = glm::inverse(projection);
    glm::mat4 invView = glm::inverse(view);

    // Update uniform buffer
    CameraData cameraData;
    cameraData.invProjection = invProjection;
    cameraData.invView = invView;
    cameraData.cameraPos = cameraPosition;
    cameraData._padding0 = 0.0f;
    cameraData.gridResolution = gridResolution;
    cameraData.lodBias = 1.0f;  // Default LOD bias
    cameraData._padding1[0] = 0.0f;
    cameraData._padding1[1] = 0.0f;

    std::memcpy(mappedPtr, &cameraData, sizeof(CameraData));
}

void CameraNode::CleanupImpl(TypedCleanupContext& ctx) {
    NODE_LOG_INFO("CameraNode cleanup");

    if (!vulkanDevice) {
        return;
    }

    // Wait for device idle before cleanup
    vkDeviceWaitIdle(vulkanDevice->device);

    // Cleanup per-frame resources
    perFrameResources.Cleanup();
}

} // namespace Vixen::RenderGraph
