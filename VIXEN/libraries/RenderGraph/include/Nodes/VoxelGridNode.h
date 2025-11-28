#pragma once

#include "Core/TypedNodeInstance.h"
#include "Core/NodeType.h"
#include "Core/NodeLogging.h"
#include "Data/Nodes/VoxelGridNodeConfig.h"
#include <memory>
#include <vector>

// Forward declarations
namespace Vixen::Vulkan::Resources {
    class VulkanDevice;
}

namespace VIXEN::RenderGraph {
    class VoxelGrid;
    class SparseVoxelOctree;
}

namespace SVO {
    struct Octree;
}

namespace Vixen::RenderGraph {

/**
 * @brief Node type for voxel grid generation
 */
class VoxelGridNodeType : public TypedNodeType<VoxelGridNodeConfig> {
public:
    VoxelGridNodeType(const std::string& typeName = "VoxelGrid")
        : TypedNodeType<VoxelGridNodeConfig>(typeName) {}
    virtual ~VoxelGridNodeType() = default;

    std::unique_ptr<NodeInstance> CreateInstance(
        const std::string& instanceName
    ) const override;
};

/**
 * @brief Voxel grid generation node with sparse octree support
 *
 * Generates procedural voxel scenes and uploads to GPU as sparse octree SSBO buffers.
 *
 * Phase H: Voxel Data Infrastructure
 *
 * Scene types:
 * - "test": Simple test pattern (all solid voxels for debug)
 * - "cornell": Cornell Box (10% density - sparse)
 * - "cave": Cave system (50% density - medium)
 * - "urban": Urban grid (90% density - dense)
 */
class VoxelGridNode : public TypedNode<VoxelGridNodeConfig> {
public:

    VoxelGridNode(
        const std::string& instanceName,
        NodeType* nodeType
    );
    ~VoxelGridNode() override = default;

protected:
    void SetupImpl(TypedSetupContext& ctx) override;
    void CompileImpl(TypedCompileContext& ctx) override;
    void ExecuteImpl(TypedExecuteContext& ctx) override;
    void CleanupImpl(TypedCleanupContext& ctx) override;

private:
    // Scene generation
    void GenerateProceduralScene(VIXEN::RenderGraph::VoxelGrid& grid);

    // Buffer upload orchestration
    void UploadOctreeBuffers(const VIXEN::RenderGraph::SparseVoxelOctree& octree);

    // New ESVO buffer upload (SVO::Octree structure + direct grid access)
    void UploadESVOBuffers(const SVO::Octree& octree, const VIXEN::RenderGraph::VoxelGrid& grid);

    // Buffer creation steps (extracted from UploadOctreeBuffers)
    void CreateOctreeNodesBuffer(VkDeviceSize size, const void* nodeData);
    void CreateOctreeBricksBuffer(const VIXEN::RenderGraph::SparseVoxelOctree& octree);
    void CreateOctreeMaterialsBuffer();
    void UploadBufferDataViaStagingBuffer();

    // Cleanup steps (extracted from CleanupImpl)
    void DestroyOctreeBuffers();
    void LogCleanupProgress(const std::string& stage);

    // Device reference
    Vixen::Vulkan::Resources::VulkanDevice* vulkanDevice = nullptr;
    VkCommandPool commandPool = VK_NULL_HANDLE;

    // Octree SSBO buffers
    VkBuffer octreeNodesBuffer = VK_NULL_HANDLE;
    VkDeviceMemory octreeNodesMemory = VK_NULL_HANDLE;
    VkBuffer octreeBricksBuffer = VK_NULL_HANDLE;
    VkDeviceMemory octreeBricksMemory = VK_NULL_HANDLE;
    VkBuffer octreeMaterialsBuffer = VK_NULL_HANDLE;
    VkDeviceMemory octreeMaterialsMemory = VK_NULL_HANDLE;

    // Parameters
    uint32_t resolution = 128;
    std::string sceneType = "test";
};

} // namespace Vixen::RenderGraph
