#pragma once

/**
 * @file VulkanForwardDeclarations.h
 * @brief Vulkan handle forward declarations for header optimization
 *
 * Include this header instead of <vulkan/vulkan.h> in headers that only need
 * Vulkan handle types (VkBuffer, VkImage, etc.) as opaque pointers.
 *
 * Safe to include alongside vulkan.h - uses guards to avoid redefinition.
 *
 * Full vulkan.h should be included in:
 * - .cpp files that call Vulkan API functions
 * - Headers that need Vulkan enums (VkFormat, VkDescriptorType, etc.)
 * - Headers that need Vulkan structs (VkExtent2D, VkRect2D, etc.)
 *
 * Usage:
 *   // In header file (MyClass.h):
 *   #include <Core/VulkanForwardDeclarations.h>
 *   class MyClass {
 *       VkBuffer m_buffer;  // OK - just storing handle
 *   };
 *
 *   // In source file (MyClass.cpp):
 *   #include <vulkan/vulkan.h>  // Full definitions for API calls
 *   void MyClass::Create(VkDevice device) {
 *       vkCreateBuffer(device, ...);  // Needs full vulkan.h
 *   }
 */

// If vulkan.h or vulkan_core.h is already included, skip forward declarations
#if defined(VULKAN_H_) || defined(VULKAN_CORE_H_)
    // Vulkan headers already included - nothing to do
#else

// Include cstdint for uint64_t
#include <cstdint>

// ============================================================================
// HANDLE DEFINITION MACROS
// ============================================================================
// These match the Vulkan SDK definitions exactly

#if !defined(VK_DEFINE_HANDLE)
#define VK_DEFINE_HANDLE(object) typedef struct object##_T* object;
#define VIXEN_DEFINED_VK_DEFINE_HANDLE
#endif

#if !defined(VK_DEFINE_NON_DISPATCHABLE_HANDLE)
    // On 64-bit, non-dispatchable handles are also pointers
    // On 32-bit, they're uint64_t (to maintain 64-bit size)
    #if defined(__LP64__) || defined(_WIN64) || (defined(__x86_64__) && !defined(__ILP32__)) || defined(_M_X64) || defined(__ia64) || defined(_M_IA64) || defined(__aarch64__) || defined(__powerpc64__)
        #define VK_DEFINE_NON_DISPATCHABLE_HANDLE(object) typedef struct object##_T *object;
    #else
        #define VK_DEFINE_NON_DISPATCHABLE_HANDLE(object) typedef uint64_t object;
    #endif
    #define VIXEN_DEFINED_VK_DEFINE_NON_DISPATCHABLE_HANDLE
#endif

// ============================================================================
// DISPATCHABLE HANDLES (Pointer to opaque struct)
// ============================================================================

#if !defined(VkInstance)
VK_DEFINE_HANDLE(VkInstance)
#endif
#if !defined(VkPhysicalDevice)
VK_DEFINE_HANDLE(VkPhysicalDevice)
#endif
#if !defined(VkDevice)
VK_DEFINE_HANDLE(VkDevice)
#endif
#if !defined(VkQueue)
VK_DEFINE_HANDLE(VkQueue)
#endif
#if !defined(VkCommandBuffer)
VK_DEFINE_HANDLE(VkCommandBuffer)
#endif

// ============================================================================
// NON-DISPATCHABLE HANDLES (64-bit opaque handles)
// ============================================================================

