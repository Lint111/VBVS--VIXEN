// Copyright (C) 2025 Lior Yanai (eLiorg). Licensed under the MIT License.
#pragma once
#include "Core/TypedNodeInstance.h"
#include "Core/NodeType.h"
#include "Core/PerFrameResources.h"
#include "Data/Nodes/PrevCameraConfigNodeConfig.h"
#include <memory>

namespace Vixen::RenderGraph {

/**
 * @brief Node type for PrevCameraConfigNode.
 */
class PrevCameraConfigNodeType : public TypedNodeType<PrevCameraConfigNodeConfig> {
public:
    PrevCameraConfigNodeType(const std::string& typeName = "PrevCameraConfig")
        : TypedNodeType<PrevCameraConfigNodeConfig>(typeName) {}
    virtual ~PrevCameraConfigNodeType() = default;

    std::unique_ptr<NodeInstance> CreateInstance(const std::string& instanceName) const override;
};

/**
 * @brief Uploads a Vixen::Gpu::PrevCameraConfig record (a single prevViewProj
 * mat4) into a ring-buffered, host-visible storage buffer — one SSBO per
 * frame-in-flight (mirrors AccumulationConfigNode's PerFrameResources ring
 * pattern exactly).
 *
 * Sampled Lighting Inc2 M3: prev-frame camera matrix plumbing. Copies
 * CameraNode's PREV_VIEW_PROJ output verbatim into this frame's ring slot
 * every Execute — no computation of its own, no frame-counter state (unlike
 * AccumulationConfigNode, which owns reset-on-motion tracking; this node's
 * only job is getting a CPU-retained matrix onto the GPU). Bound at binding
 * 21 (see BuildRenderGraph.cpp) — declared in the shader but NOT read by any
 * path affecting outColor this milestone (M4 consumes it for reprojection).
 *
 * Separate node vs. folding into AccumulationConfigNode: same separate-vs-
 * extend rationale as every other *ConfigNode in this codebase (see
 * ShadowConfigNode.h's file header) — this is a distinct concern (camera
 * matrix history) from accumulation's own compute-budget/reset-policy data.
 *
 * Lifecycle: the ring buffers persist across graph recompile; released on
 * FinalTeardown (see CleanupImpl) — identical KI-004-safe pattern to
 * ShadowConfigNode/AccumulationConfigNode.
 */
class PrevCameraConfigNode : public TypedNode<PrevCameraConfigNodeConfig> {
public:
    using Base = TypedNode<PrevCameraConfigNodeConfig>;

    PrevCameraConfigNode(const std::string& instanceName, NodeType* nodeType);
    ~PrevCameraConfigNode() override = default;

protected:
    void SetupImpl(TypedSetupContext&    ctx) override;
    void CompileImpl(TypedCompileContext& ctx) override;
    void ExecuteImpl(TypedExecuteContext& ctx) override;
    void CleanupImpl(TypedCleanupContext& ctx) override;

private:
    static const uint32_t kRingSize;  // = FrameSyncNodeConfig::MAX_FRAMES_IN_FLIGHT

    PerFrameResources perFrame_;
};

} // namespace Vixen::RenderGraph
