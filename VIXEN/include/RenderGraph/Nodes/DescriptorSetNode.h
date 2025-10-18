#pragma once
#include "RenderGraph/NodeInstance.h"
#include "RenderGraph/NodeType.h"
#include <memory>

namespace Vixen::RenderGraph {

/**
 * @brief Node type for creating descriptor set layouts, pools, and descriptor sets
 * 
 * Manages the creation and updating of Vulkan descriptor sets for shader resource binding.
 * Supports uniform buffers and combined image samplers.
 * 
 * Type ID: 107
 */
class DescriptorSetNodeType : public NodeType {
public:
    DescriptorSetNodeType();
    virtual ~DescriptorSetNodeType() = default;

    std::unique_ptr<NodeInstance> CreateInstance(
        const std::string& instanceName,
        Vixen::Vulkan::Resources::VulkanDevice* device
    ) const override;
};

/**
 * @brief Node instance for descriptor set management
 * 
 * Parameters:
 * - uniformBufferSize (uint32_t): Size of the uniform buffer in bytes
 * - useTexture (bool): Whether to include a combined image sampler binding
 * - maxSets (uint32_t): Maximum number of descriptor sets to allocate (default: 1)
 * - uniformBufferBinding (uint32_t): Binding point for uniform buffer (default: 0)
 * - samplerBinding (uint32_t): Binding point for sampler (default: 1)
 * 
 * Inputs:
 * - textureImage (optional): Texture to bind to the sampler (if useTexture=true)
 * 
 * Outputs:
 * - descriptorSetLayout: Layout defining descriptor bindings
 * - descriptorPool: Pool for allocating descriptor sets
 * - descriptorSets: Allocated and updated descriptor sets
 * - uniformBuffer: Uniform buffer resource
 */
class DescriptorSetNode : public NodeInstance {
public:
    DescriptorSetNode(
        const std::string& instanceName,
        NodeType* nodeType,
        Vixen::Vulkan::Resources::VulkanDevice* device
    );
    virtual ~DescriptorSetNode();

    void Setup() override;
    void Compile() override;
    void Execute(VkCommandBuffer commandBuffer) override;
    void Cleanup() override;

    // Accessors for other nodes
    VkDescriptorSetLayout GetDescriptorSetLayout() const { return descriptorSetLayout; }
    VkDescriptorPool GetDescriptorPool() const { return descriptorPool; }
    const std::vector<VkDescriptorSet>& GetDescriptorSets() const { return descriptorSets; }
    VkBuffer GetUniformBuffer() const { return uniformBuffer; }
    VkDeviceMemory GetUniformMemory() const { return uniformMemory; }
    
    // Update uniform buffer data
    void UpdateUniformBuffer(const void* data, VkDeviceSize size);

private:
    // Descriptor resources
    VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> descriptorSets;

    // Uniform buffer
    VkBuffer uniformBuffer = VK_NULL_HANDLE;
    VkDeviceMemory uniformMemory = VK_NULL_HANDLE;
    VkDeviceSize uniformBufferSize = 0;

    // Configuration
    bool useTexture = false;
    uint32_t maxSets = 1;
    uint32_t uniformBufferBinding = 0;
    uint32_t samplerBinding = 1;

    // Texture reference (if used)
    VkImageView textureView = VK_NULL_HANDLE;
    VkSampler textureSampler = VK_NULL_HANDLE;
    VkDescriptorImageInfo textureImageInfo{};

    // Helper functions
    void CreateDescriptorSetLayout();
    void CreateDescriptorPool();
    void CreateUniformBuffer();
    void AllocateDescriptorSets();
    void UpdateDescriptorSets();
};

} // namespace Vixen::RenderGraph
