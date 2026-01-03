#include "pch.h"
#include "AccelerationStructureCacher.h"
#include "TLASUpdateRequest.h"
#include "VulkanDevice.h"
#include "error/VulkanError.h"

#include <cstring>
#include <stdexcept>
#include <sstream>

namespace CashSystem {

// ============================================================================
// Note: VoxelAABBData::Cleanup() removed - cleanup now handled by VoxelAABBCacher
// via FreeBufferTracked() using the allocator infrastructure.
// ============================================================================

// ============================================================================
// Note: AccelerationStructureData::Cleanup() and CachedAccelerationStructure::Cleanup()
// removed - cleanup now handled by AccelerationStructureCacher via FreeBufferTracked()
// using the allocator infrastructure. AS handles (blas/tlas) still destroyed directly.
// ============================================================================

// ============================================================================
// ACCELERATION STRUCTURE CACHER - PUBLIC API
// ============================================================================

std::shared_ptr<CachedAccelerationStructure> AccelerationStructureCacher::GetOrCreate(const AccelStructCreateInfo& ci) {
    return TypedCacher<CachedAccelerationStructure, AccelStructCreateInfo>::GetOrCreate(ci);
}

// ============================================================================
// DYNAMIC MODE UPDATE API (Phase 3.5)
// ============================================================================

void AccelerationStructureCacher::QueueTLASUpdate(CachedAccelerationStructure* cached, uint32_t imageIndex) {
    if (!cached) {
        LOG_WARNING("[AccelerationStructureCacher::QueueTLASUpdate] Null cached structure");
        return;
    }

    // Static mode doesn't use dynamic updates
    if (cached->buildMode == ASBuildMode::Static) {
        return;
    }

    // Dynamic/SubScene mode requires both components
    if (!cached->dynamicTLAS || !cached->instanceManager) {
        LOG_WARNING("[AccelerationStructureCacher::QueueTLASUpdate] Dynamic mode but missing TLAS/manager");
        return;
    }

    if (!m_device) {
        LOG_ERROR("[AccelerationStructureCacher::QueueTLASUpdate] No device set");
        return;
    }

    // Create update request (device for loading RT function pointers)
    auto request = std::make_unique<TLASUpdateRequest>(
        m_device,
        cached->dynamicTLAS.get(),
        cached->instanceManager.get(),
        cached->instanceManager->GetDirtyLevel(),
        imageIndex
    );

    // Queue via device's generalized update API
    m_device->QueueUpdate(std::move(request));

    LOG_DEBUG("[AccelerationStructureCacher::QueueTLASUpdate] Queued TLAS update for frame " +
              std::to_string(imageIndex));
}

void AccelerationStructureCacher::QueueTLASUpdate(uint64_t cacheKey, uint32_t imageIndex) {
    // Look up cached entry by key
    std::shared_lock lock(m_lock);
    auto it = m_entries.find(cacheKey);
    if (it == m_entries.end() || !it->second.resource) {
        LOG_WARNING("[AccelerationStructureCacher::QueueTLASUpdate] Cache key not found: " +
                    std::to_string(cacheKey));
        return;
    }

    // Unlock before calling the other overload (which may log)
    CachedAccelerationStructure* cached = it->second.resource.get();
    lock.unlock();

    QueueTLASUpdate(cached, imageIndex);
}

// ============================================================================
// ACCELERATION STRUCTURE CACHER - TYPEDCACHER IMPLEMENTATION
// ============================================================================

std::shared_ptr<CachedAccelerationStructure> AccelerationStructureCacher::Create(const AccelStructCreateInfo& ci) {
    LOG_INFO("[AccelerationStructureCacher::Create] Creating acceleration structure, mode=" +
             std::string(ci.buildMode == ASBuildMode::Static ? "Static" :
                        ci.buildMode == ASBuildMode::Dynamic ? "Dynamic" : "SubScene"));

    if (!IsInitialized()) {
        throw std::runtime_error("[AccelerationStructureCacher::Create] Cacher not initialized with device");
    }

    if (!ci.aabbData || !ci.aabbData->IsValid()) {
        throw std::runtime_error("[AccelerationStructureCacher::Create] AABB data is required and must be valid");
    }

    // Validate Dynamic/SubScene mode requirements
    if (ci.buildMode != ASBuildMode::Static && ci.imageCount == 0) {
        throw std::runtime_error("[AccelerationStructureCacher::Create] imageCount required for Dynamic/SubScene mode");
    }

    // Load RT extension functions on first use
    LoadRTFunctions();

    auto cached = std::make_shared<CachedAccelerationStructure>();

    // Store build mode and AABB count
    cached->buildMode = ci.buildMode;
    cached->sourceAABBCount = ci.aabbData->aabbCount;

    LOG_INFO("[AccelerationStructureCacher::Create] Using AABB data with " +
             std::to_string(ci.aabbData->aabbCount) + " AABBs");

    if (ci.aabbData->aabbCount == 0) {
        LOG_INFO("[AccelerationStructureCacher::Create] No AABBs to build AS from");
        return cached;
    }

    // Build BLAS from AABBs (always needed)
    BuildBLAS(ci, *ci.aabbData, cached->accelStruct);

    // Mode-specific TLAS handling
    if (ci.buildMode == ASBuildMode::Static) {
        // Static mode: Build TLAS with single instance (existing behavior)
        BuildTLAS(ci, cached->accelStruct);
    } else {
        // Dynamic/SubScene mode: Create instance manager and dynamic TLAS
        cached->instanceManager = std::make_unique<TLASInstanceManager>();

        cached->dynamicTLAS = std::make_unique<DynamicTLAS>();
        DynamicTLAS::Config tlasConfig;
        tlasConfig.maxInstances = ci.maxInstances;
        tlasConfig.preferFastTrace = ci.preferFastTrace;
        tlasConfig.allowUpdate = ci.allowUpdate;

        // DynamicTLAS uses VulkanDevice's centralized allocation API
        if (!cached->dynamicTLAS->Initialize(m_device, ci.imageCount, tlasConfig)) {
            throw std::runtime_error("[AccelerationStructureCacher::Create] Failed to initialize DynamicTLAS");
        }

        // Add initial instance pointing to our BLAS
        TLASInstanceManager::Instance initialInstance;
        initialInstance.blasKey = ci.ComputeHash();
        initialInstance.blasAddress = cached->accelStruct.blasDeviceAddress;
        // Identity transform (default)
        cached->instanceManager->AddInstance(initialInstance);

        LOG_INFO("[AccelerationStructureCacher::Create] Initialized Dynamic TLAS with " +
                 std::to_string(ci.maxInstances) + " max instances, " +
                 std::to_string(ci.imageCount) + " frames");
    }

    LOG_INFO("[AccelerationStructureCacher::Create] Created AS with " +
             std::to_string(cached->accelStruct.primitiveCount) + " primitives");

    return cached;
}

std::uint64_t AccelerationStructureCacher::ComputeKey(const AccelStructCreateInfo& ci) const {
    return ci.ComputeHash();
}

void AccelerationStructureCacher::Cleanup() {
    LOG_INFO("[AccelerationStructureCacher::Cleanup] Cleaning up cached acceleration structures");

    // Load destroy function for acceleration structures
    auto destroyAS = reinterpret_cast<PFN_vkDestroyAccelerationStructureKHR>(
        vkGetDeviceProcAddr(m_device->device, "vkDestroyAccelerationStructureKHR")
    );

    // Cleanup all cached entries
    for (auto& [key, entry] : m_entries) {
        if (entry.resource) {
            auto& asData = entry.resource->accelStruct;

            // Cleanup Dynamic TLAS resources first (if present)
            if (entry.resource->dynamicTLAS) {
                entry.resource->dynamicTLAS->Cleanup(nullptr);
                entry.resource->dynamicTLAS.reset();
            }
            entry.resource->instanceManager.reset();

            // Destroy acceleration structure handles
            if (destroyAS) {
                if (asData.blas != VK_NULL_HANDLE) {
                    destroyAS(m_device->device, asData.blas, nullptr);
                    asData.blas = VK_NULL_HANDLE;
                }
                if (asData.tlas != VK_NULL_HANDLE) {
                    destroyAS(m_device->device, asData.tlas, nullptr);
                    asData.tlas = VK_NULL_HANDLE;
                }
            }

            // Free buffer allocations via FreeBufferTracked
            FreeBufferTracked(asData.blasAllocation);
            FreeBufferTracked(asData.tlasAllocation);
            FreeBufferTracked(asData.instanceAllocation);
            FreeBufferTracked(asData.scratchAllocation);

            asData.blasDeviceAddress = 0;
            asData.tlasDeviceAddress = 0;
            asData.primitiveCount = 0;
            entry.resource->sourceAABBCount = 0;
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
    // Use device address from allocation (already computed during allocation)
    VkDeviceAddress aabbDeviceAddress = aabbData.GetAABBDeviceAddress();
    if (aabbDeviceAddress == 0) {
        // Fallback: query device address if not stored in allocation
        VkBufferDeviceAddressInfo aabbAddressInfo{};
        aabbAddressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
        aabbAddressInfo.buffer = aabbData.GetAABBBuffer();
        aabbDeviceAddress = vkGetBufferDeviceAddressKHR(m_device->device, &aabbAddressInfo);
    }

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

    // Allocate BLAS buffer via AllocateBufferTracked
    auto blasAlloc = AllocateBufferTracked(
        sizeInfo.accelerationStructureSize,
        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        "AccelStruct_BLAS"
    );
    if (!blasAlloc) {
        throw std::runtime_error("[AccelerationStructureCacher::BuildBLAS] Failed to allocate BLAS buffer");
    }
    asData.blasAllocation = *blasAlloc;

    // Allocate scratch buffer via AllocateBufferTracked
    auto scratchAlloc = AllocateBufferTracked(
        sizeInfo.buildScratchSize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        "AccelStruct_scratch"
    );
    if (!scratchAlloc) {
        FreeBufferTracked(asData.blasAllocation);
        throw std::runtime_error("[AccelerationStructureCacher::BuildBLAS] Failed to allocate scratch buffer");
    }
    asData.scratchAllocation = *scratchAlloc;

    // Create acceleration structure
    VkAccelerationStructureCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
    createInfo.buffer = asData.blasAllocation.buffer;
    createInfo.size = sizeInfo.accelerationStructureSize;
    createInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;

    VK_CHECK_LOG(vkCreateAccelerationStructureKHR(m_device->device, &createInfo, nullptr, &asData.blas), "Create BLAS");

    // Get scratch buffer device address (use stored address from allocation)
    VkDeviceAddress scratchAddress = asData.scratchAllocation.deviceAddress;
    if (scratchAddress == 0) {
        // Fallback: query device address if not stored
        VkBufferDeviceAddressInfo scratchAddressInfo{};
        scratchAddressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
        scratchAddressInfo.buffer = asData.scratchAllocation.buffer;
        scratchAddress = vkGetBufferDeviceAddressKHR(m_device->device, &scratchAddressInfo);
    }

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

    // Allocate instance buffer via AllocateBufferTracked (host-visible for direct upload)
    auto instanceAlloc = AllocateBufferTracked(
        sizeof(VkAccelerationStructureInstanceKHR),
        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        "AccelStruct_instance"
    );
    if (!instanceAlloc) {
        throw std::runtime_error("[AccelerationStructureCacher::BuildTLAS] Failed to allocate instance buffer");
    }
    asData.instanceAllocation = *instanceAlloc;

    // Direct upload via MapBufferTracked
    void* mappedData = MapBufferTracked(asData.instanceAllocation);
    if (!mappedData) {
        FreeBufferTracked(asData.instanceAllocation);
        throw std::runtime_error("[AccelerationStructureCacher::BuildTLAS] Failed to map instance buffer");
    }
    std::memcpy(mappedData, &instance, sizeof(instance));
    UnmapBufferTracked(asData.instanceAllocation);

    // Get instance buffer device address (use stored address from allocation)
    VkDeviceAddress instanceAddress = asData.instanceAllocation.deviceAddress;
    if (instanceAddress == 0) {
        // Fallback: query device address if not stored
        VkBufferDeviceAddressInfo instanceAddressInfo{};
        instanceAddressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
        instanceAddressInfo.buffer = asData.instanceAllocation.buffer;
        instanceAddress = vkGetBufferDeviceAddressKHR(m_device->device, &instanceAddressInfo);
    }

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

    // Allocate TLAS buffer via AllocateBufferTracked
    auto tlasAlloc = AllocateBufferTracked(
        sizeInfo.accelerationStructureSize,
        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        "AccelStruct_TLAS"
    );
    if (!tlasAlloc) {
        FreeBufferTracked(asData.instanceAllocation);
        throw std::runtime_error("[AccelerationStructureCacher::BuildTLAS] Failed to allocate TLAS buffer");
    }
    asData.tlasAllocation = *tlasAlloc;

    // Get scratch buffer address (reusing from BLAS build, use stored address)
    VkDeviceAddress scratchAddress = asData.scratchAllocation.deviceAddress;
    if (scratchAddress == 0) {
        // Fallback: query device address if not stored
        VkBufferDeviceAddressInfo scratchAddressInfo{};
        scratchAddressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
        scratchAddressInfo.buffer = asData.scratchAllocation.buffer;
        scratchAddress = vkGetBufferDeviceAddressKHR(m_device->device, &scratchAddressInfo);
    }

    // Create acceleration structure
    VkAccelerationStructureCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
    createInfo.buffer = asData.tlasAllocation.buffer;
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
