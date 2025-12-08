#include "Nodes/AccelerationStructureNode.h"
#include "VulkanDevice.h"
#include "Core/NodeLogging.h"
#include "Core/RenderGraph.h"
#include "MainCacher.h"
#include "AccelerationStructureCacher.h"
#include "VoxelSceneCacher.h"  // For CashSystem::VoxelSceneData
#include <cstring>

namespace Vixen::RenderGraph {

// ============================================================================
// NODE TYPE FACTORY
// ============================================================================

std::unique_ptr<NodeInstance> AccelerationStructureNodeType::CreateInstance(
    const std::string& instanceName
) const {
    return std::unique_ptr<NodeInstance>(
        new AccelerationStructureNode(instanceName, const_cast<AccelerationStructureNodeType*>(this))
    );
}

// ============================================================================
// ACCELERATION STRUCTURE NODE IMPLEMENTATION
// ============================================================================

AccelerationStructureNode::AccelerationStructureNode(
    const std::string& instanceName,
    NodeType* nodeType
) : TypedNode<AccelerationStructureNodeConfig>(instanceName, nodeType)
{
    NODE_LOG_INFO("AccelerationStructureNode constructor (Phase K)");
}

void AccelerationStructureNode::SetupImpl(TypedSetupContext& ctx) {
    NODE_LOG_DEBUG("[AccelerationStructureNode::SetupImpl] ENTERED");

    // Read build parameters
    preferFastTrace_ = GetParameterValue<bool>(
        AccelerationStructureNodeConfig::PARAM_PREFER_FAST_TRACE, true);
    allowUpdate_ = GetParameterValue<bool>(
        AccelerationStructureNodeConfig::PARAM_ALLOW_UPDATE, false);
    allowCompaction_ = GetParameterValue<bool>(
        AccelerationStructureNodeConfig::PARAM_ALLOW_COMPACTION, false);

    NODE_LOG_INFO("AccelerationStructure setup: preferFastTrace=" +
                  std::to_string(preferFastTrace_) +
                  ", allowUpdate=" + std::to_string(allowUpdate_) +
                  ", allowCompaction=" + std::to_string(allowCompaction_));

    NODE_LOG_DEBUG("[AccelerationStructureNode::SetupImpl] COMPLETED");
}

void AccelerationStructureNode::CompileImpl(TypedCompileContext& ctx) {
    NODE_LOG_DEBUG("[AccelerationStructureNode::CompileImpl] ENTERED");
    NODE_LOG_INFO("=== AccelerationStructureNode::CompileImpl START ===");

    // Get device
    VulkanDevice* devicePtr = ctx.In(AccelerationStructureNodeConfig::VULKAN_DEVICE_IN);
    if (!devicePtr) {
        throw std::runtime_error("[AccelerationStructureNode] VULKAN_DEVICE_IN is null");
    }
    SetDevice(devicePtr);
    vulkanDevice_ = devicePtr;

    // Check RTX support
    if (!vulkanDevice_->IsRTXEnabled()) {
        throw std::runtime_error("[AccelerationStructureNode] RTX is not enabled on device");
    }

    const auto& rtxCaps = vulkanDevice_->GetRTXCapabilities();
    if (!rtxCaps.accelerationStructure) {
        throw std::runtime_error("[AccelerationStructureNode] VK_KHR_acceleration_structure not supported");
    }

    // Get command pool
    commandPool_ = ctx.In(AccelerationStructureNodeConfig::COMMAND_POOL);
    if (commandPool_ == VK_NULL_HANDLE) {
        throw std::runtime_error("[AccelerationStructureNode] COMMAND_POOL is null");
    }

    // Register AccelerationStructureCacher with CashSystem (idempotent)
    RegisterAccelerationStructureCacher();

    // Get AABB data
    VoxelAABBData* aabbData = ctx.In(AccelerationStructureNodeConfig::AABB_DATA);
    if (!aabbData || !aabbData->IsValid()) {
        throw std::runtime_error("[AccelerationStructureNode] AABB_DATA is null or invalid");
    }

    // Get VoxelSceneData (required for cacher integration)
    voxelSceneData_ = ctx.In(AccelerationStructureNodeConfig::VOXEL_SCENE_DATA);
    if (!voxelSceneData_) {
        throw std::runtime_error("[AccelerationStructureNode] VOXEL_SCENE_DATA is required - connect VoxelGridNode.VOXEL_SCENE_DATA");
    }
    NODE_LOG_INFO("AccelerationStructureNode: Received VoxelSceneData with " +
                  std::to_string(voxelSceneData_->nodeCount) + " nodes, " +
                  std::to_string(voxelSceneData_->brickCount) + " bricks");

    NODE_LOG_INFO("Building acceleration structures for " +
                  std::to_string(aabbData->aabbCount) + " AABBs");

    // Load RTX extension functions (still needed for cacher)
    if (!LoadRTXFunctions()) {
        throw std::runtime_error("[AccelerationStructureNode] Failed to load RTX extension functions");
    }

    // ========================================================================
    // Build BLAS/TLAS via cacher (the only path now)
    // ========================================================================
    if (!accelStructCacher_) {
        throw std::runtime_error("[AccelerationStructureNode] AccelerationStructureCacher not registered - cannot proceed");
    }
    CreateAccelStructViaCacher(*aabbData);
    NODE_LOG_INFO("AccelerationStructureNode: AS created via cacher");

#if 0  // ========================================================================
       // LEGACY MANUAL BLAS/TLAS BUILD - Now handled by AccelerationStructureCacher
       // ========================================================================
    // Build BLAS from AABBs
    if (!BuildBLAS(*aabbData)) {
        throw std::runtime_error("[AccelerationStructureNode] Failed to build BLAS");
    }

    // Build TLAS with single instance
    if (!BuildTLAS()) {
        throw std::runtime_error("[AccelerationStructureNode] Failed to build TLAS");
    }
#endif // LEGACY MANUAL BLAS/TLAS BUILD

    // Output the acceleration structure data
    ctx.Out(AccelerationStructureNodeConfig::ACCELERATION_STRUCTURE_DATA, &accelData_);
    // Output TLAS handle separately for variadic descriptor wiring
    ctx.Out(AccelerationStructureNodeConfig::TLAS_HANDLE, accelData_.tlas);

    NODE_LOG_INFO("=== AccelerationStructureNode::CompileImpl COMPLETE ===");
    NODE_LOG_INFO("BLAS address: 0x" + std::to_string(accelData_.blasDeviceAddress));
    NODE_LOG_INFO("TLAS address: 0x" + std::to_string(accelData_.tlasDeviceAddress));
    NODE_LOG_DEBUG("[AccelerationStructureNode::CompileImpl] COMPLETED");
}

void AccelerationStructureNode::ExecuteImpl(TypedExecuteContext& ctx) {
    // Acceleration structures are static (built during compile)
    // Just pass through the cached data pointer and TLAS handle
    ctx.Out(AccelerationStructureNodeConfig::ACCELERATION_STRUCTURE_DATA, &accelData_);
    ctx.Out(AccelerationStructureNodeConfig::TLAS_HANDLE, accelData_.tlas);
}

void AccelerationStructureNode::CleanupImpl(TypedCleanupContext& ctx) {
    NODE_LOG_INFO("AccelerationStructureNode cleanup");
    DestroyAccelerationStructures();
}

// ============================================================================
// RTX FUNCTION LOADING
// ============================================================================

bool AccelerationStructureNode::LoadRTXFunctions() {
    VkDevice device = vulkanDevice_->device;

    vkGetAccelerationStructureBuildSizesKHR_ =
        (PFN_vkGetAccelerationStructureBuildSizesKHR)vkGetDeviceProcAddr(
            device, "vkGetAccelerationStructureBuildSizesKHR");

    vkCreateAccelerationStructureKHR_ =
        (PFN_vkCreateAccelerationStructureKHR)vkGetDeviceProcAddr(
            device, "vkCreateAccelerationStructureKHR");

    vkDestroyAccelerationStructureKHR_ =
        (PFN_vkDestroyAccelerationStructureKHR)vkGetDeviceProcAddr(
            device, "vkDestroyAccelerationStructureKHR");

    vkCmdBuildAccelerationStructuresKHR_ =
        (PFN_vkCmdBuildAccelerationStructuresKHR)vkGetDeviceProcAddr(
            device, "vkCmdBuildAccelerationStructuresKHR");

    vkGetAccelerationStructureDeviceAddressKHR_ =
        (PFN_vkGetAccelerationStructureDeviceAddressKHR)vkGetDeviceProcAddr(
            device, "vkGetAccelerationStructureDeviceAddressKHR");

    vkGetBufferDeviceAddressKHR_ =
        (PFN_vkGetBufferDeviceAddressKHR)vkGetDeviceProcAddr(
            device, "vkGetBufferDeviceAddressKHR");

    bool success =
        vkGetAccelerationStructureBuildSizesKHR_ &&
        vkCreateAccelerationStructureKHR_ &&
        vkDestroyAccelerationStructureKHR_ &&
        vkCmdBuildAccelerationStructuresKHR_ &&
        vkGetAccelerationStructureDeviceAddressKHR_ &&
        vkGetBufferDeviceAddressKHR_;

    if (!success) {
        NODE_LOG_ERROR("Failed to load one or more RTX extension functions");
    }

    return success;
}

#if 0  // ========================================================================
       // LEGACY BLAS/TLAS BUILD METHODS - Now handled by AccelerationStructureCacher
       // ========================================================================
       // These methods are no longer used because AccelerationStructureCacher handles:
       // - BuildBLAS: BLAS creation from AABB data
       // - BuildTLAS: TLAS creation with single instance
       // - CreateInstanceBuffer: Instance buffer for TLAS
       // - CreateBuffer: Generic buffer creation helper
       // - QueryBLASBuildSizes / QueryTLASBuildSizes: Size queries
       // - GetBufferDeviceAddress / GetAccelerationStructureDeviceAddress: Address helpers
       // ========================================================================

// ============================================================================
// BLAS BUILDING
// ============================================================================

bool AccelerationStructureNode::BuildBLAS(const VoxelAABBData& aabbData) {
    NODE_LOG_INFO("Building BLAS from " + std::to_string(aabbData.aabbCount) + " AABBs");

    VkDevice device = vulkanDevice_->device;

    // Get device address of AABB buffer
    VkDeviceAddress aabbBufferAddress = GetBufferDeviceAddress(aabbData.aabbBuffer);
    if (aabbBufferAddress == 0) {
        NODE_LOG_ERROR("Failed to get AABB buffer device address");
        return false;
    }

    // Setup AABB geometry data
    VkAccelerationStructureGeometryAabbsDataKHR aabbsData{};
    aabbsData.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_AABBS_DATA_KHR;
    aabbsData.data.deviceAddress = aabbBufferAddress;
    aabbsData.stride = sizeof(VoxelAABB);  // 24 bytes per AABB

    // Setup geometry
    VkAccelerationStructureGeometryKHR geometry{};
    geometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    geometry.geometryType = VK_GEOMETRY_TYPE_AABBS_KHR;
    geometry.geometry.aabbs = aabbsData;
    geometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;  // Opaque voxels

    // Query build sizes
    auto buildSizes = QueryBLASBuildSizes(geometry, aabbData.aabbCount);

    NODE_LOG_INFO("BLAS size requirements: accelSize=" + std::to_string(buildSizes.accelerationStructureSize) +
                  ", scratchSize=" + std::to_string(buildSizes.buildScratchSize));

    // Create BLAS buffer
    if (!CreateBuffer(
            buildSizes.accelerationStructureSize,
            VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR |
            VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            accelData_.blasBuffer,
            accelData_.blasMemory)) {
        NODE_LOG_ERROR("Failed to create BLAS buffer");
        return false;
    }

    // Create scratch buffer
    if (!CreateBuffer(
            buildSizes.buildScratchSize,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
            VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            accelData_.scratchBuffer,
            accelData_.scratchMemory)) {
        NODE_LOG_ERROR("Failed to create scratch buffer");
        return false;
    }

    VkDeviceAddress scratchAddress = GetBufferDeviceAddress(accelData_.scratchBuffer);

    // Create acceleration structure
    VkAccelerationStructureCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
    createInfo.buffer = accelData_.blasBuffer;
    createInfo.size = buildSizes.accelerationStructureSize;
    createInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;

    if (vkCreateAccelerationStructureKHR_(device, &createInfo, nullptr, &accelData_.blas) != VK_SUCCESS) {
        NODE_LOG_ERROR("vkCreateAccelerationStructureKHR failed for BLAS");
        return false;
    }

    // Build geometry info
    VkAccelerationStructureBuildGeometryInfoKHR buildInfo{};
    buildInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    buildInfo.flags = preferFastTrace_ ?
        VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR :
        VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR;
    if (allowUpdate_) {
        buildInfo.flags |= VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR;
    }
    if (allowCompaction_) {
        buildInfo.flags |= VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_KHR;
    }
    buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    buildInfo.dstAccelerationStructure = accelData_.blas;
    buildInfo.geometryCount = 1;
    buildInfo.pGeometries = &geometry;
    buildInfo.scratchData.deviceAddress = scratchAddress;

    // Build range info
    VkAccelerationStructureBuildRangeInfoKHR rangeInfo{};
    rangeInfo.primitiveCount = aabbData.aabbCount;
    rangeInfo.primitiveOffset = 0;
    rangeInfo.firstVertex = 0;
    rangeInfo.transformOffset = 0;

    const VkAccelerationStructureBuildRangeInfoKHR* pRangeInfo = &rangeInfo;

    // Record build command
    VkCommandBufferAllocateInfo cmdAllocInfo{};
    cmdAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdAllocInfo.commandPool = commandPool_;
    cmdAllocInfo.commandBufferCount = 1;

    VkCommandBuffer cmdBuffer;
    vkAllocateCommandBuffers(device, &cmdAllocInfo, &cmdBuffer);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(cmdBuffer, &beginInfo);
    vkCmdBuildAccelerationStructuresKHR_(cmdBuffer, 1, &buildInfo, &pRangeInfo);
    vkEndCommandBuffer(cmdBuffer);

    // Submit and wait
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmdBuffer;

    vkQueueSubmit(vulkanDevice_->queue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(vulkanDevice_->queue);

    vkFreeCommandBuffers(device, commandPool_, 1, &cmdBuffer);

    // Get BLAS device address
    accelData_.blasDeviceAddress = GetAccelerationStructureDeviceAddress(accelData_.blas);
    accelData_.primitiveCount = aabbData.aabbCount;

    NODE_LOG_INFO("BLAS built successfully: address=0x" + std::to_string(accelData_.blasDeviceAddress));

    return true;
}

VkAccelerationStructureBuildSizesInfoKHR AccelerationStructureNode::QueryBLASBuildSizes(
    const VkAccelerationStructureGeometryKHR& geometry,
    uint32_t primitiveCount
) {
    VkAccelerationStructureBuildGeometryInfoKHR buildInfo{};
    buildInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    buildInfo.flags = preferFastTrace_ ?
        VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR :
        VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR;
    buildInfo.geometryCount = 1;
    buildInfo.pGeometries = &geometry;

    VkAccelerationStructureBuildSizesInfoKHR sizeInfo{};
    sizeInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;

    vkGetAccelerationStructureBuildSizesKHR_(
        vulkanDevice_->device,
        VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
        &buildInfo,
        &primitiveCount,
        &sizeInfo);

    return sizeInfo;
}

// ============================================================================
// TLAS BUILDING
// ============================================================================

bool AccelerationStructureNode::BuildTLAS() {
    NODE_LOG_INFO("Building TLAS with single instance");

    if (!CreateInstanceBuffer()) {
        NODE_LOG_ERROR("Failed to create instance buffer");
        return false;
    }

    VkDevice device = vulkanDevice_->device;
    VkDeviceAddress instanceBufferAddress = GetBufferDeviceAddress(accelData_.instanceBuffer);

    // Query TLAS size
    auto buildSizes = QueryTLASBuildSizes(1);  // Single instance

    NODE_LOG_INFO("TLAS size requirements: accelSize=" + std::to_string(buildSizes.accelerationStructureSize) +
                  ", scratchSize=" + std::to_string(buildSizes.buildScratchSize));

    // Create TLAS buffer
    if (!CreateBuffer(
            buildSizes.accelerationStructureSize,
            VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR |
            VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            accelData_.tlasBuffer,
            accelData_.tlasMemory)) {
        NODE_LOG_ERROR("Failed to create TLAS buffer");
        return false;
    }

    // Reuse scratch buffer if large enough, otherwise recreate
    // For simplicity, we'll just use the existing scratch buffer
    // (BLAS scratch is typically larger than TLAS scratch)
    VkDeviceAddress scratchAddress = GetBufferDeviceAddress(accelData_.scratchBuffer);

    // Create TLAS
    VkAccelerationStructureCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
    createInfo.buffer = accelData_.tlasBuffer;
    createInfo.size = buildSizes.accelerationStructureSize;
    createInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;

    if (vkCreateAccelerationStructureKHR_(device, &createInfo, nullptr, &accelData_.tlas) != VK_SUCCESS) {
        NODE_LOG_ERROR("vkCreateAccelerationStructureKHR failed for TLAS");
        return false;
    }

    // Setup instances geometry
    VkAccelerationStructureGeometryInstancesDataKHR instancesData{};
    instancesData.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
    instancesData.arrayOfPointers = VK_FALSE;
    instancesData.data.deviceAddress = instanceBufferAddress;

    VkAccelerationStructureGeometryKHR geometry{};
    geometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    geometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
    geometry.geometry.instances = instancesData;

    // Build geometry info
    VkAccelerationStructureBuildGeometryInfoKHR buildInfo{};
    buildInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    buildInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    buildInfo.dstAccelerationStructure = accelData_.tlas;
    buildInfo.geometryCount = 1;
    buildInfo.pGeometries = &geometry;
    buildInfo.scratchData.deviceAddress = scratchAddress;

    // Build range info
    VkAccelerationStructureBuildRangeInfoKHR rangeInfo{};
    rangeInfo.primitiveCount = 1;  // Single instance
    rangeInfo.primitiveOffset = 0;
    rangeInfo.firstVertex = 0;
    rangeInfo.transformOffset = 0;

    const VkAccelerationStructureBuildRangeInfoKHR* pRangeInfo = &rangeInfo;

    // Record build command
    VkCommandBufferAllocateInfo cmdAllocInfo{};
    cmdAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdAllocInfo.commandPool = commandPool_;
    cmdAllocInfo.commandBufferCount = 1;

    VkCommandBuffer cmdBuffer;
    vkAllocateCommandBuffers(device, &cmdAllocInfo, &cmdBuffer);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(cmdBuffer, &beginInfo);
    vkCmdBuildAccelerationStructuresKHR_(cmdBuffer, 1, &buildInfo, &pRangeInfo);
    vkEndCommandBuffer(cmdBuffer);

    // Submit and wait
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmdBuffer;

    vkQueueSubmit(vulkanDevice_->queue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(vulkanDevice_->queue);

    vkFreeCommandBuffers(device, commandPool_, 1, &cmdBuffer);

    // Get TLAS device address
    accelData_.tlasDeviceAddress = GetAccelerationStructureDeviceAddress(accelData_.tlas);

    NODE_LOG_INFO("TLAS built successfully: address=0x" + std::to_string(accelData_.tlasDeviceAddress));

    return true;
}

bool AccelerationStructureNode::CreateInstanceBuffer() {
    VkDevice device = vulkanDevice_->device;

    // Create instance data (single instance with identity transform)
    VkAccelerationStructureInstanceKHR instance{};

    // Identity transform matrix (3x4 row-major)
    instance.transform.matrix[0][0] = 1.0f;
    instance.transform.matrix[1][1] = 1.0f;
    instance.transform.matrix[2][2] = 1.0f;

    instance.instanceCustomIndex = 0;  // Custom index for shader
    instance.mask = 0xFF;              // Visibility mask
    instance.instanceShaderBindingTableRecordOffset = 0;
    instance.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
    instance.accelerationStructureReference = accelData_.blasDeviceAddress;

    VkDeviceSize bufferSize = sizeof(VkAccelerationStructureInstanceKHR);

    // Create staging buffer for instance data
    VkBuffer stagingBuffer;
    VkDeviceMemory stagingMemory;

    if (!CreateBuffer(
            bufferSize,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            stagingBuffer,
            stagingMemory)) {
        return false;
    }

    // Copy instance data to staging buffer
    void* data;
    vkMapMemory(device, stagingMemory, 0, bufferSize, 0, &data);
    memcpy(data, &instance, bufferSize);
    vkUnmapMemory(device, stagingMemory);

    // Create device-local instance buffer
    if (!CreateBuffer(
            bufferSize,
            VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
            VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
            VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            accelData_.instanceBuffer,
            accelData_.instanceMemory)) {
        vkDestroyBuffer(device, stagingBuffer, nullptr);
        vkFreeMemory(device, stagingMemory, nullptr);
        return false;
    }

    // Copy from staging to device-local
    VkCommandBufferAllocateInfo cmdAllocInfo{};
    cmdAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdAllocInfo.commandPool = commandPool_;
    cmdAllocInfo.commandBufferCount = 1;

    VkCommandBuffer cmdBuffer;
    vkAllocateCommandBuffers(device, &cmdAllocInfo, &cmdBuffer);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(cmdBuffer, &beginInfo);

    VkBufferCopy copyRegion{};
    copyRegion.size = bufferSize;
    vkCmdCopyBuffer(cmdBuffer, stagingBuffer, accelData_.instanceBuffer, 1, &copyRegion);

    vkEndCommandBuffer(cmdBuffer);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmdBuffer;

    vkQueueSubmit(vulkanDevice_->queue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(vulkanDevice_->queue);

    vkFreeCommandBuffers(device, commandPool_, 1, &cmdBuffer);
    vkDestroyBuffer(device, stagingBuffer, nullptr);
    vkFreeMemory(device, stagingMemory, nullptr);

    NODE_LOG_INFO("Created instance buffer: " + std::to_string(bufferSize) + " bytes");

    return true;
}

VkAccelerationStructureBuildSizesInfoKHR AccelerationStructureNode::QueryTLASBuildSizes(
    uint32_t instanceCount
) {
    VkAccelerationStructureGeometryInstancesDataKHR instancesData{};
    instancesData.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
    instancesData.arrayOfPointers = VK_FALSE;

    VkAccelerationStructureGeometryKHR geometry{};
    geometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    geometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
    geometry.geometry.instances = instancesData;

    VkAccelerationStructureBuildGeometryInfoKHR buildInfo{};
    buildInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    buildInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    buildInfo.geometryCount = 1;
    buildInfo.pGeometries = &geometry;

    VkAccelerationStructureBuildSizesInfoKHR sizeInfo{};
    sizeInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;

    vkGetAccelerationStructureBuildSizesKHR_(
        vulkanDevice_->device,
        VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
        &buildInfo,
        &instanceCount,
        &sizeInfo);

    return sizeInfo;
}

// ============================================================================
// HELPER FUNCTIONS
// ============================================================================

bool AccelerationStructureNode::CreateBuffer(
    VkDeviceSize size,
    VkBufferUsageFlags usage,
    VkMemoryPropertyFlags properties,
    VkBuffer& buffer,
    VkDeviceMemory& memory
) {
    VkDevice device = vulkanDevice_->device;

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(device, &bufferInfo, nullptr, &buffer) != VK_SUCCESS) {
        return false;
    }

    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(device, buffer, &memRequirements);

    VkMemoryAllocateFlagsInfo allocFlagsInfo{};
    allocFlagsInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
    allocFlagsInfo.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.pNext = (usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) ? &allocFlagsInfo : nullptr;
    allocInfo.allocationSize = memRequirements.size;

    auto memTypeResult = vulkanDevice_->MemoryTypeFromProperties(
        memRequirements.memoryTypeBits,
        properties
    );

    if (!memTypeResult.has_value()) {
        vkDestroyBuffer(device, buffer, nullptr);
        return false;
    }
    allocInfo.memoryTypeIndex = memTypeResult.value();

    if (vkAllocateMemory(device, &allocInfo, nullptr, &memory) != VK_SUCCESS) {
        vkDestroyBuffer(device, buffer, nullptr);
        return false;
    }

    vkBindBufferMemory(device, buffer, memory, 0);

    return true;
}

VkDeviceAddress AccelerationStructureNode::GetBufferDeviceAddress(VkBuffer buffer) {
    VkBufferDeviceAddressInfo addressInfo{};
    addressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    addressInfo.buffer = buffer;

    return vkGetBufferDeviceAddressKHR_(vulkanDevice_->device, &addressInfo);
}

VkDeviceAddress AccelerationStructureNode::GetAccelerationStructureDeviceAddress(
    VkAccelerationStructureKHR accel
) {
    VkAccelerationStructureDeviceAddressInfoKHR addressInfo{};
    addressInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
    addressInfo.accelerationStructure = accel;

    return vkGetAccelerationStructureDeviceAddressKHR_(vulkanDevice_->device, &addressInfo);
}
#endif // LEGACY BLAS/TLAS BUILD METHODS

void AccelerationStructureNode::DestroyAccelerationStructures() {
    if (!vulkanDevice_) {
        return;
    }

    // If using cached data, just release the shared_ptr - cacher owns the resources
    if (cachedAccelStruct_) {
        NODE_LOG_DEBUG("AccelerationStructureNode: Releasing cached accel struct (cacher owns resources)");
        cachedAccelStruct_.reset();
        // Reset all handles (we don't own them)
        accelData_ = AccelerationStructureData{};
        NODE_LOG_INFO("Released cached acceleration structure data");
        return;
    }

#if 0  // Legacy cleanup - only needed if manual BLAS/TLAS build was used
    VkDevice device = vulkanDevice_->device;

    // Destroy TLAS
    if (accelData_.tlas != VK_NULL_HANDLE && vkDestroyAccelerationStructureKHR_) {
        vkDestroyAccelerationStructureKHR_(device, accelData_.tlas, nullptr);
        accelData_.tlas = VK_NULL_HANDLE;
    }

    if (accelData_.tlasBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, accelData_.tlasBuffer, nullptr);
        accelData_.tlasBuffer = VK_NULL_HANDLE;
    }

    if (accelData_.tlasMemory != VK_NULL_HANDLE) {
        vkFreeMemory(device, accelData_.tlasMemory, nullptr);
        accelData_.tlasMemory = VK_NULL_HANDLE;
    }

    // Destroy BLAS
    if (accelData_.blas != VK_NULL_HANDLE && vkDestroyAccelerationStructureKHR_) {
        vkDestroyAccelerationStructureKHR_(device, accelData_.blas, nullptr);
        accelData_.blas = VK_NULL_HANDLE;
    }

    if (accelData_.blasBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, accelData_.blasBuffer, nullptr);
        accelData_.blasBuffer = VK_NULL_HANDLE;
    }

    if (accelData_.blasMemory != VK_NULL_HANDLE) {
        vkFreeMemory(device, accelData_.blasMemory, nullptr);
        accelData_.blasMemory = VK_NULL_HANDLE;
    }

    // Destroy instance buffer
    if (accelData_.instanceBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, accelData_.instanceBuffer, nullptr);
        accelData_.instanceBuffer = VK_NULL_HANDLE;
    }

