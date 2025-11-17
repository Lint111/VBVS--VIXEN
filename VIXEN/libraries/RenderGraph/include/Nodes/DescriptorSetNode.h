#pragma once
#include "Core/TypedNodeInstance.h"
#include "Core/NodeType.h"
#include "Core/PerFrameResources.h"
#include "Data/Nodes/DescriptorSetNodeConfig.h"
// TEMPORARILY REMOVED - MVP uses hardcoded descriptor layouts
// #include "DescriptorLayoutSpec.h"
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
    const ::ShaderManagement::DescriptorLayoutSpec* GetLayoutSpec() const { return layoutSpec; }

protected:
	// Template method pattern - override *Impl() methods
	void SetupImpl(TypedSetupContext& ctx) override;
	void CompileImpl(TypedCompileContext& ctx) override;
	void ExecuteImpl(TypedExecuteContext& ctx) override;
	void CleanupImpl(TypedCleanupContext& ctx) override;

private:
    // Node state flags (bitwise combinable)
    enum class NodeFlags : uint8_t {
        None                = 0,
        Paused              = 1 << 0,  // Rendering paused (swapchain recreation)
        NeedsInitialBind    = 1 << 1,  // Needs to bind Dependency descriptors on first Execute
        // Add more flags here as needed
    };

    // State helper functions
    inline bool HasFlag(NodeFlags flag) const {
        return (static_cast<uint8_t>(flags) & static_cast<uint8_t>(flag)) != 0;
    }
    inline void SetFlag(NodeFlags flag) {
        flags = static_cast<NodeFlags>(static_cast<uint8_t>(flags) | static_cast<uint8_t>(flag));
    }
    inline void ClearFlag(NodeFlags flag) {
        flags = static_cast<NodeFlags>(static_cast<uint8_t>(flags) & ~static_cast<uint8_t>(flag));
    }
    inline bool IsPaused() const { return HasFlag(NodeFlags::Paused); }
    inline bool IsRendering() const { return !IsPaused(); }

    // Configuration
    const ::ShaderManagement::DescriptorLayoutSpec* layoutSpec = nullptr;

    // Vulkan resources
    VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> descriptorSets;

    // CashSystem integration - cached during Compile()
    CashSystem::DescriptorCacher* descriptorCacher = nullptr;

    // Pipeline layout for binding descriptor sets
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;

    // Phase H: Persistent storage for data-driven descriptor binding
    std::vector<std::vector<VkDescriptorImageInfo>> perFrameImageInfos;
    std::vector<std::vector<VkDescriptorBufferInfo>> perFrameBufferInfos;

    // Consolidated node flags (replaces individual bools)
    NodeFlags flags = NodeFlags::None;

    // Helpers for CompileImpl
    void SetupDeviceAndShaderBundle(TypedCompileContext& ctx, std::shared_ptr<::ShaderManagement::ShaderDataBundle>& outShaderBundle);
    void RegisterDescriptorCacher();
    void CreateDescriptorSetLayout(const std::vector<::ShaderManagement::SpirvDescriptorBinding>& descriptorBindings);
    void CreateDescriptorPool(const ::ShaderManagement::ShaderDataBundle& shaderBundle, uint32_t imageCount);
    void AllocateDescriptorSets(uint32_t imageCount);

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
        const std::vector<DescriptorHandleVariant>& descriptorResources,
        const std::vector<::ShaderManagement::SpirvDescriptorBinding>& descriptorBindings,
        std::vector<VkDescriptorImageInfo>& imageInfos,
        std::vector<VkDescriptorBufferInfo>& bufferInfos,
        const std::vector<SlotRole>& slotRoles = {},
        SlotRole roleFilter = SlotRole::Dependency  // Filter to process (Dependency or ExecuteOnly)
    );

    // Descriptor write helpers - extracted from BuildDescriptorWrites for readability
    VkSampler FindSamplerResource(const std::vector<DescriptorHandleVariant>& descriptorResources, uint32_t targetBinding);
    bool ValidateAndFilterBinding(
        const ::ShaderManagement::SpirvDescriptorBinding& binding,
        const std::vector<DescriptorHandleVariant>& descriptorResources,
        const std::vector<SlotRole>& slotRoles,
        SlotRole roleFilter
    );
    void HandleStorageImage(
        const ::ShaderManagement::SpirvDescriptorBinding& binding,
        const DescriptorHandleVariant& resourceVariant,
        uint32_t imageIndex,
        VkWriteDescriptorSet& write,
        std::vector<VkDescriptorImageInfo>& imageInfos,
        std::vector<VkWriteDescriptorSet>& writes
    );
    void HandleSampledImage(
        const ::ShaderManagement::SpirvDescriptorBinding& binding,
        const DescriptorHandleVariant& resourceVariant,
        uint32_t imageIndex,
        VkWriteDescriptorSet& write,
        std::vector<VkDescriptorImageInfo>& imageInfos,
        std::vector<VkWriteDescriptorSet>& writes
    );
    void HandleSampler(
        const ::ShaderManagement::SpirvDescriptorBinding& binding,
        const DescriptorHandleVariant& resourceVariant,
        VkWriteDescriptorSet& write,
        std::vector<VkDescriptorImageInfo>& imageInfos,
        std::vector<VkWriteDescriptorSet>& writes
    );
    void HandleCombinedImageSampler(
        const ::ShaderManagement::SpirvDescriptorBinding& binding,
        const DescriptorHandleVariant& resourceVariant,
        const std::vector<DescriptorHandleVariant>& descriptorResources,
        uint32_t imageIndex,
        size_t bindingIdx,
        VkWriteDescriptorSet& write,
        std::vector<VkDescriptorImageInfo>& imageInfos,
        std::vector<VkWriteDescriptorSet>& writes
    );
    void HandleBuffer(
        const ::ShaderManagement::SpirvDescriptorBinding& binding,
        const DescriptorHandleVariant& resourceVariant,
        VkWriteDescriptorSet& write,
        std::vector<VkDescriptorBufferInfo>& bufferInfos,
        std::vector<VkWriteDescriptorSet>& writes
    );
};

} // namespace Vixen::RenderGraph
