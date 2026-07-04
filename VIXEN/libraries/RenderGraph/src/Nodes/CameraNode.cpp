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

    // pose_seq: the host bumps this on every console pose write (setcam/lookcam/pick-flight).
    // A change (including first sight) forces every PRESENT pose param below to reapply even if
    // its value is unchanged from lastApplied — otherwise a reset to an already-current value
    // (e.g. `lookcam 0 0` when yaw/pitch are already 0) was a silent no-op.
    bool forceApply = false;
    if (GetParameter(CameraNodeConfig::PARAM_POSE_SEQ) != nullptr) {
        const float seq = GetParameterValue<float>(CameraNodeConfig::PARAM_POSE_SEQ, 0.0f);
        if (std::isnan(lastPoseSeq_) || seq != lastPoseSeq_) {
            forceApply = true;
            lastPoseSeq_ = seq;
        }
    }

    // Pose params are REQUESTS, applied only when their stored value changed since the last
    // apply (NaN sentinel = first sight always applies). Setup re-runs on every recompile —
    // resize, any node's SetParameter — and the old unconditional re-read snapped the live
    // input-driven orbit camera back to its t0 pose each time.
    auto applyIfChanged = [&](const char* name, float& lastApplied, auto&& apply) {
        if (GetParameter(name) == nullptr) return;          // never set on this node
        const float v = GetParameterValue<float>(name, 0.0f);
        if (!forceApply && !std::isnan(lastApplied) && v == lastApplied) return;
        apply(v);
        lastApplied = v;
    };
    applyIfChanged(CameraNodeConfig::PARAM_CAMERA_X, lastParamCamX_, [&](float v) { cameraPosition.x = v; });
    applyIfChanged(CameraNodeConfig::PARAM_CAMERA_Y, lastParamCamY_, [&](float v) { cameraPosition.y = v; });
    applyIfChanged(CameraNodeConfig::PARAM_CAMERA_Z, lastParamCamZ_, [&](float v) { cameraPosition.z = v; });
    applyIfChanged(CameraNodeConfig::PARAM_YAW,   lastParamYaw_,   [&](float v) { yaw = v; });
    applyIfChanged(CameraNodeConfig::PARAM_PITCH, lastParamPitch_, [&](float v) { pitch = v; });
    // Orbit-model pose requests (host click-to-fly / console): re-anchor the orbit camera.
    applyIfChanged(CameraNodeConfig::PARAM_ORBIT_CENTER_X, lastParamOrbitCX_, [&](float v) { orbitCenter.x = v; });
    applyIfChanged(CameraNodeConfig::PARAM_ORBIT_CENTER_Y, lastParamOrbitCY_, [&](float v) { orbitCenter.y = v; });
    applyIfChanged(CameraNodeConfig::PARAM_ORBIT_CENTER_Z, lastParamOrbitCZ_, [&](float v) { orbitCenter.z = v; });
    applyIfChanged(CameraNodeConfig::PARAM_ORBIT_DISTANCE, lastParamOrbitDist_, [&](float v) {
        orbitDistance = glm::clamp(v, kOrbitDistanceMin, kOrbitDistanceMax);
    });
    applyIfChanged(CameraNodeConfig::PARAM_CAMERA_MODE, lastParamCameraMode_, [&](float v) {
        mode = (v >= 0.5f) ? CameraMode::Orbit : CameraMode::FreeFly;
    });
    applyIfChanged(CameraNodeConfig::PARAM_INITIAL_FLY_X, lastParamInitialFlyX_, [&](float v) { flyPosition.x = v; });
    applyIfChanged(CameraNodeConfig::PARAM_INITIAL_FLY_Y, lastParamInitialFlyY_, [&](float v) { flyPosition.y = v; });
    applyIfChanged(CameraNodeConfig::PARAM_INITIAL_FLY_Z, lastParamInitialFlyZ_, [&](float v) { flyPosition.z = v; });
    applyIfChanged(CameraNodeConfig::PARAM_INITIAL_FLY_YAW, lastParamInitialFlyYaw_, [&](float v) { flyYaw = v; });
    applyIfChanged(CameraNodeConfig::PARAM_INITIAL_FLY_PITCH, lastParamInitialFlyPitch_, [&](float v) { flyPitch = v; });

    {
        const glm::vec3 pos = Vixen::RenderGraph::ExtractPosition(transform);
        NODE_LOG_INFO("Camera position: (" + std::to_string(pos.x) + ", " +
                      std::to_string(pos.y) + ", " + std::to_string(pos.z) + ")");
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

    Vixen::Vulkan::Resources::IRenderTarget* swapchainInfo = ValidateInput<Vixen::Vulkan::Resources::IRenderTarget*>(
        ctx, "SwapChainPublic", CameraNodeConfig::SWAPCHAIN_PUBLIC);

    // Initialize camera data with valid values
    float aspectRatio = static_cast<float>(swapchainInfo->GetExtent().width) /
                        static_cast<float>(swapchainInfo->GetExtent().height);

    // Reuse the same per-frame update path for the very first frame too — `transform` already
    // holds the FreeFly boot seed from the field initializer, so this produces byte-identical
    // output to the old hand-duplicated version of this same math.
    UpdateCameraData(aspectRatio);

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
        // Rotation: middle-mouse-drag always rotates, in both modes, unconditionally (spec
        // 2026-07-04 decision 4 — replaces the old button-gated orbitEngaged switch entirely;
        // OrbitButton::RightMouse/LeftDrag/Always no longer exist as a choice).
        if (inputState->mouseButtons[2]) {
            rotationDelta.x += inputState->mouseDelta.x;
            rotationDelta.y += inputState->mouseDelta.y;
        }

        // A recompile/WSLg stall must not teleport the camera (field bug 2026-07-03: latched
        // arrows at 500·dt flew the camera hundreds of units during multi-second stalls). Clamp
        // the dt used for ALL input application, including the arrow-look term below.
        const float clampedDt = glm::min(inputState->deltaTime, 0.1f);

        // Arrow keys for smooth look rotation (scaled for comfortable speed)
        float lookHorizontal = inputState->GetAxisLookHorizontal();
        float lookVertical = inputState->GetAxisLookVertical();
        // 120 (was 500): at the 0.1s dt clamp that's 12 rad/s peak instead of 50 rad/s — 500·dt at
        // a 57ms stall frame (28 rad/s) was uncontrollable even before the clamp existed.
        const float arrowKeyLookSpeed = 120.0f;
        rotationDelta.x += lookHorizontal * arrowKeyLookSpeed * clampedDt;
        rotationDelta.y -= lookVertical * arrowKeyLookSpeed * clampedDt;  // Inverted Y

        // Tab toggles world/local movement — FreeFly only (spec 2026-07-04 decision 2).
        if (mode == CameraMode::FreeFly && inputState->IsKeyPressed(EventBus::KeyCode::Tab)) {
            localMovement = !localMovement;
        }

        // Wheel: adjusts orbit distance (Orbit mode, unchanged) or fly speed (FreeFly mode, new).
        if (inputState->wheelZoom && inputState->wheelDelta.y != 0.0f) {
            if (mode == CameraMode::Orbit) {
                orbitDistance -= inputState->wheelDelta.y * inputState->wheelZoomSpeed;
                orbitDistance = glm::clamp(orbitDistance, kOrbitDistanceMin, kOrbitDistanceMax);
            } else {
                flySpeed *= std::pow(kFlySpeedScrollFactor, inputState->wheelDelta.y);
                flySpeed = glm::clamp(flySpeed, kFlySpeedMin, kFlySpeedMax);
            }
        }

        // Get keyboard movement axes
        float horizontal = inputState->GetAxisHorizontal();
        float vertical = inputState->GetAxisVertical();
        float upDown = inputState->GetAxisUpDown();

        movementDelta.x += horizontal;
        movementDelta.z += vertical;
        movementDelta.y += upDown;

        // F key: Orbit -> FreeFly, seeded from the current orbit-derived pose (Task 4 fills this
        // transition's body — placeholder guard kept here so the key is read in the same place as
        // every other per-frame input, per this node's existing convention).
        if (mode == CameraMode::Orbit && inputState->IsKeyPressed(EventBus::KeyCode::F)) {
            ExitOrbitToFreeFly();
        }
    }

    // Apply accumulated input deltas to camera state. Same stall-proofing clamp as above (0.1s
    // max) — ApplyMovement's zoom/pan speeds are also dt-scaled and would otherwise teleport too.
    float deltaTime = glm::min(inputState ? inputState->deltaTime : (1.0f / 60.0f), 0.1f);
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

    if (mode == CameraMode::Orbit) {
        // ORBIT MODE: compute the orbit-derived position/look-at, then rebuild `transform` as
        // the SAME inverse-lookAt shape ComposeTransform produces — both modes converge on one
        // representation, only what feeds glm::lookAt differs (a composed forward vs. an
        // explicit look-at target).
        glm::vec3 orbitOffset;
        orbitOffset.x = orbitDistance * cos(orbitPitch) * sin(orbitYaw);
        orbitOffset.y = orbitDistance * sin(orbitPitch);
        orbitOffset.z = orbitDistance * cos(orbitPitch) * cos(orbitYaw);
        const glm::vec3 cameraPos = orbitCenter + orbitOffset;
        transform = glm::inverse(glm::lookAt(cameraPos, orbitCenter, glm::vec3(0.0f, 1.0f, 0.0f)));
    }
    // FreeFly mode: `transform` is already current — ApplyRotation/ApplyMovement wrote it
    // directly this frame (or it's unchanged from the last frame if there was no input).

    const glm::vec3 cameraPosition = Vixen::RenderGraph::ExtractPosition(transform);
    const glm::vec3 forward = glm::normalize(-glm::vec3(transform[2]));
    const glm::vec3 right = glm::normalize(glm::vec3(transform[0]));
    const glm::vec3 up = glm::normalize(glm::vec3(transform[1]));

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
    currentCameraData.invView = transform;   // `transform` IS the inverse-view matrix already —
                                              // no local `view`/`glm::inverse` round-trip needed.

    // DEBUG: Log camera state once
    static bool loggedCamera = false;
    if (!loggedCamera) {
        NODE_LOG_DEBUG("[CameraNode] Camera position: (" + std::to_string(cameraPosition.x) + ", " + std::to_string(cameraPosition.y) + ", " + std::to_string(cameraPosition.z) + ")");
        NODE_LOG_DEBUG("[CameraNode] forward = (" + std::to_string(forward.x) + ", " + std::to_string(forward.y) + ", " + std::to_string(forward.z) + ")");
        NODE_LOG_DEBUG("[CameraNode] right = (" + std::to_string(right.x) + ", " + std::to_string(right.y) + ", " + std::to_string(right.z) + ")");
        NODE_LOG_DEBUG("[CameraNode] up = (" + std::to_string(up.x) + ", " + std::to_string(up.y) + ", " + std::to_string(up.z) + ")");
        loggedCamera = true;
    }
}

