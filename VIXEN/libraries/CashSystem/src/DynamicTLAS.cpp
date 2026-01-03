#include "pch.h"
#include "DynamicTLAS.h"
#include "VulkanDevice.h"
#include "error/VulkanError.h"
#include "Memory/DeviceBudgetManager.h"

#include <cstring>
#include <stdexcept>

namespace CashSystem {

// ============================================================================
// LIFECYCLE
// ============================================================================

DynamicTLAS::~DynamicTLAS() {
    Cleanup(nullptr);
}

DynamicTLAS::DynamicTLAS(DynamicTLAS&& other) noexcept
    : device_(other.device_)
    , config_(other.config_)
    , frameTLAS_(std::move(other.frameTLAS_))
    , instanceBuffer_(std::move(other.instanceBuffer_))
    , vkCreateAS_(other.vkCreateAS_)
    , vkDestroyAS_(other.vkDestroyAS_)
    , vkGetASSizes_(other.vkGetASSizes_)
    , vkCmdBuildAS_(other.vkCmdBuildAS_)
    , vkGetASAddress_(other.vkGetASAddress_)
    , vkGetBufferAddress_(other.vkGetBufferAddress_)
    , rtFunctionsLoaded_(other.rtFunctionsLoaded_)
    , buildCommandPool_(other.buildCommandPool_)
{
    other.device_ = nullptr;
    other.buildCommandPool_ = VK_NULL_HANDLE;
    other.rtFunctionsLoaded_ = false;
}

DynamicTLAS& DynamicTLAS::operator=(DynamicTLAS&& other) noexcept {
    if (this != &other) {
        Cleanup(nullptr);

        device_ = other.device_;
        config_ = other.config_;
        frameTLAS_ = std::move(other.frameTLAS_);
        instanceBuffer_ = std::move(other.instanceBuffer_);
        vkCreateAS_ = other.vkCreateAS_;
        vkDestroyAS_ = other.vkDestroyAS_;
        vkGetASSizes_ = other.vkGetASSizes_;
        vkCmdBuildAS_ = other.vkCmdBuildAS_;
        vkGetASAddress_ = other.vkGetASAddress_;
        vkGetBufferAddress_ = other.vkGetBufferAddress_;
        rtFunctionsLoaded_ = other.rtFunctionsLoaded_;
        buildCommandPool_ = other.buildCommandPool_;

        other.device_ = nullptr;
        other.buildCommandPool_ = VK_NULL_HANDLE;
        other.rtFunctionsLoaded_ = false;
    }
    return *this;
}

bool DynamicTLAS::Initialize(
    Vixen::Vulkan::Resources::VulkanDevice* device,
    uint32_t imageCount,
    const Config& config)
{
    // Initialize logger for this subsystem
    InitializeLogger("DynamicTLAS", true);

    if (!device || imageCount == 0) {
        LOG_ERROR("[DynamicTLAS::Initialize] Invalid parameters");
        return false;
    }

    Cleanup(nullptr);

    device_ = device;
    config_ = config;

    // Load RT functions
    LoadRTFunctions();
    if (!vkCreateAS_ || !vkCmdBuildAS_) {
        LOG_ERROR("[DynamicTLAS::Initialize] RT extensions not available");
        Cleanup(nullptr);
        return false;
    }

    // Initialize frame TLAS container
    frameTLAS_.resize(imageCount);
    for (size_t i = 0; i < imageCount; ++i) {
        frameTLAS_.MarkDirty(i);  // All frames start dirty
    }

    // Initialize instance buffer via VulkanDevice centralized API
    TLASInstanceBuffer::Config bufConfig;
    bufConfig.maxInstances = config.maxInstances;

    if (!instanceBuffer_.Initialize(device, imageCount, bufConfig)) {
        LOG_ERROR("[DynamicTLAS::Initialize] Failed to initialize instance buffer");
        Cleanup(nullptr);
        return false;
    }

    LOG_INFO("[DynamicTLAS::Initialize] Initialized with " + std::to_string(imageCount) +
             " frames, max " + std::to_string(config.maxInstances) + " instances");

    return true;
}

void DynamicTLAS::Cleanup(ResourceManagement::DeferredDestructionQueue* deferQueue) {
    if (!device_) {
        return;
    }

    // Cleanup per-frame TLAS structures
    for (size_t i = 0; i < frameTLAS_.size(); ++i) {
        auto& ft = frameTLAS_[i].value;

        if (ft.tlas != VK_NULL_HANDLE && vkDestroyAS_) {
            if (deferQueue) {
                // Deferred destruction for zero-stutter
                VkDevice dev = device_->device;
                VkAccelerationStructureKHR tlas = ft.tlas;
                auto destroyFn = vkDestroyAS_;
                deferQueue->AddGeneric([dev, tlas, destroyFn]() {
                    destroyFn(dev, tlas, nullptr);
                }, 0);
            } else {
                vkDestroyAS_(device_->device, ft.tlas, nullptr);
            }
            ft.tlas = VK_NULL_HANDLE;
        }

        if (ft.tlasBuffer.buffer != VK_NULL_HANDLE) {
            device_->FreeBuffer(ft.tlasBuffer);
            ft.tlasBuffer = {};
        }

        if (ft.scratchBuffer.buffer != VK_NULL_HANDLE) {
            device_->FreeBuffer(ft.scratchBuffer);
            ft.scratchBuffer = {};
        }

        ft.deviceAddress = 0;
        ft.lastInstanceCount = 0;
    }
    frameTLAS_.clear();

    // Cleanup instance buffer
    instanceBuffer_.Cleanup();

    // Cleanup command pool
    if (buildCommandPool_ != VK_NULL_HANDLE) {
        vkDestroyCommandPool(device_->device, buildCommandPool_, nullptr);
        buildCommandPool_ = VK_NULL_HANDLE;
    }

    device_ = nullptr;
    rtFunctionsLoaded_ = false;

    LOG_DEBUG("[DynamicTLAS::Cleanup] Cleanup complete");
}

// ============================================================================
// PER-FRAME OPERATIONS
// ============================================================================

void DynamicTLAS::UpdateInstances(uint32_t imageIndex, const TLASInstanceManager& manager) {
    if (!ValidateImageIndex(imageIndex)) {
        return;
    }

    // Generate Vulkan instances from manager
    std::vector<VkAccelerationStructureInstanceKHR> instances;
    manager.GenerateVulkanInstances(instances);

    // Write to frame's instance buffer
    instanceBuffer_.WriteInstances(imageIndex, instances);

    // Check if instance count changed (structural change)
    auto& ft = frameTLAS_[imageIndex].value;
    if (ft.lastInstanceCount != static_cast<uint32_t>(instances.size())) {
        frameTLAS_.MarkDirty(imageIndex);  // Need full rebuild
    }
}

TLASBuildParams DynamicTLAS::PrepareBuild(
    uint32_t imageIndex,
    TLASInstanceManager::DirtyLevel dirtyLevel)
{
    TLASBuildParams params{};

    if (!ValidateImageIndex(imageIndex)) {
        return params;
    }

    const uint32_t instanceCount = instanceBuffer_.GetInstanceCount(imageIndex);
    if (instanceCount == 0) {
        LOG_DEBUG("[DynamicTLAS::PrepareBuild] No instances - skipping build");
        return params;
    }

    // Ensure TLAS buffer is allocated
    if (!EnsureTLASBuffer(imageIndex, instanceCount)) {
        return params;
    }

    auto& ft = frameTLAS_[imageIndex].value;

    // Determine build mode
    VkBuildAccelerationStructureModeKHR buildMode =
        VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;

    if (config_.allowUpdate &&
        dirtyLevel == TLASInstanceManager::DirtyLevel::TransformsOnly &&
        ft.tlas != VK_NULL_HANDLE &&
        ft.lastInstanceCount == instanceCount)
    {
        buildMode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR;
        params.isUpdate = true;
    }

    // Setup geometry (instances)
    params.instancesData.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
    params.instancesData.data.deviceAddress = instanceBuffer_.GetDeviceAddress(imageIndex);

    params.geometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    params.geometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
    params.geometry.geometry.instances = params.instancesData;

    // Build info
    params.buildInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    params.buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    params.buildInfo.flags = GetBuildFlags();
    params.buildInfo.mode = buildMode;
    params.buildInfo.srcAccelerationStructure = (buildMode == VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR)
        ? ft.tlas : VK_NULL_HANDLE;
    params.buildInfo.dstAccelerationStructure = ft.tlas;
    params.buildInfo.geometryCount = 1;
    params.buildInfo.pGeometries = &params.geometry;
    params.buildInfo.scratchData.deviceAddress = ft.scratchBuffer.deviceAddress;

    // Build range
    params.rangeInfo.primitiveCount = instanceCount;
    params.rangeInfo.primitiveOffset = 0;
    params.rangeInfo.firstVertex = 0;
    params.rangeInfo.transformOffset = 0;

    params.shouldBuild = true;

    LOG_DEBUG("[DynamicTLAS::PrepareBuild] Frame " + std::to_string(imageIndex) +
              " prepared with " + std::to_string(instanceCount) + " instances, mode=" +
              (params.isUpdate ? "UPDATE" : "BUILD"));

    return params;
}

void DynamicTLAS::MarkBuilt(uint32_t imageIndex, uint32_t instanceCount) {
    if (!ValidateImageIndex(imageIndex)) {
        return;
    }

    auto& ft = frameTLAS_[imageIndex].value;
    ft.lastInstanceCount = instanceCount;
    frameTLAS_.MarkReady(imageIndex);
}

#pragma warning(push)
#pragma warning(disable: 4996)  // Disable deprecation warning for this implementation
bool DynamicTLAS::BuildOrUpdate(
    uint32_t imageIndex,
    TLASInstanceManager::DirtyLevel dirtyLevel,
    VkCommandBuffer cmdBuffer)
{
    if (!ValidateImageIndex(imageIndex)) {
        return false;
    }

    const uint32_t instanceCount = instanceBuffer_.GetInstanceCount(imageIndex);
    if (instanceCount == 0) {
        LOG_DEBUG("[DynamicTLAS::BuildOrUpdate] No instances - skipping build");
        return false;
    }

    // Ensure TLAS buffer is allocated
    if (!EnsureTLASBuffer(imageIndex, instanceCount)) {
        return false;
    }

    auto& ft = frameTLAS_[imageIndex].value;

    // Determine build mode
    VkBuildAccelerationStructureModeKHR buildMode =
        VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;

    // Use UPDATE mode if:
    // - allowUpdate is enabled
    // - Only transforms changed (not structural)
    // - TLAS already exists
    // - Instance count hasn't changed
    if (config_.allowUpdate &&
        dirtyLevel == TLASInstanceManager::DirtyLevel::TransformsOnly &&
        ft.tlas != VK_NULL_HANDLE &&
        ft.lastInstanceCount == instanceCount)
    {
        buildMode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR;
    }

    // Setup geometry (instances)
    VkAccelerationStructureGeometryInstancesDataKHR instancesData{};
    instancesData.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
    instancesData.data.deviceAddress = instanceBuffer_.GetDeviceAddress(imageIndex);

    VkAccelerationStructureGeometryKHR geometry{};
    geometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    geometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
    geometry.geometry.instances = instancesData;

    // Build info
    VkAccelerationStructureBuildGeometryInfoKHR buildInfo{};
    buildInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    buildInfo.flags = GetBuildFlags();
    buildInfo.mode = buildMode;
    buildInfo.srcAccelerationStructure = (buildMode == VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR)
        ? ft.tlas : VK_NULL_HANDLE;
    buildInfo.dstAccelerationStructure = ft.tlas;
    buildInfo.geometryCount = 1;
    buildInfo.pGeometries = &geometry;
    buildInfo.scratchData.deviceAddress = ft.scratchBuffer.deviceAddress;

    // Build range
    VkAccelerationStructureBuildRangeInfoKHR rangeInfo{};
    rangeInfo.primitiveCount = instanceCount;
    rangeInfo.primitiveOffset = 0;
    rangeInfo.firstVertex = 0;
    rangeInfo.transformOffset = 0;

    const VkAccelerationStructureBuildRangeInfoKHR* pRangeInfo = &rangeInfo;

    // Record build command
    vkCmdBuildAS_(cmdBuffer, 1, &buildInfo, &pRangeInfo);

    // Update state
    ft.lastInstanceCount = instanceCount;
    frameTLAS_.MarkReady(imageIndex);

    LOG_DEBUG("[DynamicTLAS::BuildOrUpdate] Frame " + std::to_string(imageIndex) +
              " built with " + std::to_string(instanceCount) + " instances, mode=" +
              (buildMode == VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR ? "UPDATE" : "BUILD"));

    return true;
}

// ============================================================================
// PER-FRAME ACCESSORS
// ============================================================================

VkAccelerationStructureKHR DynamicTLAS::GetTLAS(uint32_t imageIndex) const {
    if (!ValidateImageIndex(imageIndex)) {
        return VK_NULL_HANDLE;
    }
    return frameTLAS_[imageIndex].value.tlas;
}

VkDeviceAddress DynamicTLAS::GetDeviceAddress(uint32_t imageIndex) const {
    if (!ValidateImageIndex(imageIndex)) {
        return 0;
    }
    return frameTLAS_[imageIndex].value.deviceAddress;
}

ResourceManagement::ContainerState DynamicTLAS::GetState(uint32_t imageIndex) const {
    if (!ValidateImageIndex(imageIndex)) {
        return ResourceManagement::ContainerState::Invalid;
    }
    return frameTLAS_.GetState(imageIndex);
}

bool DynamicTLAS::IsValid(uint32_t imageIndex) const {
    if (!ValidateImageIndex(imageIndex)) {
        return false;
    }
    return frameTLAS_[imageIndex].value.tlas != VK_NULL_HANDLE &&
           frameTLAS_.IsReady(imageIndex);
}

// ============================================================================
// BUDGET AND MEMORY
// ============================================================================

VkDeviceSize DynamicTLAS::GetCurrentMemoryUsage() const {
    VkDeviceSize total = 0;
    for (size_t i = 0; i < frameTLAS_.size(); ++i) {
        const auto& ft = frameTLAS_[i].value;
        total += ft.tlasBuffer.size;
        total += ft.scratchBuffer.size;
    }
    total += instanceBuffer_.GetMaxInstances() * sizeof(VkAccelerationStructureInstanceKHR) *
             instanceBuffer_.GetFrameCount();
    return total;
}

VkDeviceSize DynamicTLAS::GetPerFrameMemoryUsage() const {
    if (frameTLAS_.empty()) {
        return 0;
    }
    const auto& ft = frameTLAS_[0].value;
    return ft.tlasBuffer.size + ft.scratchBuffer.size +
           instanceBuffer_.GetMaxInstances() * sizeof(VkAccelerationStructureInstanceKHR);
}

VkDeviceSize DynamicTLAS::GetMaxMemoryUsage() const {
    return GetCurrentMemoryUsage();  // Currently at max (pre-allocated)
}

// ============================================================================
// INTERNAL HELPERS
// ============================================================================

void DynamicTLAS::LoadRTFunctions() {
    if (rtFunctionsLoaded_ || !device_) {
        return;
    }

    VkDevice dev = device_->device;

    vkCreateAS_ = reinterpret_cast<PFN_vkCreateAccelerationStructureKHR>(
        vkGetDeviceProcAddr(dev, "vkCreateAccelerationStructureKHR"));
    vkDestroyAS_ = reinterpret_cast<PFN_vkDestroyAccelerationStructureKHR>(
        vkGetDeviceProcAddr(dev, "vkDestroyAccelerationStructureKHR"));
    vkGetASSizes_ = reinterpret_cast<PFN_vkGetAccelerationStructureBuildSizesKHR>(
        vkGetDeviceProcAddr(dev, "vkGetAccelerationStructureBuildSizesKHR"));
    vkCmdBuildAS_ = reinterpret_cast<PFN_vkCmdBuildAccelerationStructuresKHR>(
        vkGetDeviceProcAddr(dev, "vkCmdBuildAccelerationStructuresKHR"));
    vkGetASAddress_ = reinterpret_cast<PFN_vkGetAccelerationStructureDeviceAddressKHR>(
        vkGetDeviceProcAddr(dev, "vkGetAccelerationStructureDeviceAddressKHR"));
    vkGetBufferAddress_ = reinterpret_cast<PFN_vkGetBufferDeviceAddressKHR>(
        vkGetDeviceProcAddr(dev, "vkGetBufferDeviceAddressKHR"));

    rtFunctionsLoaded_ = true;

    LOG_DEBUG("[DynamicTLAS] RT functions loaded: create=" +
              std::string(vkCreateAS_ ? "yes" : "no") +
              ", build=" + std::string(vkCmdBuildAS_ ? "yes" : "no"));
}

VkBuildAccelerationStructureFlagsKHR DynamicTLAS::GetBuildFlags() const {
    VkBuildAccelerationStructureFlagsKHR flags = 0;

    if (config_.preferFastTrace) {
        flags |= VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    } else {
        flags |= VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR;
    }

    if (config_.allowUpdate) {
        flags |= VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR;
    }

    return flags;
}

bool DynamicTLAS::EnsureTLASBuffer(uint32_t imageIndex, uint32_t instanceCount) {
    auto& ft = frameTLAS_[imageIndex].value;

    // Check if we need to (re)allocate
    bool needsAlloc = (ft.tlas == VK_NULL_HANDLE);

    // For simplicity, we don't resize - just ensure allocated for max
    // In production, might want to grow dynamically

    if (!needsAlloc && ft.tlas != VK_NULL_HANDLE) {
        return true;  // Already allocated
    }

    // Query size requirements
    VkAccelerationStructureGeometryInstancesDataKHR instancesData{};
    instancesData.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;

    VkAccelerationStructureGeometryKHR geometry{};
    geometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    geometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
    geometry.geometry.instances = instancesData;

    VkAccelerationStructureBuildGeometryInfoKHR buildInfo{};
    buildInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    buildInfo.flags = GetBuildFlags();
    buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    buildInfo.geometryCount = 1;
    buildInfo.pGeometries = &geometry;

    VkAccelerationStructureBuildSizesInfoKHR sizeInfo{};
    sizeInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;

    uint32_t maxCount = config_.maxInstances;
    vkGetASSizes_(device_->device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
                  &buildInfo, &maxCount, &sizeInfo);

    // Budget check via VulkanDevice
    auto* budgetManager = device_->GetBudgetManager();
    if (budgetManager && budgetManager->IsNearBudgetLimit()) {
        LOG_WARNING("[DynamicTLAS::EnsureTLASBuffer] Near budget limit for frame " +
                    std::to_string(imageIndex));
        // Continue anyway - allocation will fail if truly out of memory
    }

    // Allocate TLAS buffer via VulkanDevice centralized API
    ResourceManagement::BufferAllocationRequest tlasReq;
    tlasReq.size = sizeInfo.accelerationStructureSize;
    tlasReq.usage = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR |
                    VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    tlasReq.location = ResourceManagement::MemoryLocation::DeviceLocal;
    tlasReq.debugName = "DynamicTLAS";

    auto tlasAlloc = device_->AllocateBuffer(tlasReq);
    if (!tlasAlloc) {
        LOG_ERROR("[DynamicTLAS::EnsureTLASBuffer] Failed to allocate TLAS buffer");
        return false;
    }
    ft.tlasBuffer = *tlasAlloc;

    // Allocate scratch buffer via VulkanDevice centralized API
    ResourceManagement::BufferAllocationRequest scratchReq;
    scratchReq.size = sizeInfo.buildScratchSize;
    scratchReq.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                       VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    scratchReq.location = ResourceManagement::MemoryLocation::DeviceLocal;
    scratchReq.debugName = "DynamicTLAS_scratch";

    auto scratchAlloc = device_->AllocateBuffer(scratchReq);
    if (!scratchAlloc) {
        LOG_ERROR("[DynamicTLAS::EnsureTLASBuffer] Failed to allocate scratch buffer");
        device_->FreeBuffer(ft.tlasBuffer);
        ft.tlasBuffer = {};
        return false;
    }
    ft.scratchBuffer = *scratchAlloc;

    // Create acceleration structure
    VkAccelerationStructureCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
    createInfo.buffer = ft.tlasBuffer.buffer;
    createInfo.size = sizeInfo.accelerationStructureSize;
    createInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;

    VkResult result = vkCreateAS_(device_->device, &createInfo, nullptr, &ft.tlas);
    if (result != VK_SUCCESS) {
        LOG_ERROR("[DynamicTLAS::EnsureTLASBuffer] Failed to create TLAS");
        device_->FreeBuffer(ft.tlasBuffer);
        device_->FreeBuffer(ft.scratchBuffer);
        ft.tlasBuffer = {};
        ft.scratchBuffer = {};
        return false;
    }

    // Get device address
    VkAccelerationStructureDeviceAddressInfoKHR addressInfo{};
    addressInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
    addressInfo.accelerationStructure = ft.tlas;
    ft.deviceAddress = vkGetASAddress_(device_->device, &addressInfo);

    LOG_INFO("[DynamicTLAS::EnsureTLASBuffer] Allocated frame " + std::to_string(imageIndex) +
             " TLAS=" + std::to_string(sizeInfo.accelerationStructureSize / 1024) + "KB" +
             " scratch=" + std::to_string(sizeInfo.buildScratchSize / 1024) + "KB");

    return true;
}

bool DynamicTLAS::ValidateImageIndex(uint32_t imageIndex) const {
    if (imageIndex >= frameTLAS_.size()) {
        LOG_ERROR("[DynamicTLAS] Invalid imageIndex " + std::to_string(imageIndex) +
                  " >= " + std::to_string(frameTLAS_.size()));
        return false;
    }
    return true;
}

} // namespace CashSystem
