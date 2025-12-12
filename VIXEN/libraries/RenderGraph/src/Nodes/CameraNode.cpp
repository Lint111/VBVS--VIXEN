#include "Headers.h"  // MUST be first to define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include "Nodes/CameraNode.h"
#include "VulkanDevice.h"
#include "VulkanSwapChain.h"
#include "InputEvents.h"
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

    // ALWAYS read camera parameters on setup (for debugging)
    // TODO: Restore initialSetupComplete check after fixing camera position
    cameraPosition.x = GetParameterValue<float>(CameraNodeConfig::PARAM_CAMERA_X, 0.0f);
    cameraPosition.y = GetParameterValue<float>(CameraNodeConfig::PARAM_CAMERA_Y, 0.0f);
    cameraPosition.z = GetParameterValue<float>(CameraNodeConfig::PARAM_CAMERA_Z, 3.0f);

    yaw = GetParameterValue<float>(CameraNodeConfig::PARAM_YAW, 0.0f);
    pitch = GetParameterValue<float>(CameraNodeConfig::PARAM_PITCH, 0.0f);

    NODE_LOG_INFO("Camera position: (" + std::to_string(cameraPosition.x) + ", " +
                  std::to_string(cameraPosition.y) + ", " + std::to_string(cameraPosition.z) +
                  "), yaw=" + std::to_string(yaw) + ", pitch=" + std::to_string(pitch));

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

    // Vulkan Y-flip: Vulkan's clip space has Y pointing down, unlike OpenGL
    projection[1][1] *= -1.0f;

    glm::vec3 target = cameraPosition + forward;
    glm::mat4 view = glm::lookAt(cameraPosition, target, glm::vec3(0.0f, 1.0f, 0.0f));

    // Fill initial camera data
    // MUST match shader PushConstants layout in VoxelRayMarch.comp!
    currentCameraData.cameraPos = cameraPosition;
    currentCameraData.time = 0.0f;  // Will be updated per-frame
    currentCameraData.cameraDir = forward;
    currentCameraData.fov = fov;    // Degrees (shader converts to radians)
    currentCameraData.cameraUp = up;
    currentCameraData.aspect = aspectRatio;
    currentCameraData.cameraRight = right;
    currentCameraData.debugMode = 0;  // Normal rendering mode
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

        // Arrow keys for smooth look rotation (scaled for comfortable speed)
        float lookHorizontal = inputState->GetAxisLookHorizontal();
        float lookVertical = inputState->GetAxisLookVertical();
        const float arrowKeyLookSpeed = 500.0f;  // Increased for faster orbit rotation
        rotationDelta.x += lookHorizontal * arrowKeyLookSpeed * inputState->deltaTime;
        rotationDelta.y -= lookVertical * arrowKeyLookSpeed * inputState->deltaTime;  // Inverted Y

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

    // Vulkan Y-flip: Vulkan's clip space has Y pointing down, unlike OpenGL
    // This flips the projection to match OpenGL conventions used in our shaders
    projection[1][1] *= -1.0f;

    // ORBIT MODE: Camera orbits around orbitCenter
    // yaw/pitch control the orbit angle, camera looks at orbitCenter
    // Camera position is computed from orbit parameters
    glm::vec3 orbitOffset;
    orbitOffset.x = orbitDistance * cos(pitch) * sin(yaw);
    orbitOffset.y = orbitDistance * sin(pitch);
    orbitOffset.z = orbitDistance * cos(pitch) * cos(yaw);

    cameraPosition = orbitCenter + orbitOffset;

    // Forward direction points toward orbit center
    glm::vec3 forward = glm::normalize(orbitCenter - cameraPosition);

    glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3(0.0f, 1.0f, 0.0f)));
    glm::vec3 up = glm::normalize(glm::cross(right, forward));

    glm::mat4 view = glm::lookAt(cameraPosition, orbitCenter, glm::vec3(0.0f, 1.0f, 0.0f));

    // Update camera data struct
    // MUST match shader PushConstants layout in VoxelRayMarch.comp!
    currentCameraData.cameraPos = cameraPosition;
    currentCameraData.time += 1.0f / 60.0f;  // Increment time (approximate)
    currentCameraData.cameraDir = forward;
    currentCameraData.fov = fov;    // Degrees (shader converts to radians)
    currentCameraData.cameraUp = up;
    currentCameraData.aspect = aspectRatio;
    currentCameraData.cameraRight = right;
    // debugMode is set via input (not updated here)
    currentCameraData.invProjection = glm::inverse(projection);
    currentCameraData.invView = glm::inverse(view);

    // DEBUG: Log camera state once
    static bool loggedCamera = false;
    if (!loggedCamera) {
        NODE_LOG_DEBUG("[CameraNode] Camera params: yaw=" + std::to_string(yaw) + ", pitch=" + std::to_string(pitch));
        NODE_LOG_DEBUG("[CameraNode] Camera position: (" + std::to_string(cameraPosition.x) + ", " + std::to_string(cameraPosition.y) + ", " + std::to_string(cameraPosition.z) + ")");
        NODE_LOG_DEBUG("[CameraNode] forward = (" + std::to_string(forward.x) + ", " + std::to_string(forward.y) + ", " + std::to_string(forward.z) + ")");
        NODE_LOG_DEBUG("[CameraNode] right = (" + std::to_string(right.x) + ", " + std::to_string(right.y) + ", " + std::to_string(right.z) + ")");
        NODE_LOG_DEBUG("[CameraNode] up = (" + std::to_string(up.x) + ", " + std::to_string(up.y) + ", " + std::to_string(up.z) + ")");
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

    // ORBIT MODE:
    // W/S: Zoom in/out (change orbit distance)
    // A/D: Move orbit center left/right (X axis)
    // Q/E: Move orbit center up/down (Y axis)

    // W/S controls zoom (orbit distance)
    float zoomSpeed = 100.0f;  // Scaled for 128^3 world
    orbitDistance -= movementDelta.z * zoomSpeed * deltaTime;  // W zooms in, S zooms out
    orbitDistance = glm::clamp(orbitDistance, 5.0f, 120.0f);  // Keep camera inside 128^3 world bounds

    // A/D and Q/E move the orbit center
    glm::vec3 moveVector(0.0f);
    moveVector.x = movementDelta.x;  // A/D moves left/right (X axis)
    moveVector.y = movementDelta.y;  // Q/E moves up/down (Y axis)

    // Apply movement to orbit center
    orbitCenter += moveVector * moveSpeed * deltaTime;

    // Clear movement delta
    movementDelta = glm::vec3(0.0f);
}

} // namespace Vixen::RenderGraph
