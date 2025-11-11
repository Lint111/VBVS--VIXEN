#pragma once

#include "RM.h"
#include "../../RenderGraph/include/Core/VulkanLimits.h"
#include "../../RenderGraph/include/Core/StackTracker.h"
#include <array>
#include <cstddef>
#include <string_view>

namespace ResourceManagement {

/**
 * @file StackAllocatedRM.h
 * @brief Stack-allocated resource wrapper with integrated tracking
 *
 * Extends RM<T> to support stack-allocated arrays with automatic
 * size tracking and StackTracker integration.
 *
 * Key Features:
 * - Fixed-capacity arrays on the stack (std::array)
 * - Automatic size tracking (count variable)
 * - Integrated with StackTracker for debug monitoring
 * - Compatible with RM<T> state management
 * - Vector-like API for easy migration
 * - Zero-overhead in release builds
 *
 * Use Cases:
 * - Per-frame temporary buffers
 * - Bounded Vulkan resource arrays
 * - Hot path optimizations
 *
 * Example:
 * @code
 * // Replace this:
 * RM<std::vector<VkImageView>> views;
 * views.Set(std::vector<VkImageView>{view1, view2});
 *
 * // With this:
 * StackAllocatedRM<VkImageView, MAX_SWAPCHAIN_IMAGES> views("swapchain:views");
 * views.Add(view1);
 * views.Add(view2);
 * @endcode
 */

/**
 * @brief Stack-allocated array with RM state management
 *
 * @tparam T Element type
 * @tparam N Maximum capacity (compile-time constant)
 */
template<typename T, size_t N>
class StackAllocatedRM {
public:
    /**
     * @brief Construct with optional debug name
     * @param debugName Name for StackTracker logging (debug builds only)
     */
    explicit StackAllocatedRM(std::string_view debugName = "unnamed")
        : debugName(debugName)
        , scopeTracker(debugName, sizeof(data))
    {
        state.MarkReady();
        // Set metadata for diagnostics
        state.SetMetadata("allocation_type", std::string("stack"));
        state.SetMetadata("capacity", N);
        state.SetMetadata("element_size", sizeof(T));
        state.SetMetadata("total_size", sizeof(data));
    }

    // ========================================================================
    // ARRAY ACCESS (Vector-like API)
    // ========================================================================

    /**
     * @brief Add element to array (no bounds check in release)
     */
    void Add(const T& value) {
        #ifndef NDEBUG
        if (count >= N) {
            throw std::runtime_error("StackAllocatedRM overflow: " + std::string(debugName));
        }
        #endif
        data[count++] = value;
        state.IncrementGeneration();
    }

    /**
     * @brief Add element to array (move version)
     */
    void Add(T&& value) {
        #ifndef NDEBUG
        if (count >= N) {
            throw std::runtime_error("StackAllocatedRM overflow: " + std::string(debugName));
        }
        #endif
        data[count++] = std::move(value);
        state.IncrementGeneration();
    }

    /**
     * @brief Access element by index (bounds-checked in debug)
     */
    T& operator[](size_t index) {
        #ifndef NDEBUG
        if (index >= count) {
            throw std::out_of_range("StackAllocatedRM index out of range");
        }
        #endif
        return data[index];
    }

    const T& operator[](size_t index) const {
        #ifndef NDEBUG
        if (index >= count) {
            throw std::out_of_range("StackAllocatedRM index out of range");
        }
        #endif
        return data[index];
    }

    /**
     * @brief Get current element count
     */
    size_t Size() const { return count; }

    /**
     * @brief Get maximum capacity
     */
    constexpr size_t Capacity() const { return N; }

    /**
     * @brief Check if array is empty
     */
    bool Empty() const { return count == 0; }

    /**
     * @brief Check if array is full
     */
    bool Full() const { return count >= N; }

    /**
     * @brief Clear all elements (doesn't deallocate)
     */
    void Clear() {
        count = 0;
        state.IncrementGeneration();
    }

    /**
     * @brief Get raw pointer to data (for Vulkan APIs)
     */
    T* Data() { return data.data(); }
    const T* Data() const { return data.data(); }

    /**
     * @brief Iterator support
     */
    T* begin() { return data.data(); }
    T* end() { return data.data() + count; }
    const T* begin() const { return data.data(); }
    const T* end() const { return data.data() + count; }

    // ========================================================================
    // RM STATE MANAGEMENT INTEGRATION
    // ========================================================================

    /**
     * @brief Get underlying RM state manager
     */
    RM<size_t>& GetState() { return state; }
    const RM<size_t>& GetState() const { return state; }

    /**
     * @brief Check if resource is ready
     */
    bool Ready() const { return state.Ready(); }

    /**
     * @brief Mark resource as outdated
     */
    void MarkOutdated() { state.MarkOutdated(); }

    /**
     * @brief Mark resource as ready
     */
    void MarkReady() { state.MarkReady(); }

    /**
     * @brief Lock resource (prevent modification)
     */
    void Lock() { state.Lock(); }

    /**
     * @brief Unlock resource
     */
    void Unlock() { state.Unlock(); }

    /**
     * @brief Get generation number (for cache invalidation)
     */
    uint64_t GetGeneration() const { return state.GetGeneration(); }

    // ========================================================================
    // DIAGNOSTICS
    // ========================================================================

    /**
     * @brief Get debug name
     */
    std::string_view GetDebugName() const { return debugName; }

    /**
     * @brief Get stack usage in bytes
     */
    size_t GetStackUsage() const { return sizeof(data); }

    /**
     * @brief Get utilization percentage (0-100)
     */
    double GetUtilization() const {
        return (static_cast<double>(count) / N) * 100.0;
    }

