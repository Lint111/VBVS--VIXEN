#pragma once

#include "Core/TypedNodeInstance.h"
#include "Core/NodeType.h"
#include "Core/NodeLogging.h"
#include "Data/Nodes/AccelerationStructureNodeConfig.h"
#include <memory>

namespace Vixen::Vulkan::Resources {
    class VulkanDevice;
}

namespace CashSystem {
    class AccelerationStructureCacher;
}

namespace Vixen::RenderGraph {

/**
 * @brief Node type for acceleration structure building
 */
class AccelerationStructureNodeType : public TypedNodeType<AccelerationStructureNodeConfig> {
public:
    AccelerationStructureNodeType(const std::string& typeName = "AccelerationStructure")
        : TypedNodeType<AccelerationStructureNodeConfig>(typeName) {}
    virtual ~AccelerationStructureNodeType() = default;

    std::unique_ptr<NodeInstance> CreateInstance(
        const std::string& instanceName
    ) const override;
};

/**
 * @brief Builds BLAS and TLAS from voxel AABBs for hardware ray tracing
 *
 * Phase K: Hardware Ray Tracing
 *
 * This node takes the AABB buffer from VoxelAABBConverterNode and builds:
 * 1. BLAS (Bottom-Level Acceleration Structure) - Contains voxel AABBs as procedural geometry
 * 2. TLAS (Top-Level Acceleration Structure) - Contains single instance of BLAS
 *
 * The BLAS uses VK_GEOMETRY_TYPE_AABBS_KHR for procedural intersection testing.
 * Ray-AABB intersection will be handled by intersection shaders (.rint).
 *
 * Build Process:
 * 1. Query size requirements for BLAS
 * 2. Allocate scratch and result buffers
 * 3. Build BLAS with vkCmdBuildAccelerationStructuresKHR
 * 4. Create instance buffer for TLAS (single identity transform instance)
 * 5. Query size requirements for TLAS
 * 6. Build TLAS referencing the BLAS instance
 *
 * Input:
 * - AABB_DATA from VoxelAABBConverterNode
 *
 * Output:
 * - ACCELERATION_STRUCTURE_DATA: AccelerationStructureData struct containing:
 *   - blas/tlas handles
 *   - Buffer references for cleanup
 *   - Device addresses for shader access
 */
class AccelerationStructureNode : public TypedNode<AccelerationStructureNodeConfig> {
public:
    AccelerationStructureNode(
        const std::string& instanceName,
        NodeType* nodeType
    );
    ~AccelerationStructureNode() override = default;

protected:
    void SetupImpl(TypedSetupContext& ctx) override;
    void CompileImpl(TypedCompileContext& ctx) override;
    void ExecuteImpl(TypedExecuteContext& ctx) override;
    void CleanupImpl(TypedCleanupContext& ctx) override;

private:
    // ===== BLAS Building =====

    /**
     * @brief Build bottom-level acceleration structure from AABBs
     *
     * Creates BLAS with VK_GEOMETRY_TYPE_AABBS_KHR geometry type.
     * This enables procedural intersection via intersection shaders.
     *
     * @param aabbData AABB buffer and count from VoxelAABBConverterNode
     * @return true if BLAS was built successfully
     */
    bool BuildBLAS(const VoxelAABBData& aabbData);

    /**
     * @brief Query BLAS build size requirements
     */
    VkAccelerationStructureBuildSizesInfoKHR QueryBLASBuildSizes(
        const VkAccelerationStructureGeometryKHR& geometry,
        uint32_t primitiveCount
    );

    // ===== TLAS Building =====

    /**
     * @brief Build top-level acceleration structure with single instance
     *
     * Creates TLAS containing one instance of the BLAS with identity transform.
     *
     * @return true if TLAS was built successfully
     */
    bool BuildTLAS();

    /**
     * @brief Create instance buffer for TLAS
     *
     * Contains VkAccelerationStructureInstanceKHR with:
     * - Identity transform matrix
     * - Reference to BLAS device address
     * - Custom index for shader SBT lookup
     */
    bool CreateInstanceBuffer();

    /**
     * @brief Query TLAS build size requirements
     */
    VkAccelerationStructureBuildSizesInfoKHR QueryTLASBuildSizes(uint32_t instanceCount);

    // ===== Helper Functions =====

    /**
     * @brief Create buffer with device address support
     */
    bool CreateBuffer(
        VkDeviceSize size,
        VkBufferUsageFlags usage,
        VkMemoryPropertyFlags properties,
        VkBuffer& buffer,
        VkDeviceMemory& memory
    );

    /**
     * @brief Get device address of a buffer
     */
    VkDeviceAddress GetBufferDeviceAddress(VkBuffer buffer);

    /**
     * @brief Get device address of an acceleration structure
     */
    VkDeviceAddress GetAccelerationStructureDeviceAddress(VkAccelerationStructureKHR accel);

    /**
     * @brief Cleanup all acceleration structure resources
     */
    void DestroyAccelerationStructures();

    // ===== Extension Function Pointers =====

    /**
     * @brief Load RTX extension function pointers
     *
     * These functions are not part of Vulkan core and must be loaded dynamically.
     */
    bool LoadRTXFunctions();

    // Device reference
    Vixen::Vulkan::Resources::VulkanDevice* vulkanDevice_ = nullptr;
    VkCommandPool commandPool_ = VK_NULL_HANDLE;

    // Output data (persists until cleanup)
    AccelerationStructureData accelData_;

    // Build parameters
    bool preferFastTrace_ = true;
    bool allowUpdate_ = false;
    bool allowCompaction_ = false;

    // RTX Extension function pointers
    PFN_vkGetAccelerationStructureBuildSizesKHR vkGetAccelerationStructureBuildSizesKHR_ = nullptr;
    PFN_vkCreateAccelerationStructureKHR vkCreateAccelerationStructureKHR_ = nullptr;
    PFN_vkDestroyAccelerationStructureKHR vkDestroyAccelerationStructureKHR_ = nullptr;
    PFN_vkCmdBuildAccelerationStructuresKHR vkCmdBuildAccelerationStructuresKHR_ = nullptr;
    PFN_vkGetAccelerationStructureDeviceAddressKHR vkGetAccelerationStructureDeviceAddressKHR_ = nullptr;
    PFN_vkGetBufferDeviceAddressKHR vkGetBufferDeviceAddressKHR_ = nullptr;

    // CashSystem integration - cached during Compile()
    CashSystem::AccelerationStructureCacher* accelStructCacher_ = nullptr;

    // Cacher registration helper
    void RegisterAccelerationStructureCacher();
};

} // namespace Vixen::RenderGraph
