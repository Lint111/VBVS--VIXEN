#pragma once

#include "Core/TypedNodeInstance.h"
#include "Core/NodeType.h"
#include "Core/NodeLogging.h"
#include "Data/Nodes/CameraNodeConfig.h"
#include "Data/CameraData.h"
#include "Data/CameraTransform.h"
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

    /// Live pose model (spec 2026-07-04): lets the host detect a mode transition it didn't
    /// trigger itself (e.g. the F-key Orbit->FreeFly exit, which is handled entirely inside
    /// ExecuteImpl — the host has no other way to know it happened). Poll once per tick and
    /// compare against the previously-polled value to detect the edge.
    CameraMode GetMode() const { return mode; }

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

    // Camera state — ONE stored pose (position + orientation) for BOTH FreeFly and Orbit mode
    // (spec 2026-07-04 camera-transform-refactor). Rebuilt fresh from angle every frame via
    // ComposeTransform — never incrementally rotated, so there is nothing to accumulate drift on.
    glm::mat4 transform = Vixen::RenderGraph::ComposeTransform(glm::vec3(0.0f, 5.0f, 30.0f), 0.0f, 0.0f);
    float fov = 45.0f;
    float nearPlane = 0.1f;
    float farPlane = 5000.0f;  // Extended for far viewing
    uint32_t gridResolution = 128;

    // Orbit mode: WASD/QE moves orbit center, arrow keys rotate around it
    glm::vec3 orbitCenter{5.0f, 5.0f, 5.0f};  // Center of grid (10/2 for 10^3 world size)
    float orbitDistance = 30.0f;  // Distance from orbit center (scaled for 10^3 world)
    float orbitYaw = 0.0f;    // orbit angle around orbitCenter (was the shared `yaw` field pre-refactor)
    float orbitPitch = 0.0f;  // orbit angle around orbitCenter (was the shared `pitch` field pre-refactor)

    // Orbit distance bounds (keeps camera inside the 128^3 world). Shared by W/S zoom
    // (ApplyMovement) and wheel zoom (ExecuteImpl, M4) so both paths agree on one ceiling.
    static constexpr float kOrbitDistanceMin = 5.0f;
    static constexpr float kOrbitDistanceMax = 120.0f;

    // Which pose model is currently active. Starts in FreeFly (spec 2026-07-04): a player should
    // be able to fly around before ever clicking a body into orbit.
    CameraMode mode = CameraMode::FreeFly;

    // Free-fly's own initial seed is set directly into `transform` in the constructor (see
    // CameraNode.cpp) rather than tracked as separate fields here — both modes now share the one
    // `transform` field above.
    // sceneScale (spec 2026-07-04 camera-speed-scale-derived): the host-sent win.halfExtent,
    // used to derive flySpeed/zoomSpeed/moveSpeed proportional to the system's actual extent.
    // Default 1.0f is a placeholder until the first PARAM_SCENE_SCALE arrives at boot — never
    // divided into, only multiplied, so a stale default can't produce a divide-by-zero.
    float sceneScale = 1.0f;
    float flySpeed = sceneScale * 0.2f;   // units/sec, scroll-wheel adjustable in FreeFly
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

    // Camera control parameters — moveSpeed/zoomSpeed now DERIVED from sceneScale (spec 2026-07-04
    // camera-speed-scale-derived), set alongside flySpeed whenever PARAM_SCENE_SCALE changes.
    // zoomSpeed was previously a local variable inside ApplyMovement's Orbit branch; promoted to a
    // member field so it updates in the same place as flySpeed/moveSpeed.
    float moveSpeed = sceneScale * 0.2f;   // Horizontal movement: units per second
    float zoomSpeed = sceneScale * 0.2f;   // Orbit W/S distance-zoom speed
    float verticalSpeed = 20.0f;   // Vertical movement (QE): units per second (unused — see note below; left as-is, out of this fix's scope)
    float mouseSensitivity = 0.0015f;  // Radians per pixel (reduced from 0.004 for less sensitivity)
    float mouseSmoothingFactor = 0.6f;  // 0=no smoothing, 1=instant (0.6 = responsive)
    float maxRotationDeltaPerFrame = 100.0f;  // Max pixels per frame to prevent jumps

    // Last-applied pose-param values (NaN = never applied). SetupImpl re-runs on EVERY graph
    // recompile (resize, any node's param edit) — applying a pose param only when its stored
    // value actually changed keeps recompiles from snapping the live orbit camera back to t0,
    // while setcam/lookcam-style param writes still land exactly once.
    // Last-applied pose-param tracking for the NEW Vec3/Mat4 params (spec 2026-07-04
    // camera-transform-refactor). NaN doesn't generalize as a "never applied" sentinel for
    // vec3/mat4 (no natural all-NaN check without per-component comparison), so these use an
    // explicit `bool hasApplied` flag instead, paired with the last-applied value for
    // change-detection (mirrors the float applyIfChanged's semantics exactly, just typed).
    bool hasAppliedCameraPosition_ = false;
    glm::vec3 lastCameraPosition_{0.0f};
    bool hasAppliedOrbitCenter_ = false;
    glm::vec3 lastOrbitCenter_{0.0f};
    bool hasAppliedInitialFlyTransform_ = false;
    glm::mat4 lastInitialFlyTransform_{1.0f};
    float lastParamYaw_ = NAN, lastParamPitch_ = NAN;
    float lastParamOrbitDist_ = NAN;
    float lastParamCameraMode_ = NAN;   // mirrors the other lastParam*_ change-tracking fields
    float lastParamSceneScale_ = NAN;   // tracks PARAM_SCENE_SCALE (plain float, same pattern)

    // Last-seen pose_seq value (NaN = never seen). A change here (including the first sight)
    // forces every PRESENT pose param to reapply this SetupImpl regardless of lastApplied — see
    // CameraNodeConfig::PARAM_POSE_SEQ.
    float lastPoseSeq_ = NAN;
};

} // namespace Vixen::RenderGraph
