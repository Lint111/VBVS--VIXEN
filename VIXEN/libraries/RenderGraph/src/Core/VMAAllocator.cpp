/**
 * @file VMAAllocator.cpp
 * @brief VMA-based memory allocator implementation
 *
 * This file defines VMA_IMPLEMENTATION, so vk_mem_alloc.h will compile
 * the VMA implementation here. Only one .cpp file should do this.
 */

#define VMA_IMPLEMENTATION
#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1

#include <vk_mem_alloc.h>

#include "Core/VMAAllocator.h"

namespace Vixen::RenderGraph {

VMAAllocator::VMAAllocator(
    VkInstance instance,
    VkPhysicalDevice physicalDevice,
    VkDevice device,
    ResourceBudgetManager* budgetManager)
    : device_(device)
    , budgetManager_(budgetManager)
{
    // VMA requires valid Vulkan handles - skip creation if any are null
    if (instance == VK_NULL_HANDLE ||
        physicalDevice == VK_NULL_HANDLE ||
        device == VK_NULL_HANDLE) {
        allocator_ = nullptr;
        return;
    }

    VmaVulkanFunctions vulkanFunctions{};
    vulkanFunctions.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
    vulkanFunctions.vkGetDeviceProcAddr = vkGetDeviceProcAddr;

    VmaAllocatorCreateInfo allocatorInfo{};
    allocatorInfo.flags = VMA_ALLOCATOR_CREATE_EXT_MEMORY_BUDGET_BIT;
    allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_3;
    allocatorInfo.physicalDevice = physicalDevice;
    allocatorInfo.device = device;
    allocatorInfo.instance = instance;
    allocatorInfo.pVulkanFunctions = &vulkanFunctions;

    VkResult result = vmaCreateAllocator(&allocatorInfo, &allocator_);
    if (result != VK_SUCCESS) {
        allocator_ = nullptr;
    }
}

VMAAllocator::~VMAAllocator() {
    if (allocator_) {
        // Verify all allocations were freed
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!allocationRecords_.empty()) {
                // Log warning about leaked allocations
                // In production, this indicates a resource leak
            }
        }

        vmaDestroyAllocator(allocator_);
        allocator_ = nullptr;
    }
}

std::expected<BufferAllocation, AllocationError>
VMAAllocator::AllocateBuffer(const BufferAllocationRequest& request) {
    if (!allocator_) {
        return std::unexpected(AllocationError::Unknown);
    }

    if (request.size == 0) {
        return std::unexpected(AllocationError::InvalidParameters);
    }

    // Check budget before allocating
    if (budgetManager_) {
        if (!budgetManager_->TryAllocate(BudgetResourceType::DeviceMemory, request.size)) {
            return std::unexpected(AllocationError::OverBudget);
        }
    }

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = request.size;
    bufferInfo.usage = request.usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo{};

    // Map MemoryLocation to VMA usage
    switch (request.location) {
        case MemoryLocation::DeviceLocal:
            allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
            break;
        case MemoryLocation::HostVisible:
            allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
            allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                             VMA_ALLOCATION_CREATE_MAPPED_BIT;
            break;
        case MemoryLocation::HostCached:
            allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
            allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT |
                             VMA_ALLOCATION_CREATE_MAPPED_BIT;
            break;
        case MemoryLocation::Auto:
        default:
            allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
            break;
    }

    if (request.dedicated) {
        allocInfo.flags |= VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
    }

    if (request.allowAliasing) {
        allocInfo.flags |= VMA_ALLOCATION_CREATE_CAN_ALIAS_BIT;
    }

    VkBuffer buffer = VK_NULL_HANDLE;
    VmaAllocation allocation = nullptr;
    VmaAllocationInfo allocationInfo{};

    VkResult result = vmaCreateBuffer(
        allocator_,
        &bufferInfo,
        &allocInfo,
        &buffer,
        &allocation,
        &allocationInfo);

    if (result != VK_SUCCESS) {
        AllocationError error = AllocationError::Unknown;
        if (result == VK_ERROR_OUT_OF_DEVICE_MEMORY) {
            error = AllocationError::OutOfDeviceMemory;
        } else if (result == VK_ERROR_OUT_OF_HOST_MEMORY) {
            error = AllocationError::OutOfHostMemory;
        }
        return std::unexpected(error);
    }

    // Set debug name if provided
    if (!request.debugName.empty()) {
        vmaSetAllocationName(allocator_, allocation, std::string(request.debugName).c_str());
    }

    // Track allocation
    {
        std::lock_guard<std::mutex> lock(mutex_);
        allocationRecords_[static_cast<void*>(allocation)] = AllocationRecord{
            .vmaAllocation = allocation,
            .size = allocationInfo.size,
            .isMapped = (allocationInfo.pMappedData != nullptr),
            .canAlias = request.allowAliasing,
            .isAliased = false
        };
    }

    // Report to budget manager
    if (budgetManager_) {
        budgetManager_->RecordAllocation(BudgetResourceType::DeviceMemory, allocationInfo.size);
    }

    return BufferAllocation{
        .buffer = buffer,
        .allocation = static_cast<AllocationHandle>(allocation),
        .size = allocationInfo.size,
        .offset = allocationInfo.offset,
        .mappedData = allocationInfo.pMappedData,
        .canAlias = request.allowAliasing,
        .isAliased = false
    };
}

