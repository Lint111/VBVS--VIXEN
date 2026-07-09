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

    /// Live camera as rendered this frame (orbit-derived position + basis), resolved by the
    /// most recent ExecuteImpl. The pose PARAMS are only setup-time requests; anything that needs
    /// the actual view (e.g. the host's CPU body pick, VulkanGraphApplication::Update's residency
    /// trigger) must read this, not the params — they diverge as soon as the user orbits/zooms.
    /// Mirrors the live-GetInstance-lookup pattern of GetWindowHandle()/SetBodyInstances() (host
    /// reads node state each tick, no new graph wiring). Empty/default before the first
    /// ExecuteImpl runs (mirrors every other node's pre-Compile state).
    const CameraData& GetCurrentCameraData() const { return currentCameraData; }

    /**
     * @brief Directly set the live orbit distance/yaw (Sparse-Mip ESVO LOD Inc1 M4c live gate).
     *
     * `orbitDistance`/`yaw` are read every ExecuteImpl (no Setup-time-only re-fetch, unlike
     * `fov`/`orbitCenter`) — mirrors the existing wheel-zoom/mouse-drag live-mutation path
     * (CameraNode.cpp's ExecuteImpl) but driven directly by a host script instead of
     * InputState, for an unattended VIXEN_RESIDENCY_GATE_DEMO run that needs to move the
     * camera toward/away from a body over many frames with no real window/mouse. Clamped to
     * the same [kOrbitDistanceMin, kOrbitDistanceMax] bounds every other zoom path respects.
     */
    void SetOrbitDistanceForTest(float distance) {
        orbitDistance = glm::clamp(distance, kOrbitDistanceMin, kOrbitDistanceMax);
    }
    void SetYawForTest(float yawRadians) { yaw = yawRadians; }

protected:
    void SetupImpl(TypedSetupContext& ctx) override;
    void CompileImpl(TypedCompileContext& ctx) override;
    void ExecuteImpl(TypedExecuteContext& ctx) override;
    void CleanupImpl(TypedCleanupContext& ctx) override;

private:
    void UpdateCameraData(float aspectRatio);

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
    // Min is a near-zero floor (not 0) only to avoid a degenerate/undefined view direction
    // exactly at the orbit center — small enough to zoom arbitrarily close to fine surface
    // detail (e.g. inspecting a sub-voxel artifact), which the old 5.0 floor (tuned for the
    // main app's 10-unit Cornell-box demo scene) prevented for the editor's smaller objects.
    static constexpr float kOrbitDistanceMin = 0.1f;
    static constexpr float kOrbitDistanceMax = 120.0f;

    // Accumulated input deltas (cleared after applying)
    glm::vec3 movementDelta{0.0f};  // Local-space WASD + global Y for QE
    glm::vec2 rotationDelta{0.0f};  // Yaw/pitch from mouse (raw accumulation)
    glm::vec2 smoothedRotationDelta{0.0f};  // Smoothed rotation for jitter reduction

    // LeftDrag orbit-gate state (input-rework M4): tracks cumulative in-press motion since the
    // left button went down, so a press stays a click below InputState::dragThresholdPx and only
    // becomes an orbit-drag once crossed. Reset on release.
    float dragAccumPx_ = 0.0f;
    bool dragThresholdCrossed_ = false;

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

    // Last-seen pose_seq value (NaN = never seen). A change here (including the first sight)
    // forces every PRESENT pose param to reapply this SetupImpl regardless of lastApplied — see
    // CameraNodeConfig::PARAM_POSE_SEQ.
    float lastPoseSeq_ = NAN;
};

} // namespace Vixen::RenderGraph
