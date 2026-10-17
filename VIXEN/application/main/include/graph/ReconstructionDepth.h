#pragma once

#include "Core/RenderGraph.h"
#include "Connection/Modifiers/FieldExtractionModifier.h"
#include "Data/CameraData.h"
#include "Data/ReconstructionDepth.h"
#include "Data/Nodes/CameraNodeConfig.h"
#include "Nodes/BufferSyncGathererNode.h"
#include "Nodes/ComputeStageNode.h"
#include "Data/Nodes/ComputeDispatchNodeConfig.h"
#include "Nodes/ConstantNode.h"
#include "Nodes/ConstantNodeType.h"
#include "Nodes/ImageSyncGathererNode.h"
#include "Nodes/RenderTargetNode.h"
#include "Nodes/SdiStageSynthesis.h"
#include "Nodes/ShaderLibraryNode.h"
#include "Nodes/StorageBufferNode.h"
#include "ShaderFamily.h"
#include "merged/ReconstructionDepth-SDI.g.h"

#include <array>
#include <functional>

namespace Vixen::RenderGraph {

CONSTEXPR_NODE_CONFIG(ReconstructionDepthPorts, 0, 5, SlotArrayMode::Single) {
    OUTPUT_SLOT(VIEW_DEPTH, Vixen::Vulkan::Resources::IRenderTarget*, 0,
                SlotNullability::Required, SlotMutability::WriteOnly);
    OUTPUT_SLOT(NORMALIZED_DEPTH, Vixen::Vulkan::Resources::IRenderTarget*, 1,
                SlotNullability::Required, SlotMutability::WriteOnly);
    OUTPUT_SLOT(VIEW_DEPTH_VIEW, VkImageView, 2,
                SlotNullability::Required, SlotMutability::WriteOnly);
    OUTPUT_SLOT(NORMALIZED_DEPTH_VIEW, VkImageView, 3,
                SlotNullability::Required, SlotMutability::WriteOnly);
    OUTPUT_SLOT(RENDER_COMPLETE, VkSemaphore, 4,
                SlotNullability::Required, SlotMutability::WriteOnly);

    ReconstructionDepthPorts() {
        HandleDescriptor target{"IRenderTarget*"};
        HandleDescriptor view{"VkImageView"};
        HandleDescriptor semaphore{"VkSemaphore"};
        INIT_OUTPUT_DESC(VIEW_DEPTH, "view_depth", ResourceLifetime::Persistent, target);
        INIT_OUTPUT_DESC(NORMALIZED_DEPTH, "normalized_depth", ResourceLifetime::Persistent, target);
        INIT_OUTPUT_DESC(VIEW_DEPTH_VIEW, "view_depth_view", ResourceLifetime::Persistent, view);
        INIT_OUTPUT_DESC(NORMALIZED_DEPTH_VIEW, "normalized_depth_view", ResourceLifetime::Persistent, view);
        INIT_OUTPUT_DESC(RENDER_COMPLETE, "render_complete", ResourceLifetime::Transient, semaphore);
    }
};

struct ReconstructionDepthParams {
    ConnectionBatch* batch = nullptr;
    SdiStageCommon common{};
    NodeHandle sourceTarget{};
    NodeHandle camera{};
    NodeHandle hitRecords{};
    NodeHandle hitProducer{};
    ReconstructionDepthConvention convention = ReconstructionDepthConvention::ForwardZeroToOne;
    std::function<std::shared_ptr<ShaderManagement::ShaderFamily>(const std::string&, const std::string&)>
        makeShaderFamily;
    std::function<void(NodeHandle, std::shared_ptr<ShaderManagement::ShaderFamily>)> registerShaderFamily;
    std::function<void(std::string, NodeHandle)> registerPassMetric;
};

// Each expansion owns its internal-resolution images. It neither samples color nor
// changes primary rays, so exposing these guides does not relax the scene output.
class ReconstructionDepthModule final : public SubGraphType<ReconstructionDepthModule> {
public:
    using PortConfig = ReconstructionDepthPorts;
    using Params = ReconstructionDepthParams;

