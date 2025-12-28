#pragma once

/**
 * @file VulkanDeviceFwd.h
 * @brief Forward declaration for VulkanDevice class
 *
 * Include this header in files that only need VulkanDevice* pointers
 * without accessing VulkanDevice members.
 *
 * Full VulkanDevice.h should be included in:
 * - .cpp files that call VulkanDevice methods
 * - Headers that access VulkanDevice members
 *
 * Usage:
 *   // In header file (MyNodeConfig.h):
 *   #include "VulkanDeviceFwd.h"
 *   using VulkanDevice = Vixen::Vulkan::Resources::VulkanDevice;
 *   // Now VulkanDevice* can be used in slot types
 *
 *   // In source file (MyNode.cpp):
 *   #include "VulkanDevice.h"  // Full definition for method calls
 *   vulkanDevice->CreateDevice(...);
 */

namespace Vixen::Vulkan::Resources {
    class VulkanDevice;
}

// Also forward declare RTXCapabilities since it's part of VulkanDevice API
namespace Vixen::Vulkan::Resources {
    struct RTXCapabilities;
}
