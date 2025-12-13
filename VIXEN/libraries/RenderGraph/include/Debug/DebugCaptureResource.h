#pragma once

#include "IDebugCapture.h"
#include "IDebugBuffer.h"
#include "RayTraceBuffer.h"
#include "ShaderCountersBuffer.h"
#include <vulkan/vulkan.h>
#include <string>
#include <memory>
#include <variant>

namespace Vixen::RenderGraph::Debug {

/**
 * @brief A debug capture resource that owns a polymorphic debug buffer
 *
 * @deprecated Use RayTraceBuffer or ShaderCountersBuffer directly instead.
 * These concrete types now have `using conversion_type = VkBuffer;` which
 * enables the type system to handle descriptor extraction automatically.
 * No wrapper class needed.
 *
 * Example (new pattern):
 * @code
 * // Use concrete type directly:
 * auto rayBuffer = std::make_unique<RayTraceBuffer>(2048);
 * rayBuffer->Create(device, physicalDevice);
 *
 * // Output uses wrapper type's conversion_type for descriptor extraction:
 * ctx.Out(VoxelGridNodeConfig::DEBUG_CAPTURE_BUFFER, rayBuffer.get());
 * @endcode
 *
 * This class remains for backward compatibility but should not be used
 * in new code.
 */
class [[deprecated("Use RayTraceBuffer or ShaderCountersBuffer directly")]]
DebugCaptureResource : public IDebugCapture {
public:
    /**
     * @brief Create a ray trace capture resource
     */
    static std::unique_ptr<DebugCaptureResource> CreateRayTrace(
        VkDevice device,
        VkPhysicalDevice physicalDevice,
        uint32_t capacity,
        const std::string& debugName,
        uint32_t bindingIndex
    ) {
        auto resource = std::unique_ptr<DebugCaptureResource>(new DebugCaptureResource(device, debugName, bindingIndex));
        resource->buffer_ = std::make_unique<RayTraceBuffer>(capacity);
        if (!static_cast<RayTraceBuffer*>(resource->buffer_.get())->Create(device, physicalDevice)) {
            return nullptr;
        }
        return resource;
    }

    /**
     * @brief Create a shader counters capture resource
     */
    static std::unique_ptr<DebugCaptureResource> CreateCounters(
        VkDevice device,
        VkPhysicalDevice physicalDevice,
        uint32_t capacity,
        const std::string& debugName,
        uint32_t bindingIndex
    ) {
        auto resource = std::unique_ptr<DebugCaptureResource>(new DebugCaptureResource(device, debugName, bindingIndex));
        resource->buffer_ = std::make_unique<ShaderCountersBuffer>(capacity);
        if (!static_cast<ShaderCountersBuffer*>(resource->buffer_.get())->Create(device, physicalDevice)) {
            return nullptr;
        }
        return resource;
    }

    ~DebugCaptureResource() override {
        if (buffer_ && buffer_->IsValid() && device_ != VK_NULL_HANDLE) {
            // Destroy based on buffer type
            if (buffer_->GetType() == DebugBufferType::RayTrace) {
                static_cast<RayTraceBuffer*>(buffer_.get())->Destroy(device_);
            } else if (buffer_->GetType() == DebugBufferType::ShaderCounters) {
                static_cast<ShaderCountersBuffer*>(buffer_.get())->Destroy(device_);
            }
        }
    }

    // Non-copyable
    DebugCaptureResource(const DebugCaptureResource&) = delete;
    DebugCaptureResource& operator=(const DebugCaptureResource&) = delete;

    // Movable
    DebugCaptureResource(DebugCaptureResource&& other) noexcept
        : device_(other.device_)
        , buffer_(std::move(other.buffer_))
        , debugName_(std::move(other.debugName_))
        , bindingIndex_(other.bindingIndex_)
        , captureEnabled_(other.captureEnabled_)
    {
        other.device_ = VK_NULL_HANDLE;
    }

    DebugCaptureResource& operator=(DebugCaptureResource&& other) noexcept {
        if (this != &other) {
            // Destroy current buffer
            if (buffer_ && buffer_->IsValid() && device_ != VK_NULL_HANDLE) {
                if (buffer_->GetType() == DebugBufferType::RayTrace) {
                    static_cast<RayTraceBuffer*>(buffer_.get())->Destroy(device_);
                } else if (buffer_->GetType() == DebugBufferType::ShaderCounters) {
                    static_cast<ShaderCountersBuffer*>(buffer_.get())->Destroy(device_);
                }
            }

            device_ = other.device_;
            buffer_ = std::move(other.buffer_);
            debugName_ = std::move(other.debugName_);
            bindingIndex_ = other.bindingIndex_;
            captureEnabled_ = other.captureEnabled_;
            other.device_ = VK_NULL_HANDLE;
        }
        return *this;
    }

    // =========================================================================
    // IDebugCapture implementation
    // =========================================================================

    IDebugBuffer* GetBuffer() override { return buffer_.get(); }
    const IDebugBuffer* GetBuffer() const override { return buffer_.get(); }

    std::string GetDebugName() const override { return debugName_; }
    uint32_t GetBindingIndex() const override { return bindingIndex_; }

    bool IsCaptureEnabled() const override { return captureEnabled_; }
    void SetCaptureEnabled(bool enabled) override { captureEnabled_ = enabled; }

    // =========================================================================
    // Buffer access
    // =========================================================================

    /**
     * @brief Get the VkBuffer handle for binding to descriptor sets
     */
    VkBuffer GetVkBuffer() const { return buffer_ ? buffer_->GetVkBuffer() : VK_NULL_HANDLE; }

    /**
     * @brief Get the buffer size
     */
    VkDeviceSize GetBufferSize() const { return buffer_ ? buffer_->GetBufferSize() : 0; }

    /**
     * @brief Check if the buffer is valid
     */
    bool IsValid() const { return buffer_ && buffer_->IsValid(); }

    /**
     * @brief Get the buffer type
     */
    DebugBufferType GetBufferType() const { return buffer_ ? buffer_->GetType() : DebugBufferType::Unknown; }

    /**
     * @brief Reset the buffer before each capture frame
     */
    bool Reset() {
        return buffer_ ? buffer_->Reset(device_) : false;
    }

    /**
     * @brief Read data from GPU
     * @return Number of items read
     */
    uint32_t Read() {
        return buffer_ ? buffer_->Read(device_) : 0;
    }

    // =========================================================================
    // Type-safe accessors
    // =========================================================================

    /**
     * @brief Get as RayTraceBuffer (returns nullptr if wrong type)
     */
    RayTraceBuffer* AsRayTrace() {
        return (buffer_ && buffer_->GetType() == DebugBufferType::RayTrace)
            ? static_cast<RayTraceBuffer*>(buffer_.get())
            : nullptr;
    }

    const RayTraceBuffer* AsRayTrace() const {
        return (buffer_ && buffer_->GetType() == DebugBufferType::RayTrace)
            ? static_cast<const RayTraceBuffer*>(buffer_.get())
            : nullptr;
    }

    /**
     * @brief Get as ShaderCountersBuffer (returns nullptr if wrong type)
     */
    ShaderCountersBuffer* AsCounters() {
        return (buffer_ && buffer_->GetType() == DebugBufferType::ShaderCounters)
            ? static_cast<ShaderCountersBuffer*>(buffer_.get())
            : nullptr;
    }

    const ShaderCountersBuffer* AsCounters() const {
        return (buffer_ && buffer_->GetType() == DebugBufferType::ShaderCounters)
            ? static_cast<const ShaderCountersBuffer*>(buffer_.get())
            : nullptr;
    }

private:
    DebugCaptureResource(VkDevice device, const std::string& debugName, uint32_t bindingIndex)
        : device_(device)
        , debugName_(debugName)
        , bindingIndex_(bindingIndex)
        , captureEnabled_(true)
    {}

    VkDevice device_ = VK_NULL_HANDLE;
    std::unique_ptr<IDebugBuffer> buffer_;
    std::string debugName_;
    uint32_t bindingIndex_ = 0;
    bool captureEnabled_ = true;
};

} // namespace Vixen::RenderGraph::Debug
