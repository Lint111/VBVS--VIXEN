#include "graph/ReconstructionHistory.h"
#include "Nodes/CameraNode.h"
#include "Nodes/ReconstructionHistoryStateNode.h"
#include "Nodes/RenderTargetNode.h"
#include "Nodes/SceneRadianceNode.h"
#include "Data/Nodes/SceneRadianceNodeConfig.h"
#include "Nodes/ComputeStageNode.h"
#include "Nodes/ImageSyncGathererNode.h"
#include "Nodes/ShaderLibraryNode.h"
#include "merged/ReconstructionHistory-SDI.g.h"

namespace Vixen::RenderGraph {

void ReconstructionHistoryModule::Build(GraphScope& scope, const Params& params) {
    if (!params.batch || !params.sourceTarget.IsValid() || !params.camera.IsValid())
        throw std::invalid_argument("Reconstruction history requires its viewport's camera and targets");
    auto& batch = *params.batch;
    const auto first = scope.AddNode<RenderTargetNodeType>("history_a");
    const auto second = scope.AddNode<RenderTargetNodeType>("history_b");
    const auto state = scope.AddNode<ReconstructionHistoryStateNodeType>("state");
    scope.GetInstance<ReconstructionHistoryStateNode>(state)->SetSamplingPath(params.samplingPath);
    scope.BindOutput(ReconstructionHistoryPorts::JITTER, state, ReconstructionHistoryStateNodeConfig::JITTER);
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
    batch.Connect(params.camera, CameraNodeConfig::HISTORY_EPOCH,
                  state, ReconstructionHistoryStateNodeConfig::CAMERA_CUT_EPOCH,
                  SlotRoleModifier(SlotRole::Execute));
    batch.Connect(params.camera, CameraNodeConfig::CAMERA_DATA, state, ReconstructionHistoryStateNodeConfig::CAMERA,
                  SlotRoleModifier(SlotRole::Execute))
         .Connect(params.sourceTarget, RenderTargetNodeConfig::RENDER_TARGET,
                  state, ReconstructionHistoryStateNodeConfig::INTERNAL_TARGET)
         .ConnectOutput(params.common.presentationTarget, presentation.target,
                        state, ReconstructionHistoryStateNodeConfig::OUTPUT_TARGET);
    if (params.sceneRadiance.IsValid() && params.sceneProducer.IsValid()) {
        const auto shader = scope.AddNode<ShaderLibraryNodeType>("seed_shader");
        const auto stage = scope.AddNode<ComputeStageNodeType>("seed");
        const auto writes = scope.AddNode<ImageSyncGathererNodeType>("seed_writes");
        const auto reads = scope.AddNode<ImageSyncGathererNodeType>("seed_reads");
        scope.GetInstance<ImageSyncGathererNode>(writes)->PreRegisterImageSlots(2);
        // SpatialReuse writes the current scene-radiance target while its temporal input
        // remains the previous target. Gather both pair members on the existing producer
        // connection so reconstruction graphs retain one IMAGE_WRITE_ARRAY owner.
        scope.GetInstance<ImageSyncGathererNode>(reads)->PreRegisterImageSlots(2);
        params.registerShaderFamily(shader, params.makeShaderFamily("ReconstructionHistory.comp", "ReconstructionHistory"));
        SdiProviderRegistry providers;
        providers.Provide("sceneRadianceImage", params.sceneRadiance,
            SceneRadianceNodeConfig::HISTORY_IMAGE_VIEW, SlotRole::Execute);
        providers.Provide("historyA", first, RenderTargetNodeConfig::CURRENT_VIEW, SlotRole::Execute);
        providers.Provide("historyB", second, RenderTargetNodeConfig::CURRENT_VIEW, SlotRole::Execute);
        providers.Provide("writeIndex", state, ReconstructionHistoryStateNodeConfig::WRITE_INDEX, SlotRole::Execute);
        const auto synthesized = SynthesizeComputeStage<ShaderInterface::ReconstructionHistory::Metadata,
            ShaderInterface::ReconstructionHistory::MEMBERS>(
                &scope, batch, "seed", shader, stage, params.common, providers, {});
        (void)synthesized;
        scope.GetInstance<ComputeStageNode>(stage)->SetParameter(ComputeStageNodeConfig::PARAM_IS_CONSUMER, false);
        batch.Connect(first, RenderTargetNodeConfig::RENDER_TARGET, writes, 0,
                      SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute))
             .Connect(second, RenderTargetNodeConfig::RENDER_TARGET, writes, 1,
                      SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute))
             .Connect(writes, ImageSyncGathererNodeConfig::IMAGE_ARRAY,
                      stage, ComputeStageNodeConfig::IMAGE_WRITE_ARRAY, SlotRoleModifier(SlotRole::Execute))
             .Connect(params.sceneRadiance, SceneRadianceNodeConfig::HISTORY_TARGET, reads, 0,
                      SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute))
             .Connect(params.sceneRadiance, SceneRadianceNodeConfig::CURRENT_HISTORY_TARGET, reads, 1,
                      SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute))
             .Connect(reads, ImageSyncGathererNodeConfig::IMAGE_ARRAY,
                      params.sceneProducer, ComputeStageNodeConfig::IMAGE_WRITE_ARRAY, SlotRoleModifier(SlotRole::Execute))
             .Connect(reads, ImageSyncGathererNodeConfig::IMAGE_ARRAY,
                      stage, ComputeStageNodeConfig::IMAGE_READ_ARRAY, SlotRoleModifier(SlotRole::Execute));
        batch.Connect(params.sceneProducer, ComputeStageNodeConfig::RENDER_COMPLETE_SEMAPHORE,
                      stage, ComputeStageNodeConfig::ORDERING_WAIT_SEMAPHORE);
        if (params.registerPassMetric) params.registerPassMetric("reconstruction_history_seed", stage);
    }
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
