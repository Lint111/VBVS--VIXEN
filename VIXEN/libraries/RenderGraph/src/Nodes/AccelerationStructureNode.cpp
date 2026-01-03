#include "Nodes/AccelerationStructureNode.h"
#include "VulkanDevice.h"
#include "Core/NodeLogging.h"
#include "Core/RenderGraph.h"
#include "MainCacher.h"
#include "AccelerationStructureCacher.h"
#include "DynamicTLAS.h"
#include "TLASInstanceManager.h"
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

    // Note: BUILD_MODE is read during CompileImpl since ctx.In() is only available there

    NODE_LOG_INFO("AccelerationStructure setup: preferFastTrace=" +
                  std::to_string(preferFastTrace_) +
                  ", allowUpdate=" + std::to_string(allowUpdate_) +
                  ", allowCompaction=" + std::to_string(allowCompaction_));

    NODE_LOG_DEBUG("[AccelerationStructureNode::SetupImpl] COMPLETED");
}

void AccelerationStructureNode::CompileImpl(TypedCompileContext& ctx) {
    NODE_LOG_DEBUG("[AccelerationStructureNode::CompileImpl] ENTERED");
    NODE_LOG_INFO("=== AccelerationStructureNode::CompileImpl START ===");

    // Read build mode (optional input, defaults to Static)
    // For optional slots, ctx.In() returns default-initialized value if not connected
    buildMode_ = ctx.In(AccelerationStructureNodeConfig::BUILD_MODE);
    NODE_LOG_INFO("Build mode: " + std::string(IsDynamicMode() ? "Dynamic" : "Static"));

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

    // Output the acceleration structure data (BLAS always valid from cacher)
    ctx.Out(AccelerationStructureNodeConfig::ACCELERATION_STRUCTURE_DATA, &accelData_);

    // For static mode: output TLAS handle from cached data
    // For dynamic mode: TLAS handle is per-frame (output during Execute)
    if (!IsDynamicMode()) {
        ctx.Out(AccelerationStructureNodeConfig::TLAS_HANDLE, accelData_.tlas);
        NODE_LOG_INFO("Static mode: TLAS handle output during Compile");
    } else {
        // Initialize DynamicTLAS for per-frame rebuilds
        // For now, use a default imageCount of 3 (typical swapchain size)
        // TODO: Get imageCount from SwapChainNode via IMAGE_INDEX slot connection metadata
        InitializeDynamicTLAS(3);
        NODE_LOG_INFO("Dynamic mode: DynamicTLAS initialized, TLAS handle output per-frame");
    }

    NODE_LOG_INFO("=== AccelerationStructureNode::CompileImpl COMPLETE ===");
    NODE_LOG_INFO("BLAS address: 0x" + std::to_string(accelData_.blasDeviceAddress));
    if (!IsDynamicMode()) {
        NODE_LOG_INFO("TLAS address: 0x" + std::to_string(accelData_.tlasDeviceAddress));
    }
    NODE_LOG_DEBUG("[AccelerationStructureNode::CompileImpl] COMPLETED");
}

void AccelerationStructureNode::ExecuteImpl(TypedExecuteContext& ctx) {
    // Pass through BLAS data (always valid from cacher)
    ctx.Out(AccelerationStructureNodeConfig::ACCELERATION_STRUCTURE_DATA, &accelData_);

    if (!IsDynamicMode()) {
        // Static mode: pass through cached TLAS handle
        ctx.Out(AccelerationStructureNodeConfig::TLAS_HANDLE, accelData_.tlas);
    } else {
        // Dynamic mode: get per-frame TLAS from DynamicTLAS
        if (!dynamicTLAS_ || !instanceManager_) {
            NODE_LOG_ERROR("Dynamic mode but DynamicTLAS not initialized!");
            ctx.Out(AccelerationStructureNodeConfig::TLAS_HANDLE, VK_NULL_HANDLE);
            return;
        }

        // Get image index from input (optional, defaults to 0 if not connected)
        uint32_t imageIndex = ctx.In(AccelerationStructureNodeConfig::IMAGE_INDEX);

        // Update instances and rebuild TLAS if dirty
        dynamicTLAS_->UpdateInstances(imageIndex, *instanceManager_);

        // Get the per-frame TLAS handle
        VkAccelerationStructureKHR tlasHandle = dynamicTLAS_->GetTLAS(imageIndex);
        ctx.Out(AccelerationStructureNodeConfig::TLAS_HANDLE, tlasHandle);
    }
}

void AccelerationStructureNode::CleanupImpl(TypedCleanupContext& ctx) {
    NODE_LOG_INFO("AccelerationStructureNode cleanup");
    DestroyAccelerationStructures();
}

void AccelerationStructureNode::DestroyAccelerationStructures() {
    if (!vulkanDevice_) {
        return;
    }

    // Cleanup dynamic TLAS if used
    if (dynamicTLAS_) {
        NODE_LOG_DEBUG("AccelerationStructureNode: Cleaning up DynamicTLAS");
        dynamicTLAS_->Cleanup();
        dynamicTLAS_.reset();
    }
    if (instanceManager_) {
        instanceManager_.reset();
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
    // Copy all acceleration structure data at once
    // Note: BufferAllocation members are now used instead of raw VkBuffer/VkDeviceMemory
    accelData_ = src;

    NODE_LOG_INFO("AccelerationStructureNode: AS created via cacher: " +
                  std::to_string(accelData_.primitiveCount) + " primitives");
}

// ============================================================================
// DYNAMIC TLAS INITIALIZATION
// ============================================================================

void AccelerationStructureNode::InitializeDynamicTLAS(uint32_t imageCount) {
    if (!vulkanDevice_) {
        throw std::runtime_error("[AccelerationStructureNode] Device not set for dynamic TLAS");
    }

    // Create instance manager
    instanceManager_ = std::make_unique<CashSystem::TLASInstanceManager>();

    // Add the BLAS from the cached acceleration structure as default instance
    if (cachedAccelStruct_ && cachedAccelStruct_->accelStruct.IsValid()) {
        CashSystem::TLASInstanceManager::Instance instance{};
        instance.blasKey = 0;  // Default key for the primary BLAS
        instance.blasAddress = accelData_.blasDeviceAddress;
        instance.transform = glm::mat3x4(1.0f);  // Identity transform
        instance.customIndex = 0;
        instance.mask = 0xFF;
        instance.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;

        instanceManager_->AddInstance(instance);
        NODE_LOG_INFO("DynamicTLAS: Added BLAS instance with address 0x" +
                      std::to_string(accelData_.blasDeviceAddress));
    }

    // Create and initialize DynamicTLAS
    dynamicTLAS_ = std::make_unique<CashSystem::DynamicTLAS>();

    CashSystem::DynamicTLAS::Config config{};
    config.maxInstances = 1024;
    config.preferFastTrace = preferFastTrace_;
    config.allowUpdate = allowUpdate_;

    if (!dynamicTLAS_->Initialize(vulkanDevice_, imageCount, config)) {
        throw std::runtime_error("[AccelerationStructureNode] Failed to initialize DynamicTLAS");
    }

    NODE_LOG_INFO("DynamicTLAS initialized with " + std::to_string(imageCount) + " frame buffers");
}

} // namespace Vixen::RenderGraph
