#pragma once

#include <vulkan/vulkan.h>
#include <cstdint>
#include <string>
#include <any>

namespace Vixen::RenderGraph::Debug {

/**
 * @brief Type identifier for debug buffer implementations
 */
enum class DebugBufferType {
    Unknown,
    RayTrace,       // Per-ray traversal data (variable length)
    ShaderCounters  // Atomic counter statistics (fixed 8 uints)
};

/**
 * @brief Abstract interface for GPU debug buffers
 *
 * This interface abstracts the buffer type, allowing DebugBufferReaderNode
 * to work with any debug buffer implementation without knowing the specifics.
 *
 * Implementations:
 * - RayTraceBuffer: Per-ray traversal data for debugging ray marching
 * - ShaderCountersBuffer: Atomic counters for performance metrics
 *
 * The interface provides:
 * - Type identification for polymorphic dispatch
 * - Generic read/reset operations
 * - Type-safe data access via GetTypedData<T>()
 *
 * Usage in DebugBufferReaderNode:
 * ```cpp
 * IDebugBuffer* buffer = capture->GetBuffer();
 * buffer->Reset(device);
 * // ... after dispatch ...
 * buffer->Read(device);
 *
 * if (buffer->GetType() == DebugBufferType::ShaderCounters) {
 *     auto counters = buffer->GetTypedData<ShaderCounters>();
 * }
 * ```
 */
class IDebugBuffer {
public:
    virtual ~IDebugBuffer() = default;

    // =========================================================================
    // Type identification
    // =========================================================================

    /**
     * @brief Get the buffer type for polymorphic dispatch
     */
    virtual DebugBufferType GetType() const = 0;

    /**
     * @brief Get human-readable type name
     */
    virtual const char* GetTypeName() const = 0;

    // =========================================================================
    // Vulkan resource access
    // =========================================================================

    /**
     * @brief Get the VkBuffer handle for descriptor binding
     */
    virtual VkBuffer GetVkBuffer() const = 0;

    /**
     * @brief Get the buffer size in bytes
     */
    virtual VkDeviceSize GetBufferSize() const = 0;

    /**
     * @brief Check if buffer is valid and usable
     */
    virtual bool IsValid() const = 0;

    /**
     * @brief Check if buffer uses host-visible memory (can be mapped)
     */
    virtual bool IsHostVisible() const = 0;

    // =========================================================================
    // Read/Write operations
    // =========================================================================

    /**
     * @brief Reset buffer state before capture (clear counters, reset write index)
     * @param device Vulkan device
     * @return true if reset succeeded
     */
    virtual bool Reset(VkDevice device) = 0;

    /**
     * @brief Read data from GPU buffer to CPU
     * @param device Vulkan device
     * @return Number of items read (interpretation depends on buffer type)
     */
    virtual uint32_t Read(VkDevice device) = 0;

    // =========================================================================
    // Data access
    // =========================================================================

    /**
     * @brief Get read data as std::any for generic access
     *
     * The actual type depends on GetType():
     * - RayTrace: std::vector<RayTrace>
     * - ShaderCounters: ShaderCounters struct
     */
    virtual std::any GetData() const = 0;

    /**
     * @brief Type-safe data access
     * @tparam T Expected data type
     * @return Pointer to data, or nullptr if type mismatch
     */
    template<typename T>
    const T* GetTypedData() const {
        try {
            return std::any_cast<const T*>(GetDataPtr());
        } catch (const std::bad_any_cast&) {
            return nullptr;
        }
    }

protected:
    /**
     * @brief Get pointer to internal data for GetTypedData
     * Implementation returns std::any containing pointer to actual data
     */
    virtual std::any GetDataPtr() const = 0;
};

} // namespace Vixen::RenderGraph::Debug
