#include "pch.h"
#include "TLASUpdateRequest.h"
#include "DynamicTLAS.h"
#include "VulkanDevice.h"

namespace CashSystem {

void TLASUpdateRequest::Record(VkCommandBuffer cmd) {
    if (!device || !tlas || !instanceManager) {
        return;
    }

    // Update instance buffer data
    tlas->UpdateInstances(imageIndex, *instanceManager);

    // Get build parameters (DynamicTLAS prepares data, we record commands)
    TLASBuildParams params = tlas->PrepareBuild(imageIndex, dirtyLevel);

    if (!params.shouldBuild) {
        return;  // No instances or allocation failed
    }

    // Fix up internal pointers after return-by-value
    // PrepareBuild() set pGeometries to &params.geometry, but that was in the
    // callee's stack frame. Re-establish the pointer to our local copy.
    params.geometry.geometry.instances = params.instancesData;
    params.buildInfo.pGeometries = &params.geometry;

    // Load vkCmdBuildAccelerationStructuresKHR from device
    auto vkCmdBuildAS = reinterpret_cast<PFN_vkCmdBuildAccelerationStructuresKHR>(
        vkGetDeviceProcAddr(device->device, "vkCmdBuildAccelerationStructuresKHR"));

    if (!vkCmdBuildAS) {
        return;  // RT extension not available
    }

    // Record build command
    const VkAccelerationStructureBuildRangeInfoKHR* pRangeInfo = &params.rangeInfo;
    vkCmdBuildAS(cmd, 1, &params.buildInfo, &pRangeInfo);

    // Mark frame as built
    tlas->MarkBuilt(imageIndex, params.rangeInfo.primitiveCount);
}

} // namespace CashSystem
