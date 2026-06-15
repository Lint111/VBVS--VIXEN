#pragma once

#include "Core/TypedNodeInstance.h"
#include "Core/NodeType.h"
#include "Core/NodeLogging.h"
#include "Data/Nodes/PickingNodeConfig.h"
#include <memory>

namespace Vixen::RenderGraph {

/**
 * @brief Node type for CPU click-picking (AR#35) — node type ID 125.
 */
class PickingNodeType : public TypedNodeType<PickingNodeConfig> {
public:
    PickingNodeType(const std::string& typeName = "Picking")
        : TypedNodeType<PickingNodeConfig>(typeName) {}
    ~PickingNodeType() override = default;

    std::unique_ptr<NodeInstance> CreateInstance(
        const std::string& instanceName
    ) const override;
};

/**
 * @brief CPU click-picking node.
 *
 * On a left-mouse-button PRESS edge it unprojects the cursor into a world-space
 * ray (ComputePickRay), ray-marches the CPU voxel world (GaiaVoxelWorld) to the
 * first solid voxel, logs the result and publishes a PickResultEvent on the
 * message bus. Marching only happens on the click edge, so per-frame cost is a
 * couple of pointer reads and an edge comparison.
 *
 * Pure-CPU: it touches no Vulkan resources. It reads InputState, CameraData and
 * the voxel world pointer, all produced upstream in the graph.
 */
class PickingNode : public TypedNode<PickingNodeConfig> {
public:
    PickingNode(
        const std::string& instanceName,
        NodeType* nodeType
    );
    ~PickingNode() override = default;

protected:
    void SetupImpl(TypedSetupContext& ctx) override;
    void ExecuteImpl(TypedExecuteContext& ctx) override;
    void CleanupImpl(TypedCleanupContext& ctx) override;

private:
    // Edge detection for the left mouse button (fire on the down-edge only).
    bool lastLeftDown_ = false;

    // Status mirrored to the LAST_PICK_HIT output (1 = last pick hit a voxel).
    uint32_t lastPickHit_ = 0;

    // Ray-march tuning. The voxel grid is integer-unit (1 world unit == 1 voxel;
    // GaiaVoxelWorld snaps positions via floor()), so a sub-voxel step cannot skip
    // a voxel even for grazing rays. maxDist comfortably spans a large (e.g. 128^3)
    // world plus the camera's orbit distance.
    static constexpr float kMarchStep = 0.25f;     // world units per step (< 1 voxel)
    static constexpr float kMaxDistance = 512.0f;  // world units to march before giving up
};

} // namespace Vixen::RenderGraph
