#include "Headers.h"  // MUST be first to define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include "Nodes/CameraNode.h"
#include "VulkanResources/VulkanDevice.h"
#include "VulkanSwapChain.h"
#include "EventBus/InputEvents.h"
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

    // Read parameters (always update FOV, near/far planes, and grid resolution)
    fov = GetParameterValue<float>(CameraNodeConfig::PARAM_FOV, 45.0f);
    nearPlane = GetParameterValue<float>(CameraNodeConfig::PARAM_NEAR_PLANE, 0.1f);
    farPlane = GetParameterValue<float>(CameraNodeConfig::PARAM_FAR_PLANE, 1000.0f);
    gridResolution = GetParameterValue<uint32_t>(CameraNodeConfig::PARAM_GRID_RESOLUTION, 128u);

    // Only initialize camera position/orientation on FIRST setup
    // After that, preserve user-controlled position from input
    if (!initialSetupComplete) {
        cameraPosition.x = GetParameterValue<float>(CameraNodeConfig::PARAM_CAMERA_X, 0.0f);
        cameraPosition.y = GetParameterValue<float>(CameraNodeConfig::PARAM_CAMERA_Y, 0.0f);
        cameraPosition.z = GetParameterValue<float>(CameraNodeConfig::PARAM_CAMERA_Z, 3.0f);

        yaw = GetParameterValue<float>(CameraNodeConfig::PARAM_YAW, 0.0f);
        pitch = GetParameterValue<float>(CameraNodeConfig::PARAM_PITCH, 0.0f);

        initialSetupComplete = true;
        NODE_LOG_INFO("Camera position initialized from parameters");
    } else {
        NODE_LOG_INFO("Camera position preserved from previous state (recompilation)");
    }

    // Subscribe to input events
    if (GetMessageBus()) {
        keyEventSub = GetMessageBus()->Subscribe(
            EventBus::KeyEvent::TYPE,
            [this](const EventBus::BaseEventMessage& msg) { return OnKeyEvent(msg); }
        );

        // Subscribe to mouse start/end events (state-based input)
        mouseStartSub = GetMessageBus()->Subscribe(
            EventBus::MouseMoveStartEvent::TYPE,
            [this](const EventBus::BaseEventMessage& msg) { return OnMouseMoveStart(msg); }
        );

        // Legacy: Still subscribe to continuous events for compatibility
        // TODO: Remove once all systems use state-based input
        mouseSub = GetMessageBus()->Subscribe(
            EventBus::MouseMoveEvent::TYPE,
            [this](const EventBus::BaseEventMessage& msg) { return OnMouseMove(msg); }
        );

        NODE_LOG_INFO("CameraNode subscribed to input events");
    } else {
        NODE_LOG_WARNING("CameraNode: MessageBus not available, input disabled");
    }
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

    // Apply accumulated input deltas to camera state
    // TODO: Get actual delta time from frame timing
    float deltaTime = 1.0f / 60.0f;  // Assume 60fps for now
    ApplyInputDeltas(deltaTime);

    // Update the shared camera UBO for this frame
    // Always use buffer index 0 (the only buffer)
    // Frame-in-flight sync ensures GPU finished reading before we write
    UpdateCameraMatrices(0, 0, aspectRatio);
}

void CameraNode::UpdateCameraMatrices(uint32_t frameIndex, uint32_t imageIndex, float aspectRatio) {
    // Safety check: perFrameResources might be cleaned up during recompilation
    if (!vulkanDevice || vulkanDevice->device == VK_NULL_HANDLE) {
        return;
    }

    void* mappedPtr = nullptr;
    try {
        mappedPtr = perFrameResources.GetUniformBufferMapped(imageIndex);
    } catch (...) {
        // During recompilation, perFrameResources might be in inconsistent state
        return;
    }

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

    // DEBUG: Log invView matrix once
    static bool loggedInvView = false;
    if (!loggedInvView) {
        std::cout << "[CameraNode] Camera params: yaw=" << yaw << ", pitch=" << pitch << std::endl;
        std::cout << "[CameraNode] Camera position: (" << cameraPosition.x << ", " << cameraPosition.y << ", " << cameraPosition.z << ")" << std::endl;
        std::cout << "[CameraNode] forward = (" << forward.x << ", " << forward.y << ", " << forward.z << ")" << std::endl;
        std::cout << "[CameraNode] invView col2: (" << invView[0][2] << ", " << invView[1][2] << ", " << invView[2][2] << ")" << std::endl;
        std::cout << "[CameraNode] -invView col2: (" << -invView[0][2] << ", " << -invView[1][2] << ", " << -invView[2][2] << ")" << std::endl;
        loggedInvView = true;
    }

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

    // Unsubscribe from events
    if (GetMessageBus()) {
        if (keyEventSub != 0) {
            GetMessageBus()->Unsubscribe(keyEventSub);
            keyEventSub = 0;
        }
        if (mouseSub != 0) {
            GetMessageBus()->Unsubscribe(mouseSub);
            mouseSub = 0;
        }
    }

    if (!vulkanDevice) {
        return;
    }

    // Wait for device idle before cleanup
    vkDeviceWaitIdle(vulkanDevice->device);

    // Cleanup per-frame resources
    perFrameResources.Cleanup();
}

