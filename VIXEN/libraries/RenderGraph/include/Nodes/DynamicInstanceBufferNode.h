#pragma once
#include "Core/TypedNodeInstance.h"
#include "Core/NodeType.h"
#include "Core/PerFrameResources.h"
#include "Data/Nodes/DynamicInstanceBufferNodeConfig.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <cstdint>
#include <memory>

namespace Vixen::RenderGraph {

/**
 * @brief Node type for the per-frame animated per-instance model-matrix SSBO
 * Type ID: 124
 */
class DynamicInstanceBufferNodeType : public TypedNodeType<DynamicInstanceBufferNodeConfig> {
public:
    DynamicInstanceBufferNodeType(const std::string& typeName = "DynamicInstanceBuffer")
        : TypedNodeType<DynamicInstanceBufferNodeConfig>(typeName) {}
    virtual ~DynamicInstanceBufferNodeType() = default;

    std::unique_ptr<NodeInstance> CreateInstance(const std::string& instanceName) const override;
};

/**
 * @brief Animated sibling of InstanceBufferNode.
 *
 * Maintains a ring of host-visible/host-coherent storage buffers (one per
 * frame-in-flight, via PerFrameResources) holding N = gridDim^2 glm::mat4
 * per-instance model matrices. Each frame ExecuteImpl recomputes the transforms
 * (a planar grid of translations + a per-instance Y-axis rotation driven by an
 * internal frame counter), memcpy's them into the current ring buffer, and emits
 * THAT buffer on INSTANCE_BUFFER so the descriptor binds the just-written data.
 * No GPU readback; the ring prevents CPU/GPU races.
 *
 * FR-7 lifecycle: the ring buffers persist across graph recompile; released only
 * on FinalTeardown.
 */
class DynamicInstanceBufferNode : public TypedNode<DynamicInstanceBufferNodeConfig> {
public:
    using Base = TypedNode<DynamicInstanceBufferNodeConfig>;

    DynamicInstanceBufferNode(const std::string& instanceName, NodeType* nodeType);
    ~DynamicInstanceBufferNode() override = default;

protected:
    void SetupImpl(TypedSetupContext&    ctx) override;
    void CompileImpl(TypedCompileContext& ctx) override;
    void ExecuteImpl(TypedExecuteContext& ctx) override;
    void CleanupImpl(TypedCleanupContext& ctx) override;

private:
    // Number of ring buffers — one per frame-in-flight (matches the value
    // CURRENT_FRAME_INDEX cycles through). Defined in the .cpp from
    // FrameSyncNodeConfig::MAX_FRAMES_IN_FLIGHT.
    static const uint32_t kRingSize;

    PerFrameResources                       perFrame_;                 // ring of storage buffers
    uint32_t                                instanceCount_ = 0;
    uint32_t                                gridDim_       = 8;
    float                                   spacing_       = 2.0f;
    float                                   rotationSpeed_ = 0.01f;
    uint64_t                                frameCounter_  = 0;        // deterministic animation clock
};

} // namespace Vixen::RenderGraph
