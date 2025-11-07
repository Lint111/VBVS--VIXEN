#pragma once
#include "Core/TypedNodeInstance.h"
#include "Core/NodeType.h"
#include "Core/PerFrameResources.h"
#include "Data/Nodes/DescriptorSetNodeConfig.h"
// TEMPORARILY REMOVED - MVP uses hardcoded descriptor layouts
// #include "ShaderManagement/DescriptorLayoutSpec.h"
#include <memory>
#include <unordered_map>

// Forward declarations
namespace CashSystem {
    class DescriptorCacher;
}

namespace ShaderManagement {
    struct SpirvDescriptorBinding;
}

namespace Vixen::RenderGraph {

/**
 * @brief Node type for creating descriptor set layouts, pools, and descriptor sets
 *
 * DATA-DRIVEN descriptor set management using DescriptorLayoutSpec.
 * Supports manual layout specification or automatic extraction from SPIRV.
 *
 * Type ID: 107
 */
class DescriptorSetNodeType : public TypedNodeType<DescriptorSetNodeConfig> {
public:
    DescriptorSetNodeType(const std::string& typeName = "DescriptorSet")
        : TypedNodeType<DescriptorSetNodeConfig>(typeName) {}
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
    ~DescriptorSetNode() override = default;

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

protected:
	// Template method pattern - override *Impl() methods
	void SetupImpl(TypedSetupContext& ctx) override;
	void CompileImpl(TypedCompileContext& ctx) override;
	void ExecuteImpl(TypedExecuteContext& ctx) override;
	void CleanupImpl(TypedCleanupContext& ctx) override;

private:
    // Configuration
    const ShaderManagement::DescriptorLayoutSpec* layoutSpec = nullptr;

    // Vulkan resources
    VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> descriptorSets;

    VulkanDevicePtr vulkanDevice = VK_NULL_HANDLE;

    // Phase 0.1: Per-frame resources to prevent CPU-GPU race conditions
    PerFrameResources perFrameResources;
    float rotationAngle = 0.0f;

    // CashSystem integration - cached during Compile()
    CashSystem::DescriptorCacher* descriptorCacher = nullptr;
    
    // Pipeline layout for binding descriptor sets
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;

    // Phase H: Persistent storage for data-driven descriptor binding
    std::vector<std::vector<VkDescriptorImageInfo>> perFrameImageInfos;
    std::vector<std::vector<VkDescriptorBufferInfo>> perFrameBufferInfos;

    // Helpers
    void ValidateLayoutSpec();
    void CreateDescriptorSetLayout();
    void CreateDescriptorPool();
    void AllocateDescriptorSets();
    void CreateDescriptorSetLayoutManually();

    /**
     * @brief Update descriptor sets from resource array using shader metadata
     * @param imageIndex Swapchain image index (which descriptor set to update)
     * @param descriptorResources Resource array from DescriptorResourceGathererNode
     * @param descriptorBindings Shader bindings metadata
     * @param imageInfos Output vector for image infos (keep alive for vkUpdateDescriptorSets)
     * @param bufferInfos Output vector for buffer infos (keep alive for vkUpdateDescriptorSets)
     * @return VkWriteDescriptorSet array ready for vkUpdateDescriptorSets
     */
    std::vector<VkWriteDescriptorSet> BuildDescriptorWrites(
        uint32_t imageIndex,
        const std::vector<ResourceVariant>& descriptorResources,
        const std::vector<ShaderManagement::SpirvDescriptorBinding>& descriptorBindings,
        std::vector<VkDescriptorImageInfo>& imageInfos,
        std::vector<VkDescriptorBufferInfo>& bufferInfos
    );
};

} // namespace Vixen::RenderGraph
