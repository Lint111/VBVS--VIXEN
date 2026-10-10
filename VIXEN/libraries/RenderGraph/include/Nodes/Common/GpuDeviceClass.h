#pragma once
#include <vulkan/vulkan.h>

namespace Vixen::RenderGraph {
// R503: the `[GPUYIELD]` line names the device class so the timing gate asserts
// its budget only on native devices (integrated/discrete) and checks
// correctness only on translated or CPU devices (Dozen).
inline const char* GpuDeviceClassName(VkPhysicalDeviceType type) {
    switch (type) {
        case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: return "integrated";
        case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:   return "discrete";
        case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:    return "virtual";
        case VK_PHYSICAL_DEVICE_TYPE_CPU:            return "cpu";
        default:                                     return "other";
    }
}
}
