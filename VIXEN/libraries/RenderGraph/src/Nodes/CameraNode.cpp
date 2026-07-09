#include "Headers.h"  // MUST be first to define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include "Nodes/CameraNode.h"
#include "Core/NodeRegistration.h"
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

    // Orbit target: defaults match the main app's Cornell-box demo scene. A consumer whose
    // geometry sits elsewhere (e.g. vixen_editor's object-centered documents) sets these params
    // so the orbit camera actually frames its content instead of empty space.
    orbitCenter.x = GetParameterValue<float>(CameraNodeConfig::PARAM_ORBIT_CENTER_X, 5.0f);
    orbitCenter.y = GetParameterValue<float>(CameraNodeConfig::PARAM_ORBIT_CENTER_Y, 5.0f);
    orbitCenter.z = GetParameterValue<float>(CameraNodeConfig::PARAM_ORBIT_CENTER_Z, 5.0f);
    orbitDistance = GetParameterValue<float>(CameraNodeConfig::PARAM_ORBIT_DISTANCE, 30.0f);

    // Bodies-0 root-cause fix: a consumer that explicitly configures ANY orbit param (e.g.
    // EditorApplication::BuildRenderGraph, which sets all four to frame its own document) is
    // declaring orbit-mode intent up front -- treat that the same as a live orbit interaction
    // so its camera orbits from frame 1 as it always has. A consumer that only sets the fixed
    // PARAM_CAMERA_* pose (e.g. the main app's body-render scenes) leaves these untouched and
    // gets the fixed pose at rest instead of silently inheriting the stale Cornell-box orbit
    // defaults above (root cause of the bodies-0 bug -- see EngageOrbit/UpdateCameraData).
    if (GetParameter(CameraNodeConfig::PARAM_ORBIT_CENTER_X) ||
        GetParameter(CameraNodeConfig::PARAM_ORBIT_CENTER_Y) ||
        GetParameter(CameraNodeConfig::PARAM_ORBIT_CENTER_Z) ||
        GetParameter(CameraNodeConfig::PARAM_ORBIT_DISTANCE)) {
        orbitActive_ = true;
    }

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

    Vixen::Vulkan::Resources::IRenderTarget* swapchainInfo = ValidateInput<Vixen::Vulkan::Resources::IRenderTarget*>(
        ctx, "SwapChainPublic", CameraNodeConfig::SWAPCHAIN_PUBLIC);

    // Initialize camera data with valid values
    float aspectRatio = static_cast<float>(swapchainInfo->GetExtent().width) /
                        static_cast<float>(swapchainInfo->GetExtent().height);

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
    Vixen::Vulkan::Resources::IRenderTarget* swapchainInfo = ctx.In(CameraNodeConfig::SWAPCHAIN_PUBLIC);
    if (!swapchainInfo) {
        return;
    }

    float aspectRatio = static_cast<float>(swapchainInfo->GetExtent().width) /
                        static_cast<float>(swapchainInfo->GetExtent().height);

    // Modern polling-based input: Read InputState once per frame
    InputStatePtr inputState = ctx.In(CameraNodeConfig::INPUT_STATE);
    if (inputState) {
        // Orbit gate (critique V2, M4): mouseDelta only drives rotation while the configured
        // orbit control is engaged, per InputConfig::OrbitButton (mirrored via InputState —
        // CameraNode has no InputNode reference, see InputState.h). Buttons: [0]=left,[1]=right.
        bool orbitEngaged = false;
        switch (inputState->orbitButton) {
            case 0: {  // RightMouse: apply only while the right button is held
                orbitEngaged = inputState->mouseButtons[1];
                break;
            }
            case 1: {  // LeftDrag: apply only once cumulative in-press motion crosses the
                       // threshold; below it the press stays a click for the selection path.
                if (inputState->mouseButtons[0]) {
                    dragAccumPx_ += glm::length(inputState->mouseDelta);
                    if (dragAccumPx_ > inputState->dragThresholdPx) {
                        dragThresholdCrossed_ = true;
                    }
                } else {
                    dragAccumPx_ = 0.0f;
                    dragThresholdCrossed_ = false;
                }
                orbitEngaged = dragThresholdCrossed_;
                break;
            }
            default:  // 2=Always (legacy) and any unrecognized value: unconditional, matches
                      // pre-M4 behavior. InputNode clamps the live param, so this is a static
                      // wire-value guard, not the primary validation.
                orbitEngaged = true;
                break;
        }

        if (orbitEngaged) {
            EngageOrbit();
            rotationDelta.x += inputState->mouseDelta.x;
            rotationDelta.y += inputState->mouseDelta.y;
        }

        // Wheel zoom: fold scroll into orbit distance, reusing ApplyMovement's W/S clamp
        // (kOrbitDistanceMin/Max) so both paths agree on the world-bounds ceiling. A wheel
        // event is itself an orbit interaction (zooming only means something once the camera
        // is orbiting), so it engages orbit too, independent of the drag gate above.
        if (inputState->wheelZoom && inputState->wheelDelta.y != 0.0f) {
            EngageOrbit();
            orbitDistance -= inputState->wheelDelta.y * inputState->wheelZoomSpeed;
            orbitDistance = glm::clamp(orbitDistance, kOrbitDistanceMin, kOrbitDistanceMax);
        }

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

    // Bodies-0 root-cause fix: the configured PARAM_CAMERA_* pose (cameraPosition, set in
    // Setup/CompileImpl) is authoritative at rest. Only recompute position from orbit
    // parameters once orbit has actually been engaged this session (EngageOrbit latches
    // orbitActive_ — see its call sites in ExecuteImpl/ApplyMovement/the two ForTest
    // setters). Before that, keep the fixed cameraPosition and derive forward the same
    // way CompileImpl does (from yaw/pitch), so arrow-key look-rotation still works
    // without silently teleporting the camera to orbitCenter+orbitDistance.
    glm::vec3 forward;
    glm::vec3 lookTarget;
    if (orbitActive_) {
        // ORBIT MODE: Camera orbits around orbitCenter
        // yaw/pitch control the orbit angle, camera looks at orbitCenter
        // Camera position is computed from orbit parameters
        glm::vec3 orbitOffset;
        orbitOffset.x = orbitDistance * cos(pitch) * sin(yaw);
        orbitOffset.y = orbitDistance * sin(pitch);
        orbitOffset.z = orbitDistance * cos(pitch) * cos(yaw);

        cameraPosition = orbitCenter + orbitOffset;
        forward = glm::normalize(orbitCenter - cameraPosition);
        lookTarget = orbitCenter;
    } else {
        // FIXED MODE: cameraPosition stays at its configured value; forward is derived
        // from yaw/pitch directly (mirrors CompileImpl's initial-frame formula).
        forward.x = cos(pitch) * sin(yaw);
        forward.y = sin(pitch);
        forward.z = -cos(pitch) * cos(yaw);
        forward = glm::normalize(forward);
        lookTarget = cameraPosition + forward;
    }

    glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3(0.0f, 1.0f, 0.0f)));
    glm::vec3 up = glm::normalize(glm::cross(right, forward));

    glm::mat4 view = glm::lookAt(cameraPosition, lookTarget, glm::vec3(0.0f, 1.0f, 0.0f));

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

