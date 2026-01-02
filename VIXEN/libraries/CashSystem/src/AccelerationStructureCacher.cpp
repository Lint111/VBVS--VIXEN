#include "pch.h"
#include "AccelerationStructureCacher.h"
#include "VulkanDevice.h"
#include "error/VulkanError.h"

#include <cstring>
#include <stdexcept>
#include <sstream>

namespace CashSystem {

// ============================================================================
// VOXEL AABB DATA - CLEANUP
// ============================================================================

void VoxelAABBData::Cleanup(VkDevice device) {
    if (device == VK_NULL_HANDLE) {
        return;
    }

    if (aabbBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, aabbBuffer, nullptr);
        aabbBuffer = VK_NULL_HANDLE;
    }
    if (aabbBufferMemory != VK_NULL_HANDLE) {
        vkFreeMemory(device, aabbBufferMemory, nullptr);
        aabbBufferMemory = VK_NULL_HANDLE;
    }
    if (materialIdBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, materialIdBuffer, nullptr);
        materialIdBuffer = VK_NULL_HANDLE;
    }
    if (materialIdBufferMemory != VK_NULL_HANDLE) {
        vkFreeMemory(device, materialIdBufferMemory, nullptr);
        materialIdBufferMemory = VK_NULL_HANDLE;
    }
    if (brickMappingBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, brickMappingBuffer, nullptr);
        brickMappingBuffer = VK_NULL_HANDLE;
    }
    if (brickMappingBufferMemory != VK_NULL_HANDLE) {
        vkFreeMemory(device, brickMappingBufferMemory, nullptr);
        brickMappingBufferMemory = VK_NULL_HANDLE;
    }

    aabbCount = 0;
    aabbBufferSize = 0;
    materialIdBufferSize = 0;
    brickMappingBufferSize = 0;
}

// ============================================================================
// ACCELERATION STRUCTURE DATA - CLEANUP
// ============================================================================

void AccelerationStructureData::Cleanup(VkDevice device) {
    if (device == VK_NULL_HANDLE) {
        return;
    }

    // Load destroy function if needed
    auto destroyAS = reinterpret_cast<PFN_vkDestroyAccelerationStructureKHR>(
        vkGetDeviceProcAddr(device, "vkDestroyAccelerationStructureKHR")
    );

    if (destroyAS) {
        if (blas != VK_NULL_HANDLE) {
            destroyAS(device, blas, nullptr);
            blas = VK_NULL_HANDLE;
        }
        if (tlas != VK_NULL_HANDLE) {
            destroyAS(device, tlas, nullptr);
            tlas = VK_NULL_HANDLE;
        }
    }

    // Destroy BLAS resources
    if (blasBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, blasBuffer, nullptr);
        blasBuffer = VK_NULL_HANDLE;
    }
    if (blasMemory != VK_NULL_HANDLE) {
        vkFreeMemory(device, blasMemory, nullptr);
        blasMemory = VK_NULL_HANDLE;
    }

    // Destroy TLAS resources
    if (tlasBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, tlasBuffer, nullptr);
        tlasBuffer = VK_NULL_HANDLE;
    }
    if (tlasMemory != VK_NULL_HANDLE) {
        vkFreeMemory(device, tlasMemory, nullptr);
        tlasMemory = VK_NULL_HANDLE;
    }

    // Destroy instance buffer
    if (instanceBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, instanceBuffer, nullptr);
        instanceBuffer = VK_NULL_HANDLE;
    }
    if (instanceMemory != VK_NULL_HANDLE) {
        vkFreeMemory(device, instanceMemory, nullptr);
        instanceMemory = VK_NULL_HANDLE;
    }

    // Destroy scratch buffer
    if (scratchBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, scratchBuffer, nullptr);
        scratchBuffer = VK_NULL_HANDLE;
    }
    if (scratchMemory != VK_NULL_HANDLE) {
        vkFreeMemory(device, scratchMemory, nullptr);
        scratchMemory = VK_NULL_HANDLE;
    }

    blasDeviceAddress = 0;
    tlasDeviceAddress = 0;
    primitiveCount = 0;
}

