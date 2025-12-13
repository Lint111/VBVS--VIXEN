#include "Debug/RayTraceBuffer.h"
#include <cstring>
#include <algorithm>

namespace Vixen::RenderGraph::Debug {

// ============================================================================
// Constructor / Destructor
// ============================================================================

RayTraceBuffer::RayTraceBuffer(uint32_t rayCapacity)
    : capacity_(rayCapacity)
    , bufferSize_(CalculateBufferSize(rayCapacity))
{
    rayTraces_.reserve(capacity_);
}

RayTraceBuffer::~RayTraceBuffer() {
    // Note: Destroy() must be called explicitly with VkDevice before destruction
}

// ============================================================================
// Move operations
// ============================================================================

RayTraceBuffer::RayTraceBuffer(RayTraceBuffer&& other) noexcept
    : buffer_(other.buffer_)
    , memory_(other.memory_)
    , bufferSize_(other.bufferSize_)
    , capacity_(other.capacity_)
    , isHostVisible_(other.isHostVisible_)
    , rayTraces_(std::move(other.rayTraces_))
    , capturedCount_(other.capturedCount_)
    , totalWrites_(other.totalWrites_)
{
    other.buffer_ = VK_NULL_HANDLE;
    other.memory_ = VK_NULL_HANDLE;
    other.bufferSize_ = 0;
    other.capacity_ = 0;
    other.capturedCount_ = 0;
    other.totalWrites_ = 0;
}

RayTraceBuffer& RayTraceBuffer::operator=(RayTraceBuffer&& other) noexcept {
    if (this != &other) {
        buffer_ = other.buffer_;
        memory_ = other.memory_;
        bufferSize_ = other.bufferSize_;
        capacity_ = other.capacity_;
        isHostVisible_ = other.isHostVisible_;
        rayTraces_ = std::move(other.rayTraces_);
        capturedCount_ = other.capturedCount_;
        totalWrites_ = other.totalWrites_;

        other.buffer_ = VK_NULL_HANDLE;
        other.memory_ = VK_NULL_HANDLE;
        other.bufferSize_ = 0;
        other.capacity_ = 0;
        other.capturedCount_ = 0;
        other.totalWrites_ = 0;
    }
    return *this;
}

// ============================================================================
// Lifecycle
// ============================================================================

bool RayTraceBuffer::Create(VkDevice device, VkPhysicalDevice physicalDevice) {
    if (device == VK_NULL_HANDLE || physicalDevice == VK_NULL_HANDLE) {
        return false;
    }

    if (capacity_ == 0) {
        return false;
    }

    // Create buffer
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = bufferSize_;
    bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(device, &bufferInfo, nullptr, &buffer_) != VK_SUCCESS) {
        return false;
    }

    // Get memory requirements
    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(device, buffer_, &memRequirements);

    // Find suitable memory type (HOST_VISIBLE | HOST_COHERENT)
    VkMemoryPropertyFlags memProperties =
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

    uint32_t memoryTypeIndex = FindMemoryType(physicalDevice, memRequirements.memoryTypeBits, memProperties);
    if (memoryTypeIndex == UINT32_MAX) {
        vkDestroyBuffer(device, buffer_, nullptr);
        buffer_ = VK_NULL_HANDLE;
        return false;
    }

    // Allocate memory
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = memoryTypeIndex;

    if (vkAllocateMemory(device, &allocInfo, nullptr, &memory_) != VK_SUCCESS) {
        vkDestroyBuffer(device, buffer_, nullptr);
        buffer_ = VK_NULL_HANDLE;
        return false;
    }

    // Bind memory to buffer
    if (vkBindBufferMemory(device, buffer_, memory_, 0) != VK_SUCCESS) {
        vkFreeMemory(device, memory_, nullptr);
        vkDestroyBuffer(device, buffer_, nullptr);
        buffer_ = VK_NULL_HANDLE;
        memory_ = VK_NULL_HANDLE;
        return false;
    }

    isHostVisible_ = true;

