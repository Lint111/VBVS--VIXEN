#pragma once
#include "Core/TypedNodeInstance.h"
#include "Core/NodeType.h"
#include "DescriptorSetNodeConfig.h"
#include "ShaderManagement/DescriptorLayoutSpec.h"
#include <memory>
#include <unordered_map>

namespace Vixen::RenderGraph {

/**
 * @brief Node type for creating descriptor set layouts, pools, and descriptor sets
 * 
 * DATA-DRIVEN descriptor set management using DescriptorLayoutSpec.
 * Supports manual layout specification or automatic extraction from SPIRV.
 * 
 * Type ID: 107
 */
class DescriptorSetNodeType : public NodeType {
public:
    DescriptorSetNodeType(const std::string& typeName = "DescriptorSet");
    virtual ~DescriptorSetNodeType() = default;

    std::unique_ptr<NodeInstance> CreateInstance(
        const std::string& instanceName
    ) const override;
};

/**
 * @brief Descriptor resource update specification
 * 
 * Used to update descriptor sets with actual Vulkan resources.
 */
struct DescriptorUpdate {
    uint32_t binding;
    VkDescriptorType type;
    std::vector<VkDescriptorBufferInfo> bufferInfos;
    std::vector<VkDescriptorImageInfo> imageInfos;
};

/**
 * @brief Typed node instance for data-driven descriptor set management
 * 
 * Accepts DescriptorLayoutSpec defining all bindings (from shader reflection or manual).
 * Creates layout/pool/sets, then user updates with actual resources via UpdateDescriptorSet().
 * 
 * Outputs:
 * - DESCRIPTOR_SET_LAYOUT - For pipeline creation
 * - DESCRIPTOR_POOL - For management
 * - DESCRIPTOR_SETS - Allocated sets (updated via UpdateDescriptorSet())
 */
class DescriptorSetNode : public TypedNode<DescriptorSetNodeConfig> {
public:
    DescriptorSetNode(
        const std::string& instanceName,
        NodeType* nodeType
    );
    virtual ~DescriptorSetNode();

    void Setup() override;
    void Compile() override;
    void Execute(VkCommandBuffer commandBuffer) override;
    void Cleanup() override;

    /**
     * @brief Update descriptor set with actual resources
     * @param setIndex Which set to update (0 to maxSets-1)
     * @param updates Bindings to update
     */
    void UpdateDescriptorSet(uint32_t setIndex, const std::vector<DescriptorUpdate>& updates);

    /**
     * @brief Update single buffer binding
     */
    void UpdateBinding(uint32_t setIndex, uint32_t binding, const VkDescriptorBufferInfo& bufferInfo);

    /**
     * @brief Update single image binding
     */
    void UpdateBinding(uint32_t setIndex, uint32_t binding, const VkDescriptorImageInfo& imageInfo);

    // Accessors
    VkDescriptorSetLayout GetDescriptorSetLayout() const { return descriptorSetLayout; }
    VkDescriptorPool GetDescriptorPool() const { return descriptorPool; }
    const std::vector<VkDescriptorSet>& GetDescriptorSets() const { return descriptorSets; }
    VkDescriptorSet GetDescriptorSet(uint32_t index) const {
        return (index < descriptorSets.size()) ? descriptorSets[index] : VK_NULL_HANDLE;
    }
    const ShaderManagement::DescriptorLayoutSpec* GetLayoutSpec() const { return layoutSpec; }

private:
    // Configuration
    const ShaderManagement::DescriptorLayoutSpec* layoutSpec = nullptr;

    // Vulkan resources
    VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> descriptorSets;

    // Helpers
    void ValidateLayoutSpec();
    void CreateDescriptorSetLayout();
    void CreateDescriptorPool();
    void AllocateDescriptorSets();
};

} // namespace Vixen::RenderGraph
