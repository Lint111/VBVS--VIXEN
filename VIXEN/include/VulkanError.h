#pragma once

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
 * @brief Result type for Vulkan operations that return void
 *
 * Usage:
 * @code
 * VulkanSuccess destroyDevice(VkDevice device) {
 *     if (!device) {
 *         return std::unexpected(VulkanError{VK_ERROR_INITIALIZATION_FAILED, "Invalid device"});
 *     }
 *     vkDestroyDevice(device, nullptr);
 *     return {};  // Success
 * }
 * @endcode
 */
using VulkanSuccess = std::expected<void, VulkanError>;

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
 * VulkanSuccess initialize() {
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