// ============================================================================
// INPUT HANDLING
// ============================================================================

bool CameraNode::OnKeyEvent(const EventBus::BaseEventMessage& msg) {
    const auto& keyEvent = static_cast<const EventBus::KeyEvent&>(msg);

    // Only handle Held events for continuous movement
    if (keyEvent.eventType != EventBus::KeyEventType::Held) {
        return false;  // Let other subscribers handle it
    }

    // WASD for local-space horizontal movement
    // QE for global Y-axis vertical movement
    switch (keyEvent.key) {
        case EventBus::KeyCode::W:
            movementDelta.z += 1.0f;  // Forward (local +Z)
            break;
        case EventBus::KeyCode::S:
            movementDelta.z -= 1.0f;  // Backward (local -Z)
            break;
        case EventBus::KeyCode::A:
            movementDelta.x -= 1.0f;  // Left (local -X)
            break;
        case EventBus::KeyCode::D:
            movementDelta.x += 1.0f;  // Right (local +X)
            break;
        case EventBus::KeyCode::Q:
            movementDelta.y -= 1.0f;  // Down (global -Y)
            break;
        case EventBus::KeyCode::E:
            movementDelta.y += 1.0f;  // Up (global +Y)
            break;
        default:
            break;
    }

    return false;  // Don't consume event
}

bool CameraNode::OnMouseMoveStart(const EventBus::BaseEventMessage& msg) {
    // Mouse movement session started - could log or prepare state
    return false;  // Don't consume event
}

bool CameraNode::OnMouseMove(const EventBus::BaseEventMessage& msg) {
    const auto& mouseEvent = static_cast<const EventBus::MouseMoveEvent&>(msg);

    // Accumulate rotation delta (will be applied in ApplyInputDeltas)
    rotationDelta.x += mouseEvent.deltaX;  // Yaw (horizontal)
    rotationDelta.y += mouseEvent.deltaY;  // Pitch (vertical)

    return false;  // Don't consume event
}

void CameraNode::ApplyInputDeltas(float deltaTime) {
    // Apply rotation (mouse look)
    yaw += rotationDelta.x * mouseSensitivity;
    pitch -= rotationDelta.y * mouseSensitivity;  // Invert Y: down = look down

    // Clamp pitch to avoid gimbal lock
    const float maxPitch = glm::radians(89.0f);
    pitch = glm::clamp(pitch, -maxPitch, maxPitch);

    // Clear rotation delta
    rotationDelta = glm::vec2(0.0f);

    // Apply movement (helicopter controls)
    if (glm::length(movementDelta) > 0.0f) {
        // Compute local-space forward/right vectors
        glm::vec3 forward;
        forward.x = cos(pitch) * sin(yaw);
        forward.y = sin(pitch);
        forward.z = -cos(pitch) * cos(yaw);
        forward = glm::normalize(forward);

        glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3(0.0f, 1.0f, 0.0f)));

        // For helicopter controls:
        // - WASD movement uses local forward/right (XZ plane)
        // - QE movement uses global Y-axis

        // Local horizontal movement (WASD in camera-space, but keep Y=0 for helicopter feel)
        glm::vec3 forwardHorizontal = glm::normalize(glm::vec3(forward.x, 0.0f, forward.z));
        glm::vec3 rightHorizontal = glm::normalize(glm::vec3(right.x, 0.0f, right.z));

        glm::vec3 moveVector = forwardHorizontal * movementDelta.z + rightHorizontal * movementDelta.x;

        // Normalize to prevent faster diagonal movement (only for horizontal)
        float horizontalLength = glm::length(glm::vec2(moveVector.x, moveVector.z));
        if (horizontalLength > 1.0f) {
            moveVector.x /= horizontalLength;
            moveVector.z /= horizontalLength;
        }

        // Apply horizontal movement with speed and delta time
        cameraPosition += moveVector * moveSpeed * deltaTime;

        // Apply vertical movement (QE) separately with its own speed
        cameraPosition.y += movementDelta.y * verticalSpeed * deltaTime;
    }

    // Clear movement delta
    movementDelta = glm::vec3(0.0f);
}

} // namespace Vixen::RenderGraph