    void Build(GraphScope& scope, const Params& params) {
        if (!params.batch || !params.makeShaderFamily || !params.registerShaderFamily ||
            !params.sourceTarget.IsValid() || !params.camera.IsValid() || !params.hitRecords.IsValid() ||
            !params.hitProducer.IsValid()) {
            throw std::invalid_argument("Reconstruction depth module is missing its frame-input producers");
        }
        auto& batch = *params.batch;
        const auto view = scope.AddNode<RenderTargetNodeType>("view_depth");
        const auto normalized = scope.AddNode<RenderTargetNodeType>("normalized_depth");
        const auto shader = scope.AddNode<ShaderLibraryNodeType>("depth_shader");
        const auto stage = scope.AddNode<ComputeStageNodeType>("depth");
        const auto encoding = scope.AddNode<ConstantNodeType>("depth_encoding");
        const auto reads = scope.AddNode<BufferSyncGathererNodeType>("depth_reads");
        const auto writes = scope.AddNode<ImageSyncGathererNodeType>("depth_writes");
        scope.GetInstance<ConstantNode>(encoding)->SetValue(ReconstructionDepthEncoding(params.convention));
        scope.GetInstance<BufferSyncGathererNode>(reads)->PreRegisterBufferSlots(1);
        scope.GetInstance<ImageSyncGathererNode>(writes)->PreRegisterImageSlots(2);

        for (auto target : {view, normalized}) {
            scope.GetInstance<RenderTargetNode>(target)->SetParameter(
                RenderTargetNodeConfig::PARAM_FORMAT, static_cast<uint32_t>(VK_FORMAT_R32_SFLOAT));
            batch.Connect(params.common.device, DeviceNodeConfig::VULKAN_DEVICE_OUT,
                          target, RenderTargetNodeConfig::VULKAN_DEVICE_IN)
                 .Connect(params.sourceTarget, RenderTargetNodeConfig::RENDER_TARGET,
                          target, RenderTargetNodeConfig::EXTENT_SOURCE);
        }
        params.registerShaderFamily(shader, params.makeShaderFamily("ReconstructionDepth.comp", "ReconstructionDepth"));
        SdiProviderRegistry providers;
        providers.Provide("HitRecordBuffer", params.hitRecords, StorageBufferNodeConfig::STORAGE_BUFFER,
                          SlotRole::Dependency | SlotRole::Execute);
        providers.Provide("viewDepthImage", view, RenderTargetNodeConfig::CURRENT_VIEW, SlotRole::Execute);
        providers.Provide("normalizedDepthImage", normalized, RenderTargetNodeConfig::CURRENT_VIEW, SlotRole::Execute);
        providers.Provide("depthEncoding", encoding, ConstantNodeConfig::OUTPUT, SlotRole::Dependency);
        const auto cameraField = [&](const char* name, auto member) {
            providers.ProvideCustom(name, [source = params.camera, member](ConnectionBatch& b, NodeHandle target, uint32_t slot) {
                b.Connect(source, CameraNodeConfig::CAMERA_DATA, target, slot,
                          ExtractField(member, SlotRole::Execute));
            });
        };
        cameraField("invProjection", &CameraData::invProjection);
        cameraField("cameraPos", &CameraData::cameraPos);
        cameraField("cameraDir", &CameraData::cameraDir);
        const auto synthesized = SynthesizeComputeStage<ShaderInterface::ReconstructionDepth::Metadata,
                                                         ShaderInterface::ReconstructionDepth::MEMBERS>(
            &scope, batch, "depth", shader, stage, params.common, providers, {});
        (void)synthesized;
        scope.GetInstance<ComputeStageNode>(stage)->SetParameter(ComputeStageNodeConfig::PARAM_IS_CONSUMER, false);
        // Dispatch is derived live from the first write image, including after a resize.
        batch.Connect(params.hitRecords, StorageBufferNodeConfig::STORAGE_BUFFER, reads, 0,
                      SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute))
             .Connect(reads, BufferSyncGathererNodeConfig::BUFFER_ARRAY,
                      stage, ComputeStageNodeConfig::BUFFER_READ_ARRAY, SlotRoleModifier(SlotRole::Execute))
             .Connect(view, RenderTargetNodeConfig::RENDER_TARGET, writes, 0,
                      SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute))
             .Connect(normalized, RenderTargetNodeConfig::RENDER_TARGET, writes, 1,
                      SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute))
             .Connect(writes, ImageSyncGathererNodeConfig::IMAGE_ARRAY,
                      stage, ComputeStageNodeConfig::IMAGE_WRITE_ARRAY, SlotRoleModifier(SlotRole::Execute));
        batch.Connect(params.hitProducer, ComputeDispatchNodeConfig::RENDER_COMPLETE_SEMAPHORE,
                      stage, ComputeStageNodeConfig::ORDERING_WAIT_SEMAPHORE);
        scope.BindOutput(ReconstructionDepthPorts::VIEW_DEPTH, view, RenderTargetNodeConfig::RENDER_TARGET);
        scope.BindOutput(ReconstructionDepthPorts::NORMALIZED_DEPTH, normalized, RenderTargetNodeConfig::RENDER_TARGET);
        scope.BindOutput(ReconstructionDepthPorts::VIEW_DEPTH_VIEW, view, RenderTargetNodeConfig::CURRENT_VIEW);
        scope.BindOutput(ReconstructionDepthPorts::NORMALIZED_DEPTH_VIEW, normalized, RenderTargetNodeConfig::CURRENT_VIEW);
        scope.BindOutput(ReconstructionDepthPorts::RENDER_COMPLETE, stage, ComputeStageNodeConfig::RENDER_COMPLETE_SEMAPHORE);
        if (params.registerPassMetric) params.registerPassMetric("reconstruction_depth", stage);
    }
};

} // namespace Vixen::RenderGraph