void CameraNode::ExitOrbitToFreeFly() {
    // `transform` already holds the current orbit-derived pose (both modes share the one field)
    // — there is nothing to copy or reseed. Only the mode flag needs to flip; UpdateCameraData's
    // FreeFly branch will simply stop overwriting `transform` from orbitCenter/orbitDistance next
    // frame, leaving it exactly where it was at the moment of the transition (no jump).
    mode = CameraMode::FreeFly;
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

    if (mode == CameraMode::FreeFly) {
        // Extract the CURRENT angle from the transform (never stored separately), apply the
        // smoothed delta, then rebuild the transform from scratch — no incremental rotation, so
        // no drift accumulates on `transform` itself (spec 2026-07-04 decision 2).
        auto [curYaw, curPitch] = Vixen::RenderGraph::ExtractYawPitch(transform);
        float newYaw = curYaw + smoothedRotationDelta.x * mouseSensitivity;
        float newPitch = curPitch - smoothedRotationDelta.y * mouseSensitivity;
        const float maxPitch = glm::radians(89.0f);
        newPitch = glm::clamp(newPitch, -maxPitch, maxPitch);
        transform = Vixen::RenderGraph::ComposeTransform(
            Vixen::RenderGraph::ExtractPosition(transform), newYaw, newPitch);
    } else {
        // Orbit mode: yaw/pitch still accumulate as transient locals feeding the orbit-sphere
        // formula in UpdateCameraData — the ORBIT ANGLE is not the same thing as "the camera's
        // own look direction" (the camera always looks AT orbitCenter in this mode), so it's
        // tracked separately from `transform`'s orientation.
        orbitYaw += smoothedRotationDelta.x * mouseSensitivity;
        orbitPitch -= smoothedRotationDelta.y * mouseSensitivity;
        const float maxPitch = glm::radians(89.0f);
        orbitPitch = glm::clamp(orbitPitch, -maxPitch, maxPitch);
    }

    // Clear raw rotation delta
    rotationDelta = glm::vec2(0.0f);
}

