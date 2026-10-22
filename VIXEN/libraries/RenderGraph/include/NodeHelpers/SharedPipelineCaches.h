#pragma once

#include "NodeHelpers/CacherHelpers.h"
#include "PipelineCacher.h"

namespace RenderGraph::NodeHelpers {

/// The device-wide VkPipelineCache that PipelineCacher seeds from, and persists to, the
/// device/driver-keyed shared store. A node that builds its own graphics or ray-tracing pipeline
/// passes it to vkCreate*Pipelines, so that pipeline is reused across runs and build trees like the
/// cacher's own. VK_NULL_HANDLE only if the driver cannot create a cache.
inline VkPipelineCache SharedGraphicsPipelineCache(auto* graph, auto device) {
    return RegisterCacherIfNeeded<
        CashSystem::PipelineCacher,
        CashSystem::PipelineWrapper,
        CashSystem::PipelineCreateParams
    >(graph, device, "Pipeline")->SharedPipelineCache();
}

} // namespace RenderGraph::NodeHelpers