// ============================================================================
// CACHED ACCELERATION STRUCTURE - CLEANUP
// ============================================================================

void CachedAccelerationStructure::Cleanup(VkDevice device) {
    // Only clean up the acceleration structure resources we own
    accelStruct.Cleanup(device);
    sourceAABBCount = 0;
}

// ============================================================================
// ACCELERATION STRUCTURE CACHER - PUBLIC API
// ============================================================================

std::shared_ptr<CachedAccelerationStructure> AccelerationStructureCacher::GetOrCreate(const AccelStructCreateInfo& ci) {
    return TypedCacher<CachedAccelerationStructure, AccelStructCreateInfo>::GetOrCreate(ci);
}

// ============================================================================
// ACCELERATION STRUCTURE CACHER - TYPEDCACHER IMPLEMENTATION
// ============================================================================

std::shared_ptr<CachedAccelerationStructure> AccelerationStructureCacher::Create(const AccelStructCreateInfo& ci) {
    LOG_INFO("[AccelerationStructureCacher::Create] Creating acceleration structure");

    if (!IsInitialized()) {
        throw std::runtime_error("[AccelerationStructureCacher::Create] Cacher not initialized with device");
    }

    if (!ci.aabbData || !ci.aabbData->IsValid()) {
        throw std::runtime_error("[AccelerationStructureCacher::Create] AABB data is required and must be valid");
    }

    // Load RT extension functions on first use
    LoadRTFunctions();

    auto cached = std::make_shared<CachedAccelerationStructure>();

    // Store AABB count for IsValid() check (no pointer dependency)
    cached->sourceAABBCount = ci.aabbData->aabbCount;

    LOG_INFO("[AccelerationStructureCacher::Create] Using AABB data with " +
             std::to_string(ci.aabbData->aabbCount) + " AABBs");

    if (ci.aabbData->aabbCount == 0) {
        LOG_INFO("[AccelerationStructureCacher::Create] No AABBs to build AS from");
        return cached;
    }

    // Build BLAS from AABBs
    BuildBLAS(ci, *ci.aabbData, cached->accelStruct);

    // Build TLAS containing single BLAS instance
    BuildTLAS(ci, cached->accelStruct);

    LOG_INFO("[AccelerationStructureCacher::Create] Created AS with " +
             std::to_string(cached->accelStruct.primitiveCount) + " primitives");

    return cached;
}

std::uint64_t AccelerationStructureCacher::ComputeKey(const AccelStructCreateInfo& ci) const {
    return ci.ComputeHash();
}

void AccelerationStructureCacher::Cleanup() {
    LOG_INFO("[AccelerationStructureCacher::Cleanup] Cleaning up cached acceleration structures");

    // Cleanup all cached entries
    for (auto& [key, entry] : m_entries) {
        if (entry.resource) {
            entry.resource->Cleanup(m_device->device);
        }
    }

    // Destroy command pool
    if (m_buildCommandPool != VK_NULL_HANDLE && m_device) {
        vkDestroyCommandPool(m_device->device, m_buildCommandPool, nullptr);
        m_buildCommandPool = VK_NULL_HANDLE;
    }

    // Clear entries
    Clear();

    LOG_INFO("[AccelerationStructureCacher::Cleanup] Cleanup complete");
}

// ============================================================================
// SERIALIZATION
// ============================================================================
// Note: AccelerationStructureCacher deliberately does not persist to disk.
// VkAccelerationStructureKHR objects are device-specific and must be rebuilt.
// AABB data is cached by VoxelAABBCacher separately.
// ============================================================================

bool AccelerationStructureCacher::SerializeToFile(const std::filesystem::path& path) const {
    (void)path;
    return true;
}

bool AccelerationStructureCacher::DeserializeFromFile(const std::filesystem::path& path, void* device) {
    (void)path;
    (void)device;
    return true;
}