    if (accelData_.instanceMemory != VK_NULL_HANDLE) {
        vkFreeMemory(device, accelData_.instanceMemory, nullptr);
        accelData_.instanceMemory = VK_NULL_HANDLE;
    }

    // Destroy scratch buffer
    if (accelData_.scratchBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, accelData_.scratchBuffer, nullptr);
        accelData_.scratchBuffer = VK_NULL_HANDLE;
    }

    if (accelData_.scratchMemory != VK_NULL_HANDLE) {
        vkFreeMemory(device, accelData_.scratchMemory, nullptr);
        accelData_.scratchMemory = VK_NULL_HANDLE;
    }

    accelData_.primitiveCount = 0;
    accelData_.blasDeviceAddress = 0;
    accelData_.tlasDeviceAddress = 0;

    NODE_LOG_INFO("Destroyed all acceleration structure resources");
#endif // Legacy cleanup
}

// ============================================================================
// CACHER REGISTRATION
// ============================================================================

void AccelerationStructureNode::RegisterAccelerationStructureCacher() {
    // Get MainCacher from owning graph
    auto& mainCacher = GetOwningGraph()->GetMainCacher();

    // Register AccelerationStructureCacher (idempotent - safe to call multiple times)
    if (!mainCacher.IsRegistered(typeid(CashSystem::CachedAccelerationStructure))) {
        mainCacher.RegisterCacher<
            CashSystem::AccelerationStructureCacher,
            CashSystem::CachedAccelerationStructure,
            CashSystem::AccelStructCreateInfo
        >(
            typeid(CashSystem::CachedAccelerationStructure),
            "AccelerationStructure",
            true  // device-dependent
        );
        NODE_LOG_DEBUG("AccelerationStructureNode: Registered AccelerationStructureCacher");
    }

    // Cache the cacher reference for use throughout node lifetime
    accelStructCacher_ = mainCacher.GetCacher<
        CashSystem::AccelerationStructureCacher,
        CashSystem::CachedAccelerationStructure,
        CashSystem::AccelStructCreateInfo
    >(typeid(CashSystem::CachedAccelerationStructure), device);

    if (accelStructCacher_) {
        NODE_LOG_INFO("AccelerationStructureNode: AccelerationStructure cache ready");
    }
}

