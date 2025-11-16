#include "Headers.h"  // MUST be first to define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include "Nodes/CameraNode.h"
#include "VulkanResources/VulkanDevice.h"
#include "VulkanResources/VulkanSwapChain.h"
#include "EventBus/InputEvents.h"
#include "NodeHelpers/ValidationHelpers.h"
#include <iostream>
#include <cstring>

using namespace RenderGraph::NodeHelpers;

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

    // Modern polling-based input (GLFW/SDL2 style)
    // No event subscriptions needed - we poll InputState once per frame in ExecuteImpl
    NODE_LOG_INFO("CameraNode using modern polling-based input");
}

void CameraNode::CompileImpl(TypedCompileContext& ctx) {
    NODE_LOG_INFO("CameraNode compile");

    // Validate inputs using helpers
    VulkanDevice* devicePtr = ctx.In(CameraNodeConfig::VULKAN_DEVICE_IN);
    SetDevice(devicePtr);

    SwapChainPublicVariables* swapchainInfo = ValidateInput<SwapChainPublicVariables*>(
        ctx, "SwapChainPublic", CameraNodeConfig::SWAPCHAIN_PUBLIC);

    // Initialize camera data with valid values
    float aspectRatio = static_cast<float>(swapchainInfo->Extent.width) /
                        static_cast<float>(swapchainInfo->Extent.height);

    // Compute initial camera vectors
    glm::vec3 forward;
    forward.x = cos(pitch) * sin(yaw);
    forward.y = sin(pitch);
    forward.z = -cos(pitch) * cos(yaw);
    forward = glm::normalize(forward);

    glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3(0.0f, 1.0f, 0.0f)));
    glm::vec3 up = glm::normalize(glm::cross(right, forward));

    // Create projection and view matrices
    glm::mat4 projection = glm::perspective(
        glm::radians(fov),
        aspectRatio,
        nearPlane,
        farPlane
    );

    glm::vec3 target = cameraPosition + forward;
    glm::mat4 view = glm::lookAt(cameraPosition, target, glm::vec3(0.0f, 1.0f, 0.0f));

    // Fill initial camera data
    currentCameraData.cameraPos = cameraPosition;
    currentCameraData.fov = glm::radians(fov);
    currentCameraData.cameraDir = forward;
    currentCameraData.aspect = aspectRatio;
    currentCameraData.cameraUp = up;
    currentCameraData.lodBias = 1.0f;
    currentCameraData.cameraRight = right;
    currentCameraData.gridResolution = gridResolution;
    currentCameraData.invProjection = glm::inverse(projection);
    currentCameraData.invView = glm::inverse(view);

    // Output pointer to the camera data struct
    ctx.Out(CameraNodeConfig::CAMERA_DATA, const_cast<const CameraData&>(currentCameraData));

    NODE_LOG_INFO("Camera data initialized successfully");
}

void CameraNode::ExecuteImpl(TypedExecuteContext& ctx) {
    // Get swapchain info for aspect ratio
    SwapChainPublicVariables* swapchainInfo = ctx.In(CameraNodeConfig::SWAPCHAIN_PUBLIC);
    if (!swapchainInfo) {
        return;
    }

    float aspectRatio = static_cast<float>(swapchainInfo->Extent.width) /
                        static_cast<float>(swapchainInfo->Extent.height);

    // Modern polling-based input: Read InputState once per frame
    InputStatePtr inputState = ctx.In(CameraNodeConfig::INPUT_STATE);
    if (inputState) {
        // Accumulate mouse delta from polled state
        rotationDelta.x += inputState->mouseDelta.x;
        rotationDelta.y += inputState->mouseDelta.y;

        // Get keyboard movement axes
        float horizontal = inputState->GetAxisHorizontal();
        float vertical = inputState->GetAxisVertical();
        float upDown = inputState->GetAxisUpDown();

        movementDelta.x += horizontal;
        movementDelta.z += vertical;
        movementDelta.y += upDown;
    }

    // Apply accumulated input deltas to camera state
    float deltaTime = inputState ? inputState->deltaTime : (1.0f / 60.0f);
    ApplyInputDeltas(deltaTime);

    // Update camera data with current state
    UpdateCameraData(aspectRatio);

    // Output pointer to the camera data struct
    ctx.Out(CameraNodeConfig::CAMERA_DATA, const_cast<const CameraData&>(currentCameraData));
}

