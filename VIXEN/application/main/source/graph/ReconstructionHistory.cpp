#include "graph/ReconstructionHistory.h"
#include "Nodes/CameraNode.h"
#include "Nodes/ReconstructionHistoryStateNode.h"
#include "Nodes/RenderTargetNode.h"

namespace Vixen::RenderGraph {

void ReconstructionHistoryModule::Build(GraphScope& scope, const Params& params) {
    if (!params.batch || !params.sourceTarget.IsValid() || !params.camera.IsValid())
        throw std::invalid_argument("Reconstruction history requires its viewport's camera and targets");
    auto& batch = *params.batch;
    const auto first = scope.AddNode<RenderTargetNodeType>("history_a");
    const auto second = scope.AddNode<RenderTargetNodeType>("history_b");
    const auto state = scope.AddNode<ReconstructionHistoryStateNodeType>("state");
    const auto presentation = batch.GetPresentationTargetContract(params.common.presentationTarget);
    for (auto target : {first, second}) {
        auto* image = scope.GetInstance<RenderTargetNode>(target);
        image->SetParameter(RenderTargetNodeConfig::PARAM_IMAGE_COUNT, 1u);
        image->SetParameter(RenderTargetNodeConfig::PARAM_FORMAT, static_cast<uint32_t>(VK_FORMAT_R16G16B16A16_SFLOAT));
        batch.Connect(params.common.device, DeviceNodeConfig::VULKAN_DEVICE_OUT,
                      target, RenderTargetNodeConfig::VULKAN_DEVICE_IN)
             .ConnectOutput(params.common.presentationTarget, presentation.target,
                            target, RenderTargetNodeConfig::EXTENT_SOURCE);
    }
    batch.Connect(params.camera, CameraNodeConfig::CAMERA_DATA, state, ReconstructionHistoryStateNodeConfig::CAMERA,
                  SlotRoleModifier(SlotRole::Execute))
         .Connect(params.sourceTarget, RenderTargetNodeConfig::RENDER_TARGET,
                  state, ReconstructionHistoryStateNodeConfig::INTERNAL_TARGET)
         .ConnectOutput(params.common.presentationTarget, presentation.target,
                        state, ReconstructionHistoryStateNodeConfig::OUTPUT_TARGET);
    scope.BindOutput(ReconstructionHistoryPorts::HISTORY_A, first, RenderTargetNodeConfig::RENDER_TARGET);
    scope.BindOutput(ReconstructionHistoryPorts::HISTORY_B, second, RenderTargetNodeConfig::RENDER_TARGET);
    scope.BindOutput(ReconstructionHistoryPorts::HISTORY_A_VIEW, first, RenderTargetNodeConfig::CURRENT_VIEW);
    scope.BindOutput(ReconstructionHistoryPorts::HISTORY_B_VIEW, second, RenderTargetNodeConfig::CURRENT_VIEW);
    scope.BindOutput(ReconstructionHistoryPorts::RESET, state, ReconstructionHistoryStateNodeConfig::RESET);
    scope.BindOutput(ReconstructionHistoryPorts::VALID, state, ReconstructionHistoryStateNodeConfig::VALID);
    scope.BindOutput(ReconstructionHistoryPorts::FRAME_INDEX, state, ReconstructionHistoryStateNodeConfig::FRAME_INDEX);
    scope.BindOutput(ReconstructionHistoryPorts::WRITE_INDEX, state, ReconstructionHistoryStateNodeConfig::WRITE_INDEX);
    scope.BindOutput(ReconstructionHistoryPorts::RESET_REASONS, state, ReconstructionHistoryStateNodeConfig::RESET_REASONS);
}

} // namespace Vixen::RenderGraph
