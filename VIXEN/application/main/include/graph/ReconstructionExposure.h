#pragma once

#include "Core/RenderGraph.h"
#include "Nodes/BufferSyncGathererNode.h"
#include "Nodes/ComputeStageNode.h"
#include "Nodes/RenderTargetNode.h"
#include "Nodes/SdiStageSynthesis.h"
#include "Nodes/ShaderLibraryNode.h"
#include "Nodes/StorageBufferNode.h"
#include "ShaderFamily.h"
#include "merged/ReconstructionExposure-SDI.g.h"

#include <functional>

namespace Vixen::RenderGraph {

CONSTEXPR_NODE_CONFIG(ReconstructionExposurePorts, 0, 3, SlotArrayMode::Single) {
    OUTPUT_SLOT(EXPOSURE, Vixen::Vulkan::Resources::IRenderTarget*, 0,
                SlotNullability::Required, SlotMutability::WriteOnly);
    OUTPUT_SLOT(EXPOSURE_VIEW, VkImageView, 1,
                SlotNullability::Required, SlotMutability::WriteOnly);
    OUTPUT_SLOT(RENDER_COMPLETE, VkSemaphore, 2,
                SlotNullability::Required, SlotMutability::WriteOnly);
    ReconstructionExposurePorts() {
        HandleDescriptor target{"IRenderTarget*"}, view{"VkImageView"}, semaphore{"VkSemaphore"};
        INIT_OUTPUT_DESC(EXPOSURE, "exposure", ResourceLifetime::Persistent, target);
        INIT_OUTPUT_DESC(EXPOSURE_VIEW, "exposure_view", ResourceLifetime::Persistent, view);
        INIT_OUTPUT_DESC(RENDER_COMPLETE, "render_complete", ResourceLifetime::Transient, semaphore);
    }
};

struct ReconstructionExposureParams {
    ConnectionBatch* batch = nullptr;
    SdiStageCommon common{};
    SdiProviderRegistry providers;
    NodeHandle meterBuffer{};
    NodeHandle meterProducer{};
    std::function<std::shared_ptr<ShaderManagement::ShaderFamily>(const std::string&, const std::string&)>
        makeShaderFamily;
    std::function<void(NodeHandle, std::shared_ptr<ShaderManagement::ShaderFamily>)> registerShaderFamily;
    std::function<void(std::string, NodeHandle)> registerPassMetric;
};

class ReconstructionExposureModule final : public SubGraphType<ReconstructionExposureModule> {
public:
    using PortConfig = ReconstructionExposurePorts;
    using Params = ReconstructionExposureParams;
    void Build(GraphScope& scope, const Params& params) {
        if (!params.batch || !params.makeShaderFamily || !params.registerShaderFamily ||
            !params.meterBuffer.IsValid() || !params.meterProducer.IsValid()) {
            throw std::invalid_argument("Reconstruction exposure requires a whole-frame exposure meter");
        }
        auto& batch = *params.batch;
        const auto target = scope.AddNode<RenderTargetNodeType>("exposure");
        const auto shader = scope.AddNode<ShaderLibraryNodeType>("exposure_shader");
        const auto stage = scope.AddNode<ComputeStageNodeType>("exposure_input");
        const auto meter = scope.AddNode<BufferSyncGathererNodeType>("exposure_meter_sync");
        scope.GetInstance<BufferSyncGathererNode>(meter)->PreRegisterBufferSlots(1);
        auto* image = scope.GetInstance<RenderTargetNode>(target);
        image->SetParameter(RenderTargetNodeConfig::PARAM_FORMAT, static_cast<uint32_t>(VK_FORMAT_R32_SFLOAT));
        image->SetParameter(RenderTargetNodeConfig::PARAM_WIDTH, 1u);
        image->SetParameter(RenderTargetNodeConfig::PARAM_HEIGHT, 1u);
        batch.Connect(params.common.device, DeviceNodeConfig::VULKAN_DEVICE_OUT,
                      target, RenderTargetNodeConfig::VULKAN_DEVICE_IN);
        params.registerShaderFamily(shader, params.makeShaderFamily("ReconstructionExposure.comp", "ReconstructionExposure"));
        auto providers = params.providers;
        providers.Provide("exposureImage", target, RenderTargetNodeConfig::CURRENT_VIEW, SlotRole::Execute);
        const auto synthesized = SynthesizeComputeStage<ShaderInterface::ReconstructionExposure::Metadata,
                                                         ShaderInterface::ReconstructionExposure::MEMBERS>(
            &scope, batch, "exposure_input", shader, stage, params.common, providers, {});
        (void)synthesized;
        auto* dispatch = scope.GetInstance<ComputeStageNode>(stage);
        dispatch->SetParameter(ComputeStageNodeConfig::PARAM_IS_CONSUMER, false);
        dispatch->SetParameter(ComputeStageNodeConfig::PARAM_DISPATCH_X, 1u);
        dispatch->SetParameter(ComputeStageNodeConfig::PARAM_DISPATCH_Y, 1u);
        dispatch->SetParameter(ComputeStageNodeConfig::PARAM_DISPATCH_Z, 1u);
        // Describe the real meter write/read hazard, rather than introducing an ordering hint.
        batch.Connect(params.meterBuffer, StorageBufferNodeConfig::STORAGE_BUFFER, meter, 0,
                      SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute))
             .Connect(meter, BufferSyncGathererNodeConfig::BUFFER_ARRAY,
                      params.meterProducer, ComputeStageNodeConfig::BUFFER_WRITE_ARRAY,
                      SlotRoleModifier(SlotRole::Execute))
             .Connect(meter, BufferSyncGathererNodeConfig::BUFFER_ARRAY,
                      stage, ComputeStageNodeConfig::BUFFER_READ_ARRAY, SlotRoleModifier(SlotRole::Execute))
             .Connect(target, RenderTargetNodeConfig::RENDER_TARGET,
                      stage, ComputeStageNodeConfig::IMAGE_WRITE, SlotRoleModifier(SlotRole::Execute));
        scope.BindOutput(ReconstructionExposurePorts::EXPOSURE, target, RenderTargetNodeConfig::RENDER_TARGET);
        scope.BindOutput(ReconstructionExposurePorts::EXPOSURE_VIEW, target, RenderTargetNodeConfig::CURRENT_VIEW);
        scope.BindOutput(ReconstructionExposurePorts::RENDER_COMPLETE, stage, ComputeStageNodeConfig::RENDER_COMPLETE_SEMAPHORE);
        if (params.registerPassMetric) params.registerPassMetric("reconstruction_exposure", stage);
    }
};

} // namespace Vixen::RenderGraph
