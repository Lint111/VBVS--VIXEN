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

    /// Recipe-Live-App-Bucketed-Dispatch Inc4 M3: BY-VALUE accessor for the live view-proj
    /// matrix, for a caller OUTSIDE the render graph's connection system (e.g. PreTick feeding
    /// a ConstantNode each frame) that needs a plain glm::mat4 -- CURRENT_VIEW_PROJ's own graph
    /// output slot is `const glm::mat4&` (reference semantics, for cheap node-to-node pass-
    /// through), which is NOT interchangeable with PushConstantGathererNode's generic
    /// ExtractResourceAs<T>() path (that path always does GetHandle<T>() with T as a bare VALUE
    /// type -- wiring a reference-typed slot directly into a raw variadic push-constant field
    /// index is a producer/consumer Resource-tag mismatch that throws std::bad_any_cast at
    /// Execute time, not a hypothetical -- this is precisely the bug this accessor exists to let
    /// a caller work around by copying the value at the point of use instead).
    glm::mat4 GetCurrentViewProj() const { return currentViewProj; }

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
        EngageOrbit();
        orbitDistance = glm::clamp(distance, kOrbitDistanceMin, kOrbitDistanceMax);
    }
    void SetYawForTest(float yawRadians) { EngageOrbit(); yaw = yawRadians; }
    /**
     * @brief Directly set the live orbit pitch. Mirrors SetYawForTest exactly -- together they
     * choose where on the orbit sphere the camera sits at the current orbitDistance.
     *
     * NOTE (Tiered-ESVO Inc3 M6): yaw/pitch only relocate the camera along the orbit sphere --
     * by default `forward` is `normalize(orbitCenter - cameraPosition)` (see UpdateCameraData),
     * so the camera looks at orbitCenter regardless of yaw/pitch unless a look-target has been
     * set (see SetLookTargetForTest / PARAM_LOOK_TARGET_*, added Inc3 M8). See the Tiered-ESVO
     * Inc3 M6 Progress Log for the investigation that found the original constraint.
     */
    void SetPitchForTest(float pitchRadians) { EngageOrbit(); pitch = pitchRadians; }

    /**
     * @brief Directly set a live look-target, independent of orbitCenter (Tiered-ESVO Inc3 M8).
     *
     * When set, UpdateCameraData aims `forward` at `normalize(lookTarget - cameraPosition)`
     * instead of `normalize(orbitCenter - cameraPosition)` -- this is what actually breaks the
     * "camera always looks at orbitCenter" constraint (yaw/pitch alone cannot, per
     * SetPitchForTest's note above). Mirrors the EngageOrbit-on-write convention of the other
     * ForTest setters. Call ClearLookTargetForTest() to fall back to orbitCenter again.
     */
    void SetLookTargetForTest(glm::vec3 target) { EngageOrbit(); hasLookTarget_ = true; lookTarget_ = target; }

    /// Reverts to the default behavior (forward aimed at orbitCenter). See SetLookTargetForTest.
    void ClearLookTargetForTest() { hasLookTarget_ = false; }

    /**
     * @brief Directly set the live camera POSITION in FIXED mode (Tiered-ESVO Inc3 M8 Task 19).
     *
     * Unlike SetOrbitDistanceForTest (which scripts distance from orbitCenter along the orbit
     * sphere), this scripts cameraPosition itself -- the only way to express a genuinely
     * TRANSLATING flight path (position changes along an arbitrary trajectory, not just radius
     * from a fixed pivot). Does NOT call EngageOrbit(): a scripted position is a FIXED-mode
     * pose write, mirroring how PARAM_CAMERA_X/Y/Z behave at rest (UpdateCameraData's FIXED
     * MODE branch keeps cameraPosition as configured). If orbit was already engaged this
     * session (e.g. a prior SetOrbitDistanceForTest call), UpdateCameraData's orbit branch
     * would recompute cameraPosition from orbitCenter/orbitDistance every frame and silently
     * override this write -- callers driving a flight path must not also drive orbit distance
     * in the same session. Combine with SetLookTargetForTest to aim the flight independent of
     * position, exactly as a real approach-and-orbit trajectory does.
     */
    void SetPositionForTest(glm::vec3 position) { cameraPosition = position; }

    /**
     * @brief Set a live look-target WITHOUT engaging orbit mode (Tiered-ESVO Inc3 M8 Task 19).
     *
     * SetLookTargetForTest() calls EngageOrbit() (mirrors every other ForTest setter's
     * convention), which is correct for Task 16/17's orbit-around-center + retarget-aim demo,
     * but WRONG for a translating flight path driven by SetPositionForTest(): engaging orbit
     * latches orbitActive_, and UpdateCameraData's ORBIT MODE branch then recomputes
     * cameraPosition from orbitCenter/orbitDistance every frame, silently overriding the
     * flight path's own position writes (found live: Task 19's first capture attempt rendered
     * a static, unchanging frame across all 400 scripted ticks). This variant only mutates
     * hasLookTarget_/lookTarget_ -- FIXED MODE's own branch already honors a look-target
     * (UpdateCameraData: `if (hasLookTarget_) { forward = normalize(lookTarget_ -
     * cameraPosition); }`), so combining this with SetPositionForTest gives full
     * position+aim control while staying in FIXED mode throughout.
     */
    void SetLookTargetNoOrbitForTest(glm::vec3 target) { hasLookTarget_ = true; lookTarget_ = target; }

    /**
     * @brief Read-only introspection of live camera pose state (Sampled Lighting Inc4 M5).
     * Added for live-gate debugging (confirming a scripted ForTest camera write actually
     * took effect, rather than being silently overridden by an already-engaged orbit mode
     * or a subsequent frame's recompute) -- no prior getter existed for any of this state.
     */
    glm::vec3 GetCameraPositionForTest() const { return cameraPosition; }
    bool GetOrbitActiveForTest() const { return orbitActive_; }
    glm::vec3 GetOrbitCenterForTest() const { return orbitCenter; }

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

    // Bodies-0 root-cause fix: the configured PARAM_CAMERA_* pose is authoritative at rest
    // (fd33f632's "button-gated orbit" intent, completed here — that commit gated the
    // rotation *delta* but left UpdateCameraData's *position* recompute unconditional, so
    // every scene's camera silently snapped to orbitCenter+orbitDistance from frame 1). Call
    // once on the frame orbit first actually engages (drag-threshold crossed, wheel-zoom
    // event, WASD/QE movement, or a direct SetOrbitDistanceForTest/SetYawForTest write) to
    // latch orbitActive_ and re-seed orbitDistance/yaw/pitch from the CURRENT fixed
    // cameraPosition relative to orbitCenter, so engaging orbit does not teleport the camera
    // to the stale default orbit pose. Idempotent after the first call (orbitActive_ guard).
    // NOTE: SetupImpl also latches orbitActive_ directly (no seeding needed there) when a
    // consumer explicitly configures any PARAM_ORBIT_* parameter — see its own comment.
    void EngageOrbit();

    bool orbitActive_ = false;

    // Current camera data struct
    CameraData currentCameraData;

    // Sampled Lighting Inc2 M3: prev-frame view*proj retention. Computed fresh each
    // ExecuteImpl/CompileImpl as `projection * view` (the current frame's own matrices,
    // recomputed from invProjection/invView already in currentCameraData rather than a
    // second glm::perspective/lookAt call — see UpdateCameraData), then STORED here for
    // the NEXT frame to read as its "previous" — i.e. this always lags currentCameraData
    // by exactly one frame/Execute. NOT a CameraData field (see PREV_VIEW_PROJ_Slot's own
    // comment in CameraNodeConfig.h for why: CameraData's layout is push-constant-frozen).
    // Seeded to the identity at construction; the very first frame's "previous" is
    // therefore not a real prior pose, but M3 never consumes this value (upload-only,
    // byte-identical-output gate) so an unused seed is harmless — M4 (the first real
    // consumer) must treat frame 1 / any reset-on-motion frame as "no valid history"
    // exactly as historyImage already requires (see AccumulationConfigNode's own
    // alpha>=1.0 history-skip guard).
    glm::mat4 prevViewProj{1.0f};

    // Recipe Bucketing Inc2 M1: THIS frame's view*proj, published every Compile/Execute
    // under CURRENT_VIEW_PROJ. Computed at the exact same call sites as prevViewProj's
    // assignment (see CameraNode.cpp) -- this is the freshly-computed `projection * view`
    // BEFORE it becomes next frame's "previous", unlike prevViewProj which always lags by
    // one frame. Consumed by the recipe instance-bucketing pre-pass to project world-space
    // bound spheres to screen space for THIS frame's actual view.
    glm::mat4 currentViewProj{1.0f};

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

    // Tiered-ESVO Inc3 M8: genuine look-target, decoupled from orbitCenter (see
    // SetLookTargetForTest / PARAM_LOOK_TARGET_* / UpdateCameraData). hasLookTarget_ == false
    // (the default, and every pre-M8 scene) means UpdateCameraData aims forward at orbitCenter
    // exactly as before -- lookTarget_ itself is unused in that case.
    bool hasLookTarget_ = false;
    glm::vec3 lookTarget_{0.0f};

    // Orbit distance bounds (keeps camera inside the 128^3 world). Shared by W/S zoom
    // (ApplyMovement) and wheel zoom (ExecuteImpl, M4) so both paths agree on one ceiling.
    // Min is a near-zero floor (not 0) only to avoid a degenerate/undefined view direction
    // exactly at the orbit center — small enough to zoom arbitrarily close to fine surface
    // detail (e.g. inspecting a sub-voxel artifact), which the old 5.0 floor (tuned for the
    // main app's 10-unit Cornell-box demo scene) prevented for the editor's smaller objects.
    // Tiered-ESVO Inc3 M4: kOrbitDistanceMin widened from 0.1 to 1e-6 for the
    // Earth-scale surface-to-orbit demo (VIXEN_TIER_EARTH_DEMO/VIXEN_TIER_EARTH_ZOOM_DEMO).
    // That demo's T2 (bedrock) tier has a world-unit diameter of ~4.6e-5 (48 world units *
    // (2^-10)^2 across both magnified hops -- see BuildRenderGraph.cpp's own derivation
    // comment) -- the OLD 0.1 floor was ~2200x TOO COARSE to frame T2-scale detail in orbit
    // at all (every reachable orbitDistance would already be many T2-diameters away). This
    // is the opposite widening from what the plan anticipated (it expected the MAX bound to
    // need raising for a literal Earth-diameter-in-world-units mapping); this construction
    // instead keeps T0's own world-unit diameter unchanged (48, matching every existing
    // demo body) and lets the CHAINED MAGNIFICATION do the scale work, so it is the NEAR
    // bound that turns out to be the actual constraint. kOrbitDistanceMax is UNCHANGED
    // (120 already comfortably frames a full 48-unit-diameter T0 orbit view).
    static constexpr float kOrbitDistanceMin = 1e-6f;
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
    float lastParamLookX_ = NAN, lastParamLookY_ = NAN, lastParamLookZ_ = NAN;

    // Last-seen pose_seq value (NaN = never seen). A change here (including the first sight)
    // forces every PRESENT pose param to reapply this SetupImpl regardless of lastApplied — see
    // CameraNodeConfig::PARAM_POSE_SEQ.
    float lastPoseSeq_ = NAN;
};

} // namespace Vixen::RenderGraph
