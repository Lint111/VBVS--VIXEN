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

void CameraNode::SetupImpl(Context& ctx) {
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

void CameraNode::CompileImpl(Context& ctx) {
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

    // Create per-frame uniform buffers
    uniformBuffers.resize(imageCount);
    uniformMemories.resize(imageCount);
    mappedData.resize(imageCount);

    VkDeviceSize bufferSize = sizeof(CameraData);

    for (uint32_t i = 0; i < imageCount; ++i) {
        // Create buffer
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = bufferSize;
        bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VkResult result = vkCreateBuffer(vulkanDevice->device, &bufferInfo, nullptr, &uniformBuffers[i]);
        if (result != VK_SUCCESS) {
            throw std::runtime_error("[CameraNode] Failed to create uniform buffer: " + std::to_string(result));
        }

        // Allocate memory
        VkMemoryRequirements memRequirements;
        vkGetBufferMemoryRequirements(vulkanDevice->device, uniformBuffers[i], &memRequirements);

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memRequirements.size;

        // Find host-visible, host-coherent memory type
        VkPhysicalDeviceMemoryProperties memProperties;
        vkGetPhysicalDeviceMemoryProperties(vulkanDevice->physicalDevice, &memProperties);

        uint32_t memoryTypeIndex = UINT32_MAX;
        VkMemoryPropertyFlags properties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

        for (uint32_t j = 0; j < memProperties.memoryTypeCount; ++j) {
            if ((memRequirements.memoryTypeBits & (1 << j)) &&
                (memProperties.memoryTypes[j].propertyFlags & properties) == properties) {
                memoryTypeIndex = j;
                break;
            }
        }

        if (memoryTypeIndex == UINT32_MAX) {
            throw std::runtime_error("[CameraNode] Failed to find suitable memory type");
        }

        allocInfo.memoryTypeIndex = memoryTypeIndex;

        result = vkAllocateMemory(vulkanDevice->device, &allocInfo, nullptr, &uniformMemories[i]);
        if (result != VK_SUCCESS) {
            throw std::runtime_error("[CameraNode] Failed to allocate uniform buffer memory: " + std::to_string(result));
        }

        // Bind memory to buffer
        vkBindBufferMemory(vulkanDevice->device, uniformBuffers[i], uniformMemories[i], 0);

        // Map memory (persistent mapping)
        result = vkMapMemory(vulkanDevice->device, uniformMemories[i], 0, bufferSize, 0, &mappedData[i]);
        if (result != VK_SUCCESS) {
            throw std::runtime_error("[CameraNode] Failed to map uniform buffer memory: " + std::to_string(result));
        }
    }

    // Output the first buffer (descriptor set will index into array)
    Out(CameraNodeConfig::CAMERA_BUFFER, uniformBuffers[0]);
    Out(CameraNodeConfig::CAMERA_BUFFER_SIZE, static_cast<uint64_t>(bufferSize));

    NODE_LOG_INFO("Created " + std::to_string(imageCount) + " camera UBOs successfully");
}

void CameraNode::ExecuteImpl(Context& ctx) {
    // Get current image index
    uint32_t imageIndex = ctx.In(CameraNodeConfig::IMAGE_INDEX);

    // Get swapchain info for aspect ratio
    SwapChainPublicVariables* swapchainInfo = ctx.In(CameraNodeConfig::SWAPCHAIN_PUBLIC);
    if (!swapchainInfo) {
        return;
    }

    float aspectRatio = static_cast<float>(swapchainInfo->swapChainExtent.width) /
                        static_cast<float>(swapchainInfo->swapChainExtent.height);

    // Update camera matrices for this frame
    UpdateCameraMatrices(0, imageIndex, aspectRatio);

    // Output the buffer for this frame
    Out(CameraNodeConfig::CAMERA_BUFFER, uniformBuffers[imageIndex]);
}

void CameraNode::UpdateCameraMatrices(uint32_t frameIndex, uint32_t imageIndex, float aspectRatio) {
    if (imageIndex >= uniformBuffers.size() || !mappedData[imageIndex]) {
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

    std::memcpy(mappedData[imageIndex], &cameraData, sizeof(CameraData));
}

void CameraNode::CleanupImpl() {
    NODE_LOG_INFO("CameraNode cleanup");

    if (!vulkanDevice) {
        return;
    }

    // Wait for device idle before cleanup
    vkDeviceWaitIdle(vulkanDevice->device);

    // Unmap and destroy buffers
    for (size_t i = 0; i < uniformBuffers.size(); ++i) {
        if (mappedData[i]) {
            vkUnmapMemory(vulkanDevice->device, uniformMemories[i]);
            mappedData[i] = nullptr;
        }

        if (uniformBuffers[i] != VK_NULL_HANDLE) {
            vkDestroyBuffer(vulkanDevice->device, uniformBuffers[i], nullptr);
            uniformBuffers[i] = VK_NULL_HANDLE;
        }

        if (uniformMemories[i] != VK_NULL_HANDLE) {
            vkFreeMemory(vulkanDevice->device, uniformMemories[i], nullptr);
            uniformMemories[i] = VK_NULL_HANDLE;
        }
    }

    uniformBuffers.clear();
    uniformMemories.clear();
    mappedData.clear();
}

} // namespace Vixen::RenderGraph
