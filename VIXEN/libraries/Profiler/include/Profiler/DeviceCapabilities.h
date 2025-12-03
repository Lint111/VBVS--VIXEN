#pragma once

#include <vulkan/vulkan.h>
#include <string>
#include <cstdint>

namespace Vixen::Profiler {

/// GPU device information captured once per test suite
/// Provides context for interpreting benchmark results
struct DeviceCapabilities {
    // Device identification
    std::string deviceName;
    std::string driverVersion;
    std::string vulkanVersion;
    uint32_t vendorID = 0;
    uint32_t deviceID = 0;
    VkPhysicalDeviceType deviceType = VK_PHYSICAL_DEVICE_TYPE_OTHER;

    // Memory properties
    uint64_t totalVRAM_MB = 0;
    uint64_t maxAllocationSize_MB = 0;
    uint32_t memoryHeapCount = 0;

    // Compute capabilities
    uint32_t maxComputeWorkGroupCount[3] = {0, 0, 0};
    uint32_t maxComputeWorkGroupSize[3] = {0, 0, 0};
    uint32_t maxComputeWorkGroupInvocations = 0;
    uint32_t maxComputeSharedMemorySize = 0;

    // Timestamp support
    bool timestampSupported = false;
    float timestampPeriod = 0.0f;  // nanoseconds per tick

    // Extension support
    bool performanceQuerySupported = false;
    bool memoryBudgetSupported = false;

    /// Capture device capabilities from physical device
    static DeviceCapabilities Capture(VkPhysicalDevice physicalDevice);

    /// Format driver version for display (vendor-specific)
    static std::string FormatDriverVersion(uint32_t driverVersion, uint32_t vendorID);

    /// Get human-readable device type string
    std::string GetDeviceTypeString() const;

    /// Generate summary string for CSV header
    std::string GetSummaryString() const;
};

} // namespace Vixen::Profiler
