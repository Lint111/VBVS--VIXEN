// Copyright (C) 2025 Lior Yanai (eLiorg). Licensed under the MIT License.
#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include "Core/FrameSyncSchedule.h"
#include "Data/PassStep.h"

namespace Vixen::RenderGraph {

/// Replay one group's baked barriers as a single vkCmdPipelineBarrier2.
/// Buffer/memory barriers only in P4 (GroupBarrier.isImage==false); image arm is a P5 no-op stub.
void ReplayGroupBarriers(VkCommandBuffer cmd, const std::vector<GroupBarrier>& barriers);

/// Record an ordered pass list into `cmd`, replaying schedule.groups[i].entryBarriers before pass i.
/// Baggage-free: depends only on Vulkan handles, the pass list, the baked schedule, and imageIndex.
/// Caller owns vkBeginCommandBuffer/vkEndCommandBuffer and the submit.
void RecordPassGroup(VkCommandBuffer cmd, const std::vector<PassStep>& passes,
                     const FrameSyncSchedule& schedule, uint32_t imageIndex);

} // namespace Vixen::RenderGraph
