// Copyright (C) 2025 Lior Yanai (eLiorg). Licensed under the MIT License.
#pragma once
#include "Core/TypedNodeInstance.h"
#include "Core/NodeType.h"
#include "Core/PerFrameResources.h"
#include "Data/Nodes/LightTreeBufferNodeConfig.h"
#include "LightTree.h"
#include <memory>
#include <mutex>
#include <vector>

namespace Vixen::RenderGraph {

/**
 * @brief Node type for LightTreeBufferNode.
 */
class LightTreeBufferNodeType : public TypedNodeType<LightTreeBufferNodeConfig> {
public:
    LightTreeBufferNodeType(const std::string& typeName = "LightTreeBuffer")
        : TypedNodeType<LightTreeBufferNodeConfig>(typeName) {}
    virtual ~LightTreeBufferNodeType() = default;

    std::unique_ptr<NodeInstance> CreateInstance(const std::string& instanceName) const override;
};

/**
 * @brief Uploads the CPU-computed mip-cut light-tree (Vixen::SVO::LightTree.h's
 * BuildLightTreeCut output) into a ring-buffered SSBO (Sampled Lighting Inc3 M4).
 *
 * Host->node seam mirrors BodyOctreeSceneNode::SetInstances: SetLightTreeCut stashes
 * the cut; ExecuteImpl uploads it into the current ring slot every frame (bounded by
 * kMaxLightTreeNodes=64 -- a cut that exceeds the cap is TRUNCATED + logged, not
 * silently overrun, since LightTreeBuffer's array is fixed-capacity).
 *
 * SCOPE NOTE (M4): LightTreeNode::worldPos (from BuildLightTreeCut) is in the SAME
 * grid/local space SdfBake.h bakes into ([0,n)^3), NOT the world space DirectLighting.
 * comp shades in (p_world = p_base*instance.renderScale + instance.worldPos -- see
 * TraceWorld.glsl). The caller (test/gate scene setup) is responsible for pre-
 * transforming a cut's worldPos fields into WORLD space before calling
 * SetLightTreeCut when the target scene uses a non-identity instance transform
 * (renderScale != 1 or worldPos != 0) -- this milestone does not do that transform
 * for the caller (would require threading instance transform data through this node,
 * out of scope for the single-instance gate scene M4 targets). A future milestone
 * generalizing to multi-instance emissive content must add that transform here.
 *
 * Lifecycle: ring buffers persist across graph recompile; released on FinalTeardown --
 * identical KI-004-safe pattern to ReservoirConfigNode/ShadowConfigNode.
 */
class LightTreeBufferNode : public TypedNode<LightTreeBufferNodeConfig> {
public:
    using Base = TypedNode<LightTreeBufferNodeConfig>;

    LightTreeBufferNode(const std::string& instanceName, NodeType* nodeType);
    ~LightTreeBufferNode() override = default;

    /**
     * @brief Push a new light-tree cut (host -> node seam). Uploaded on the next
     * Execute; thread-safe against concurrent ExecuteImpl reads via cutMutex_.
     */
    void SetLightTreeCut(std::vector<Vixen::SVO::LightTreeNode> cut);

protected:
    void SetupImpl(TypedSetupContext&    ctx) override;
    void CompileImpl(TypedCompileContext& ctx) override;
    void ExecuteImpl(TypedExecuteContext& ctx) override;
    void CleanupImpl(TypedCleanupContext& ctx) override;

private:
    static const uint32_t kRingSize;  // = FrameSyncNodeConfig::MAX_FRAMES_IN_FLIGHT

    PerFrameResources perFrame_;
    std::vector<Vixen::SVO::LightTreeNode> cut_;
    std::mutex cutMutex_;
};

} // namespace Vixen::RenderGraph