// ============================================================================
// PRIVATE HELPER METHODS
// ============================================================================

void AccelerationStructureCacher::LoadRTFunctions() {
    if (m_rtFunctionsLoaded) {
        return;
    }

    VkDevice device = m_device->device;

    vkCreateAccelerationStructureKHR = reinterpret_cast<PFN_vkCreateAccelerationStructureKHR>(
        vkGetDeviceProcAddr(device, "vkCreateAccelerationStructureKHR")
    );
    vkDestroyAccelerationStructureKHR = reinterpret_cast<PFN_vkDestroyAccelerationStructureKHR>(
        vkGetDeviceProcAddr(device, "vkDestroyAccelerationStructureKHR")
    );
    vkGetAccelerationStructureBuildSizesKHR = reinterpret_cast<PFN_vkGetAccelerationStructureBuildSizesKHR>(
        vkGetDeviceProcAddr(device, "vkGetAccelerationStructureBuildSizesKHR")
    );
    vkCmdBuildAccelerationStructuresKHR = reinterpret_cast<PFN_vkCmdBuildAccelerationStructuresKHR>(
        vkGetDeviceProcAddr(device, "vkCmdBuildAccelerationStructuresKHR")
    );
    vkGetAccelerationStructureDeviceAddressKHR = reinterpret_cast<PFN_vkGetAccelerationStructureDeviceAddressKHR>(
        vkGetDeviceProcAddr(device, "vkGetAccelerationStructureDeviceAddressKHR")
    );
    vkGetBufferDeviceAddressKHR = reinterpret_cast<PFN_vkGetBufferDeviceAddressKHR>(
        vkGetDeviceProcAddr(device, "vkGetBufferDeviceAddressKHR")
    );

    m_rtFunctionsLoaded = true;

    LOG_DEBUG("[AccelerationStructureCacher] RT functions loaded: createAS=" +
              std::string(vkCreateAccelerationStructureKHR ? "yes" : "no") +
              ", buildAS=" + std::string(vkCmdBuildAccelerationStructuresKHR ? "yes" : "no"));
}

VkBuildAccelerationStructureFlagsKHR AccelerationStructureCacher::GetBuildFlags(const AccelStructCreateInfo& ci) const {
    VkBuildAccelerationStructureFlagsKHR flags = 0;

    if (ci.preferFastTrace) {
        flags |= VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    } else {
        flags |= VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR;
    }

    if (ci.allowUpdate) {
        flags |= VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR;
    }

    if (ci.allowCompaction) {
        flags |= VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_KHR;
    }

    return flags;
}

// ============================================================================
// BUILD BLAS - Create bottom-level acceleration structure from AABBs
// ============================================================================

