#include "Core/DirectAllocator.h"
#include <stdexcept>
#include <cstring>

namespace Vixen::RenderGraph {

DirectAllocator::DirectAllocator(
    VkPhysicalDevice physicalDevice,
    VkDevice device,
    ResourceBudgetManager* budgetManager)
    : physicalDevice_(physicalDevice)
    , device_(device)
    , budgetManager_(budgetManager)
{
    if (physicalDevice_ != VK_NULL_HANDLE) {
        vkGetPhysicalDeviceMemoryProperties(physicalDevice_, &memProperties_);
    }
}

DirectAllocator::~DirectAllocator() {
    std::lock_guard lock(mutex_);

    // Free any remaining allocations
    for (auto& [handle, record] : allocations_) {
        if (record.memory != VK_NULL_HANDLE && device_ != VK_NULL_HANDLE) {
            vkFreeMemory(device_, record.memory, nullptr);
        }
    }
    allocations_.clear();
}

std::expected<BufferAllocation, AllocationError>
DirectAllocator::AllocateBuffer(const BufferAllocationRequest& request) {
    if (request.size == 0) {
        return std::unexpected(AllocationError::InvalidParameters);
    }

    if (device_ == VK_NULL_HANDLE) {
        return std::unexpected(AllocationError::InvalidParameters);
    }

    // Check budget before allocation
    if (budgetManager_) {
        if (!budgetManager_->TryAllocate(BudgetResourceType::DeviceMemory, request.size)) {
            return std::unexpected(AllocationError::OverBudget);
        }
    }

    // Create buffer
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = request.size;
    bufferInfo.usage = request.usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkBuffer buffer = VK_NULL_HANDLE;
    VkResult result = vkCreateBuffer(device_, &bufferInfo, nullptr, &buffer);
    if (result != VK_SUCCESS) {
        return std::unexpected(result == VK_ERROR_OUT_OF_DEVICE_MEMORY
            ? AllocationError::OutOfDeviceMemory
            : AllocationError::Unknown);
    }

    // Get memory requirements
    VkMemoryRequirements memReq;
    vkGetBufferMemoryRequirements(device_, buffer, &memReq);

    // Find appropriate memory type
    VkMemoryPropertyFlags memProps = GetMemoryProperties(request.location);
    uint32_t memTypeIndex;
    try {
        memTypeIndex = FindMemoryType(memReq.memoryTypeBits, memProps);
    } catch (...) {
        vkDestroyBuffer(device_, buffer, nullptr);
        return std::unexpected(AllocationError::OutOfDeviceMemory);
    }

    // Allocate memory
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReq.size;
    allocInfo.memoryTypeIndex = memTypeIndex;

    VkDeviceMemory memory = VK_NULL_HANDLE;
    result = vkAllocateMemory(device_, &allocInfo, nullptr, &memory);
    if (result != VK_SUCCESS) {
        vkDestroyBuffer(device_, buffer, nullptr);
        return std::unexpected(result == VK_ERROR_OUT_OF_DEVICE_MEMORY
            ? AllocationError::OutOfDeviceMemory
            : AllocationError::OutOfHostMemory);
    }

    // Bind memory to buffer
    result = vkBindBufferMemory(device_, buffer, memory, 0);
    if (result != VK_SUCCESS) {
        vkFreeMemory(device_, memory, nullptr);
        vkDestroyBuffer(device_, buffer, nullptr);
        return std::unexpected(AllocationError::Unknown);
    }

    // Create allocation record
    auto* record = new AllocationRecord{
        .memory = memory,
        .size = memReq.size,
        .memoryTypeIndex = memTypeIndex,
        .isMapped = false,
        .mappedPtr = nullptr
    };

    // Track allocation
    {
        std::lock_guard lock(mutex_);
        allocations_[record] = *record;
        stats_.totalAllocatedBytes += memReq.size;
        stats_.totalUsedBytes += request.size;
        stats_.allocationCount++;
        stats_.blockCount++;
    }

    // Record to budget manager
    if (budgetManager_) {
        budgetManager_->RecordAllocation(BudgetResourceType::DeviceMemory, memReq.size);
    }

    return BufferAllocation{
        .buffer = buffer,
        .allocation = record,
        .size = memReq.size,
        .offset = 0,
        .mappedData = nullptr
    };
}

void DirectAllocator::FreeBuffer(BufferAllocation& allocation) {
    if (!allocation.buffer || !allocation.allocation) {
        return;
    }

    AllocationRecord* record = static_cast<AllocationRecord*>(allocation.allocation);
    VkDeviceSize freedSize = 0;

    {
        std::lock_guard lock(mutex_);

        auto it = allocations_.find(allocation.allocation);
        if (it != allocations_.end()) {
            freedSize = it->second.size;

            // Unmap if mapped
            if (it->second.isMapped && device_ != VK_NULL_HANDLE) {
                vkUnmapMemory(device_, it->second.memory);
            }

            allocations_.erase(it);

            stats_.totalAllocatedBytes -= freedSize;
            stats_.totalUsedBytes -= allocation.size;
            stats_.allocationCount--;
            stats_.blockCount--;
        }
    }

    // Free Vulkan resources
    if (device_ != VK_NULL_HANDLE) {
        vkDestroyBuffer(device_, allocation.buffer, nullptr);
        if (record->memory != VK_NULL_HANDLE) {
            vkFreeMemory(device_, record->memory, nullptr);
        }
    }

    // Record to budget manager
    if (budgetManager_ && freedSize > 0) {
        budgetManager_->RecordDeallocation(BudgetResourceType::DeviceMemory, freedSize);
    }

    delete record;

    // Invalidate allocation
    allocation.buffer = VK_NULL_HANDLE;
    allocation.allocation = nullptr;
    allocation.mappedData = nullptr;
}

std::expected<ImageAllocation, AllocationError>
DirectAllocator::AllocateImage(const ImageAllocationRequest& request) {
    if (device_ == VK_NULL_HANDLE) {
        return std::unexpected(AllocationError::InvalidParameters);
    }

    // Create image
    VkImage image = VK_NULL_HANDLE;
    VkResult result = vkCreateImage(device_, &request.createInfo, nullptr, &image);
    if (result != VK_SUCCESS) {
        return std::unexpected(result == VK_ERROR_OUT_OF_DEVICE_MEMORY
            ? AllocationError::OutOfDeviceMemory
            : AllocationError::Unknown);
    }

    // Get memory requirements
    VkMemoryRequirements memReq;
    vkGetImageMemoryRequirements(device_, image, &memReq);

    // Check budget
    if (budgetManager_) {
        if (!budgetManager_->TryAllocate(BudgetResourceType::DeviceMemory, memReq.size)) {
            vkDestroyImage(device_, image, nullptr);
            return std::unexpected(AllocationError::OverBudget);
        }
    }

    // Find memory type
    VkMemoryPropertyFlags memProps = GetMemoryProperties(request.location);
    uint32_t memTypeIndex;
    try {
        memTypeIndex = FindMemoryType(memReq.memoryTypeBits, memProps);
    } catch (...) {
        vkDestroyImage(device_, image, nullptr);
        return std::unexpected(AllocationError::OutOfDeviceMemory);
    }

    // Allocate memory
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReq.size;
    allocInfo.memoryTypeIndex = memTypeIndex;

    VkDeviceMemory memory = VK_NULL_HANDLE;
    result = vkAllocateMemory(device_, &allocInfo, nullptr, &memory);
    if (result != VK_SUCCESS) {
        vkDestroyImage(device_, image, nullptr);
        return std::unexpected(result == VK_ERROR_OUT_OF_DEVICE_MEMORY
            ? AllocationError::OutOfDeviceMemory
            : AllocationError::OutOfHostMemory);
    }

    // Bind memory
    result = vkBindImageMemory(device_, image, memory, 0);
    if (result != VK_SUCCESS) {
        vkFreeMemory(device_, memory, nullptr);
        vkDestroyImage(device_, image, nullptr);
        return std::unexpected(AllocationError::Unknown);
    }

    // Create record
    auto* record = new AllocationRecord{
        .memory = memory,
        .size = memReq.size,
        .memoryTypeIndex = memTypeIndex,
        .isMapped = false,
        .mappedPtr = nullptr
    };

    {
        std::lock_guard lock(mutex_);
        allocations_[record] = *record;
        stats_.totalAllocatedBytes += memReq.size;
        stats_.totalUsedBytes += memReq.size;
        stats_.allocationCount++;
        stats_.blockCount++;
    }

    if (budgetManager_) {
        budgetManager_->RecordAllocation(BudgetResourceType::DeviceMemory, memReq.size);
    }

    return ImageAllocation{
        .image = image,
        .allocation = record,
        .size = memReq.size
    };
}

void DirectAllocator::FreeImage(ImageAllocation& allocation) {
    if (!allocation.image || !allocation.allocation) {
        return;
    }

    AllocationRecord* record = static_cast<AllocationRecord*>(allocation.allocation);
    VkDeviceSize freedSize = 0;

    {
        std::lock_guard lock(mutex_);

        auto it = allocations_.find(allocation.allocation);
        if (it != allocations_.end()) {
            freedSize = it->second.size;
            allocations_.erase(it);

            stats_.totalAllocatedBytes -= freedSize;
            stats_.totalUsedBytes -= allocation.size;
            stats_.allocationCount--;
            stats_.blockCount--;
        }
    }

    if (device_ != VK_NULL_HANDLE) {
        vkDestroyImage(device_, allocation.image, nullptr);
        if (record->memory != VK_NULL_HANDLE) {
            vkFreeMemory(device_, record->memory, nullptr);
        }
    }

    if (budgetManager_ && freedSize > 0) {
        budgetManager_->RecordDeallocation(BudgetResourceType::DeviceMemory, freedSize);
    }

    delete record;

    allocation.image = VK_NULL_HANDLE;
    allocation.allocation = nullptr;
}

void* DirectAllocator::MapBuffer(const BufferAllocation& allocation) {
    if (!allocation.allocation) {
        return nullptr;
    }

    std::lock_guard lock(mutex_);

    auto it = allocations_.find(allocation.allocation);
    if (it == allocations_.end()) {
        return nullptr;
    }

    if (it->second.isMapped) {
        return it->second.mappedPtr;
    }

    void* data = nullptr;
    VkResult result = vkMapMemory(device_, it->second.memory, 0, it->second.size, 0, &data);
    if (result != VK_SUCCESS) {
        return nullptr;
    }

    it->second.isMapped = true;
    it->second.mappedPtr = data;
    return data;
}

void DirectAllocator::UnmapBuffer(const BufferAllocation& allocation) {
    if (!allocation.allocation) {
        return;
    }

    std::lock_guard lock(mutex_);

    auto it = allocations_.find(allocation.allocation);
    if (it == allocations_.end() || !it->second.isMapped) {
        return;
    }

    vkUnmapMemory(device_, it->second.memory);
    it->second.isMapped = false;
    it->second.mappedPtr = nullptr;
}

void DirectAllocator::FlushMappedRange(
    const BufferAllocation& allocation,
    VkDeviceSize offset,
    VkDeviceSize size)
{
    if (!allocation.allocation || device_ == VK_NULL_HANDLE) {
        return;
    }

    std::lock_guard lock(mutex_);

    auto it = allocations_.find(allocation.allocation);
    if (it == allocations_.end() || !it->second.isMapped) {
        return;
    }

    VkMappedMemoryRange range{};
    range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
    range.memory = it->second.memory;
    range.offset = offset;
    range.size = (size == VK_WHOLE_SIZE) ? it->second.size : size;

    vkFlushMappedMemoryRanges(device_, 1, &range);
}

void DirectAllocator::InvalidateMappedRange(
    const BufferAllocation& allocation,
    VkDeviceSize offset,
    VkDeviceSize size)
{
    if (!allocation.allocation || device_ == VK_NULL_HANDLE) {
        return;
    }

    std::lock_guard lock(mutex_);

    auto it = allocations_.find(allocation.allocation);
    if (it == allocations_.end() || !it->second.isMapped) {
        return;
    }

    VkMappedMemoryRange range{};
    range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
    range.memory = it->second.memory;
    range.offset = offset;
    range.size = (size == VK_WHOLE_SIZE) ? it->second.size : size;

    vkInvalidateMappedMemoryRanges(device_, 1, &range);
}

AllocationStats DirectAllocator::GetStats() const {
    std::lock_guard lock(mutex_);
    AllocationStats result = stats_;

    // Calculate fragmentation
    if (result.totalAllocatedBytes > 0) {
        result.fragmentationRatio = 1.0f -
            (static_cast<float>(result.totalUsedBytes) /
             static_cast<float>(result.totalAllocatedBytes));
    }

    return result;
}

void DirectAllocator::SetBudgetManager(ResourceBudgetManager* budgetManager) {
    std::lock_guard lock(mutex_);
    budgetManager_ = budgetManager;
}

ResourceBudgetManager* DirectAllocator::GetBudgetManager() const {
    std::lock_guard lock(mutex_);
    return budgetManager_;
}

uint32_t DirectAllocator::FindMemoryType(
    uint32_t typeFilter,
    VkMemoryPropertyFlags properties) const
{
    for (uint32_t i = 0; i < memProperties_.memoryTypeCount; ++i) {
        if ((typeFilter & (1 << i)) &&
            (memProperties_.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    throw std::runtime_error("Failed to find suitable memory type");
}

VkMemoryPropertyFlags DirectAllocator::GetMemoryProperties(MemoryLocation location) const {
    switch (location) {
        case MemoryLocation::DeviceLocal:
            return VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
        case MemoryLocation::HostVisible:
            return VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        case MemoryLocation::HostCached:
            return VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
        case MemoryLocation::Auto:
        default:
            return VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    }
}

DirectAllocator::AllocationRecord* DirectAllocator::GetRecord(AllocationHandle handle) {
    auto it = allocations_.find(handle);
    return (it != allocations_.end()) ? &it->second : nullptr;
}

const DirectAllocator::AllocationRecord* DirectAllocator::GetRecord(AllocationHandle handle) const {
    auto it = allocations_.find(handle);
    return (it != allocations_.end()) ? &it->second : nullptr;
}

// Factory implementation
std::unique_ptr<IMemoryAllocator> MemoryAllocatorFactory::CreateDirectAllocator(
    VkPhysicalDevice physicalDevice,
    VkDevice device,
    ResourceBudgetManager* budgetManager)
{
    return std::make_unique<DirectAllocator>(physicalDevice, device, budgetManager);
}

// Note: CreateVMAAllocator is implemented in VMAAllocator.cpp

} // namespace Vixen::RenderGraph
