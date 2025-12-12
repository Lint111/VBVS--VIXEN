#pragma once

#include "Core/TypedNodeInstance.h"
#include "Core/NodeType.h"
#include "Core/NodeLogging.h"
#include "Data/Nodes/VoxelAABBConverterNodeConfig.h"
#include <VoxelAABBCacher.h>  // CashSystem::VoxelAABBCacher
#include <memory>

namespace Vixen::Vulkan::Resources {
    class VulkanDevice;
}

namespace Vixen::RenderGraph {

/**
 * @brief Node type for voxel AABB conversion
 */
class VoxelAABBConverterNodeType : public TypedNodeType<VoxelAABBConverterNodeConfig> {
public:
    VoxelAABBConverterNodeType(const std::string& typeName = "VoxelAABBConverter")
        : TypedNodeType<VoxelAABBConverterNodeConfig>(typeName) {}
    virtual ~VoxelAABBConverterNodeType() = default;

    std::unique_ptr<NodeInstance> CreateInstance(
        const std::string& instanceName
    ) const override;
};

/**
 * @brief Converts voxel scene data to AABB buffer for hardware ray tracing
 *
 * Phase K: Hardware Ray Tracing
 *
 * This node uses VoxelAABBCacher to extract axis-aligned bounding boxes
 * from cached VoxelSceneData. The cacher handles GPU buffer creation and
 * caching based on (sceneDataKey, voxelSize, resolution).
 *
 * Input:
 * - VOXEL_SCENE_DATA from VoxelGridNode (via VoxelSceneCacher)
 *
 * Output:
 * - AABB_DATA: VoxelAABBData struct containing:
 *   - aabbBuffer: VkBuffer with VoxelAABB array
 *   - materialIdBuffer: VkBuffer with material IDs per AABB
 *   - brickMappingBuffer: VkBuffer with brick mappings per AABB
 *   - aabbCount: Number of solid voxels
 */
class VoxelAABBConverterNode : public TypedNode<VoxelAABBConverterNodeConfig> {
public:
    VoxelAABBConverterNode(
        const std::string& instanceName,
        NodeType* nodeType
    );
    ~VoxelAABBConverterNode() override = default;

protected:
    void SetupImpl(TypedSetupContext& ctx) override;
    void CompileImpl(TypedCompileContext& ctx) override;
    void ExecuteImpl(TypedExecuteContext& ctx) override;
    void CleanupImpl(TypedCleanupContext& ctx) override;

private:
    /**
     * @brief Ensure cacher is registered and initialized
     */
    void EnsureCacherRegistered();

    // Device reference
    Vixen::Vulkan::Resources::VulkanDevice* vulkanDevice_ = nullptr;

    // Cached AABB data from cacher (shared ownership)
    std::shared_ptr<CashSystem::VoxelAABBData> cachedAABBData_;

    // Input scene data reference
    CashSystem::VoxelSceneData* voxelSceneData_ = nullptr;

    // Parameters
    uint32_t gridResolution_ = 128;
    float voxelSize_ = 1.0f;
};

} // namespace Vixen::RenderGraph
