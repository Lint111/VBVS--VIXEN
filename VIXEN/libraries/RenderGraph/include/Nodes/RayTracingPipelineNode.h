#pragma once

#include "Core/TypedNodeInstance.h"
#include "Core/NodeType.h"
#include "Core/NodeLogging.h"
#include "Data/Nodes/RayTracingPipelineNodeConfig.h"
#include <memory>
#include <vector>

namespace Vixen::Vulkan::Resources {
    class VulkanDevice;
}

namespace Vixen::RenderGraph {

/**
 * @brief Node type for ray tracing pipeline creation
 */
class RayTracingPipelineNodeType : public TypedNodeType<RayTracingPipelineNodeConfig> {
public:
    RayTracingPipelineNodeType(const std::string& typeName = "RayTracingPipeline")
        : TypedNodeType<RayTracingPipelineNodeConfig>(typeName) {}
    virtual ~RayTracingPipelineNodeType() = default;

    std::unique_ptr<NodeInstance> CreateInstance(
        const std::string& instanceName
    ) const override;
};

/**
 * @brief Creates ray tracing pipeline and Shader Binding Table for voxel rendering
 *
 * Phase K: Hardware Ray Tracing
 *
 * This node builds the VK_KHR_ray_tracing_pipeline with:
 * - Ray Generation shader: Generates camera rays
 * - Intersection shader: Custom AABB intersection for voxels
 * - Closest Hit shader: Computes shading at hit points
 * - Miss shader: Returns background color
 *
 * The Shader Binding Table (SBT) is a GPU-side table that maps:
 * - Ray types to shader groups
 * - Geometry instances to hit shaders
 *
 * SBT Layout for voxels:
 * +------------+--------+---------+----------+
 * | RayGen     | Miss   | Hit     | Callable |
 * | (1 entry)  | (1)    | (1)     | (0)      |
 * +------------+--------+---------+----------+
 *
 * Hit Group Structure:
 * - Intersection shader: Tests ray against voxel AABB
 * - Closest-hit shader: Shades the voxel surface
 * - Any-hit shader: Not used (opaque voxels)
 *
 * Input:
 * - ACCELERATION_STRUCTURE_DATA from AccelerationStructureNode
 * - Shader modules from ShaderLibraryNode
 *
 * Output:
 * - RT_PIPELINE_DATA: RayTracingPipelineData containing:
 *   - VkPipeline handle
 *   - VkPipelineLayout
 *   - ShaderBindingTable with region addresses
 */
class RayTracingPipelineNode : public TypedNode<RayTracingPipelineNodeConfig> {
public:
    RayTracingPipelineNode(
        const std::string& instanceName,
        NodeType* nodeType
    );
    ~RayTracingPipelineNode() override = default;

protected:
    void SetupImpl(TypedSetupContext& ctx) override;
    void CompileImpl(TypedCompileContext& ctx) override;
    void ExecuteImpl(TypedExecuteContext& ctx) override;
    void CleanupImpl(TypedCleanupContext& ctx) override;

private:
    // ===== Pipeline Creation =====

    /**
     * @brief Create descriptor set layout for ray tracing
     *
     * Bindings:
     * - 0: Acceleration structure (TLAS)
     * - 1: Output image (storage image)
     * - 2: Camera data (uniform buffer or push constants)
     * - 3: Voxel data buffers (octree, materials)
     */
    bool CreateDescriptorSetLayout();

    /**
     * @brief Create pipeline layout with push constants
     */
    bool CreatePipelineLayout();

    /**
     * @brief Create ray tracing pipeline
     *
     * Shader stages:
     * - VK_SHADER_STAGE_RAYGEN_BIT_KHR
     * - VK_SHADER_STAGE_MISS_BIT_KHR
     * - VK_SHADER_STAGE_INTERSECTION_BIT_KHR
     * - VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR
     *
     * @return true if pipeline created successfully
     */
    bool CreateRTPipeline();

    // ===== SBT Building =====

    /**
     * @brief Build Shader Binding Table
     *
     * Allocates buffer and copies shader group handles for:
     * - Ray generation region
     * - Miss region
     * - Hit group region (intersection + closest-hit)
     *
     * @return true if SBT built successfully
     */
    bool BuildShaderBindingTable();

    /**
     * @brief Calculate aligned size for SBT entries
     */
    VkDeviceSize AlignedSize(VkDeviceSize size, VkDeviceSize alignment) const;

    // ===== Cleanup =====

    /**
     * @brief Destroy all pipeline resources
     */
    void DestroyPipeline();

    // ===== RTX Function Loading =====

    bool LoadRTXFunctions();

    // ===== Shader Loading =====

    /**
     * @brief Load shaders from ShaderDataBundle
     */
    bool LoadShadersFromBundle(const ShaderManagement::ShaderDataBundle& bundle);

    /**
     * @brief Load shaders from parameter paths
     */
    bool LoadShadersFromPaths();

    /**
     * @brief Create VkShaderModule from SPIR-V file
     */
    VkShaderModule CreateShaderModule(const std::string& path);

    /**
     * @brief Destroy locally-created shader modules
     */
    void DestroyShaderModules();

    // Flag indicating we own the shader modules (created from paths)
    bool ownsShaderModules_ = false;

    // Device reference
    Vixen::Vulkan::Resources::VulkanDevice* vulkanDevice_ = nullptr;

    // Cached shader bundle (for reflection data access)
    std::shared_ptr<ShaderManagement::ShaderDataBundle> shaderBundle_;

    // Shader modules (from inputs)
    VkShaderModule raygenShader_ = VK_NULL_HANDLE;
    VkShaderModule missShader_ = VK_NULL_HANDLE;
    VkShaderModule intersectionShader_ = VK_NULL_HANDLE;
    VkShaderModule closestHitShader_ = VK_NULL_HANDLE;

    // Output data
    RayTracingPipelineData pipelineData_;

    // Parameters
    uint32_t maxRayRecursion_ = 1;  // No recursion for primary rays only
    uint32_t outputWidth_ = 1920;
    uint32_t outputHeight_ = 1080;

    // RTX properties (from VulkanDevice)
    uint32_t shaderGroupHandleSize_ = 0;
    uint32_t shaderGroupBaseAlignment_ = 0;
    uint32_t shaderGroupHandleAlignment_ = 0;

    // RTX function pointers
    PFN_vkCreateRayTracingPipelinesKHR vkCreateRayTracingPipelinesKHR_ = nullptr;
    PFN_vkGetRayTracingShaderGroupHandlesKHR vkGetRayTracingShaderGroupHandlesKHR_ = nullptr;
    PFN_vkGetBufferDeviceAddressKHR vkGetBufferDeviceAddressKHR_ = nullptr;
};

} // namespace Vixen::RenderGraph
