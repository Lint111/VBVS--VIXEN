#pragma once

// R516 part 2 (gpuharden): typed GPU faults and watchdog-bounded host waits.
//
// Runtime code never throws (R213). A submit or wait that fails is classified here, reported to the
// graph (RenderGraph::ReportGpuFault), and the frame returns a typed VkResult instead of unwinding.
// Host waits are bounded under the platform watchdog so a wedged GPU surfaces as a fault rather than
// an infinite block: Windows TDR fires near 2 s, and Dozen (D3D12) reports removal through the next
// wait, so a wait that outlives kDefaultWaitBoundNs means the device is not making progress.

#include <cstdint>
#include <cstdlib>

#include <vulkan/vulkan.h>

namespace Vixen::RenderGraph {

enum class GpuFaultKind : uint8_t {
    None,             ///< VK_SUCCESS (or a non-fault status such as VK_NOT_READY).
    DeviceLost,       ///< VK_ERROR_DEVICE_LOST: recover by recreating the device.
    WatchdogTimeout,  ///< A bounded host wait expired: the GPU is not making progress; treated as loss.
    OutOfMemory,      ///< VK_ERROR_OUT_OF_{DEVICE,HOST}_MEMORY: not recoverable by recreating the device.
    Other,            ///< Any other failing VkResult: the frame fails with that result.
};

[[nodiscard]] constexpr GpuFaultKind ClassifyGpuResult(VkResult result, bool fromBoundedWait = false) {
    switch (result) {
        case VK_SUCCESS:
        case VK_NOT_READY:
        case VK_SUBOPTIMAL_KHR:
            return GpuFaultKind::None;
        case VK_TIMEOUT:
            return fromBoundedWait ? GpuFaultKind::WatchdogTimeout : GpuFaultKind::None;
        case VK_ERROR_DEVICE_LOST:
            return GpuFaultKind::DeviceLost;
        case VK_ERROR_OUT_OF_DEVICE_MEMORY:
        case VK_ERROR_OUT_OF_HOST_MEMORY:
            return GpuFaultKind::OutOfMemory;
        default:
            return GpuFaultKind::Other;
    }
}

[[nodiscard]] constexpr const char* GpuFaultKindName(GpuFaultKind kind) {
    switch (kind) {
        case GpuFaultKind::None: return "none";
        case GpuFaultKind::DeviceLost: return "device-lost";
        case GpuFaultKind::WatchdogTimeout: return "watchdog-timeout";
        case GpuFaultKind::OutOfMemory: return "out-of-memory";
        case GpuFaultKind::Other: return "other";
    }
    return "other";
}

/// Default bound on a host wait: well above Windows TDR (~2 s) and a heavily contended Dozen frame,
/// far below "forever". Override with VIXEN_GPU_WAIT_BOUND_MS (positive integer milliseconds).
inline constexpr uint64_t kDefaultWaitBoundNs = 20'000'000'000ull;

[[nodiscard]] inline uint64_t GpuWaitBoundNs() {
    static const uint64_t bound = []() -> uint64_t {
        if (const char* env = std::getenv("VIXEN_GPU_WAIT_BOUND_MS")) {
            char* end = nullptr;
            const unsigned long long ms = std::strtoull(env, &end, 10);
            if (end != env && *end == '\0' && ms > 0 && ms < 3'600'000ull) return ms * 1'000'000ull;
        }
        return kDefaultWaitBoundNs;
    }();
    return bound;
}

/// Result of a bounded wait: a Vulkan status plus its classification.
struct GpuWaitResult {
    VkResult result = VK_SUCCESS;
    GpuFaultKind fault = GpuFaultKind::None;
    [[nodiscard]] bool Ok() const { return fault == GpuFaultKind::None; }
};

[[nodiscard]] constexpr GpuWaitResult MakeWaitResult(VkResult result) {
    return {result, ClassifyGpuResult(result, true)};
}

/// vkWaitSemaphores bounded by GpuWaitBoundNs(). A timeout is reported as WatchdogTimeout.
[[nodiscard]] inline GpuWaitResult BoundedWaitSemaphore(
    VkDevice device, VkSemaphore semaphore, uint64_t value, uint64_t boundNs = GpuWaitBoundNs()) {
    VkSemaphoreWaitInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
    info.semaphoreCount = 1;
    info.pSemaphores = &semaphore;
    info.pValues = &value;
    return MakeWaitResult(vkWaitSemaphores(device, &info, boundNs));
}

/// vkWaitForFences bounded by GpuWaitBoundNs(). A timeout is reported as WatchdogTimeout.
[[nodiscard]] inline GpuWaitResult BoundedWaitFence(
    VkDevice device, VkFence fence, uint64_t boundNs = GpuWaitBoundNs()) {
    return MakeWaitResult(vkWaitForFences(device, 1, &fence, VK_TRUE, boundNs));
}

}  // namespace Vixen::RenderGraph
