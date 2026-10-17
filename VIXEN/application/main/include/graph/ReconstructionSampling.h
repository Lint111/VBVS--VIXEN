#pragma once
#include "Core/RenderGraph.h"
#include "Nodes/StorageBufferNode.h"
#include "Data/Nodes/PrevCameraConfigNodeConfig.h"

namespace Vixen::RenderGraph {

CONSTEXPR_NODE_CONFIG(ReconstructionSamplingPorts, 1, 1, SlotArrayMode::Single) {
    INPUT_SLOT(JITTER, glm::vec2, 0, SlotNullability::Required, SlotRole::Execute,
        SlotMutability::ReadOnly, SlotScope::NodeLevel);
    OUTPUT_SLOT(VISUAL_HITS, VkBuffer, 0, SlotNullability::Required, SlotMutability::WriteOnly);
    ReconstructionSamplingPorts() {
        INIT_INPUT_DESC(JITTER, "primary_jitter", ResourceLifetime::Transient, HandleDescriptor{"glm::vec2"});
        inputs[JITTER.index].acceptsVisualRelaxation = true;
        INIT_OUTPUT_DESC(VISUAL_HITS, "jittered_visibility", ResourceLifetime::Persistent, HandleDescriptor{"VkBuffer"});
        outputs[VISUAL_HITS.index].visualRelaxationReason = "R494: subpixel primary-ray sampling for visual reconstruction";
    }
};

struct ReconstructionSamplingParams {
    NodeHandle hitRecords, previousCamera;
};

// Visibility samples are relaxed (subpixel jitter). Picking reads the same hit records the
// frame's own trace writes; PickIdTargetNode's ID image is not a module output.
class ReconstructionSamplingModule final : public SubGraphType<ReconstructionSamplingModule> {
public:
    using PortConfig = ReconstructionSamplingPorts;
    using Params = ReconstructionSamplingParams;
    void Build(GraphScope& scope, const Params& params) {
        scope.BindInput(PortConfig::JITTER, params.previousCamera, PrevCameraConfigNodeConfig::PRIMARY_JITTER);
        scope.BindOutput(PortConfig::VISUAL_HITS, params.hitRecords, StorageBufferNodeConfig::STORAGE_BUFFER);
    }
};
}
