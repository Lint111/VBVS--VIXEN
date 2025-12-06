#pragma once

#include "Core/TypedNodeInstance.h"
#include "Core/NodeType.h"
#include "Core/NodeLogging.h"
#include "Data/Nodes/VoxelAABBConverterNodeConfig.h"
#include <memory>
#include <vector>

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
 * @brief Converts voxel octree to AABB buffer for hardware ray tracing
 *
 * Phase K: Hardware Ray Tracing
 *
 * This node iterates through the sparse voxel octree and extracts
 * axis-aligned bounding boxes for each solid voxel. The output buffer
 * is formatted for VkAccelerationStructureGeometryAabbsDataKHR.
 *
 * Input:
 * - OCTREE_NODES_BUFFER from VoxelGridNode
 *
 * Output:
 * - AABB_DATA: VoxelAABBData struct containing:
 *   - aabbBuffer: VkBuffer with VoxelAABB array
 *   - aabbCount: Number of solid voxels
 *   - aabbBufferMemory: Device memory for cleanup
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
     * @brief Extract AABBs from dense voxel grid
     *
     * Iterates through grid resolution³ positions and collects
     * solid voxels into AABB array.
     *
     * @return Vector of VoxelAABB for upload to GPU
     */
    std::vector<VoxelAABB> ExtractAABBsFromGrid();

    /**
     * @brief Create GPU buffer for AABB data
     *
     * Creates device-local buffer with ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT
     * usage flag for BLAS construction.
     */
    void CreateAABBBuffer(const std::vector<VoxelAABB>& aabbs);

    /**
     * @brief Upload AABB data via staging buffer
     */
    void UploadAABBData(const std::vector<VoxelAABB>& aabbs);

    /**
     * @brief Cleanup GPU resources
     */
    void DestroyAABBBuffer();

    // Device reference
    Vixen::Vulkan::Resources::VulkanDevice* vulkanDevice_ = nullptr;
    VkCommandPool commandPool_ = VK_NULL_HANDLE;

    // Output data (persists until cleanup)
    VoxelAABBData aabbData_;

    // Parameters
    uint32_t gridResolution_ = 128;
    float voxelSize_ = 1.0f;

    // Cached grid data for AABB extraction
    // In production, this would read from octree buffer
    // For now, we regenerate based on parameters
    std::string sceneType_ = "cornell";
};

} // namespace Vixen::RenderGraph
