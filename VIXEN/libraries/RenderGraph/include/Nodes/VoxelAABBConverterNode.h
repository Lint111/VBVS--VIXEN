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
     * @brief Extract AABBs, material IDs, and brick mappings from dense voxel grid
     *
     * Iterates through grid resolution³ positions and collects
     * solid voxels into AABB array with corresponding material IDs and brick mappings.
     *
     * @param outMaterialIds Output vector of material IDs (one per AABB)
     * @param outBrickMappings Output vector of brick mappings (one per AABB)
     * @param brickGridLookup Lookup buffer from VoxelGridNode mapping grid coords to Morton-sorted brick indices
     * @return Vector of VoxelAABB for upload to GPU
     */
    std::vector<VoxelAABB> ExtractAABBsFromGrid(
        std::vector<uint32_t>& outMaterialIds,
        std::vector<VoxelBrickMapping>& outBrickMappings,
        const std::vector<uint32_t>& brickGridLookup);

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
     * @brief Create GPU buffer for material IDs
     */
    void CreateMaterialIdBuffer(const std::vector<uint32_t>& materialIds);

    /**
     * @brief Upload material ID data via staging buffer
     */
    void UploadMaterialIdData(const std::vector<uint32_t>& materialIds);

    /**
     * @brief Create GPU buffer for brick mappings
     */
    void CreateBrickMappingBuffer(const std::vector<VoxelBrickMapping>& brickMappings);

    /**
     * @brief Upload brick mapping data via staging buffer
     */
    void UploadBrickMappingData(const std::vector<VoxelBrickMapping>& brickMappings);

    /**
     * @brief Download GPU buffer contents to host memory
     *
     * Used to read the brick grid lookup buffer from VoxelGridNode
     * for Morton-sorted brick index lookup during AABB extraction.
     *
     * @param srcBuffer Source GPU buffer to download
     * @param bufferSize Size of buffer in bytes
     * @return Vector of uint32_t values from the buffer
     */
    std::vector<uint32_t> DownloadBufferToHost(VkBuffer srcBuffer, VkDeviceSize bufferSize);

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