void CameraNode::EngageOrbit() {
    if (orbitActive_) {
        return;  // already engaged this session — idempotent, no re-seed on every frame
    }
    orbitActive_ = true;

    // Re-seed orbitDistance/yaw/pitch from the CURRENT fixed cameraPosition relative to
    // orbitCenter, inverting UpdateCameraData's orbitOffset formula, so the very first
    // orbit-active frame reproduces the same cameraPosition the fixed pose just had — no
    // teleport to the stale default orbit pose (orbitCenter=(5,5,5), orbitDistance=30,
    // left over from the Cornell-box demo). orbitCenter itself is the pivot and can't be
    // derived from position alone; it keeps whatever was configured/moved via WASD/QE.
    const glm::vec3 offset = cameraPosition - orbitCenter;
    const float distance = glm::length(offset);
    if (distance > 1e-4f) {
        orbitDistance = glm::clamp(distance, kOrbitDistanceMin, kOrbitDistanceMax);
        pitch = asin(glm::clamp(offset.y / distance, -1.0f, 1.0f));
        yaw   = atan2(offset.x, offset.z);
    }
    // else: camera already sits at orbitCenter — keep whatever yaw/pitch/orbitDistance the
    // node already had (a zero-length offset has no meaningful direction to invert).
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

    // This whole method is orbit-specific (zoom via orbitDistance, pan via orbitCenter) —
    // a WASD/QE press is itself an orbit interaction, same as wheel zoom above.
    EngageOrbit();

    // ORBIT MODE:
    // W/S: Zoom in/out (change orbit distance)
    // A/D: Move orbit center left/right (X axis)
    // Q/E: Move orbit center up/down (Y axis)

    // W/S controls zoom (orbit distance)
    float zoomSpeed = 100.0f;  // Scaled for 128^3 world
    orbitDistance -= movementDelta.z * zoomSpeed * deltaTime;  // W zooms in, S zooms out
    orbitDistance = glm::clamp(orbitDistance, kOrbitDistanceMin, kOrbitDistanceMax);  // Keep camera inside 128^3 world bounds

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

// Self-registration (M3): registrar kept in this TU; RenderGraphNodes is whole-archived so it is not stripped.
VIXEN_REGISTER_NODE(Vixen::RenderGraph::CameraNodeType);