    /**
     * @brief Print statistics (debug builds only)
     */
    void PrintStats() const {
        #ifndef NDEBUG
        std::cout << "[StackAllocatedRM] " << debugName << "\n"
                  << "  Count:        " << count << " / " << N << "\n"
                  << "  Utilization:  " << GetUtilization() << "%\n"
                  << "  Stack usage:  " << GetStackUsage() << " bytes\n"
                  << "  Generation:   " << GetGeneration() << "\n"
                  << "  Ready:        " << (Ready() ? "Yes" : "No") << "\n";
        #endif
    }

private:
    std::array<T, N> data;                          // Stack-allocated array
    size_t count = 0;                               // Current element count
    std::string_view debugName;                     // Debug identifier
    RM<size_t> state;                              // State management

    // Stack tracking (zero overhead in release)
    Vixen::RenderGraph::ScopedStackAllocation scopeTracker;
};

// ============================================================================
// CONVENIENCE ALIASES FOR COMMON VULKAN TYPES
// ============================================================================

/**
 * @brief Stack-allocated image view array
 *
 * Usage:
 * @code
 * StackImageViewArray views("framebuffer:color");
 * views.Add(colorView);
 * vkCreateFramebuffer(..., views.Size(), views.Data());
 * @endcode
 */
using StackImageViewArray = StackAllocatedRM<
    VkImageView,
    Vixen::RenderGraph::MAX_SWAPCHAIN_IMAGES
>;

/**
 * @brief Stack-allocated descriptor write array
 */
using StackDescriptorWriteArray = StackAllocatedRM<
    VkWriteDescriptorSet,
    Vixen::RenderGraph::MAX_DESCRIPTOR_BINDINGS
>;

/**
 * @brief Stack-allocated descriptor image info array
 */
using StackDescriptorImageInfoArray = StackAllocatedRM<
    VkDescriptorImageInfo,
    Vixen::RenderGraph::MAX_DESCRIPTOR_BINDINGS
>;

/**
 * @brief Stack-allocated descriptor buffer info array
 */
using StackDescriptorBufferInfoArray = StackAllocatedRM<
    VkDescriptorBufferInfo,
    Vixen::RenderGraph::MAX_DESCRIPTOR_BINDINGS
>;

/**
 * @brief Stack-allocated shader stage array
 */
using StackShaderStageArray = StackAllocatedRM<
    VkPipelineShaderStageCreateInfo,
    Vixen::RenderGraph::MAX_SHADER_STAGES
>;

/**
 * @brief Stack-allocated push constant range array
 */
using StackPushConstantArray = StackAllocatedRM<
    VkPushConstantRange,
    Vixen::RenderGraph::MAX_PUSH_CONSTANT_RANGES
>;

/**
 * @brief Stack-allocated vertex attribute array
 */
using StackVertexAttributeArray = StackAllocatedRM<
    VkVertexInputAttributeDescription,
    Vixen::RenderGraph::MAX_VERTEX_ATTRIBUTES
>;

/**
 * @brief Stack-allocated vertex binding array
 */
using StackVertexBindingArray = StackAllocatedRM<
    VkVertexInputBindingDescription,
    Vixen::RenderGraph::MAX_VERTEX_BINDINGS
>;

/**
 * @brief Stack-allocated framebuffer attachment array
 */
using StackAttachmentArray = StackAllocatedRM<
    VkImageView,
    Vixen::RenderGraph::MAX_FRAMEBUFFER_ATTACHMENTS
>;

/**
 * @brief Stack-allocated command buffer array
 */
using StackCommandBufferArray = StackAllocatedRM<
    VkCommandBuffer,
    Vixen::RenderGraph::MAX_FRAMES_IN_FLIGHT
>;

// ============================================================================
// MIGRATION HELPERS
// ============================================================================

/**
 * @brief Helper to convert std::vector to StackAllocatedRM
 *
 * Use during migration to identify overflow cases.
 *
 * @code
 * std::vector<VkImageView> views = GetViews();
 * auto stackViews = ToStackAllocated<VkImageView, MAX_SWAPCHAIN_IMAGES>(
 *     views, "converted:views"
 * );
 * @endcode
 */
template<typename T, size_t N>
StackAllocatedRM<T, N> ToStackAllocated(
    const std::vector<T>& vec,
    std::string_view debugName = "converted"
) {
    StackAllocatedRM<T, N> result(debugName);

    #ifndef NDEBUG
    if (vec.size() > N) {
        std::cerr << "[WARNING] Vector overflow during conversion: "
                  << debugName << " (" << vec.size() << " > " << N << ")\n";
    }
    #endif

    size_t copyCount = std::min(vec.size(), N);
    for (size_t i = 0; i < copyCount; ++i) {
        result.Add(vec[i]);
    }

    return result;
}

/**
 * @brief Helper to populate StackAllocatedRM from initializer list
 *
 * @code
 * auto views = MakeStackAllocated<VkImageView, 4>(
 *     "my:views",
 *     {view1, view2, view3}
 * );
 * @endcode
 */
template<typename T, size_t N>
StackAllocatedRM<T, N> MakeStackAllocated(
    std::string_view debugName,
    std::initializer_list<T> values
) {
    StackAllocatedRM<T, N> result(debugName);

    #ifndef NDEBUG
    if (values.size() > N) {
        std::cerr << "[WARNING] Initializer list overflow: "
                  << debugName << " (" << values.size() << " > " << N << ")\n";
    }
    #endif

    for (const auto& value : values) {
        if (result.Full()) break;
        result.Add(value);
    }

    return result;
}

} // namespace ResourceManagement