void VMAAllocator::FreeBuffer(BufferAllocation& allocation) {
    if (!allocator_ || !allocation.buffer) {
        return;
    }

    VmaAllocation vmaAlloc = static_cast<VmaAllocation>(allocation.allocation);

    // Get size for budget tracking before freeing
    VkDeviceSize size = 0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = allocationRecords_.find(allocation.allocation);
        if (it != allocationRecords_.end()) {
            size = it->second.size;
            allocationRecords_.erase(it);
        }
    }

    vmaDestroyBuffer(allocator_, allocation.buffer, vmaAlloc);

    // Report deallocation to budget manager
    if (budgetManager_ && size > 0) {
        budgetManager_->RecordDeallocation(BudgetResourceType::DeviceMemory, size);
    }

    // Invalidate the allocation struct
    allocation.buffer = VK_NULL_HANDLE;
    allocation.allocation = nullptr;
    allocation.size = 0;
    allocation.mappedData = nullptr;
}

std::expected<ImageAllocation, AllocationError>
VMAAllocator::AllocateImage(const ImageAllocationRequest& request) {
    if (!allocator_) {
        return std::unexpected(AllocationError::Unknown);
    }

    // Validate image create info
    if (request.createInfo.sType != VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO) {
        return std::unexpected(AllocationError::InvalidParameters);
    }

    // Calculate approximate image size for budget check
    VkDeviceSize estimatedSize = request.createInfo.extent.width *
                                  request.createInfo.extent.height *
                                  request.createInfo.extent.depth * 4; // Approximate bytes per pixel

    if (budgetManager_) {
        if (!budgetManager_->TryAllocate(BudgetResourceType::DeviceMemory, estimatedSize)) {
            return std::unexpected(AllocationError::OverBudget);
        }
    }

    VmaAllocationCreateInfo allocInfo{};

    // Map MemoryLocation to VMA usage
    switch (request.location) {
        case MemoryLocation::DeviceLocal:
            allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
            break;
        case MemoryLocation::HostVisible:
        case MemoryLocation::HostCached:
            allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
            allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;
            break;
        case MemoryLocation::Auto:
        default:
            allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
            break;
    }

    if (request.dedicated) {
        allocInfo.flags |= VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
    }

    if (request.allowAliasing) {
        allocInfo.flags |= VMA_ALLOCATION_CREATE_CAN_ALIAS_BIT;
    }

    VkImage image = VK_NULL_HANDLE;
    VmaAllocation allocation = nullptr;
    VmaAllocationInfo allocationInfo{};

    VkResult result = vmaCreateImage(
        allocator_,
        &request.createInfo,
        &allocInfo,
        &image,
        &allocation,
        &allocationInfo);

    if (result != VK_SUCCESS) {
        AllocationError error = AllocationError::Unknown;
        if (result == VK_ERROR_OUT_OF_DEVICE_MEMORY) {
            error = AllocationError::OutOfDeviceMemory;
        } else if (result == VK_ERROR_OUT_OF_HOST_MEMORY) {
            error = AllocationError::OutOfHostMemory;
        }
        return std::unexpected(error);
    }

    // Set debug name if provided
    if (!request.debugName.empty()) {
        vmaSetAllocationName(allocator_, allocation, std::string(request.debugName).c_str());
    }

    // Track allocation
    {
        std::lock_guard<std::mutex> lock(mutex_);
        allocationRecords_[static_cast<void*>(allocation)] = AllocationRecord{
            .vmaAllocation = allocation,
            .size = allocationInfo.size,
            .isMapped = false,
            .canAlias = request.allowAliasing,
            .isAliased = false
        };
    }

    // Report actual size to budget manager
    if (budgetManager_) {
        budgetManager_->RecordAllocation(BudgetResourceType::DeviceMemory, allocationInfo.size);
    }

    return ImageAllocation{
        .image = image,
        .allocation = static_cast<AllocationHandle>(allocation),
        .size = allocationInfo.size,
        .canAlias = request.allowAliasing,
        .isAliased = false
    };
}

