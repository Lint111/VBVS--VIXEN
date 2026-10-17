#pragma once

#include "Data/ReconstructionDepth.h"
#include "graph/UpscalerContract.h"
#include "Nodes/ComputeStageNode.h"

#include <functional>

namespace Vixen::RenderGraph {

struct Fsr3UpscalerHooks {
    std::function<UpscalerProviderInitializationResult(VulkanDevice*)> initialize;
    ComputeStageNode::ExternalRecorder record;
    std::function<void()> cleanup;
};

Fsr3UpscalerHooks CreateFsr3UpscalerHooks(
    VkExtent2D renderExtent,
    VkExtent2D outputExtent,
    ReconstructionDepthConvention depthConvention,
    float nearPlane,
    float farPlane,
    std::function<void(UpscalerProviderInitializationResult)> reportRecordFailure = {});

} // namespace Vixen::RenderGraph