    // Initialize buffer header
    void* data = nullptr;
    if (vkMapMemory(device, memory_, 0, sizeof(TraceBufferHeader), 0, &data) == VK_SUCCESS) {
        auto* header = static_cast<TraceBufferHeader*>(data);
        header->writeIndex = 0;
        header->capacity = capacity_;
        header->_padding[0] = 0;
        header->_padding[1] = 0;
        vkUnmapMemory(device, memory_);
    }

    return true;
}

void RayTraceBuffer::Destroy(VkDevice device) {
    if (buffer_ != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, buffer_, nullptr);
        buffer_ = VK_NULL_HANDLE;
    }

    if (memory_ != VK_NULL_HANDLE) {
        vkFreeMemory(device, memory_, nullptr);
        memory_ = VK_NULL_HANDLE;
    }

    bufferSize_ = 0;
    rayTraces_.clear();
    capturedCount_ = 0;
    totalWrites_ = 0;
}

// ============================================================================
// IDebugBuffer interface implementation
// ============================================================================

bool RayTraceBuffer::Reset(VkDevice device) {
    if (!IsValid() || !isHostVisible_) {
        return false;
    }

    void* data = nullptr;
    if (vkMapMemory(device, memory_, 0, sizeof(TraceBufferHeader), 0, &data) != VK_SUCCESS) {
        return false;
    }

    // Reset write index
    auto* header = static_cast<TraceBufferHeader*>(data);
    header->writeIndex = 0;

    vkUnmapMemory(device, memory_);

    // Clear CPU-side data
    rayTraces_.clear();
    capturedCount_ = 0;
    totalWrites_ = 0;

    return true;
}

uint32_t RayTraceBuffer::Read(VkDevice device) {
    if (!IsValid() || !isHostVisible_) {
        return 0;
    }

    void* data = nullptr;
    if (vkMapMemory(device, memory_, 0, bufferSize_, 0, &data) != VK_SUCCESS) {
        return 0;
    }

    // Read header
    const auto* header = static_cast<const TraceBufferHeader*>(data);
    totalWrites_ = header->writeIndex;
    capturedCount_ = std::min(totalWrites_, capacity_);

    // Clear previous traces
    rayTraces_.clear();
    rayTraces_.reserve(capturedCount_);

    if (capturedCount_ > 0) {
        const uint8_t* rawData = static_cast<const uint8_t*>(data) + sizeof(TraceBufferHeader);

        for (uint32_t i = 0; i < capturedCount_; ++i) {
            // Handle ring buffer wrapping
            uint32_t slotIndex = (totalWrites_ > capacity_)
                ? ((totalWrites_ % capacity_) + i) % capacity_  // Wrapped: oldest first
                : i;                                             // Not wrapped: linear

            const uint8_t* rayData = rawData + (slotIndex * TRACE_RAY_SIZE);

            // Read ray header
            RayTrace trace;
            const auto* rayHeader = reinterpret_cast<const RayTraceHeader*>(rayData);
            trace.header = *rayHeader;

            // Read steps
            uint32_t stepCount = std::min(rayHeader->stepCount, MAX_TRACE_STEPS);
            trace.steps.reserve(stepCount);

            const uint8_t* stepData = rayData + sizeof(RayTraceHeader);
            for (uint32_t s = 0; s < stepCount; ++s) {
                const auto* step = reinterpret_cast<const TraceStep*>(stepData + s * sizeof(TraceStep));
                trace.steps.push_back(*step);
            }

            rayTraces_.push_back(std::move(trace));
        }
    }

    vkUnmapMemory(device, memory_);
    return capturedCount_;
}

std::any RayTraceBuffer::GetData() const {
    return rayTraces_;
}

std::any RayTraceBuffer::GetDataPtr() const {
    if (rayTraces_.empty()) {
        return static_cast<const std::vector<RayTrace>*>(nullptr);
    }
    return &rayTraces_;
}

// ============================================================================
// Helpers
// ============================================================================

uint32_t RayTraceBuffer::FindMemoryType(
    VkPhysicalDevice physicalDevice,
    uint32_t typeFilter,
    VkMemoryPropertyFlags properties
) {
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);

    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) &&
            (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }

    return UINT32_MAX;
}

} // namespace Vixen::RenderGraph::Debug