void CameraNode::ApplyMovement(float deltaTime) {
    if (glm::length(movementDelta) == 0.0f) {
        movementDelta = glm::vec3(0.0f);
        return;
    }

    if (mode == CameraMode::FreeFly) {
        // W/S/A/D: forward/back/strafe. World mode projects onto world X/Z regardless of facing
        // (matches Orbit's existing A/D-moves-center convention). Local mode uses camera facing,
        // flattened to the horizontal plane so looking down doesn't dive you into the ground.
        auto [curYaw, curPitch] = Vixen::RenderGraph::ExtractYawPitch(transform);
        (void)curPitch;  // only yaw is needed for flattened local-mode movement
        glm::vec3 moveForward, moveRight;
        if (localMovement) {
            moveForward = glm::vec3(sin(curYaw), 0.0f, -cos(curYaw));
            moveRight = glm::vec3(cos(curYaw), 0.0f, sin(curYaw));
        } else {
            moveForward = glm::vec3(0.0f, 0.0f, -1.0f);
            moveRight = glm::vec3(1.0f, 0.0f, 0.0f);
        }
        glm::vec3 newPos = Vixen::RenderGraph::ExtractPosition(transform);
        newPos += moveForward * movementDelta.z * flySpeed * deltaTime;
        newPos += moveRight * movementDelta.x * flySpeed * deltaTime;
        // Q/E: always world Y up/down, unconditionally (spec 2026-07-04 decision 6).
        newPos.y += movementDelta.y * flySpeed * deltaTime;
        Vixen::RenderGraph::SetPosition(transform, newPos);
    } else {
        // ORBIT MODE (unchanged):
        // W/S: Zoom in/out (change orbit distance)
        // A/D: Move orbit center left/right (X axis)
        // Q/E: Move orbit center up/down (Y axis)
        float zoomSpeed = 100.0f;  // Scaled for 128^3 world
        orbitDistance -= movementDelta.z * zoomSpeed * deltaTime;  // W zooms in, S zooms out
        orbitDistance = glm::clamp(orbitDistance, kOrbitDistanceMin, kOrbitDistanceMax);

        glm::vec3 moveVector(0.0f);
        moveVector.x = movementDelta.x;  // A/D moves left/right (X axis)
        moveVector.y = movementDelta.y;  // Q/E moves up/down (Y axis)
        orbitCenter += moveVector * moveSpeed * deltaTime;
    }

    movementDelta = glm::vec3(0.0f);
}

} // namespace Vixen::RenderGraph

// Self-registration (M3): registrar kept in this TU; RenderGraphNodes is whole-archived so it is not stripped.
VIXEN_REGISTER_NODE(Vixen::RenderGraph::CameraNodeType);