#if !defined(VkBuffer)
VK_DEFINE_NON_DISPATCHABLE_HANDLE(VkBuffer)
#endif
#if !defined(VkImage)
VK_DEFINE_NON_DISPATCHABLE_HANDLE(VkImage)
#endif
#if !defined(VkImageView)
VK_DEFINE_NON_DISPATCHABLE_HANDLE(VkImageView)
#endif
#if !defined(VkBufferView)
VK_DEFINE_NON_DISPATCHABLE_HANDLE(VkBufferView)
#endif
#if !defined(VkSampler)
VK_DEFINE_NON_DISPATCHABLE_HANDLE(VkSampler)
#endif
#if !defined(VkDeviceMemory)
VK_DEFINE_NON_DISPATCHABLE_HANDLE(VkDeviceMemory)
#endif
#if !defined(VkSemaphore)
VK_DEFINE_NON_DISPATCHABLE_HANDLE(VkSemaphore)
#endif
#if !defined(VkFence)
VK_DEFINE_NON_DISPATCHABLE_HANDLE(VkFence)
#endif
#if !defined(VkEvent)
VK_DEFINE_NON_DISPATCHABLE_HANDLE(VkEvent)
#endif
#if !defined(VkQueryPool)
VK_DEFINE_NON_DISPATCHABLE_HANDLE(VkQueryPool)
#endif
#if !defined(VkFramebuffer)
VK_DEFINE_NON_DISPATCHABLE_HANDLE(VkFramebuffer)
#endif
#if !defined(VkRenderPass)
VK_DEFINE_NON_DISPATCHABLE_HANDLE(VkRenderPass)
#endif
#if !defined(VkPipelineCache)
VK_DEFINE_NON_DISPATCHABLE_HANDLE(VkPipelineCache)
#endif
#if !defined(VkPipeline)
VK_DEFINE_NON_DISPATCHABLE_HANDLE(VkPipeline)
#endif
#if !defined(VkPipelineLayout)
VK_DEFINE_NON_DISPATCHABLE_HANDLE(VkPipelineLayout)
#endif
#if !defined(VkDescriptorSetLayout)
VK_DEFINE_NON_DISPATCHABLE_HANDLE(VkDescriptorSetLayout)
#endif
#if !defined(VkDescriptorPool)
VK_DEFINE_NON_DISPATCHABLE_HANDLE(VkDescriptorPool)
#endif
#if !defined(VkDescriptorSet)
VK_DEFINE_NON_DISPATCHABLE_HANDLE(VkDescriptorSet)
#endif
#if !defined(VkShaderModule)
VK_DEFINE_NON_DISPATCHABLE_HANDLE(VkShaderModule)
#endif
#if !defined(VkCommandPool)
VK_DEFINE_NON_DISPATCHABLE_HANDLE(VkCommandPool)
#endif

// ============================================================================
// EXTENSION HANDLES (KHR/EXT extensions)
// ============================================================================

#if !defined(VkSurfaceKHR)
VK_DEFINE_NON_DISPATCHABLE_HANDLE(VkSurfaceKHR)
#endif
#if !defined(VkSwapchainKHR)
VK_DEFINE_NON_DISPATCHABLE_HANDLE(VkSwapchainKHR)
#endif
#if !defined(VkAccelerationStructureKHR)
VK_DEFINE_NON_DISPATCHABLE_HANDLE(VkAccelerationStructureKHR)
#endif
#if !defined(VkDebugReportCallbackEXT)
VK_DEFINE_NON_DISPATCHABLE_HANDLE(VkDebugReportCallbackEXT)
#endif
#if !defined(VkDebugUtilsMessengerEXT)
VK_DEFINE_NON_DISPATCHABLE_HANDLE(VkDebugUtilsMessengerEXT)
#endif

// ============================================================================
// NULL HANDLE CONSTANT
// ============================================================================

#if !defined(VK_NULL_HANDLE)
    #if defined(__LP64__) || defined(_WIN64) || (defined(__x86_64__) && !defined(__ILP32__)) || defined(_M_X64) || defined(__ia64) || defined(_M_IA64) || defined(__aarch64__) || defined(__powerpc64__)
        #define VK_NULL_HANDLE nullptr
    #else
        #define VK_NULL_HANDLE 0ULL
    #endif
#endif

// ============================================================================
// COMMON VULKAN TYPEDEFS (Compatible with vulkan.h definitions)
// ============================================================================
// These use 'using' but are compatible with vulkan.h's typedef

#if !defined(VkDeviceSize)
using VkDeviceSize = uint64_t;
#endif
#if !defined(VkDeviceAddress)
using VkDeviceAddress = uint64_t;
#endif
#if !defined(VkFlags)
using VkFlags = uint32_t;
#endif
#if !defined(VkFlags64)
using VkFlags64 = uint64_t;
#endif
#if !defined(VkBool32)
using VkBool32 = uint32_t;
#endif

#endif // VULKAN_H_ || VULKAN_CORE_H_

// ============================================================================
// COMMON FORWARD DECLARATIONS FOR PROJECT TYPES
// ============================================================================
// These are always provided regardless of whether vulkan.h is included

namespace Vixen::Vulkan::Resources {
    class VulkanDevice;
    class VulkanInstance;
    class VulkanSwapChain;
}

namespace ShaderManagement {
    struct ShaderDataBundle;
    struct CompiledProgram;
}