// ============================================================================
// CACHER GET-OR-CREATE
// ============================================================================

void AccelerationStructureNode::CreateAccelStructViaCacher(const VoxelAABBData& aabbData) {
    if (!accelStructCacher_) {
        throw std::runtime_error("[AccelerationStructureNode] AccelerationStructureCacher not registered");
    }

    if (!voxelSceneData_) {
        throw std::runtime_error("[AccelerationStructureNode] VoxelSceneData not provided - cannot use cacher");
    }

    // Build cache parameters from node config
    // NOTE: We need to create a shared_ptr for the cacher's AccelStructCreateInfo.
    // Since the VoxelSceneData is owned by VoxelGridNode's cachedSceneData_ (shared_ptr),
    // we create a non-owning shared_ptr using aliasing constructor with empty deleter.
    // This is safe because:
    // 1. VoxelGridNode's cachedSceneData_ keeps the data alive for the node lifetime
    // 2. The cacher only needs the data during Create() - it doesn't store the shared_ptr
    auto nonOwningSceneData = std::shared_ptr<CashSystem::VoxelSceneData>(
        std::shared_ptr<CashSystem::VoxelSceneData>{},  // empty control block
        voxelSceneData_  // managed pointer (non-owning)
    );

    // Compute scene data key from the scene metadata
    // We reconstruct the key from the stored metadata in VoxelSceneData
    CashSystem::VoxelSceneCreateInfo sceneParams;
    sceneParams.sceneType = voxelSceneData_->sceneType;
    sceneParams.resolution = voxelSceneData_->resolution;
    sceneParams.density = 0.5f;  // Default density (not stored in VoxelSceneData)
    sceneParams.seed = 42;       // Default seed (not stored in VoxelSceneData)
    uint64_t sceneDataKey = sceneParams.ComputeHash();

    CashSystem::AccelStructCreateInfo params;
    params.sceneData = nonOwningSceneData;
    params.sceneDataKey = sceneDataKey;
    params.preferFastTrace = preferFastTrace_;
    params.allowUpdate = allowUpdate_;
    params.allowCompaction = allowCompaction_;

    NODE_LOG_INFO("AccelerationStructureNode: Requesting AS via cacher: sceneKey=" +
                  std::to_string(sceneDataKey) +
                  ", fastTrace=" + std::to_string(preferFastTrace_) +
                  ", update=" + std::to_string(allowUpdate_) +
                  ", compact=" + std::to_string(allowCompaction_));

    // Call GetOrCreate - cacher handles AABB conversion and BLAS/TLAS build
    cachedAccelStruct_ = accelStructCacher_->GetOrCreate(params);

    if (!cachedAccelStruct_ || !cachedAccelStruct_->accelStruct.IsValid()) {
        throw std::runtime_error("[AccelerationStructureNode] Failed to get or create cached acceleration structure");
    }

    // Extract handles from cached data - copy fields manually between namespaced structs
    // CashSystem::AccelerationStructureData -> Vixen::RenderGraph::AccelerationStructureData
    const auto& src = cachedAccelStruct_->accelStruct;
    accelData_.blas = src.blas;
    accelData_.blasBuffer = src.blasBuffer;
    accelData_.blasMemory = src.blasMemory;
    accelData_.blasDeviceAddress = src.blasDeviceAddress;
    accelData_.tlas = src.tlas;
    accelData_.tlasBuffer = src.tlasBuffer;
    accelData_.tlasMemory = src.tlasMemory;
    accelData_.tlasDeviceAddress = src.tlasDeviceAddress;
    accelData_.instanceBuffer = src.instanceBuffer;
    accelData_.instanceMemory = src.instanceMemory;
    accelData_.scratchBuffer = src.scratchBuffer;
    accelData_.scratchMemory = src.scratchMemory;
    accelData_.primitiveCount = src.primitiveCount;

    NODE_LOG_INFO("AccelerationStructureNode: AS created via cacher: " +
                  std::to_string(accelData_.primitiveCount) + " primitives");
}

} // namespace Vixen::RenderGraph
