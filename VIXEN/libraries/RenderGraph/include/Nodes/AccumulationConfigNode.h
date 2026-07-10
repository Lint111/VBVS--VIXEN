// Copyright (C) 2025 Lior Yanai (eLiorg). Licensed under the MIT License.
#pragma once
#include "Core/TypedNodeInstance.h"
#include "Core/NodeType.h"
#include "Core/PerFrameResources.h"
#include "Data/Nodes/AccumulationConfigNodeConfig.h"
#include <glm/glm.hpp>
#include <memory>

namespace Vixen::RenderGraph {

/**
 * @brief Node type for AccumulationConfigNode.
 */
class AccumulationConfigNodeType : public TypedNodeType<AccumulationConfigNodeConfig> {
public:
    AccumulationConfigNodeType(const std::string& typeName = "AccumulationConfig")
        : TypedNodeType<AccumulationConfigNodeConfig>(typeName) {}
    virtual ~AccumulationConfigNodeType() = default;

    std::unique_ptr<NodeInstance> CreateInstance(const std::string& instanceName) const override;
};

/**
 * @brief Uploads a Vixen::Gpu::AccumulationConfig record into a ring-buffered,
 * host-visible storage buffer — one SSBO per frame-in-flight (mirrors
 * ShadowConfigNode's PerFrameResources ring pattern exactly).
 *
 * Separate node vs. extending LightingConfigNode/ShadowConfigNode to carry
 * more records (Sampled Lighting Inc2 M1 decision, same rationale as
 * ShadowConfigNode.h's file header): a separate node keeps
 * AccumulationConfig's own lifecycle/content independent of lighting/shadow
 * data (a future UI-driven alpha/maxFrames tuning control would only touch
 * this node), and avoids growing an unrelated node's config/slot surface for
 * a logically distinct concern (temporal-accumulation COMPUTE BUDGET, not
 * light or shadow DATA).
 *
 * Content: enabled=0 by default (M1's zero-visual-delta gate — the accumulate
 * seam in BodyInstanceRayMarch.comp stays a pure passthrough); the
 * VIXEN_ACCUMULATION_ENABLED=1 env lever (mirrors ShadowConfigNode's own
 * VIXEN_SHADOW_CONFIG_ENABLED convention) flips on M2's default behavior:
 * alpha=0 (sentinel for the converging 1/N mode), maxFrames capping that
 * convergence, resetOnMotion=1 (hard-reset the frame counter to 1 the
 * instant the camera moves — zero ghosting by construction). See
 * MakeDefaultAccumulationConfig in the .cpp. Re-uploaded every Execute
 * (16 B, negligible) so a future milestone can mutate it via
 * SetAccumulationConfig() with no graph rewiring.
 *
 * Sampled Lighting Inc2 M2: this node also owns the consecutive-static-
 * camera frame counter (FRAME_COUNTER output) that drives the shader's
 * converging-1/N alpha. It reads CAMERA_DATA every Execute and epsilon-
 * compares cameraPos/cameraDir against the previous frame (same
 * change-detection idiom as VulkanGraphApplication::UpdateBodySceneResidency);
 * on any motion (and resetOnMotion != 0) the counter drops back to 1,
 * otherwise it increments (clamped to accumulationConfig.maxFrames when
 * set). The counter lives here, not on CameraNode, so it can react to
 * resetOnMotion — an accumulation-owned policy — without adding an
 * accumulation-specific field to CameraData's shader-layout-frozen struct.
 *
 * Sampled Lighting Inc2 M4: when accumulationConfig.reprojectionEnabled != 0
 * (VIXEN_ACCUMULATION_REPROJECT=1), this node's resetOnMotion hard-reset is
 * suppressed on motion (the counter keeps incrementing) — the shader's own
 * per-pixel reprojection+validation against PrevCameraConfig.prevViewProj
 * (Inc2 M3) takes over disocclusion handling instead. The very first Execute
 * still always resets to 1 in every mode (no valid previous frame yet).
 *
 * Lifecycle: the ring buffers persist across graph recompile; released on
 * FinalTeardown (see CleanupImpl) — identical KI-004-safe pattern to
 * ShadowConfigNode.
 */
class AccumulationConfigNode : public TypedNode<AccumulationConfigNodeConfig> {
public:
    using Base = TypedNode<AccumulationConfigNodeConfig>;

    AccumulationConfigNode(const std::string& instanceName, NodeType* nodeType);
    ~AccumulationConfigNode() override = default;

protected:
    void SetupImpl(TypedSetupContext&    ctx) override;
    void CompileImpl(TypedCompileContext& ctx) override;
    void ExecuteImpl(TypedExecuteContext& ctx) override;
    void CleanupImpl(TypedCleanupContext& ctx) override;

private:
    static const uint32_t kRingSize;  // = FrameSyncNodeConfig::MAX_FRAMES_IN_FLIGHT

    PerFrameResources perFrame_;

    // Reset-on-motion frame counter state (Inc2 M2). frameCounterEverEvaluated_ == false
    // means "first Execute" -- always treated as a reset (counter starts at 1), mirroring
    // UpdateBodySceneResidency's own residencyTriggerEverEvaluated_ first-frame convention.
    bool      frameCounterEverEvaluated_ = false;
    glm::vec3 lastCameraPos_{0.0f};
    glm::vec3 lastCameraDir_{0.0f};
    uint32_t  accumFrameCounter_ = 1;
};

} // namespace Vixen::RenderGraph
