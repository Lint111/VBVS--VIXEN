#pragma once
#include "Core/RenderGraph.h"
#include "Nodes/StorageBufferNode.h"
#include "Nodes/PickIdTargetNode.h"
#include "Data/Nodes/PrevCameraConfigNodeConfig.h"

namespace Vixen::RenderGraph {

CONSTEXPR_NODE_CONFIG(ReconstructionSamplingPorts, 1, 2, SlotArrayMode::Single) {
    INPUT_SLOT(JITTER, glm::vec2, 0, SlotNullability::Required, SlotRole::Execute,
        SlotMutability::ReadOnly, SlotScope::NodeLevel);
    OUTPUT_SLOT(VISUAL_HITS, VkBuffer, 0, SlotNullability::Required, SlotMutability::WriteOnly);
    OUTPUT_SLOT(NATIVE_PICKING, VkImageView, 1, SlotNullability::Required, SlotMutability::WriteOnly);
    ReconstructionSamplingPorts() {
        INIT_INPUT_DESC(JITTER, "primary_jitter", ResourceLifetime::Transient, HandleDescriptor{"glm::vec2"});
        inputs[JITTER.index].acceptsVisualRelaxation = true;
        INIT_OUTPUT_DESC(VISUAL_HITS, "jittered_visibility", ResourceLifetime::Persistent, HandleDescriptor{"VkBuffer"});
        outputs[VISUAL_HITS.index].visualRelaxationReason = "R494: subpixel primary-ray sampling for visual reconstruction";
        INIT_OUTPUT_DESC(NATIVE_PICKING, "native_unjittered_picking", ResourceLifetime::Persistent, HandleDescriptor{"VkImageView"});
    }
};

struct ReconstructionSamplingParams {
    NodeHandle hitRecords, picking, previousCamera;
};

// The two outputs have different contracts: visibility samples are relaxed;
// picking traces unjittered rays at the native extent and native LOD footprint.
class ReconstructionSamplingModule final : public SubGraphType<ReconstructionSamplingModule> {
public:
    using PortConfig = ReconstructionSamplingPorts;
    using Params = ReconstructionSamplingParams;
    void Build(GraphScope& scope, const Params& params) {
        scope.BindInput(PortConfig::JITTER, params.previousCamera, PrevCameraConfigNodeConfig::PRIMARY_JITTER);
        scope.BindOutput(PortConfig::VISUAL_HITS, params.hitRecords, StorageBufferNodeConfig::STORAGE_BUFFER);
        scope.BindOutput(PortConfig::NATIVE_PICKING, params.picking, PickIdTargetNodeConfig::ID_IMAGE_VIEW);
    }
};
}
