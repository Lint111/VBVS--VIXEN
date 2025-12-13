#include "Debug/ShaderCountersBuffer.h"
#include <cstring>
#include <algorithm>

namespace Vixen::RenderGraph::Debug {

// ============================================================================
// Constructor / Destructor
// ============================================================================

ShaderCountersBuffer::ShaderCountersBuffer(uint32_t entryCount)
    : capacity_(entryCount)
    , bufferSize_(CalculateBufferSize(entryCount))
{
    counters_.reserve(capacity_);
}

ShaderCountersBuffer::~ShaderCountersBuffer() {
    // Note: Destroy() must be called explicitly with VkDevice before destruction
    // We can't destroy Vulkan resources here without the device handle
}

// ============================================================================
// Move operations
// ============================================================================

ShaderCountersBuffer::ShaderCountersBuffer(ShaderCountersBuffer&& other) noexcept
    : buffer_(other.buffer_)
    , memory_(other.memory_)
    , bufferSize_(other.bufferSize_)
    , capacity_(other.capacity_)
    , isHostVisible_(other.isHostVisible_)
    , counters_(std::move(other.counters_))
    , readCount_(other.readCount_)
{
    other.buffer_ = VK_NULL_HANDLE;
    other.memory_ = VK_NULL_HANDLE;
    other.bufferSize_ = 0;
    other.capacity_ = 0;
    other.readCount_ = 0;
}

ShaderCountersBuffer& ShaderCountersBuffer::operator=(ShaderCountersBuffer&& other) noexcept {
    if (this != &other) {
        // Note: Caller responsible for calling Destroy() on this before move
        buffer_ = other.buffer_;
        memory_ = other.memory_;
        bufferSize_ = other.bufferSize_;
        capacity_ = other.capacity_;
        isHostVisible_ = other.isHostVisible_;
        counters_ = std::move(other.counters_);
        readCount_ = other.readCount_;

        other.buffer_ = VK_NULL_HANDLE;
        other.memory_ = VK_NULL_HANDLE;
        other.bufferSize_ = 0;
        other.capacity_ = 0;
        other.readCount_ = 0;
    }
    return *this;
}

// ============================================================================
// Lifecycle
// ============================================================================

bool ShaderCountersBuffer::Create(VkDevice device, VkPhysicalDevice physicalDevice) {
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
    if (vkMapMemory(device, memory_, 0, bufferSize_, 0, &data) == VK_SUCCESS) {
        // Zero entire buffer
        std::memset(data, 0, bufferSize_);

        // Set header
        auto* header = static_cast<ShaderCountersHeader*>(data);
        header->entryCount = 0;
        header->capacity = capacity_;

        vkUnmapMemory(device, memory_);
    }

    return true;
}

void ShaderCountersBuffer::Destroy(VkDevice device) {
    if (buffer_ != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, buffer_, nullptr);
        buffer_ = VK_NULL_HANDLE;
    }

    if (memory_ != VK_NULL_HANDLE) {
        vkFreeMemory(device, memory_, nullptr);
        memory_ = VK_NULL_HANDLE;
    }

    bufferSize_ = 0;
    counters_.clear();
    readCount_ = 0;
}

// ============================================================================
// IDebugBuffer interface implementation
// ============================================================================

bool ShaderCountersBuffer::Reset(VkDevice device) {
    if (!IsValid() || !isHostVisible_) {
        return false;
    }

    void* data = nullptr;
    if (vkMapMemory(device, memory_, 0, bufferSize_, 0, &data) != VK_SUCCESS) {
        return false;
    }

    // Zero entire buffer
    std::memset(data, 0, bufferSize_);

    // Reset header
    auto* header = static_cast<ShaderCountersHeader*>(data);
    header->entryCount = 0;
    header->capacity = capacity_;

    vkUnmapMemory(device, memory_);

    // Clear CPU-side data
    counters_.clear();
    readCount_ = 0;

    return true;
}

uint32_t ShaderCountersBuffer::Read(VkDevice device) {
    if (!IsValid() || !isHostVisible_) {
        return 0;
    }

    void* data = nullptr;
    if (vkMapMemory(device, memory_, 0, bufferSize_, 0, &data) != VK_SUCCESS) {
        return 0;
    }

    // Read header
    const auto* header = static_cast<const ShaderCountersHeader*>(data);
    uint32_t entryCount = std::min(header->entryCount, capacity_);

    // Read counter entries
    counters_.clear();
    counters_.reserve(entryCount);

    if (entryCount > 0) {
        const auto* entries = reinterpret_cast<const ShaderCounters*>(
            static_cast<const uint8_t*>(data) + sizeof(ShaderCountersHeader)
        );

        for (uint32_t i = 0; i < entryCount; ++i) {
            counters_.push_back(entries[i]);
        }
    }

    vkUnmapMemory(device, memory_);

    readCount_ = entryCount;
    return readCount_;
}

std::any ShaderCountersBuffer::GetData() const {
    return counters_;
}

std::any ShaderCountersBuffer::GetDataPtr() const {
    if (counters_.empty()) {
        return static_cast<const std::vector<ShaderCounters>*>(nullptr);
    }
    return &counters_;
}

// ============================================================================
// Counter-specific methods
// ============================================================================

ShaderCounters ShaderCountersBuffer::GetAggregatedCounters() const {
    ShaderCounters aggregated;

    for (const auto& counter : counters_) {
        aggregated.Accumulate(counter);
    }

    return aggregated;
}

// ============================================================================
// Helpers
// ============================================================================

uint32_t ShaderCountersBuffer::FindMemoryType(
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