void VMAAllocator::FreeImage(ImageAllocation& allocation) {
    if (!allocator_ || !allocation.image) {
        return;
    }

    VmaAllocation vmaAlloc = static_cast<VmaAllocation>(allocation.allocation);

    // Get size for budget tracking before freeing
    VkDeviceSize size = 0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = allocationRecords_.find(allocation.allocation);
        if (it != allocationRecords_.end()) {
            size = it->second.size;
            allocationRecords_.erase(it);
        }
    }

    vmaDestroyImage(allocator_, allocation.image, vmaAlloc);

    // Report deallocation to budget manager
    if (budgetManager_ && size > 0) {
        budgetManager_->RecordDeallocation(BudgetResourceType::DeviceMemory, size);
    }

    // Invalidate the allocation struct
    allocation.image = VK_NULL_HANDLE;
    allocation.allocation = nullptr;
    allocation.size = 0;
}

void* VMAAllocator::MapBuffer(const BufferAllocation& allocation) {
    if (!allocator_ || !allocation.allocation) {
        return nullptr;
    }

    // If already mapped (persistent mapping), return existing pointer
    if (allocation.mappedData) {
        return allocation.mappedData;
    }

    VmaAllocation vmaAlloc = static_cast<VmaAllocation>(allocation.allocation);
    void* mappedData = nullptr;

    VkResult result = vmaMapMemory(allocator_, vmaAlloc, &mappedData);
    if (result != VK_SUCCESS) {
        return nullptr;
    }

    // Update tracking
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = allocationRecords_.find(allocation.allocation);
        if (it != allocationRecords_.end()) {
            it->second.isMapped = true;
        }
    }

    return mappedData;
}

void VMAAllocator::UnmapBuffer(const BufferAllocation& allocation) {
    if (!allocator_ || !allocation.allocation) {
        return;
    }

    // Don't unmap persistently mapped buffers
    if (allocation.mappedData) {
        return;
    }

    VmaAllocation vmaAlloc = static_cast<VmaAllocation>(allocation.allocation);
    vmaUnmapMemory(allocator_, vmaAlloc);

    // Update tracking
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = allocationRecords_.find(allocation.allocation);
        if (it != allocationRecords_.end()) {
            it->second.isMapped = false;
        }
    }
}

