// Copyright (C) 2025 Lior Yanai (eLiorg). Licensed under the MIT License.
#pragma once
#include "Core/TypedNodeInstance.h"
#include "Core/NodeType.h"
#include "Core/PerFrameResources.h"
#include "Data/Nodes/HitAccumParamsConfigNodeConfig.h"
#include <memory>

namespace Vixen::RenderGraph {

/**
 * @brief Node type for HitAccumParamsConfigNode.
 */
class HitAccumParamsConfigNodeType : public TypedNodeType<HitAccumParamsConfigNodeConfig> {
public:
    HitAccumParamsConfigNodeType(const std::string& typeName = "HitAccumParamsConfig")
        : TypedNodeType<HitAccumParamsConfigNodeConfig>(typeName) {}
    virtual ~HitAccumParamsConfigNodeType() = default;

    std::unique_ptr<NodeInstance> CreateInstance(const std::string& instanceName) const override;
};

/**
 * @brief Ring-buffered, host-visible storage buffer for the hit-accumulate
 * pass's per-frame params (epoch, primary cone, detail, camera) — mirrors
 * ShadowConfigNode/PrevCameraConfigNode's PerFrameResources ring pattern
 * exactly (B2, docs/plans/2026-08-04-wavefront-recipe-shading.md).
 *
 * Unlike those siblings, the CONTENT is written by VulkanGraphApplication::
 * PreTick (not this node's own ExecuteImpl) — PreTick runs before the
 * graph's Execute pass, so it needs to write into the SAME ring slot this
 * frame's ExecuteImpl will emit. MapCurrentForWrite()/UnmapCurrentForWrite()
 * expose that slot by the frame index FrameSyncNode::GetCurrentFrameIndex()
 * returns (stable at PreTick time — only advances in FrameSyncNode's own
 * CleanupImpl). ExecuteImpl below does no content writing of its own; it
 * only re-emits the ring slot for the CURRENT_FRAME_INDEX it's given (same
 * value PreTick used), so the descriptor binds what PreTick just wrote.
 *
 * Lifecycle: ring buffers persist across graph recompile; released on
 * FinalTeardown (KI-004-safe pattern, identical to the siblings).
 */
class HitAccumParamsConfigNode : public TypedNode<HitAccumParamsConfigNodeConfig> {
public:
    using Base = TypedNode<HitAccumParamsConfigNodeConfig>;

    HitAccumParamsConfigNode(const std::string& instanceName, NodeType* nodeType);
    ~HitAccumParamsConfigNode() override = default;

    /// @brief Map this frame's ring slot for PreTick to write into. frameIndex
    /// should be FrameSyncNode::GetCurrentFrameIndex() (same slot ExecuteImpl
    /// will emit this frame). Returns nullptr if not yet compiled.
    void* MapCurrentForWrite(uint32_t frameIndex) const;
    void  UnmapCurrentForWrite() const {}  // host-coherent; no explicit flush needed

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
