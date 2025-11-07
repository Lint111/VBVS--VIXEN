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
    farPlane = GetParameterValue<float>(CameraNodeConfig::PARAM_FAR_PLANE, 100.0f);

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
    VulkanDevicePtr devicePtr = In(CameraNodeConfig::VULKAN_DEVICE_IN);
    if (!devicePtr) {
        throw std::runtime_error("[CameraNode] VULKAN_DEVICE_IN is null");
    }

    SetDevice(devicePtr);
    vulkanDevice = devicePtr;

    // Get swapchain info
    SwapChainPublicVariables* swapchainInfo = In(CameraNodeConfig::SWAPCHAIN_PUBLIC);
    if (!swapchainInfo) {
        throw std::runtime_error("[CameraNode] SWAPCHAIN_PUBLIC is null");
    }

    uint32_t imageCount = swapchainInfo->swapChainImageCount;
    NODE_LOG_INFO("Creating " + std::to_string(imageCount) + " camera UBOs");

    // Initialize per-frame resources
    perFrameResources.Initialize(vulkanDevice, imageCount);

    VkDeviceSize bufferSize = sizeof(CameraData);

    for (uint32_t i = 0; i < imageCount; ++i) {
        perFrameResources.CreateUniformBuffer(i, bufferSize);
    }

    // Output the first buffer (descriptor set will index into array)
    ctx.Out(CameraNodeConfig::CAMERA_BUFFER, perFrameResources.GetUniformBuffer(0));
    ctx.Out(CameraNodeConfig::CAMERA_BUFFER_SIZE, static_cast<uint64_t>(bufferSize));

    NODE_LOG_INFO("Created " + std::to_string(imageCount) + " camera UBOs successfully");
}

void CameraNode::ExecuteImpl(TypedExecuteContext& ctx) {
    // Get current image index
    uint32_t imageIndex = ctx.In(CameraNodeConfig::IMAGE_INDEX);

    // Get swapchain info for aspect ratio
    SwapChainPublicVariables* swapchainInfo = ctx.In(CameraNodeConfig::SWAPCHAIN_PUBLIC);
    if (!swapchainInfo) {
        return;
    }

    float aspectRatio = static_cast<float>(swapchainInfo->Extent.width) /
                        static_cast<float>(swapchainInfo->Extent.height);

    // Update camera matrices for this frame
    UpdateCameraMatrices(0, imageIndex, aspectRatio);

    // Output the buffer for this frame
    ctx.Out(CameraNodeConfig::CAMERA_BUFFER, perFrameResources.GetUniformBuffer(imageIndex));
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
    glm::vec3 forward;
    forward.x = cos(pitch) * cos(yaw);
    forward.y = sin(pitch);
    forward.z = cos(pitch) * sin(yaw);
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
    cameraData.gridResolution = gridResolution;

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
