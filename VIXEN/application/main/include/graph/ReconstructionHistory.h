#pragma once

#include "Core/RenderGraph.h"
#include "Nodes/SdiStageSynthesis.h"
#include "ShaderFamily.h"
#include "Data/ReconstructionJitter.h"
#include <functional>

namespace Vixen::RenderGraph {

CONSTEXPR_NODE_CONFIG(ReconstructionHistoryPorts, 0, 10, SlotArrayMode::Single) {
    OUTPUT_SLOT(HISTORY_A, Vixen::Vulkan::Resources::IRenderTarget*, 0,
                SlotNullability::Required, SlotMutability::WriteOnly);
    OUTPUT_SLOT(HISTORY_B, Vixen::Vulkan::Resources::IRenderTarget*, 1,
                SlotNullability::Required, SlotMutability::WriteOnly);
    OUTPUT_SLOT(HISTORY_A_VIEW, VkImageView, 2, SlotNullability::Required, SlotMutability::WriteOnly);
    OUTPUT_SLOT(HISTORY_B_VIEW, VkImageView, 3, SlotNullability::Required, SlotMutability::WriteOnly);
    OUTPUT_SLOT(RESET, uint32_t, 4, SlotNullability::Required, SlotMutability::WriteOnly);
    OUTPUT_SLOT(VALID, uint32_t, 5, SlotNullability::Required, SlotMutability::WriteOnly);
    OUTPUT_SLOT(FRAME_INDEX, uint32_t, 6, SlotNullability::Required, SlotMutability::WriteOnly);
    OUTPUT_SLOT(WRITE_INDEX, uint32_t, 7, SlotNullability::Required, SlotMutability::WriteOnly);
    OUTPUT_SLOT(RESET_REASONS, uint32_t, 8, SlotNullability::Required, SlotMutability::WriteOnly);
    OUTPUT_SLOT(JITTER, glm::vec2, 9, SlotNullability::Required, SlotMutability::WriteOnly);
    ReconstructionHistoryPorts() {
        HandleDescriptor target{"IRenderTarget*"}, view{"VkImageView"}, scalar{"uint32_t"};
        INIT_OUTPUT_DESC(JITTER, "primary_jitter", ResourceLifetime::Transient, HandleDescriptor{"glm::vec2"});
        outputs[JITTER.index].visualRelaxationReason = "R494 temporal primary-ray sampling";
        INIT_OUTPUT_DESC(HISTORY_A, "history_a", ResourceLifetime::Persistent, target);
        INIT_OUTPUT_DESC(HISTORY_B, "history_b", ResourceLifetime::Persistent, target);
        INIT_OUTPUT_DESC(HISTORY_A_VIEW, "history_a_view", ResourceLifetime::Persistent, view);
        INIT_OUTPUT_DESC(HISTORY_B_VIEW, "history_b_view", ResourceLifetime::Persistent, view);
        INIT_OUTPUT_DESC(RESET, "reset", ResourceLifetime::Transient, scalar);
        INIT_OUTPUT_DESC(VALID, "valid", ResourceLifetime::Transient, scalar);
        INIT_OUTPUT_DESC(FRAME_INDEX, "frame_index", ResourceLifetime::Transient, scalar);
        INIT_OUTPUT_DESC(WRITE_INDEX, "write_index", ResourceLifetime::Transient, scalar);
        INIT_OUTPUT_DESC(RESET_REASONS, "reset_reasons", ResourceLifetime::Transient, scalar);
    }
};

struct ReconstructionHistoryParams {
    ConnectionBatch* batch = nullptr;
    SdiStageCommon common{};
    NodeHandle sourceTarget{};
    NodeHandle camera{};
    ReconstructionSamplingPath samplingPath = ReconstructionSamplingPath::CanonicalCapture;
    NodeHandle sceneRadiance{};
    NodeHandle sceneProducer{};
    std::function<std::shared_ptr<ShaderManagement::ShaderFamily>(const std::string&, const std::string&)>
        makeShaderFamily;
    std::function<void(NodeHandle, std::shared_ptr<ShaderManagement::ShaderFamily>)> registerShaderFamily;
    std::function<void(std::string, NodeHandle)> registerPassMetric;
};

// One group per viewport. The optional scene-radiance seed writes WRITE_INDEX;
// its successful submission is acknowledged by the viewport owner. A temporal
// provider replaces that seed and acknowledgement with its own history write,
// reading the complement only when VALID is true and RESET is clear.
class ReconstructionHistoryModule final : public SubGraphType<ReconstructionHistoryModule> {
public:
    using PortConfig = ReconstructionHistoryPorts;
    using Params = ReconstructionHistoryParams;
    void Build(GraphScope& scope, const Params& params);
};

} // namespace Vixen::RenderGraph
