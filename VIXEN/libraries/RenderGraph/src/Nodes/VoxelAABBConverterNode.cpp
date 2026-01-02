#include "Nodes/VoxelAABBConverterNode.h"
#include "VulkanDevice.h"
#include "Core/NodeLogging.h"
#include "Core/RenderGraph.h"
#include <MainCacher.h>
#include <VoxelAABBCacher.h>

namespace Vixen::RenderGraph {

// ============================================================================
// NODE TYPE FACTORY
// ============================================================================

std::unique_ptr<NodeInstance> VoxelAABBConverterNodeType::CreateInstance(
    const std::string& instanceName
) const {
    return std::unique_ptr<NodeInstance>(
        new VoxelAABBConverterNode(instanceName, const_cast<VoxelAABBConverterNodeType*>(this))
    );
}

// ============================================================================
// VOXEL AABB CONVERTER NODE IMPLEMENTATION
// ============================================================================

VoxelAABBConverterNode::VoxelAABBConverterNode(
    const std::string& instanceName,
    NodeType* nodeType
) : TypedNode<VoxelAABBConverterNodeConfig>(instanceName, nodeType)
{
    NODE_LOG_INFO("VoxelAABBConverterNode constructor (Phase K - using VoxelAABBCacher)");
}

void VoxelAABBConverterNode::SetupImpl(TypedSetupContext& ctx) {
    NODE_LOG_DEBUG("[VoxelAABBConverterNode::SetupImpl] ENTERED");

    // Read parameters
    gridResolution_ = GetParameterValue<uint32_t>(
        VoxelAABBConverterNodeConfig::PARAM_GRID_RESOLUTION, 128u);
    voxelSize_ = GetParameterValue<float>(
        VoxelAABBConverterNodeConfig::PARAM_VOXEL_SIZE, 1.0f);

    NODE_LOG_INFO("VoxelAABBConverter setup: resolution=" + std::to_string(gridResolution_) +
                  ", voxelSize=" + std::to_string(voxelSize_));
    NODE_LOG_DEBUG("[VoxelAABBConverterNode::SetupImpl] COMPLETED");
}

void VoxelAABBConverterNode::EnsureCacherRegistered() {
    // Get MainCacher from owning graph
    auto& mainCacher = GetOwningGraph()->GetMainCacher();

    // Register VoxelAABBCacher (idempotent - safe to call multiple times)
    if (!mainCacher.IsRegistered(typeid(CashSystem::VoxelAABBData))) {
        mainCacher.RegisterCacher<
            CashSystem::VoxelAABBCacher,
            CashSystem::VoxelAABBData,
            CashSystem::VoxelAABBCreateInfo
        >(
            typeid(CashSystem::VoxelAABBData),
            "VoxelAABBCacher",
            true  // device-dependent
        );
        NODE_LOG_DEBUG("Registered VoxelAABBCacher with MainCacher");
    }
}

void VoxelAABBConverterNode::CompileImpl(TypedCompileContext& ctx) {
    NODE_LOG_DEBUG("[VoxelAABBConverterNode::CompileImpl] ENTERED");

    NODE_LOG_INFO("=== VoxelAABBConverterNode::CompileImpl START ===");

    // Get device
    VulkanDevice* devicePtr = ctx.In(VoxelAABBConverterNodeConfig::VULKAN_DEVICE_IN);
    if (!devicePtr) {
        throw std::runtime_error("[VoxelAABBConverterNode] VULKAN_DEVICE_IN is null");
    }
    SetDevice(devicePtr);
    vulkanDevice_ = devicePtr;

    // Get VoxelSceneData (required for cacher)
    voxelSceneData_ = ctx.In(VoxelAABBConverterNodeConfig::VOXEL_SCENE_DATA);
    if (!voxelSceneData_) {
        throw std::runtime_error("[VoxelAABBConverterNode] VOXEL_SCENE_DATA is required - connect VoxelGridNode.VOXEL_SCENE_DATA");
    }

    NODE_LOG_INFO("Received VoxelSceneData with " +
                  std::to_string(voxelSceneData_->solidVoxelCount) + " solid voxels, " +
                  std::to_string(voxelSceneData_->brickCount) + " bricks");

    // Ensure cacher is registered
    EnsureCacherRegistered();

    // Get or create the VoxelAABBCacher
    auto& mainCacher = GetOwningGraph()->GetMainCacher();
    auto* aabbCacher = mainCacher.GetCacher<
        CashSystem::VoxelAABBCacher,
        CashSystem::VoxelAABBData,
        CashSystem::VoxelAABBCreateInfo
    >(typeid(CashSystem::VoxelAABBData), vulkanDevice_);

    if (!aabbCacher) {
        throw std::runtime_error("[VoxelAABBConverterNode] Failed to get VoxelAABBCacher");
    }

    NODE_LOG_DEBUG("Got VoxelAABBCacher from MainCacher");

    // Build cache key from scene data
    // Note: We need to use the same key that VoxelSceneCacher used
    CashSystem::VoxelSceneCreateInfo sceneCI;
    sceneCI.sceneType = voxelSceneData_->sceneType;
    sceneCI.resolution = voxelSceneData_->resolution;
    sceneCI.density = 0.5f;  // Default density (not stored in VoxelSceneData)
    // Todo: Seed should be part of voxel scene data or render graph scope to ensure consistent caching and determinism
    sceneCI.seed = 42;       // Default seed (not stored in VoxelSceneData)
    uint64_t sceneDataKey = sceneCI.ComputeHash();

    // Create AABB creation info
    CashSystem::VoxelAABBCreateInfo aabbCI;
    aabbCI.sceneDataKey = sceneDataKey;
    aabbCI.sceneData = std::shared_ptr<CashSystem::VoxelSceneData>(
        voxelSceneData_,
        [](CashSystem::VoxelSceneData*) {}  // No-op deleter - we don't own this
    );
    aabbCI.voxelSize = voxelSize_;
    aabbCI.gridResolution = gridResolution_;

    NODE_LOG_INFO("Requesting AABB data from cacher (key=" + std::to_string(aabbCI.ComputeHash()) +
                  ", voxelSize=" + std::to_string(voxelSize_) + ")");

    // Get or create cached AABB data
    cachedAABBData_ = aabbCacher->GetOrCreate(aabbCI);

    if (!cachedAABBData_ || !cachedAABBData_->IsValid()) {
        NODE_LOG_WARNING("VoxelAABBCacher returned empty data - no solid voxels?");
        // Create minimal empty data
        cachedAABBData_ = std::make_shared<CashSystem::VoxelAABBData>();
        cachedAABBData_->gridResolution = gridResolution_;
        cachedAABBData_->voxelSize = voxelSize_;
    } else {
        NODE_LOG_INFO("Received cached AABB data: " + std::to_string(cachedAABBData_->aabbCount) + " AABBs");
    }

    // Output the AABB data
    ctx.Out(VoxelAABBConverterNodeConfig::AABB_DATA, cachedAABBData_.get());
    ctx.Out(VoxelAABBConverterNodeConfig::AABB_BUFFER, cachedAABBData_->GetAABBBuffer());
    ctx.Out(VoxelAABBConverterNodeConfig::MATERIAL_ID_BUFFER, cachedAABBData_->GetMaterialIdBuffer());
    ctx.Out(VoxelAABBConverterNodeConfig::BRICK_MAPPING_BUFFER, cachedAABBData_->GetBrickMappingBuffer());

    NODE_LOG_INFO("=== VoxelAABBConverterNode::CompileImpl COMPLETE ===");
    NODE_LOG_DEBUG("[VoxelAABBConverterNode::CompileImpl] COMPLETED");
}

void VoxelAABBConverterNode::ExecuteImpl(TypedExecuteContext& ctx) {
    // AABB data is static (created during compile via cacher)
    // Just pass through the cached data pointers
    if (cachedAABBData_) {
        ctx.Out(VoxelAABBConverterNodeConfig::AABB_DATA, cachedAABBData_.get());
        ctx.Out(VoxelAABBConverterNodeConfig::AABB_BUFFER, cachedAABBData_->GetAABBBuffer());
        ctx.Out(VoxelAABBConverterNodeConfig::MATERIAL_ID_BUFFER, cachedAABBData_->GetMaterialIdBuffer());
        ctx.Out(VoxelAABBConverterNodeConfig::BRICK_MAPPING_BUFFER, cachedAABBData_->GetBrickMappingBuffer());
    }
}

void VoxelAABBConverterNode::CleanupImpl(TypedCleanupContext& ctx) {
    NODE_LOG_INFO("VoxelAABBConverterNode cleanup - releasing cacher reference");

    // Release our shared_ptr reference to the cached data
    // The cacher owns the GPU resources and will clean them up when appropriate
    cachedAABBData_.reset();
    voxelSceneData_ = nullptr;
    vulkanDevice_ = nullptr;
}

} // namespace Vixen::RenderGraph
