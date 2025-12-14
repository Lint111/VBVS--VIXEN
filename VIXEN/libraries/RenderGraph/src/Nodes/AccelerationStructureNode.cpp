#include "Nodes/AccelerationStructureNode.h"
#include "VulkanDevice.h"
#include "Core/NodeLogging.h"
#include "Core/RenderGraph.h"
#include "MainCacher.h"
#include "AccelerationStructureCacher.h"
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
    NODE_LOG_INFO("AccelerationStructureNode constructor (Phase K - using AccelerationStructureCacher)");
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

    // Get AABB data from VoxelAABBConverterNode (via VoxelAABBCacher)
    VoxelAABBData* aabbData = ctx.In(AccelerationStructureNodeConfig::AABB_DATA);
    if (!aabbData || !aabbData->IsValid()) {
        throw std::runtime_error("[AccelerationStructureNode] AABB_DATA is null or invalid - connect VoxelAABBConverterNode.AABB_DATA");
    }

    NODE_LOG_INFO("Building acceleration structures for " +
                  std::to_string(aabbData->aabbCount) + " AABBs");

    // Build BLAS/TLAS via cacher
    if (!accelStructCacher_) {
        throw std::runtime_error("[AccelerationStructureNode] AccelerationStructureCacher not registered - cannot proceed");
    }
    CreateAccelStructViaCacher(*aabbData);
    NODE_LOG_INFO("AccelerationStructureNode: AS created via cacher");

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

    NODE_LOG_INFO("Cleanup complete (resources managed by AccelerationStructureCacher)");
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

void AccelerationStructureNode::CreateAccelStructViaCacher(VoxelAABBData& aabbData) {
    if (!accelStructCacher_) {
        throw std::runtime_error("[AccelerationStructureNode] AccelerationStructureCacher not registered");
    }

    // Build cache parameters - simplified API: just AABB data + build flags
    // AABB extraction is now handled by VoxelAABBCacher, not AccelerationStructureCacher
    CashSystem::AccelStructCreateInfo params;
    // Cast VoxelAABBData between namespaces - structs are identical (both alias CashSystem::VoxelAABBData)
    params.aabbData = reinterpret_cast<CashSystem::VoxelAABBData*>(&aabbData);
    params.preferFastTrace = preferFastTrace_;
    params.allowUpdate = allowUpdate_;
    params.allowCompaction = allowCompaction_;

    NODE_LOG_INFO("AccelerationStructureNode: Requesting AS via cacher: aabbCount=" +
                  std::to_string(aabbData.aabbCount) +
                  ", fastTrace=" + std::to_string(preferFastTrace_) +
                  ", update=" + std::to_string(allowUpdate_) +
                  ", compact=" + std::to_string(allowCompaction_));

    // Call GetOrCreate - cacher builds BLAS/TLAS from pre-extracted AABBs
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
    accelData_.blasBuildTimeMs = src.blasBuildTimeMs;
    accelData_.tlasBuildTimeMs = src.tlasBuildTimeMs;

    NODE_LOG_INFO("AccelerationStructureNode: AS created via cacher: " +
                  std::to_string(accelData_.primitiveCount) + " primitives");
}

} // namespace Vixen::RenderGraph
