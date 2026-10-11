#pragma once
#include <variant>
#include "VulkanDevice.h"

namespace Vixen::RenderGraph {
// R503: the `[GPUYIELD]` line names the device class so the timing gate asserts
// its budget only on native devices (integrated/discrete) and checks correctness
// only on translated or CPU devices. A translation layer (Mesa Dozen on D3D12,
// Venus, llvmpipe, SwiftShader) reports the underlying adapter's type, so the
// class is derived from the Vulkan driver id the capability graph observed.
inline const char* GpuDeviceClassName(VkPhysicalDeviceType type, uint32_t driverId) {
    switch (static_cast<VkDriverId>(driverId)) {
        case VK_DRIVER_ID_MESA_DOZEN:
        case VK_DRIVER_ID_MESA_VENUS:
        case VK_DRIVER_ID_MESA_LLVMPIPE:
        case VK_DRIVER_ID_GOOGLE_SWIFTSHADER:
            return "translated";
        default:
            break;
    }
    switch (type) {
        case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: return "integrated";
        case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:   return "discrete";
        case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:    return "virtual";
        case VK_PHYSICAL_DEVICE_TYPE_CPU:            return "cpu";
        default:                                     return "other";
    }
}

inline const char* GpuDeviceClassName(const Vixen::Vulkan::Resources::VulkanDevice& device) {
    uint32_t driverId = 0;
    if (const auto capability = device.GetCapabilityGraph().GetCapability(
            "BuildFact:ObservedDeviceDriverIdentity")) {
        if (const auto* fact = dynamic_cast<const Vixen::BuildFactCapability*>(capability.get())) {
            if (const auto* identity = std::get_if<Vixen::DeviceDriverIdentityBuildFact>(&fact->GetValue()))
                driverId = identity->driverId;
        }
    }
    return GpuDeviceClassName(device.gpuProperties.deviceType, driverId);
}
}
