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
    struct CachedAccelerationStructure;
    class DynamicTLAS;
    class TLASInstanceManager;
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

    /// Get acceleration structure data (after Compile)
    const AccelerationStructureData& GetAccelData() const { return accelData_; }

protected:
    void SetupImpl(TypedSetupContext& ctx) override;
    void CompileImpl(TypedCompileContext& ctx) override;
    void ExecuteImpl(TypedExecuteContext& ctx) override;
    void CleanupImpl(TypedCleanupContext& ctx) override;

private:
    /**
     * @brief Cleanup all acceleration structure resources
     */
    void DestroyAccelerationStructures();

    // ===== Cacher Registration Helper =====

    // Device reference
    Vixen::Vulkan::Resources::VulkanDevice* vulkanDevice_ = nullptr;
    VkCommandPool commandPool_ = VK_NULL_HANDLE;

    // Output data (persists until cleanup)
    AccelerationStructureData accelData_;

    // Build parameters
    bool preferFastTrace_ = true;
    bool allowUpdate_ = false;
    bool allowCompaction_ = false;

    // CashSystem integration - cached during Compile()
    CashSystem::AccelerationStructureCacher* accelStructCacher_ = nullptr;
    std::shared_ptr<CashSystem::CachedAccelerationStructure> cachedAccelStruct_;

    // Task profile for compile-time cost estimation (Sprint 6.5)
    ITaskProfile* compileProfile_ = nullptr;

    // Dynamic TLAS mode (Sprint 5 Phase 3)
    ASBuildMode buildMode_ = ASBuildMode::Static;
    std::unique_ptr<CashSystem::DynamicTLAS> dynamicTLAS_;
    std::unique_ptr<CashSystem::TLASInstanceManager> instanceManager_;

    // Cacher registration helper
    void RegisterAccelerationStructureCacher();

    // GetOrCreate helper - builds params and calls cacher
    void CreateAccelStructViaCacher(VoxelAABBData& aabbData);

    // Dynamic mode helpers
    bool IsDynamicMode() const { return buildMode_ == ASBuildMode::Dynamic; }
    void InitializeDynamicTLAS(uint32_t imageCount);
};

} // namespace Vixen::RenderGraph
