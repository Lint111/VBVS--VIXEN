#include "Profiler/DeviceCapabilities.h"
#include <sstream>
#include <iomanip>
#include <vector>

namespace Vixen::Profiler {

DeviceCapabilities DeviceCapabilities::Capture(VkPhysicalDevice physicalDevice) {
    DeviceCapabilities caps;

    // Get device properties
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(physicalDevice, &props);

    caps.deviceName = props.deviceName;
    caps.vendorID = props.vendorID;
    caps.deviceID = props.deviceID;
    caps.deviceType = props.deviceType;
    caps.driverVersion = FormatDriverVersion(props.driverVersion, props.vendorID);

    // Vulkan API version
    uint32_t major = VK_VERSION_MAJOR(props.apiVersion);
    uint32_t minor = VK_VERSION_MINOR(props.apiVersion);
    uint32_t patch = VK_VERSION_PATCH(props.apiVersion);
    std::ostringstream vkVersion;
    vkVersion << major << "." << minor << "." << patch;
    caps.vulkanVersion = vkVersion.str();

    // Timestamp support
    caps.timestampSupported = props.limits.timestampComputeAndGraphics;
    caps.timestampPeriod = props.limits.timestampPeriod;

    // Compute capabilities
    caps.maxComputeWorkGroupCount[0] = props.limits.maxComputeWorkGroupCount[0];
    caps.maxComputeWorkGroupCount[1] = props.limits.maxComputeWorkGroupCount[1];
    caps.maxComputeWorkGroupCount[2] = props.limits.maxComputeWorkGroupCount[2];
    caps.maxComputeWorkGroupSize[0] = props.limits.maxComputeWorkGroupSize[0];
    caps.maxComputeWorkGroupSize[1] = props.limits.maxComputeWorkGroupSize[1];
    caps.maxComputeWorkGroupSize[2] = props.limits.maxComputeWorkGroupSize[2];
    caps.maxComputeWorkGroupInvocations = props.limits.maxComputeWorkGroupInvocations;
    caps.maxComputeSharedMemorySize = props.limits.maxComputeSharedMemorySize;

    // Memory properties
    VkPhysicalDeviceMemoryProperties memProps;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProps);

    caps.memoryHeapCount = memProps.memoryHeapCount;
    caps.totalVRAM_MB = 0;
    for (uint32_t i = 0; i < memProps.memoryHeapCount; ++i) {
        if (memProps.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) {
            caps.totalVRAM_MB += memProps.memoryHeaps[i].size / (1024 * 1024);
        }
    }
    caps.maxAllocationSize_MB = props.limits.maxMemoryAllocationCount;

    // Check extension support
    uint32_t extensionCount = 0;
    vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &extensionCount, nullptr);
    std::vector<VkExtensionProperties> extensions(extensionCount);
    vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &extensionCount, extensions.data());

    for (const auto& ext : extensions) {
        if (strcmp(ext.extensionName, VK_KHR_PERFORMANCE_QUERY_EXTENSION_NAME) == 0) {
            caps.performanceQuerySupported = true;
        }
        if (strcmp(ext.extensionName, VK_EXT_MEMORY_BUDGET_EXTENSION_NAME) == 0) {
            caps.memoryBudgetSupported = true;
        }
    }

    return caps;
}

std::string DeviceCapabilities::FormatDriverVersion(uint32_t driverVersion, uint32_t vendorID) {
    std::ostringstream oss;

    // NVIDIA uses custom encoding
    if (vendorID == 0x10DE) {
        uint32_t major = (driverVersion >> 22) & 0x3FF;
        uint32_t minor = (driverVersion >> 14) & 0xFF;
        uint32_t patch = (driverVersion >> 6) & 0xFF;
        oss << major << "." << minor << "." << patch;
    }
    // AMD uses Vulkan standard encoding
    else if (vendorID == 0x1002) {
        oss << VK_VERSION_MAJOR(driverVersion) << "."
            << VK_VERSION_MINOR(driverVersion) << "."
            << VK_VERSION_PATCH(driverVersion);
    }
    // Intel and others use Vulkan standard encoding
    else {
        oss << VK_VERSION_MAJOR(driverVersion) << "."
            << VK_VERSION_MINOR(driverVersion) << "."
            << VK_VERSION_PATCH(driverVersion);
    }

    return oss.str();
}

std::string DeviceCapabilities::GetDeviceTypeString() const {
    switch (deviceType) {
        case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: return "Integrated GPU";
        case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU: return "Discrete GPU";
        case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU: return "Virtual GPU";
        case VK_PHYSICAL_DEVICE_TYPE_CPU: return "CPU";
        default: return "Unknown";
    }
}

std::string DeviceCapabilities::GetSummaryString() const {
    std::ostringstream oss;
    oss << deviceName << " (" << GetDeviceTypeString() << ")"
        << " | Driver: " << driverVersion
        << " | Vulkan: " << vulkanVersion
        << " | VRAM: " << totalVRAM_MB << " MB"
        << " | Timestamp: " << (timestampSupported ? "Yes" : "No")
        << " | PerfQuery: " << (performanceQuerySupported ? "Yes" : "No");
    return oss.str();
}

} // namespace Vixen::Profiler
