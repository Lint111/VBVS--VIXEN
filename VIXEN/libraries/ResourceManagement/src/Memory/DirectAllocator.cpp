#include "Memory/DirectAllocator.h"
#include <stdexcept>
#include <cstring>

namespace ResourceManagement {

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
    // Free any remaining allocations (teardown: no concurrent users by contract)
    allocations_.ForEachSlot([this](AllocationRecord& record) {
        if (record.live.exchange(false) && record.memory != VK_NULL_HANDLE && device_ != VK_NULL_HANDLE) {
            vkFreeMemory(device_, record.memory, nullptr);
        }
    });
}

DirectAllocator::AllocationRecord* DirectAllocator::AcquireRecord(
    VkDeviceMemory memory, VkDeviceSize size, uint32_t memoryTypeIndex, bool canAlias, bool isAliased)
{
    AllocationRecord* record = allocations_.Acquire();
    if (!record) return nullptr;
    record->memory = memory;
    record->size = size;
    record->memoryTypeIndex = memoryTypeIndex;
    record->isMapped = false;
    record->mappedPtr = nullptr;
    record->canAlias = canAlias;
    record->isAliased = isAliased;
    record->live.store(true, std::memory_order_release);
    return record;
}

bool DirectAllocator::ClaimForFree(AllocationHandle handle) {
    // Owns() never dereferences: an invalid handle is rejected before its memory is touched.
    if (!allocations_.Owns(handle)) return false;
    return static_cast<AllocationRecord*>(handle)->live.exchange(false, std::memory_order_acq_rel);
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
    if (auto* budget = Budget()) {
        if (!budget->TryAllocate(BudgetResourceType::DeviceMemory, request.size)) {
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

    // Check if buffer requires device address (for RT buffers)
    const bool needsDeviceAddress = (request.usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) != 0;

    // Allocate memory
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReq.size;
    allocInfo.memoryTypeIndex = memTypeIndex;

    // Enable device address if needed
    VkMemoryAllocateFlagsInfo flagsInfo{};
    if (needsDeviceAddress) {
        flagsInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
        flagsInfo.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
        allocInfo.pNext = &flagsInfo;
    }

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

    // Get device address if needed
    VkDeviceAddress deviceAddress = 0;
    if (needsDeviceAddress) {
        VkBufferDeviceAddressInfo addressInfo{};
        addressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
        addressInfo.buffer = buffer;
        deviceAddress = vkGetBufferDeviceAddress(device_, &addressInfo);
    }

    // Create allocation record (the record is the handle)
    AllocationRecord* record = AcquireRecord(memory, memReq.size, memTypeIndex, request.allowAliasing, false);
    if (!record) {
        vkFreeMemory(device_, memory, nullptr);
        vkDestroyBuffer(device_, buffer, nullptr);
        return std::unexpected(AllocationError::OutOfHostMemory);
    }

    // Track allocation
    totalAllocatedBytes_.fetch_add(memReq.size, std::memory_order_relaxed);
    totalUsedBytes_.fetch_add(request.size, std::memory_order_relaxed);
    allocationCount_.fetch_add(1, std::memory_order_relaxed);
    blockCount_.fetch_add(1, std::memory_order_relaxed);

    // Record to budget manager
    if (auto* budget = Budget()) {
        budget->RecordAllocation(BudgetResourceType::DeviceMemory, memReq.size);
    }

    return BufferAllocation{
        .buffer = buffer,
        .allocation = record,
        .size = memReq.size,
        .offset = 0,
        .mappedData = nullptr,
        .deviceAddress = deviceAddress,
        .canAlias = request.allowAliasing,
        .isAliased = false
    };
}

void DirectAllocator::FreeBuffer(BufferAllocation& allocation) {
    if (!allocation.buffer || !allocation.allocation) {
        return;
    }

    // Claim the handle first (audit V-N14): only the one caller whose exchange clears `live` may read
    // the record or free its memory; an invalid or already-freed handle returns without a deref.
    if (!ClaimForFree(allocation.allocation)) {
        return;
    }
    AllocationRecord* record = static_cast<AllocationRecord*>(allocation.allocation);
    const VkDeviceSize freedSize = record->size;
    const VkDeviceMemory freedMemory = record->memory;

    // Unmap if mapped
    if (record->isMapped && device_ != VK_NULL_HANDLE) {
        vkUnmapMemory(device_, record->memory);
    }

    totalAllocatedBytes_.fetch_sub(freedSize, std::memory_order_relaxed);
    totalUsedBytes_.fetch_sub(allocation.size, std::memory_order_relaxed);
    allocationCount_.fetch_sub(1, std::memory_order_relaxed);
    blockCount_.fetch_sub(1, std::memory_order_relaxed);

    // Free Vulkan resources
    if (device_ != VK_NULL_HANDLE) {
        vkDestroyBuffer(device_, allocation.buffer, nullptr);
        if (freedMemory != VK_NULL_HANDLE) {
            vkFreeMemory(device_, freedMemory, nullptr);
        }
    }

    // Record to budget manager
    if (auto* budget = Budget(); budget && freedSize > 0) {
        budget->RecordDeallocation(BudgetResourceType::DeviceMemory, freedSize);
    }

    allocations_.Release(record);

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
    if (auto* budget = Budget()) {
        if (!budget->TryAllocate(BudgetResourceType::DeviceMemory, memReq.size)) {
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

    // Create record (the record is the handle)
    AllocationRecord* record = AcquireRecord(memory, memReq.size, memTypeIndex, request.allowAliasing, false);
    if (!record) {
        vkFreeMemory(device_, memory, nullptr);
        vkDestroyImage(device_, image, nullptr);
        return std::unexpected(AllocationError::OutOfHostMemory);
    }

    totalAllocatedBytes_.fetch_add(memReq.size, std::memory_order_relaxed);
    totalUsedBytes_.fetch_add(memReq.size, std::memory_order_relaxed);
    allocationCount_.fetch_add(1, std::memory_order_relaxed);
    blockCount_.fetch_add(1, std::memory_order_relaxed);

    if (auto* budget = Budget()) {
        budget->RecordAllocation(BudgetResourceType::DeviceMemory, memReq.size);
    }

    return ImageAllocation{
        .image = image,
        .allocation = record,
        .size = memReq.size,
        .canAlias = request.allowAliasing,
        .isAliased = false
    };
}

void DirectAllocator::FreeImage(ImageAllocation& allocation) {
    if (!allocation.image || !allocation.allocation) {
        return;
    }

    // Same discipline as FreeBuffer (audit V-N14): claim the handle before any deref.
    if (!ClaimForFree(allocation.allocation)) {
        return;
    }
    AllocationRecord* record = static_cast<AllocationRecord*>(allocation.allocation);
    const VkDeviceSize freedSize = record->size;
    const VkDeviceMemory freedMemory = record->memory;

    totalAllocatedBytes_.fetch_sub(freedSize, std::memory_order_relaxed);
    totalUsedBytes_.fetch_sub(allocation.size, std::memory_order_relaxed);
    allocationCount_.fetch_sub(1, std::memory_order_relaxed);
    blockCount_.fetch_sub(1, std::memory_order_relaxed);

    if (device_ != VK_NULL_HANDLE) {
        vkDestroyImage(device_, allocation.image, nullptr);
        if (freedMemory != VK_NULL_HANDLE) {
            vkFreeMemory(device_, freedMemory, nullptr);
        }
    }

    if (auto* budget = Budget(); budget && freedSize > 0) {
        budget->RecordDeallocation(BudgetResourceType::DeviceMemory, freedSize);
    }

    allocations_.Release(record);

    allocation.image = VK_NULL_HANDLE;
    allocation.allocation = nullptr;
}

void* DirectAllocator::MapBuffer(const BufferAllocation& allocation) {
    if (!allocation.allocation) {
        return nullptr;
    }

    // Mapping one VkDeviceMemory is externally synchronised by Vulkan's own rules, so the map
    // state needs no lock: the caller owns the allocation while it maps, unmaps or flushes it.
    AllocationRecord* record = GetRecord(allocation.allocation);
    if (!record) {
        return nullptr;
    }

    if (record->isMapped) {
        return record->mappedPtr;
    }

    void* data = nullptr;
    VkResult result = vkMapMemory(device_, record->memory, 0, record->size, 0, &data);
    if (result != VK_SUCCESS) {
        return nullptr;
    }

    record->isMapped = true;
    record->mappedPtr = data;
    return data;
}

void DirectAllocator::UnmapBuffer(const BufferAllocation& allocation) {
    if (!allocation.allocation) {
        return;
    }

    AllocationRecord* record = GetRecord(allocation.allocation);
    if (!record || !record->isMapped) {
        return;
    }

    vkUnmapMemory(device_, record->memory);
    record->isMapped = false;
    record->mappedPtr = nullptr;
}

void DirectAllocator::FlushMappedRange(
    const BufferAllocation& allocation,
    VkDeviceSize offset,
    VkDeviceSize size)
{
    if (!allocation.allocation || device_ == VK_NULL_HANDLE) {
        return;
    }

    AllocationRecord* record = GetRecord(allocation.allocation);
    if (!record || !record->isMapped) {
        return;
    }

    VkMappedMemoryRange range{};
    range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
    range.memory = record->memory;
    range.offset = offset;
    range.size = (size == VK_WHOLE_SIZE) ? record->size : size;

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

    AllocationRecord* record = GetRecord(allocation.allocation);
    if (!record || !record->isMapped) {
        return;
    }

    VkMappedMemoryRange range{};
    range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
    range.memory = record->memory;
    range.offset = offset;
    range.size = (size == VK_WHOLE_SIZE) ? record->size : size;

    vkInvalidateMappedMemoryRanges(device_, 1, &range);
}

AllocationStats DirectAllocator::GetStats() const {
    AllocationStats result;
    result.totalAllocatedBytes = totalAllocatedBytes_.load(std::memory_order_relaxed);
    result.totalUsedBytes = totalUsedBytes_.load(std::memory_order_relaxed);
    result.allocationCount = allocationCount_.load(std::memory_order_relaxed);
    result.blockCount = blockCount_.load(std::memory_order_relaxed);

    // Calculate fragmentation
    if (result.totalAllocatedBytes > 0) {
        result.fragmentationRatio = 1.0f -
            (static_cast<float>(result.totalUsedBytes) /
             static_cast<float>(result.totalAllocatedBytes));
    }

    return result;
}

void DirectAllocator::SetBudgetManager(ResourceBudgetManager* budgetManager) {
    budgetManager_.store(budgetManager, std::memory_order_release);
}

ResourceBudgetManager* DirectAllocator::GetBudgetManager() const {
    return Budget();
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
    if (!allocations_.Owns(handle)) return nullptr;
    auto* record = static_cast<AllocationRecord*>(handle);
    return record->live.load(std::memory_order_acquire) ? record : nullptr;
}

const DirectAllocator::AllocationRecord* DirectAllocator::GetRecord(AllocationHandle handle) const {
    if (!allocations_.Owns(handle)) return nullptr;
    const auto* record = static_cast<const AllocationRecord*>(handle);
    return record->live.load(std::memory_order_acquire) ? record : nullptr;
}

// ============================================================================
// Aliased Allocations (Sprint 4 Phase B+)
// ============================================================================

std::expected<BufferAllocation, AllocationError>
DirectAllocator::CreateAliasedBuffer(const AliasedBufferRequest& request) {
    if (device_ == VK_NULL_HANDLE) {
        return std::unexpected(AllocationError::InvalidParameters);
    }

    if (request.size == 0 || !request.sourceAllocation) {
        return std::unexpected(AllocationError::InvalidParameters);
    }

    // Verify source allocation supports aliasing. Copy the fields we need (audit V-M13): GetRecord
    // returns a pointer into allocations_'s stored value, invalidated by a concurrent FreeBuffer()
    // erasing that entry — so nothing derived from the pointer may survive past this lock scope.
    VkDeviceMemory sourceMemory = VK_NULL_HANDLE;
    {
        const AllocationRecord* sourceRecord = GetRecord(request.sourceAllocation);
        if (!sourceRecord || !sourceRecord->canAlias) {
            return std::unexpected(AllocationError::InvalidParameters);
        }

        // Verify size fits within source allocation
        if (request.offsetInAllocation + request.size > sourceRecord->size) {
            return std::unexpected(AllocationError::InvalidParameters);
        }

        sourceMemory = sourceRecord->memory;
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
        return std::unexpected(AllocationError::Unknown);
    }

    // Bind to existing allocation's memory
    result = vkBindBufferMemory(device_, buffer, sourceMemory, request.offsetInAllocation);
    if (result != VK_SUCCESS) {
        vkDestroyBuffer(device_, buffer, nullptr);
        return std::unexpected(AllocationError::Unknown);
    }

    // Note: Aliased buffers share the source allocation, don't count separately in budget

    // Get device address if usage requires it
    VkDeviceAddress deviceAddress = 0;
    if (request.usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) {
        VkBufferDeviceAddressInfo addressInfo{};
        addressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
        addressInfo.buffer = buffer;
        deviceAddress = vkGetBufferDeviceAddress(device_, &addressInfo);
    }

    return BufferAllocation{
        .buffer = buffer,
        .allocation = request.sourceAllocation,
        .size = request.size,
        .offset = request.offsetInAllocation,
        .mappedData = nullptr,
        .deviceAddress = deviceAddress,
        .canAlias = true,
        .isAliased = true
    };
}

std::expected<ImageAllocation, AllocationError>
DirectAllocator::CreateAliasedImage(const AliasedImageRequest& request) {
    if (device_ == VK_NULL_HANDLE) {
        return std::unexpected(AllocationError::InvalidParameters);
    }

    if (!request.sourceAllocation) {
        return std::unexpected(AllocationError::InvalidParameters);
    }

    // Verify source allocation supports aliasing. Copy the fields we need (audit V-M13): GetRecord
    // returns a pointer into allocations_'s stored value, invalidated by a concurrent FreeImage()/
    // FreeBuffer() erasing that entry — so nothing derived from the pointer may survive past this
    // lock scope.
    VkDeviceMemory sourceMemory = VK_NULL_HANDLE;
    VkDeviceSize sourceSize = 0;
    {
        const AllocationRecord* sourceRecord = GetRecord(request.sourceAllocation);
        if (!sourceRecord || !sourceRecord->canAlias) {
            return std::unexpected(AllocationError::InvalidParameters);
        }
        sourceMemory = sourceRecord->memory;
        sourceSize = sourceRecord->size;
    }

    // Create image
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
    result = vkBindImageMemory(device_, image, sourceMemory, request.offsetInAllocation);
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

bool DirectAllocator::SupportsAliasing(AllocationHandle allocation) const {
    if (!allocation) {
        return false;
    }

    const auto* record = GetRecord(allocation);
    return record && record->canAlias;
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

} // namespace ResourceManagement