void VMAAllocator::FlushMappedRange(
    const BufferAllocation& allocation,
    VkDeviceSize offset,
    VkDeviceSize size)
{
    if (!allocator_ || !allocation.allocation) {
        return;
    }

    VmaAllocation vmaAlloc = static_cast<VmaAllocation>(allocation.allocation);
    vmaFlushAllocation(allocator_, vmaAlloc, offset, size);
}

void VMAAllocator::InvalidateMappedRange(
    const BufferAllocation& allocation,
    VkDeviceSize offset,
    VkDeviceSize size)
{
    if (!allocator_ || !allocation.allocation) {
        return;
    }

    VmaAllocation vmaAlloc = static_cast<VmaAllocation>(allocation.allocation);
    vmaInvalidateAllocation(allocator_, vmaAlloc, offset, size);
}

AllocationStats VMAAllocator::GetStats() const {
    if (!allocator_) {
        return AllocationStats{};
    }

    VmaTotalStatistics stats{};
    vmaCalculateStatistics(allocator_, &stats);

    const auto& total = stats.total;

    AllocationStats result{};
    result.totalAllocatedBytes = total.statistics.blockBytes;
    result.totalUsedBytes = total.statistics.allocationBytes;
    result.allocationCount = total.statistics.allocationCount;
    result.blockCount = total.statistics.blockCount;

    // Calculate fragmentation ratio
    if (result.totalAllocatedBytes > 0) {
        result.fragmentationRatio = 1.0f -
            (static_cast<float>(result.totalUsedBytes) / static_cast<float>(result.totalAllocatedBytes));
    }

    return result;
}

void VMAAllocator::SetBudgetManager(ResourceBudgetManager* budgetManager) {
    std::lock_guard<std::mutex> lock(mutex_);
    budgetManager_ = budgetManager;
}

ResourceBudgetManager* VMAAllocator::GetBudgetManager() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return budgetManager_;
}

VMAAllocator::AllocationRecord* VMAAllocator::GetRecord(AllocationHandle handle) {
    auto it = allocationRecords_.find(handle);
    return it != allocationRecords_.end() ? &it->second : nullptr;
}

const VMAAllocator::AllocationRecord* VMAAllocator::GetRecord(AllocationHandle handle) const {
    auto it = allocationRecords_.find(handle);
    return it != allocationRecords_.end() ? &it->second : nullptr;
}

// ============================================================================
// Aliased Allocations (Sprint 4 Phase B+)
// ============================================================================

std::expected<BufferAllocation, AllocationError>
VMAAllocator::CreateAliasedBuffer(const AliasedBufferRequest& request) {
    if (!allocator_) {
        return std::unexpected(AllocationError::Unknown);
    }

    if (request.size == 0 || !request.sourceAllocation) {
        return std::unexpected(AllocationError::InvalidParameters);
    }

    // Verify source allocation supports aliasing
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto* record = GetRecord(request.sourceAllocation);
        if (!record || !record->canAlias) {
            return std::unexpected(AllocationError::InvalidParameters);
        }

        // Verify size fits within source allocation
        if (request.offsetInAllocation + request.size > record->size) {
            return std::unexpected(AllocationError::InvalidParameters);
        }
    }

    // Create buffer without allocating new memory
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = request.size;
    bufferInfo.usage = request.usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkBuffer buffer = VK_NULL_HANDLE;
    VkResult result = vkCreateBuffer(device_, &bufferInfo, nullptr, &buffer);
    if (result != VK_SUCCESS) {
        return std::unexpected(AllocationError::Unknown);
    }

    // Bind to existing allocation's memory
    VmaAllocation sourceVma = static_cast<VmaAllocation>(request.sourceAllocation);
    VmaAllocationInfo sourceInfo{};
    vmaGetAllocationInfo(allocator_, sourceVma, &sourceInfo);

    result = vkBindBufferMemory(device_, buffer, sourceInfo.deviceMemory,
                                 sourceInfo.offset + request.offsetInAllocation);
    if (result != VK_SUCCESS) {
        vkDestroyBuffer(device_, buffer, nullptr);
        return std::unexpected(AllocationError::Unknown);
    }

    // Set debug name if provided
    if (!request.debugName.empty() && device_ != VK_NULL_HANDLE) {
        VkDebugUtilsObjectNameInfoEXT nameInfo{};
        nameInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
        nameInfo.objectType = VK_OBJECT_TYPE_BUFFER;
        nameInfo.objectHandle = reinterpret_cast<uint64_t>(buffer);
        nameInfo.pObjectName = std::string(request.debugName).c_str();
        // Note: vkSetDebugUtilsObjectNameEXT may not be available
    }

    // Note: Aliased buffers share the source allocation, don't count separately in budget
    // The memory was already counted when the source was allocated

    return BufferAllocation{
        .buffer = buffer,
        .allocation = request.sourceAllocation,  // Share source allocation handle
        .size = request.size,
        .offset = request.offsetInAllocation,
        .mappedData = nullptr,  // Mapping must go through source
        .canAlias = true,
        .isAliased = true
    };
}

