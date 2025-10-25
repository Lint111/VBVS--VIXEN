#pragma once

#include "Headers.h"

namespace Vixen::RenderGraph {

/**
 * @brief Composite handle pairing VkDevice with its source VkPhysicalDevice
 *
 * This struct ensures that a logical device and its physical device are always
 * kept together, preventing mismatches. Provides implicit conversions for
 * ergonomic usage in APIs expecting either handle type.
 */
struct DeviceHandles {
    VkDevice device = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;

    // Default constructor
    DeviceHandles() = default;

    // Constructor
    DeviceHandles(VkDevice dev, VkPhysicalDevice physDev)
        : device(dev), physicalDevice(physDev) {}

    // Implicit conversion to VkDevice
    operator VkDevice() const { return device; }

    // Implicit conversion to VkPhysicalDevice
    operator VkPhysicalDevice() const { return physicalDevice; }

    // Explicit getters for clarity when needed
    VkDevice GetDevice() const { return device; }
    VkPhysicalDevice GetPhysicalDevice() const { return physicalDevice; }

    // Validation helpers
    bool IsValid() const {
        return device != VK_NULL_HANDLE && physicalDevice != VK_NULL_HANDLE;
    }

    bool operator==(const DeviceHandles& other) const {
        return device == other.device && physicalDevice == other.physicalDevice;
    }

    bool operator!=(const DeviceHandles& other) const {
        return !(*this == other);
    }
};

} // namespace Vixen::RenderGraph
