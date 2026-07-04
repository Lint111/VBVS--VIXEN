#pragma once

#include "Core/TypedNodeInstance.h"
#include "Core/NodeType.h"
#include "Core/NodeLogging.h"
#include "Data/Nodes/CameraNodeConfig.h"
#include "Data/CameraData.h"
#include <glm/glm.hpp>
#include <cmath>
#include <memory>

// Forward declarations
namespace Vixen::Vulkan::Resources {
    class VulkanDevice;
}

namespace Vixen::RenderGraph {

/**
 * @brief Node type for camera management
 */
class CameraNodeType : public TypedNodeType<CameraNodeConfig> {
public:
    CameraNodeType(const std::string& typeName = "Camera")
        : TypedNodeType<CameraNodeConfig>(typeName) {}
    virtual ~CameraNodeType() = default;

    std::unique_ptr<NodeInstance> CreateInstance(
        const std::string& instanceName
    ) const override;
};

/**
 * @brief Which pose model currently drives CameraNode's position/orientation.
 *
 * FreeFly: cameraPosition == flyPosition, driven directly by WASD/QE/middle-drag.
 * Orbit: cameraPosition derived from orbitCenter + orbit(yaw, pitch, orbitDistance), as before
 * this feature existed. The two pose models never mix mid-frame — UpdateCameraData branches on
 * this exactly once per Execute.
 */
enum class CameraMode : uint8_t { FreeFly = 0, Orbit = 1 };

/**
 * @brief Camera uniform buffer node for raymarching shaders
 *
 * Creates per-frame uniform buffers containing camera matrices and parameters.
 * Updates camera position and orientation via parameters.
 *
 * Phase: Research implementation (voxel raymarching)
 */
class CameraNode : public TypedNode<CameraNodeConfig> {
public:

    CameraNode(
        const std::string& instanceName,
        NodeType* nodeType
    );
    ~CameraNode() override = default;

    /// Live camera as rendered this frame (orbit-derived position + basis). The pose PARAMS are
    /// only setup-time requests; anything that needs the actual view (e.g. the host's CPU body
    /// pick) must read this, not the params — they diverge as soon as the user orbits/zooms.
    const CameraData& GetCurrentCameraData() const { return currentCameraData; }

protected:
    void SetupImpl(TypedSetupContext& ctx) override;
    void CompileImpl(TypedCompileContext& ctx) override;
    void ExecuteImpl(TypedExecuteContext& ctx) override;
    void CleanupImpl(TypedCleanupContext& ctx) override;

private:
    void UpdateCameraData(float aspectRatio);

    // Orbit -> FreeFly transition (F key). Seeds flyPosition/flyYaw/flyPitch from the current
    // orbit-derived pose so the view doesn't jump. Implemented fully in a later change; stubbed
    // here so ExecuteImpl's F-key read has something to call.
    void ExitOrbitToFreeFly();

    // Apply accumulated input deltas to camera state
    void ApplyInputDeltas(float deltaTime);
    void ApplyRotation();
    void ApplyMovement(float deltaTime);


    // Current camera data struct
    CameraData currentCameraData;

    // Camera state
    glm::vec3 cameraPosition{0.0f, 0.0f, 3.0f};
    float yaw = 0.0f;
    float pitch = 0.0f;
    float fov = 45.0f;
    float nearPlane = 0.1f;
    float farPlane = 5000.0f;  // Extended for far viewing
    uint32_t gridResolution = 128;

    // Orbit mode: WASD/QE moves orbit center, arrow keys rotate around it
    glm::vec3 orbitCenter{5.0f, 5.0f, 5.0f};  // Center of grid (10/2 for 10^3 world size)
    float orbitDistance = 30.0f;  // Distance from orbit center (scaled for 10^3 world)

    // Orbit distance bounds (keeps camera inside the 128^3 world). Shared by W/S zoom
    // (ApplyMovement) and wheel zoom (ExecuteImpl, M4) so both paths agree on one ceiling.
    static constexpr float kOrbitDistanceMin = 5.0f;
    static constexpr float kOrbitDistanceMax = 120.0f;

    // Which pose model is currently active. Starts in FreeFly (spec 2026-07-04): a player should
    // be able to fly around before ever clicking a body into orbit.
    CameraMode mode = CameraMode::FreeFly;

    // Free-fly pose — independent of orbitCenter/yaw/pitch/orbitDistance above. Arbitrary start;
    // if the player enters Orbit first via a body click this is irrelevant until the next F-key
    // exit reseeds it from the orbit-derived pose (see ExecuteImpl).
    glm::vec3 flyPosition{0.0f, 5.0f, 30.0f};
    float flyYaw = 0.0f;
    float flyPitch = 0.0f;
    float flySpeed = 20.0f;               // units/sec, scroll-wheel adjustable in FreeFly
    static constexpr float kFlySpeedMin = 2.0f;
    static constexpr float kFlySpeedMax = 200.0f;
    static constexpr float kFlySpeedScrollFactor = 1.15f;  // multiplicative step per wheel notch

    // Tab toggles this in FreeFly only. World: WASD always moves along world X/Z. Local: WASD
    // moves relative to camera facing (flattened to the horizontal plane). Q/E are always world Y
    // regardless of this flag (spec 2026-07-04 decision 6).
    bool localMovement = false;

    // Accumulated input deltas (cleared after applying)
    glm::vec3 movementDelta{0.0f};  // Local-space WASD + global Y for QE
    glm::vec2 rotationDelta{0.0f};  // Yaw/pitch from mouse (raw accumulation)
    glm::vec2 smoothedRotationDelta{0.0f};  // Smoothed rotation for jitter reduction

    // Camera control parameters
    float moveSpeed = 20.0f;       // Horizontal movement: units per second (scaled for 10^3 world)
    float verticalSpeed = 20.0f;   // Vertical movement (QE): units per second
    float mouseSensitivity = 0.0015f;  // Radians per pixel (reduced from 0.004 for less sensitivity)
    float mouseSmoothingFactor = 0.6f;  // 0=no smoothing, 1=instant (0.6 = responsive)
    float maxRotationDeltaPerFrame = 100.0f;  // Max pixels per frame to prevent jumps

    // Last-applied pose-param values (NaN = never applied). SetupImpl re-runs on EVERY graph
    // recompile (resize, any node's param edit) — applying a pose param only when its stored
    // value actually changed keeps recompiles from snapping the live orbit camera back to t0,
    // while setcam/lookcam-style param writes still land exactly once.
    float lastParamCamX_ = NAN, lastParamCamY_ = NAN, lastParamCamZ_ = NAN;
    float lastParamYaw_ = NAN, lastParamPitch_ = NAN;
    float lastParamOrbitCX_ = NAN, lastParamOrbitCY_ = NAN, lastParamOrbitCZ_ = NAN;
    float lastParamOrbitDist_ = NAN;
    float lastParamCameraMode_ = NAN;   // mirrors the other lastParam*_ change-tracking fields

    // Last-seen pose_seq value (NaN = never seen). A change here (including the first sight)
    // forces every PRESENT pose param to reapply this SetupImpl regardless of lastApplied — see
    // CameraNodeConfig::PARAM_POSE_SEQ.
    float lastPoseSeq_ = NAN;
};

} // namespace Vixen::RenderGraph