std::expected<ImageAllocation, AllocationError>
VMAAllocator::CreateAliasedImage(const AliasedImageRequest& request) {
    if (!allocator_) {
        return std::unexpected(AllocationError::Unknown);
    }

    if (!request.sourceAllocation) {
        return std::unexpected(AllocationError::InvalidParameters);
    }

    // Verify source allocation supports aliasing
    VkDeviceSize sourceSize = 0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto* record = GetRecord(request.sourceAllocation);
        if (!record || !record->canAlias) {
            return std::unexpected(AllocationError::InvalidParameters);
        }
        sourceSize = record->size;
    }

    // Create image without allocating new memory
    VkImage image = VK_NULL_HANDLE;
    VkResult result = vkCreateImage(device_, &request.createInfo, nullptr, &image);
    if (result != VK_SUCCESS) {
        return std::unexpected(AllocationError::Unknown);
    }

    // Get memory requirements
    VkMemoryRequirements memReq;
    vkGetImageMemoryRequirements(device_, image, &memReq);

    // Verify size fits
    if (request.offsetInAllocation + memReq.size > sourceSize) {
        vkDestroyImage(device_, image, nullptr);
        return std::unexpected(AllocationError::InvalidParameters);
    }

    // Bind to existing allocation's memory
    VmaAllocation sourceVma = static_cast<VmaAllocation>(request.sourceAllocation);
    VmaAllocationInfo sourceInfo{};
    vmaGetAllocationInfo(allocator_, sourceVma, &sourceInfo);

    result = vkBindImageMemory(device_, image, sourceInfo.deviceMemory,
                                sourceInfo.offset + request.offsetInAllocation);
    if (result != VK_SUCCESS) {
        vkDestroyImage(device_, image, nullptr);
        return std::unexpected(AllocationError::Unknown);
    }

    return ImageAllocation{
        .image = image,
        .allocation = request.sourceAllocation,
        .size = memReq.size,
        .canAlias = true,
        .isAliased = true
    };
}

bool VMAAllocator::SupportsAliasing(AllocationHandle allocation) const {
    if (!allocation) {
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    auto* record = GetRecord(allocation);
    return record && record->canAlias;
}

// Factory implementation
std::unique_ptr<IMemoryAllocator> MemoryAllocatorFactory::CreateVMAAllocator(
    VkInstance instance,
    VkPhysicalDevice physicalDevice,
    VkDevice device,
    ResourceBudgetManager* budgetManager)
{
    auto allocator = std::make_unique<VMAAllocator>(instance, physicalDevice, device, budgetManager);
    if (!allocator->IsValid()) {
        return nullptr;
    }
    return allocator;
}

} // namespace Vixen::RenderGraph
