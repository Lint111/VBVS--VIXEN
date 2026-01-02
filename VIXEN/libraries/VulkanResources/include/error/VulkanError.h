#pragma once

#include <cassert>
#include <cstdio>
#include <expected>
#include <string>
#include <vulkan/vulkan.h>

/**
 * @brief Vulkan error information
 *
 * Contains the VkResult error code and a human-readable message
 * describing what operation failed.
 */
struct VulkanError {
    VkResult code;
    std::string message;

    /**
     * @brief Convert error to string representation
     * @return Formatted error string with code and message
     */
    std::string toString() const;

    /**
     * @brief Get human-readable name for VkResult code
     * @param result Vulkan result code
     * @return String representation of the error code
     */
    static std::string resultToString(VkResult result);
};

/**
 * @brief Result type for Vulkan operations that return a value
 * @tparam T The success value type
 *
 * Usage:
 * @code
 * VulkanResult<VkDevice> createDevice() {
 *     VkDevice device;
 *     VkResult result = vkCreateDevice(..., &device);
 *     if (result != VK_SUCCESS) {
 *         return std::unexpected(VulkanError{result, "Failed to create device"});
 *     }
 *     return device;
 * }
 * @endcode
 */
template<typename T>
using VulkanResult = std::expected<T, VulkanError>;

/**
 * @brief Status type for Vulkan operations that don't return a value
 *
 * Represents the success or failure status of an operation.
 * Can be in one of two states:
 * - Success: return {}
 * - Failure: return std::unexpected(VulkanError{...})
 *
 * Usage:
 * @code
 * VulkanStatus destroyDevice(VkDevice device) {
 *     if (!device) {
 *         return std::unexpected(VulkanError{VK_ERROR_INITIALIZATION_FAILED, "Invalid device"});
 *     }
 *     vkDestroyDevice(device, nullptr);
 *     return {};  // Success
 * }
 * @endcode
 */
using VulkanStatus = std::expected<void, VulkanError>;

/**
 * @brief Helper macro to check Vulkan result and return error if failed
 *
 * Usage:
 * @code
 * VulkanResult<VkDevice> createDevice() {
 *     VkDevice device;
 *     VK_CHECK(vkCreateDevice(..., &device), "Failed to create device");
 *     return device;
 * }
 * @endcode
 */
#define VK_CHECK(expr, msg) \
    do { \
        VkResult result = (expr); \
        if (result != VK_SUCCESS) { \
            return std::unexpected(VulkanError{result, msg}); \
        } \
    } while(0)

/**
 * @brief Helper macro to check Vulkan result with custom error message formatting
 *
 * Usage:
 * @code
 * VK_CHECK_FMT(vkCreateDevice(...), "Failed to create device for GPU: {}", gpuName);
 * @endcode
 */
#define VK_CHECK_FMT(expr, fmt, ...) \
    do { \
        VkResult result = (expr); \
        if (result != VK_SUCCESS) { \
            char buffer[512]; \
            snprintf(buffer, sizeof(buffer), fmt, __VA_ARGS__); \
            return std::unexpected(VulkanError{result, buffer}); \
        } \
    } while(0)

/**
 * @brief Helper function to propagate errors from nested calls
 *
 * Usage:
 * @code
 * VulkanStatus initialize() {
 *     auto device = createDevice();
 *     VK_PROPAGATE_ERROR(device);  // Return error if device creation failed
 *
 *     // Continue with device.value()...
 *     return {};
 * }
 * @endcode
 */
#define VK_PROPAGATE_ERROR(result) \
    do { \
        if (!(result)) { \
            return std::unexpected((result).error()); \
        } \
    } while(0)

/**
 * @brief Log-only variant for functions that don't return VulkanResult
 *
 * Logs error to stderr and asserts in debug builds, but does NOT
 * change control flow. Use VK_CHECK when possible; this is for
 * legacy code that can't easily be refactored.
 *
 * Usage:
 * @code
 * void legacyFunction() {
 *     VK_CHECK_LOG(vkCreateBuffer(...), "Failed to create buffer");
 *     // Continues even on error - caller must handle null/invalid state
 * }
 * @endcode
 */
#define VK_CHECK_LOG(expr, msg) \
    do { \
        VkResult _vk_result = (expr); \
        if (_vk_result != VK_SUCCESS) { \
            fprintf(stderr, "[VK_ERROR] %s: %s (VkResult: %d) at %s:%d\n", \
                    msg, VulkanError::resultToString(_vk_result).c_str(), \
                    static_cast<int>(_vk_result), __FILE__, __LINE__); \
            assert(false && "Vulkan call failed - see stderr for details"); \
        } \
    } while(0)

/**
 * @brief Variant that stores result for conditional handling
 *
 * Usage:
 * @code
 * VkResult result;
 * VK_CHECK_RESULT(result, vkCreateBuffer(...), "Buffer creation");
 * if (result != VK_SUCCESS) {
 *     // Handle error case
 * }
 * @endcode
 */
#define VK_CHECK_RESULT(outResult, expr, msg) \
    do { \
        outResult = (expr); \
        if (outResult != VK_SUCCESS) { \
            fprintf(stderr, "[VK_ERROR] %s: %s (VkResult: %d) at %s:%d\n", \
                    msg, VulkanError::resultToString(outResult).c_str(), \
                    static_cast<int>(outResult), __FILE__, __LINE__); \
        } \
    } while(0)
