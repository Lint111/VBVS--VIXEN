#include "pch.h"
#include "TLASInstanceBuffer.h"
#include "VulkanDevice.h"
#include "error/VulkanError.h"

#include <cstring>
#include <stdexcept>

namespace CashSystem {

// ============================================================================
// LIFECYCLE
// ============================================================================

TLASInstanceBuffer::~TLASInstanceBuffer() {
    Cleanup();
}

TLASInstanceBuffer::TLASInstanceBuffer(TLASInstanceBuffer&& other) noexcept
    : device_(other.device_)
    , maxInstances_(other.maxInstances_)
    , frameBuffers_(std::move(other.frameBuffers_))
{
    other.device_ = nullptr;
    other.maxInstances_ = 0;
}

TLASInstanceBuffer& TLASInstanceBuffer::operator=(TLASInstanceBuffer&& other) noexcept {
    if (this != &other) {
        Cleanup();

        device_ = other.device_;
        maxInstances_ = other.maxInstances_;
        frameBuffers_ = std::move(other.frameBuffers_);

        other.device_ = nullptr;
        other.maxInstances_ = 0;
    }
    return *this;
}

bool TLASInstanceBuffer::Initialize(
    Vixen::Vulkan::Resources::VulkanDevice* device,
    uint32_t imageCount,
    const Config& config)
{
    // Initialize logger for this subsystem
    InitializeLogger("TLASInstanceBuffer", true);

    if (!device || imageCount == 0 || config.maxInstances == 0) {
        LOG_ERROR("[TLASInstanceBuffer::Initialize] Invalid parameters");
        return false;
    }

    // Cleanup any existing state
    Cleanup();

    device_ = device;
    maxInstances_ = config.maxInstances;

    // Calculate buffer size (aligned to 16 bytes for device address)
    const VkDeviceSize instanceSize = sizeof(VkAccelerationStructureInstanceKHR);
    const VkDeviceSize bufferSize = instanceSize * maxInstances_;

    // Resize frame buffer container
    frameBuffers_.resize(imageCount);

    // Allocate each frame buffer via VulkanDevice centralized API
    for (uint32_t i = 0; i < imageCount; ++i) {
        ResourceManagement::BufferAllocationRequest request;
        request.size = bufferSize;
        request.usage = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
                        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
        request.location = ResourceManagement::MemoryLocation::HostVisible;
        request.debugName = "TLASInstanceBuffer";

        auto allocation = device_->AllocateBuffer(request);
        if (!allocation) {
            LOG_ERROR("[TLASInstanceBuffer::Initialize] Failed to allocate frame buffer " +
                      std::to_string(i));
            Cleanup();
            return false;
        }

        // Map the buffer via device (may already be persistently mapped)
        void* mappedPtr = allocation->mappedData;
        if (!mappedPtr) {
            mappedPtr = device_->MapBuffer(*allocation);
        }
        if (!mappedPtr) {
            LOG_ERROR("[TLASInstanceBuffer::Initialize] Failed to map frame buffer " +
                      std::to_string(i));
            device_->FreeBuffer(*allocation);
            Cleanup();
            return false;
        }

        frameBuffers_[i].value.allocation = *allocation;
        frameBuffers_[i].value.mappedPtr = mappedPtr;
        frameBuffers_[i].value.instanceCount = 0;
        frameBuffers_.MarkDirty(i);  // Start dirty
    }

    LOG_INFO("[TLASInstanceBuffer::Initialize] Allocated " + std::to_string(imageCount) +
             " frame buffers, " + std::to_string(maxInstances_) + " max instances each");

    return true;
}

void TLASInstanceBuffer::Cleanup() {
    if (!device_) {
        return;
    }

    for (size_t i = 0; i < frameBuffers_.size(); ++i) {
        auto& fb = frameBuffers_[i].value;

        // Unmap if we manually mapped (not persistently mapped)
        if (fb.mappedPtr && fb.mappedPtr != fb.allocation.mappedData) {
            device_->UnmapBuffer(fb.allocation);
        }
        fb.mappedPtr = nullptr;

        if (fb.allocation.buffer != VK_NULL_HANDLE) {
            device_->FreeBuffer(fb.allocation);
        }

        fb.allocation = {};
        fb.instanceCount = 0;
    }

    frameBuffers_.clear();
    device_ = nullptr;
    maxInstances_ = 0;

    LOG_DEBUG("[TLASInstanceBuffer::Cleanup] Cleanup complete");
}

// ============================================================================
// PER-FRAME BUFFER ACCESS
// ============================================================================

VkBuffer TLASInstanceBuffer::GetBuffer(uint32_t imageIndex) const {
    if (!ValidateImageIndex(imageIndex)) {
        return VK_NULL_HANDLE;
    }
    return frameBuffers_[imageIndex].value.allocation.buffer;
}

VkDeviceAddress TLASInstanceBuffer::GetDeviceAddress(uint32_t imageIndex) const {
    if (!ValidateImageIndex(imageIndex)) {
        return 0;
    }
    return frameBuffers_[imageIndex].value.allocation.deviceAddress;
}

void* TLASInstanceBuffer::GetMappedPtr(uint32_t imageIndex) const {
    if (!ValidateImageIndex(imageIndex)) {
        return nullptr;
    }
    return frameBuffers_[imageIndex].value.mappedPtr;
}

// ============================================================================
// INSTANCE OPERATIONS
// ============================================================================

void TLASInstanceBuffer::WriteInstances(
    uint32_t imageIndex,
    std::span<const VkAccelerationStructureInstanceKHR> instances)
{
    if (!ValidateImageIndex(imageIndex)) {
        return;
    }

    if (instances.size() > maxInstances_) {
        LOG_WARNING("[TLASInstanceBuffer::WriteInstances] Instance count " +
                    std::to_string(instances.size()) + " exceeds max " +
                    std::to_string(maxInstances_) + " - truncating");
    }

    auto& fb = frameBuffers_[imageIndex].value;
    const uint32_t count = static_cast<uint32_t>(
        std::min(instances.size(), static_cast<size_t>(maxInstances_)));

    if (fb.mappedPtr && count > 0) {
        std::memcpy(fb.mappedPtr, instances.data(),
                    count * sizeof(VkAccelerationStructureInstanceKHR));
    }

    fb.instanceCount = count;
    frameBuffers_.MarkReady(imageIndex);
}

uint32_t TLASInstanceBuffer::GetInstanceCount(uint32_t imageIndex) const {
    if (!ValidateImageIndex(imageIndex)) {
        return 0;
    }
    return frameBuffers_[imageIndex].value.instanceCount;
}

// ============================================================================
// STATE TRACKING
// ============================================================================

ResourceManagement::ContainerState TLASInstanceBuffer::GetState(uint32_t imageIndex) const {
    if (!ValidateImageIndex(imageIndex)) {
        return ResourceManagement::ContainerState::Invalid;
    }
    return frameBuffers_.GetState(imageIndex);
}

void TLASInstanceBuffer::MarkDirty(uint32_t imageIndex) {
    if (ValidateImageIndex(imageIndex)) {
        frameBuffers_.MarkDirty(imageIndex);
    }
}

bool TLASInstanceBuffer::AnyDirty() const {
    return frameBuffers_.AnyDirty();
}

// ============================================================================
// VALIDATION
// ============================================================================

bool TLASInstanceBuffer::ValidateImageIndex(uint32_t imageIndex) const {
    if (imageIndex >= frameBuffers_.size()) {
        LOG_ERROR("[TLASInstanceBuffer] Invalid imageIndex " + std::to_string(imageIndex) +
                  " >= " + std::to_string(frameBuffers_.size()));
        return false;
    }
    return true;
}

} // namespace CashSystem
