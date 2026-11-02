#pragma once
// Explicit what/where/why/fix text for failed capture-readback Vulkan calls (T-2983). CPU-only.

#include <vulkan/vulkan.h>

#include <string>

namespace Vixen::RenderGraph::Debug {

/// Name + cause + fix for a failed readback Vulkan call, so a capture log says which call failed with
/// which VkResult and whether the device is gone (a lost device invalidates every later capture in the
/// process; rerun only the failed capture in a fresh process, never recapture an expected oracle).
inline std::string DescribeReadbackFailure(const char* where, const char* call, VkResult result) {
    const char* name = "unrecognised VkResult";
    const char* why = "see the Vulkan specification for this VkResult";
    const char* fix = "rerun this capture in a fresh process";
    switch (result) {
        case VK_ERROR_DEVICE_LOST:
            name = "VK_ERROR_DEVICE_LOST";
            why = "the GPU device was lost (on WSL/Dozen: D3D12 device removal); the device is unusable";
            fix = "do not continue in this process; rerun only this capture, the comparison and the witness in fresh processes at one queue credit";
            break;
        case VK_ERROR_OUT_OF_HOST_MEMORY:
            name = "VK_ERROR_OUT_OF_HOST_MEMORY";
            why = "the host could not map or allocate the readback buffer";
            fix = "free host memory or lower queue credits, then rerun in a fresh process";
            break;
        case VK_ERROR_OUT_OF_DEVICE_MEMORY:
            name = "VK_ERROR_OUT_OF_DEVICE_MEMORY";
            why = "the device ran out of memory for the readback";
            fix = "lower queue credits or capture resolution, then rerun in a fresh process";
            break;
        case VK_ERROR_MEMORY_MAP_FAILED:
            name = "VK_ERROR_MEMORY_MAP_FAILED";
            why = "the readback allocation could not be mapped into host address space";
            fix = "rerun in a fresh process; if it repeats, check host memory pressure";
            break;
        default:
            break;
    }
    return std::string("RenderTargetReadback: ") + call + " failed in " + where + " (" + name + " = " +
           std::to_string(static_cast<int>(result)) + "): " + why + "; fix: " + fix;
}

}  // namespace Vixen::RenderGraph::Debug
