#pragma once

#include "Data/ReconstructionDepth.h"
#include "Nodes/ComputeStageNode.h"

#include <functional>

namespace Vixen::RenderGraph {

struct Fsr3UpscalerHooks {
    std::function<void(VulkanDevice*)> initialize;
    ComputeStageNode::ExternalRecorder record;
    std::function<void()> cleanup;
};

Fsr3UpscalerHooks CreateFsr3UpscalerHooks(
    VkExtent2D renderExtent,
    VkExtent2D outputExtent,
    ReconstructionDepthConvention depthConvention,
    float nearPlane,
    float farPlane);

} // namespace Vixen::RenderGraph
