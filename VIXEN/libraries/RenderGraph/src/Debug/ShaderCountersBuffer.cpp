#include "Debug/ShaderCountersBuffer.h"
#include <cstring>

namespace Vixen::RenderGraph::Debug {

// ============================================================================
// Constructor / Destructor
// ============================================================================

ShaderCountersBuffer::ShaderCountersBuffer(uint32_t /*capacity*/) {
    // capacity parameter is unused - kept for API compatibility
    counters_.Clear();
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
    , counters_(other.counters_)
{
    other.buffer_ = VK_NULL_HANDLE;
    other.memory_ = VK_NULL_HANDLE;
    other.counters_.Clear();
}

ShaderCountersBuffer& ShaderCountersBuffer::operator=(ShaderCountersBuffer&& other) noexcept {
    if (this != &other) {
        // Note: Caller responsible for calling Destroy() on this before move
        buffer_ = other.buffer_;
        memory_ = other.memory_;
        counters_ = other.counters_;

        other.buffer_ = VK_NULL_HANDLE;
        other.memory_ = VK_NULL_HANDLE;
        other.counters_.Clear();
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

    constexpr VkDeviceSize bufferSize = sizeof(GPUShaderCounters);

    // Create buffer
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = bufferSize;
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

    // Initialize buffer to zero
    void* data = nullptr;
    if (vkMapMemory(device, memory_, 0, bufferSize, 0, &data) == VK_SUCCESS) {
        std::memset(data, 0, bufferSize);
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

    counters_.Clear();
}

// ============================================================================
// IDebugBuffer interface implementation
// ============================================================================

bool ShaderCountersBuffer::Reset(VkDevice device) {
    if (!IsValid()) {
        return false;
    }

    void* data = nullptr;
    if (vkMapMemory(device, memory_, 0, sizeof(GPUShaderCounters), 0, &data) != VK_SUCCESS) {
        return false;
    }

    // Zero entire buffer
    std::memset(data, 0, sizeof(GPUShaderCounters));
    vkUnmapMemory(device, memory_);

    // Clear CPU-side cache
    counters_.Clear();

    return true;
}

uint32_t ShaderCountersBuffer::Read(VkDevice device) {
    if (!IsValid()) {
        return 0;
    }

    void* data = nullptr;
    if (vkMapMemory(device, memory_, 0, sizeof(GPUShaderCounters), 0, &data) != VK_SUCCESS) {
        return 0;
    }

    // Copy GPU data to CPU cache
    std::memcpy(&counters_, data, sizeof(GPUShaderCounters));
    vkUnmapMemory(device, memory_);

    return counters_.HasData() ? 1 : 0;
}

std::any ShaderCountersBuffer::GetData() const {
    return counters_;
}

std::any ShaderCountersBuffer::GetDataPtr() const {
    return &counters_;
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
