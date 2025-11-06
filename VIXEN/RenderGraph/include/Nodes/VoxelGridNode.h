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
 * @brief Voxel grid 3D texture node for raymarching
 *
 * Creates 3D texture (VK_IMAGE_TYPE_3D) with procedural voxel data.
 * Outputs image view and sampler for compute shader binding.
 *
 * Phase: Research implementation (voxel raymarching)
 *
 * Scene types:
 * - "test": Simple test pattern (spheres)
 * - "cornell": Cornell Box (Phase H)
 * - "cave": Cave system (Phase H)
 * - "urban": Urban grid (Phase H)
 */
class VoxelGridNode : public TypedNode<VoxelGridNodeConfig> {
public:
    VoxelGridNode(
        const std::string& instanceName,
        NodeType* nodeType
    );
    ~VoxelGridNode() override = default;

protected:
    void SetupImpl(Context& ctx) override;
    void CompileImpl(Context& ctx) override;
    void ExecuteImpl(Context& ctx) override;
    void CleanupImpl() override;

private:
    void GenerateTestPattern(std::vector<uint8_t>& voxelData);
    void UploadVoxelData(const std::vector<uint8_t>& voxelData);

    // Device reference
    Vixen::Vulkan::Resources::VulkanDevice* vulkanDevice = nullptr;
    VkCommandPool commandPool = VK_NULL_HANDLE;

    // Voxel grid resources
    VkImage voxelImage = VK_NULL_HANDLE;
    VkDeviceMemory voxelMemory = VK_NULL_HANDLE;
    VkImageView voxelImageView = VK_NULL_HANDLE;
    VkSampler voxelSampler = VK_NULL_HANDLE;

    // Staging buffer for upload
    VkBuffer stagingBuffer = VK_NULL_HANDLE;
    VkDeviceMemory stagingMemory = VK_NULL_HANDLE;

    // Parameters
    uint32_t resolution = 128;
    std::string sceneType = "test";
};

} // namespace Vixen::RenderGraph
