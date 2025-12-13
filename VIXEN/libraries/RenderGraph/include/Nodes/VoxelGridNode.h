#pragma once

#include "Core/TypedNodeInstance.h"
#include "Core/NodeType.h"
#include "Core/NodeLogging.h"
#include "Data/Nodes/VoxelGridNodeConfig.h"
#include "Debug/RayTraceBuffer.h"
#include "Debug/ShaderCountersBuffer.h"
#include "Core/GPUPerformanceLogger.h"
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

namespace Vixen::SVO {
    struct Octree;
}

namespace CashSystem {
    class VoxelSceneCacher;
    struct VoxelSceneData;
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

    /**
     * @brief Read shader counters from GPU after frame execution
     *
     * Call this after ExecuteAll() to get the accumulated counter data.
     * Returns nullptr if counters are not available or buffer is invalid.
     *
     * @return Pointer to GPUShaderCounters or nullptr
     */
    const Debug::GPUShaderCounters* ReadShaderCounters();

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
    void UploadESVOBuffers(const Vixen::SVO::Octree& octree, const VIXEN::RenderGraph::VoxelGrid& grid);

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

    // Debug capture resource for ray tracing debug data
    // Uses RayTraceBuffer directly (has conversion_type = VkBuffer for auto descriptor extraction)
    std::unique_ptr<Debug::RayTraceBuffer> debugCaptureResource_;

    // Shader counters resource for avgVoxelsPerRay metrics
    // Uses ShaderCountersBuffer directly (has conversion_type = VkBuffer for auto descriptor extraction)
    std::unique_ptr<Debug::ShaderCountersBuffer> shaderCountersResource_;

    // Parameters
    uint32_t resolution = 128;
    std::string sceneType = "test";

    // Memory tracking for benchmarking (Week 3)
    std::shared_ptr<GPUPerformanceLogger> memoryLogger_;

    // CashSystem integration - cached during Compile()
    CashSystem::VoxelSceneCacher* voxelSceneCacher_ = nullptr;
    std::shared_ptr<CashSystem::VoxelSceneData> cachedSceneData_;

    // Cacher registration helper
    void RegisterVoxelSceneCacher();

    // GetOrCreate helper - builds params and calls cacher
    void CreateSceneViaCacher();
};

} // namespace Vixen::RenderGraph
