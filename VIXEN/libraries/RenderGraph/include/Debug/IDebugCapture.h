#pragma once

#include <cstdint>
#include <string>

namespace Vixen::RenderGraph::Debug {

// Forward declaration
class IDebugBuffer;

/**
 * @brief Marker interface for resources that support debug capture
 *
 * Resources implementing this interface can be automatically detected
 * by the render graph and routed to debug nodes for analysis.
 *
 * The interface is lightweight - it simply marks a resource as "debug-capable"
 * and provides access to the underlying capture buffer.
 *
 * Usage:
 * 1. A shader declares binding 4 as debug SSBO
 * 2. VoxelGridNode creates a RayTraceBuffer or ShaderCountersBuffer
 * 3. DescriptorResourceGathererNode detects the IDebugCapture interface
 * 4. ComputeDispatchNode outputs debug-capable resources to DEBUG_OUTPUTS slot
 * 5. DebugBufferReaderNode receives and processes the debug data polymorphically
 */
class IDebugCapture {
public:
    virtual ~IDebugCapture() = default;

    /**
     * @brief Get the debug buffer (polymorphic)
     * @return Pointer to the buffer interface, or nullptr if not available
     */
    virtual IDebugBuffer* GetBuffer() = 0;
    virtual const IDebugBuffer* GetBuffer() const = 0;

    /**
     * @brief Get a human-readable name for this debug capture
     * Used for logging and export filenames
     */
    virtual std::string GetDebugName() const = 0;

    /**
     * @brief Get the shader binding index for this debug buffer
     */
    virtual uint32_t GetBindingIndex() const = 0;

    /**
     * @brief Check if capture is currently enabled
     * May be toggled at runtime
     */
    virtual bool IsCaptureEnabled() const = 0;

    /**
     * @brief Enable or disable capture
     */
    virtual void SetCaptureEnabled(bool enabled) = 0;
};

/**
 * @brief Tag type to mark a descriptor as debug-capable in the gatherer
 *
 * When the descriptor gatherer sees this tag in the slot metadata,
 * it knows to include this binding in the debug output list.
 */
struct DebugCaptureTag {
    uint32_t binding = 0;
    std::string name;
    IDebugCapture* captureInterface = nullptr;
};

} // namespace Vixen::RenderGraph::Debug