void AccelerationStructureCacher::BuildBLAS(const AccelStructCreateInfo& ci, const VoxelAABBData& aabbData, AccelerationStructureData& asData) {
    LOG_INFO("[AccelerationStructureCacher::BuildBLAS] Building BLAS...");
    auto buildStart = std::chrono::high_resolution_clock::now();

    if (!vkCreateAccelerationStructureKHR || !vkGetAccelerationStructureBuildSizesKHR || !vkCmdBuildAccelerationStructuresKHR) {
        LOG_WARNING("[AccelerationStructureCacher::BuildBLAS] RT extension not available - skipping BLAS build");
        return;
    }

    if (aabbData.aabbCount == 0) {
        LOG_INFO("[AccelerationStructureCacher::BuildBLAS] No AABBs - skipping BLAS build");
        return;
    }

    // Get AABB buffer device address
    VkBufferDeviceAddressInfo aabbAddressInfo{};
    aabbAddressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    aabbAddressInfo.buffer = aabbData.aabbBuffer;
    VkDeviceAddress aabbDeviceAddress = vkGetBufferDeviceAddressKHR(m_device->device, &aabbAddressInfo);

    // Setup AABB geometry
    VkAccelerationStructureGeometryAabbsDataKHR aabbsData{};
    aabbsData.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_AABBS_DATA_KHR;
    aabbsData.data.deviceAddress = aabbDeviceAddress;
    aabbsData.stride = sizeof(VoxelAABB);

    VkAccelerationStructureGeometryKHR geometry{};
    geometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    geometry.geometryType = VK_GEOMETRY_TYPE_AABBS_KHR;
    geometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
    geometry.geometry.aabbs = aabbsData;

    // Build info for size query
    VkAccelerationStructureBuildGeometryInfoKHR buildInfo{};
    buildInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    buildInfo.flags = GetBuildFlags(ci);
    buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    buildInfo.geometryCount = 1;
    buildInfo.pGeometries = &geometry;

    // Query build sizes
    uint32_t primitiveCount = aabbData.aabbCount;

    VkAccelerationStructureBuildSizesInfoKHR sizeInfo{};
    sizeInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;

    vkGetAccelerationStructureBuildSizesKHR(
        m_device->device,
        VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
        &buildInfo,
        &primitiveCount,
        &sizeInfo
    );

    LOG_DEBUG("[AccelerationStructureCacher::BuildBLAS] BLAS sizes: AS=" +
              std::to_string(sizeInfo.accelerationStructureSize) +
              ", build=" + std::to_string(sizeInfo.buildScratchSize));

    // Create BLAS buffer
    {
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = sizeInfo.accelerationStructureSize;
        bufferInfo.usage = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR |
                          VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VK_CHECK_LOG(vkCreateBuffer(m_device->device, &bufferInfo, nullptr, &asData.blasBuffer), "Create BLAS buffer");

        VkMemoryRequirements memReq;
        vkGetBufferMemoryRequirements(m_device->device, asData.blasBuffer, &memReq);

        VkMemoryAllocateFlagsInfo allocFlagsInfo{};
        allocFlagsInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
        allocFlagsInfo.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.pNext = &allocFlagsInfo;
        allocInfo.allocationSize = memReq.size;
        allocInfo.memoryTypeIndex = CacherAllocationHelpers::FindMemoryType(m_device->gpuMemoryProperties, memReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        VK_CHECK_LOG(vkAllocateMemory(m_device->device, &allocInfo, nullptr, &asData.blasMemory), "Allocate BLAS memory");
        VK_CHECK_LOG(vkBindBufferMemory(m_device->device, asData.blasBuffer, asData.blasMemory, 0), "Bind BLAS buffer memory");
    }

    // Create scratch buffer
    {
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = sizeInfo.buildScratchSize;
        bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VK_CHECK_LOG(vkCreateBuffer(m_device->device, &bufferInfo, nullptr, &asData.scratchBuffer), "Create scratch buffer");

        VkMemoryRequirements memReq;
        vkGetBufferMemoryRequirements(m_device->device, asData.scratchBuffer, &memReq);

        VkMemoryAllocateFlagsInfo allocFlagsInfo{};
        allocFlagsInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
        allocFlagsInfo.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.pNext = &allocFlagsInfo;
        allocInfo.allocationSize = memReq.size;
        allocInfo.memoryTypeIndex = CacherAllocationHelpers::FindMemoryType(m_device->gpuMemoryProperties, memReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        VK_CHECK_LOG(vkAllocateMemory(m_device->device, &allocInfo, nullptr, &asData.scratchMemory), "Allocate scratch memory");
        VK_CHECK_LOG(vkBindBufferMemory(m_device->device, asData.scratchBuffer, asData.scratchMemory, 0), "Bind scratch buffer memory");
    }

    // Create acceleration structure
    VkAccelerationStructureCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
    createInfo.buffer = asData.blasBuffer;
    createInfo.size = sizeInfo.accelerationStructureSize;
    createInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;

    VK_CHECK_LOG(vkCreateAccelerationStructureKHR(m_device->device, &createInfo, nullptr, &asData.blas), "Create BLAS");

    // Get scratch buffer device address
    VkBufferDeviceAddressInfo scratchAddressInfo{};
    scratchAddressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    scratchAddressInfo.buffer = asData.scratchBuffer;
    VkDeviceAddress scratchAddress = vkGetBufferDeviceAddressKHR(m_device->device, &scratchAddressInfo);

    // Update build info with destination and scratch
    buildInfo.dstAccelerationStructure = asData.blas;
    buildInfo.scratchData.deviceAddress = scratchAddress;

    // Build range info
    VkAccelerationStructureBuildRangeInfoKHR rangeInfo{};
    rangeInfo.primitiveCount = primitiveCount;
    rangeInfo.primitiveOffset = 0;
    rangeInfo.firstVertex = 0;
    rangeInfo.transformOffset = 0;

    const VkAccelerationStructureBuildRangeInfoKHR* pRangeInfo = &rangeInfo;

    // Create command pool if needed
    if (m_buildCommandPool == VK_NULL_HANDLE) {
        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        poolInfo.queueFamilyIndex = m_device->graphicsQueueIndex;
        VK_CHECK_LOG(vkCreateCommandPool(m_device->device, &poolInfo, nullptr, &m_buildCommandPool), "Create command pool (BLAS)");
    }

    // Allocate and record command buffer
    VkCommandBufferAllocateInfo cmdAllocInfo{};
    cmdAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmdAllocInfo.commandPool = m_buildCommandPool;
    cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdAllocInfo.commandBufferCount = 1;

    VkCommandBuffer cmdBuffer;
    VK_CHECK_LOG(vkAllocateCommandBuffers(m_device->device, &cmdAllocInfo, &cmdBuffer), "Allocate command buffers (BLAS)");

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    VK_CHECK_LOG(vkBeginCommandBuffer(cmdBuffer, &beginInfo), "Begin command buffer (BLAS)");
    vkCmdBuildAccelerationStructuresKHR(cmdBuffer, 1, &buildInfo, &pRangeInfo);
    VK_CHECK_LOG(vkEndCommandBuffer(cmdBuffer), "End command buffer (BLAS)");

    // Submit and wait
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmdBuffer;

    VK_CHECK_LOG(vkQueueSubmit(m_device->queue, 1, &submitInfo, VK_NULL_HANDLE), "Queue submit (BLAS)");
    vkQueueWaitIdle(m_device->queue);

    vkFreeCommandBuffers(m_device->device, m_buildCommandPool, 1, &cmdBuffer);

    // Get BLAS device address
    VkAccelerationStructureDeviceAddressInfoKHR addressInfo{};
    addressInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
    addressInfo.accelerationStructure = asData.blas;
    asData.blasDeviceAddress = vkGetAccelerationStructureDeviceAddressKHR(m_device->device, &addressInfo);

    asData.primitiveCount = primitiveCount;

    // Measure build time
    auto buildEnd = std::chrono::high_resolution_clock::now();
    asData.blasBuildTimeMs = std::chrono::duration<float, std::milli>(buildEnd - buildStart).count();

    std::ostringstream oss;
    oss << "[AccelerationStructureCacher::BuildBLAS] BLAS built successfully, address=0x" << std::hex << asData.blasDeviceAddress
        << ", time=" << std::fixed << std::setprecision(2) << asData.blasBuildTimeMs << "ms";
    LOG_INFO(oss.str());
}

// ============================================================================
// BUILD TLAS - Create top-level acceleration structure with single instance
// ============================================================================

void AccelerationStructureCacher::BuildTLAS(const AccelStructCreateInfo& ci, AccelerationStructureData& asData) {
    LOG_INFO("[AccelerationStructureCacher::BuildTLAS] Building TLAS...");
    auto buildStart = std::chrono::high_resolution_clock::now();

    if (!vkCreateAccelerationStructureKHR || !vkCmdBuildAccelerationStructuresKHR) {
        LOG_WARNING("[AccelerationStructureCacher::BuildTLAS] RT extension not available - skipping TLAS build");
        return;
    }

    if (asData.blas == VK_NULL_HANDLE) {
        LOG_INFO("[AccelerationStructureCacher::BuildTLAS] No BLAS - skipping TLAS build");
        return;
    }

    // Create instance data
    VkAccelerationStructureInstanceKHR instance{};
    // Identity transform
    instance.transform.matrix[0][0] = 1.0f;
    instance.transform.matrix[1][1] = 1.0f;
    instance.transform.matrix[2][2] = 1.0f;
    instance.instanceCustomIndex = 0;
    instance.mask = 0xFF;
    instance.instanceShaderBindingTableRecordOffset = 0;
    instance.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
    instance.accelerationStructureReference = asData.blasDeviceAddress;

    // Create instance buffer with host-visible memory for direct upload
    {
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = sizeof(VkAccelerationStructureInstanceKHR);
        bufferInfo.usage = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
                          VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VK_CHECK_LOG(vkCreateBuffer(m_device->device, &bufferInfo, nullptr, &asData.instanceBuffer), "Create instance buffer");

        VkMemoryRequirements memReq;
        vkGetBufferMemoryRequirements(m_device->device, asData.instanceBuffer, &memReq);

        VkMemoryAllocateFlagsInfo allocFlagsInfo{};
        allocFlagsInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
        allocFlagsInfo.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.pNext = &allocFlagsInfo;
        allocInfo.allocationSize = memReq.size;
        allocInfo.memoryTypeIndex = CacherAllocationHelpers::FindMemoryType(
            m_device->gpuMemoryProperties,
            memReq.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
        );

        VK_CHECK_LOG(vkAllocateMemory(m_device->device, &allocInfo, nullptr, &asData.instanceMemory), "Allocate instance memory");
        VK_CHECK_LOG(vkBindBufferMemory(m_device->device, asData.instanceBuffer, asData.instanceMemory, 0), "Bind instance buffer memory");

        // Direct upload (host-visible memory)
        void* mappedData;
        VK_CHECK_LOG(vkMapMemory(m_device->device, asData.instanceMemory, 0, sizeof(instance), 0, &mappedData), "Map instance memory");
        std::memcpy(mappedData, &instance, sizeof(instance));
        vkUnmapMemory(m_device->device, asData.instanceMemory);
    }

    // Get instance buffer device address
    VkBufferDeviceAddressInfo instanceAddressInfo{};
    instanceAddressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    instanceAddressInfo.buffer = asData.instanceBuffer;
    VkDeviceAddress instanceAddress = vkGetBufferDeviceAddressKHR(m_device->device, &instanceAddressInfo);

    // Setup geometry
    VkAccelerationStructureGeometryInstancesDataKHR instancesData{};
    instancesData.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
    instancesData.data.deviceAddress = instanceAddress;

    VkAccelerationStructureGeometryKHR geometry{};
    geometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    geometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
    geometry.geometry.instances = instancesData;

    // Build info for size query
    VkAccelerationStructureBuildGeometryInfoKHR buildInfo{};
    buildInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    buildInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    buildInfo.geometryCount = 1;
    buildInfo.pGeometries = &geometry;

    // Query build sizes
    uint32_t instanceCount = 1;

    VkAccelerationStructureBuildSizesInfoKHR sizeInfo{};
    sizeInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;

    vkGetAccelerationStructureBuildSizesKHR(
        m_device->device,
        VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
        &buildInfo,
        &instanceCount,
        &sizeInfo
    );

    // Create TLAS buffer
    {
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = sizeInfo.accelerationStructureSize;
        bufferInfo.usage = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR |
                          VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VK_CHECK_LOG(vkCreateBuffer(m_device->device, &bufferInfo, nullptr, &asData.tlasBuffer), "Create TLAS buffer");

        VkMemoryRequirements memReq;
        vkGetBufferMemoryRequirements(m_device->device, asData.tlasBuffer, &memReq);

        VkMemoryAllocateFlagsInfo allocFlagsInfo{};
        allocFlagsInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
        allocFlagsInfo.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.pNext = &allocFlagsInfo;
        allocInfo.allocationSize = memReq.size;
        allocInfo.memoryTypeIndex = CacherAllocationHelpers::FindMemoryType(m_device->gpuMemoryProperties, memReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        VK_CHECK_LOG(vkAllocateMemory(m_device->device, &allocInfo, nullptr, &asData.tlasMemory), "Allocate TLAS memory");
        VK_CHECK_LOG(vkBindBufferMemory(m_device->device, asData.tlasBuffer, asData.tlasMemory, 0), "Bind TLAS buffer memory");
    }

    // Get scratch buffer address (reusing from BLAS build)
    VkBufferDeviceAddressInfo scratchAddressInfo{};
    scratchAddressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    scratchAddressInfo.buffer = asData.scratchBuffer;
    VkDeviceAddress scratchAddress = vkGetBufferDeviceAddressKHR(m_device->device, &scratchAddressInfo);

    // Create acceleration structure
    VkAccelerationStructureCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
    createInfo.buffer = asData.tlasBuffer;
    createInfo.size = sizeInfo.accelerationStructureSize;
    createInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;

    VK_CHECK_LOG(vkCreateAccelerationStructureKHR(m_device->device, &createInfo, nullptr, &asData.tlas), "Create TLAS");

    // Update build info
    buildInfo.dstAccelerationStructure = asData.tlas;
    buildInfo.scratchData.deviceAddress = scratchAddress;

    // Build range info
    VkAccelerationStructureBuildRangeInfoKHR rangeInfo{};
    rangeInfo.primitiveCount = instanceCount;
    rangeInfo.primitiveOffset = 0;
    rangeInfo.firstVertex = 0;
    rangeInfo.transformOffset = 0;

    const VkAccelerationStructureBuildRangeInfoKHR* pRangeInfo = &rangeInfo;

    // Allocate and record command buffer
    VkCommandBufferAllocateInfo cmdAllocInfo{};
    cmdAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmdAllocInfo.commandPool = m_buildCommandPool;
    cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdAllocInfo.commandBufferCount = 1;

    VkCommandBuffer cmdBuffer;
    VK_CHECK_LOG(vkAllocateCommandBuffers(m_device->device, &cmdAllocInfo, &cmdBuffer), "Allocate command buffers (TLAS)");

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    VK_CHECK_LOG(vkBeginCommandBuffer(cmdBuffer, &beginInfo), "Begin command buffer (TLAS)");
    vkCmdBuildAccelerationStructuresKHR(cmdBuffer, 1, &buildInfo, &pRangeInfo);
    VK_CHECK_LOG(vkEndCommandBuffer(cmdBuffer), "End command buffer (TLAS)");

    // Submit and wait
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmdBuffer;

    VK_CHECK_LOG(vkQueueSubmit(m_device->queue, 1, &submitInfo, VK_NULL_HANDLE), "Queue submit (TLAS)");
    vkQueueWaitIdle(m_device->queue);

    vkFreeCommandBuffers(m_device->device, m_buildCommandPool, 1, &cmdBuffer);

    // Get TLAS device address
    VkAccelerationStructureDeviceAddressInfoKHR addressInfo{};
    addressInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
    addressInfo.accelerationStructure = asData.tlas;
    asData.tlasDeviceAddress = vkGetAccelerationStructureDeviceAddressKHR(m_device->device, &addressInfo);

    // Measure build time
    auto buildEnd = std::chrono::high_resolution_clock::now();
    asData.tlasBuildTimeMs = std::chrono::duration<float, std::milli>(buildEnd - buildStart).count();

    std::ostringstream oss;
    oss << "[AccelerationStructureCacher::BuildTLAS] TLAS built successfully, address=0x" << std::hex << asData.tlasDeviceAddress
        << ", time=" << std::fixed << std::setprecision(2) << asData.tlasBuildTimeMs << "ms";
    LOG_INFO(oss.str());
}

} // namespace CashSystem