void CameraNode::UpdateCameraData(float aspectRatio) {
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

    glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3(0.0f, 1.0f, 0.0f)));
    glm::vec3 up = glm::normalize(glm::cross(right, forward));

    glm::vec3 target = cameraPosition + forward;
    glm::mat4 view = glm::lookAt(cameraPosition, target, glm::vec3(0.0f, 1.0f, 0.0f));

    // Update camera data struct
    currentCameraData.cameraPos = cameraPosition;
    currentCameraData.fov = glm::radians(fov);
    currentCameraData.cameraDir = forward;
    currentCameraData.aspect = aspectRatio;
    currentCameraData.cameraUp = up;
    currentCameraData.lodBias = 1.0f;
    currentCameraData.cameraRight = right;
    currentCameraData.gridResolution = gridResolution;
    currentCameraData.invProjection = glm::inverse(projection);
    currentCameraData.invView = glm::inverse(view);

    // DEBUG: Log camera state once
    static bool loggedCamera = false;
    if (!loggedCamera) {
        std::cout << "[CameraNode] Camera params: yaw=" << yaw << ", pitch=" << pitch << std::endl;
        std::cout << "[CameraNode] Camera position: (" << cameraPosition.x << ", " << cameraPosition.y << ", " << cameraPosition.z << ")" << std::endl;
        std::cout << "[CameraNode] forward = (" << forward.x << ", " << forward.y << ", " << forward.z << ")" << std::endl;
        std::cout << "[CameraNode] right = (" << right.x << ", " << right.y << ", " << right.z << ")" << std::endl;
        std::cout << "[CameraNode] up = (" << up.x << ", " << up.y << ", " << up.z << ")" << std::endl;
        loggedCamera = true;
    }
}

void CameraNode::CleanupImpl(TypedCleanupContext& ctx) {
    NODE_LOG_INFO("CameraNode cleanup");

    // No resources to cleanup since we're outputting a struct now
    // Camera state is maintained internally for next setup
}

// ============================================================================
// INPUT HANDLING (MODERN POLLING-BASED)
// ============================================================================
// Input is now polled once per frame from InputState in ExecuteImpl
// No event handlers needed - eliminates event flooding and provides predictable timing

void CameraNode::ApplyInputDeltas(float deltaTime) {
    ApplyRotation();
    ApplyMovement(deltaTime);
}

void CameraNode::ApplyRotation() {
    // Clamp raw rotation delta to prevent huge jumps
    rotationDelta.x = glm::clamp(rotationDelta.x, -maxRotationDeltaPerFrame, maxRotationDeltaPerFrame);
    rotationDelta.y = glm::clamp(rotationDelta.y, -maxRotationDeltaPerFrame, maxRotationDeltaPerFrame);

    // Apply exponential smoothing to reduce jitter
    smoothedRotationDelta = glm::mix(smoothedRotationDelta, rotationDelta, mouseSmoothingFactor);

    // Apply smoothed rotation
    yaw += smoothedRotationDelta.x * mouseSensitivity;
    pitch -= smoothedRotationDelta.y * mouseSensitivity;

    // Clamp pitch to avoid gimbal lock
    const float maxPitch = glm::radians(89.0f);
    pitch = glm::clamp(pitch, -maxPitch, maxPitch);

    // Clear raw rotation delta
    rotationDelta = glm::vec2(0.0f);
}

void CameraNode::ApplyMovement(float deltaTime) {
    if (glm::length(movementDelta) == 0.0f) {
        movementDelta = glm::vec3(0.0f);
        return;
    }

    // Compute local-space vectors
    glm::vec3 forward;
    forward.x = cos(pitch) * sin(yaw);
    forward.y = sin(pitch);
    forward.z = -cos(pitch) * cos(yaw);
    forward = glm::normalize(forward);

    glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3(0.0f, 1.0f, 0.0f)));

    // Helicopter controls: horizontal movement in XZ plane
    glm::vec3 forwardHorizontal = glm::normalize(glm::vec3(forward.x, 0.0f, forward.z));
    glm::vec3 rightHorizontal = glm::normalize(glm::vec3(right.x, 0.0f, right.z));

    glm::vec3 moveVector = forwardHorizontal * movementDelta.z + rightHorizontal * movementDelta.x;

    // Normalize to prevent faster diagonal movement
    float horizontalLength = glm::length(glm::vec2(moveVector.x, moveVector.z));
    if (horizontalLength > 1.0f) {
        moveVector.x /= horizontalLength;
        moveVector.z /= horizontalLength;
    }

    // Apply horizontal and vertical movement
    cameraPosition += moveVector * moveSpeed * deltaTime;
    cameraPosition.y += movementDelta.y * verticalSpeed * deltaTime;

    // Clear movement delta
    movementDelta = glm::vec3(0.0f);
}

} // namespace Vixen::RenderGraph
