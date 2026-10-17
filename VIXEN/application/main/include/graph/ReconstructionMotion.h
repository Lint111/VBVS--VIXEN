#pragma once

#include "Core/RenderGraph.h"
#include "Nodes/ComputeDispatchNode.h"
#include "Nodes/ImageSyncGathererNode.h"
#include "Nodes/RenderTargetNode.h"
#include "Nodes/DeviceNode.h"

namespace Vixen::RenderGraph {

CONSTEXPR_NODE_CONFIG(ReconstructionMotionPorts, 0, 4, SlotArrayMode::Single) {
    OUTPUT_SLOT(MOTION, Vixen::Vulkan::Resources::IRenderTarget*, 0, SlotNullability::Required, SlotMutability::WriteOnly);
    OUTPUT_SLOT(VALIDITY, Vixen::Vulkan::Resources::IRenderTarget*, 1, SlotNullability::Required, SlotMutability::WriteOnly);
    OUTPUT_SLOT(MOTION_VIEW, VkImageView, 2, SlotNullability::Required, SlotMutability::WriteOnly);
    OUTPUT_SLOT(VALIDITY_VIEW, VkImageView, 3, SlotNullability::Required, SlotMutability::WriteOnly);
    ReconstructionMotionPorts() {
        HandleDescriptor target{"IRenderTarget*"}, view{"VkImageView"};
        INIT_OUTPUT_DESC(MOTION, "motion", ResourceLifetime::Persistent, target);
        INIT_OUTPUT_DESC(VALIDITY, "validity", ResourceLifetime::Persistent, target);
        INIT_OUTPUT_DESC(MOTION_VIEW, "motion_view", ResourceLifetime::Persistent, view);
        INIT_OUTPUT_DESC(VALIDITY_VIEW, "validity_view", ResourceLifetime::Persistent, view);
    }
};

struct ReconstructionMotionParams {
    ConnectionBatch* batch = nullptr;
    NodeHandle device, sourceTarget, primaryMarch;
};

// The raymarcher writes these at its exact internal extent, including sky
// misses. A zero validity value forbids using the vector as correspondence.
class ReconstructionMotionModule final : public SubGraphType<ReconstructionMotionModule> {
public:
    using PortConfig = ReconstructionMotionPorts;
    using Params = ReconstructionMotionParams;
    void Build(GraphScope& scope, const Params& params) {
        auto& batch = *params.batch;
        const auto motion = scope.AddNode<RenderTargetNodeType>("motion");
        const auto validity = scope.AddNode<RenderTargetNodeType>("validity");
        const auto writes = scope.AddNode<ImageSyncGathererNodeType>("writes");
        scope.GetInstance<ImageSyncGathererNode>(writes)->PreRegisterImageSlots(2);
        uint32_t index = 0;
        for (auto target : {motion, validity}) {
            scope.GetInstance<RenderTargetNode>(target)->SetParameter(RenderTargetNodeConfig::PARAM_FORMAT,
                static_cast<uint32_t>(index == 0 ? VK_FORMAT_R32G32_SFLOAT : VK_FORMAT_R32_SFLOAT));
            batch.Connect(params.device, DeviceNodeConfig::VULKAN_DEVICE_OUT,
                          target, RenderTargetNodeConfig::VULKAN_DEVICE_IN)
                 .Connect(params.sourceTarget, RenderTargetNodeConfig::RENDER_TARGET,
                          target, RenderTargetNodeConfig::EXTENT_SOURCE)
                 .Connect(target, RenderTargetNodeConfig::RENDER_TARGET, writes, index++,
                          SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));
        }
        batch.Connect(writes, ImageSyncGathererNodeConfig::IMAGE_ARRAY,
                      params.primaryMarch, ComputeDispatchNodeConfig::IMAGE_WRITE_ARRAY,
                      SlotRoleModifier(SlotRole::Execute));
        scope.BindOutput(ReconstructionMotionPorts::MOTION, motion, RenderTargetNodeConfig::RENDER_TARGET);
        scope.BindOutput(ReconstructionMotionPorts::VALIDITY, validity, RenderTargetNodeConfig::RENDER_TARGET);
        scope.BindOutput(ReconstructionMotionPorts::MOTION_VIEW, motion, RenderTargetNodeConfig::CURRENT_VIEW);
        scope.BindOutput(ReconstructionMotionPorts::VALIDITY_VIEW, validity, RenderTargetNodeConfig::CURRENT_VIEW);
    }
};

} // namespace Vixen::RenderGraph
