#pragma once
#include "Core/NodeType.h"
#include "Core/PerFrameResources.h"
#include "Core/TypedNodeInstance.h"
#include "Data/Nodes/MiningBeamBufferNodeConfig.h"
#include <cstdint>
#include <glm/glm.hpp>
#include <vector>

namespace Vixen::RenderGraph {

/** Host-authored beam endpoint pair, resolved against the current body instances. */
struct MiningBeamInput {
    uint32_t sourceInstanceIndex = 0;
    uint32_t targetInstanceIndex = 0;
    glm::vec3 sourceLocalOffset{0.5f};
    glm::vec3 targetLocalOffset{0.5f};
    float radius = 0.075f;             // world-space core radius
    float luminosity = 0.7f;           // additive core intensity
    float purposeScale = 0.12f;        // scales the modest mining-purpose halo
    glm::vec3 color{0.08f, 0.68f, 1.0f};
};

class MiningBeamBufferNodeType : public TypedNodeType<MiningBeamBufferNodeConfig> {
public:
    explicit MiningBeamBufferNodeType(const std::string& typeName = "MiningBeamBuffer")
        : TypedNodeType<MiningBeamBufferNodeConfig>(typeName) {}
    std::unique_ptr<NodeInstance> CreateInstance(const std::string& instanceName) const override;
};

/**
 * Per-frame upload node for the optional mining-beam list.
 *
 * SetBeams and SetEnabled are intended to run on the RenderGraph's owning
 * thread before RenderFrame. An empty list is represented by beamCount=0;
 * there is no sentinel record. The node starts disabled and empty.
 */
class MiningBeamBufferNode : public TypedNode<MiningBeamBufferNodeConfig> {
public:
    using Base = TypedNode<MiningBeamBufferNodeConfig>;

    MiningBeamBufferNode(const std::string& instanceName, NodeType* nodeType);
    ~MiningBeamBufferNode() override = default;

    void SetBeams(std::vector<MiningBeamInput> beams);
    void SetEnabled(bool enabled) { enabled_ = enabled; }
    bool IsEnabled() const { return enabled_; }
    size_t GetBeamCount() const { return beams_.size(); }

protected:
    void TypedSetupImpl(TypedSetupContext& ctx) override;
    void TypedCompileImpl(TypedCompileContext& ctx) override;
    void TypedExecuteImpl(TypedExecuteContext& ctx) override;
    void TypedCleanupImpl(TypedCleanupContext& ctx) override;

private:
    static const uint32_t kRingSize;

    PerFrameResources perFrame_;
    std::vector<MiningBeamInput> beams_;
    bool enabled_ = false;
};

} // namespace Vixen::RenderGraph
