#pragma once

#include "Core/VariadicTypedNode.h"
#include "StructSpreaderNodeConfig.h"
#include "VulkanSwapChain.h"

namespace Vixen::RenderGraph {

/**
 * @brief Metadata describing a struct member for spreading
 */
struct StructMemberMetadata {
    std::string name;           // Member name (e.g., "swapChainImageViews")
    size_t offset;              // Offset within struct
    ResourceType resourceType;  // Resource type for the member
    VkDescriptorType descriptorType;  // Descriptor type (if applicable)
};

/**
 * @brief Node type for generic struct spreader
 *
 * Type ID: 121
 */
class StructSpreaderNodeType : public TypedNodeType<StructSpreaderNodeConfig> {
public:
    StructSpreaderNodeType(const std::string& typeName = "StructSpreader")
        : TypedNodeType<StructSpreaderNodeConfig>(typeName) {}
    virtual ~StructSpreaderNodeType() = default;

    std::unique_ptr<NodeInstance> CreateInstance(
        const std::string& instanceName
    ) const override;
};

/**
 * @brief Variadic node that spreads struct members into individual outputs
 *
 * Takes a pointer to any struct and creates variadic output slots for each member.
 * The struct type and member metadata must be provided via PreRegisterMembers().
 *
 * Usage:
 * ```cpp
 * auto spreader = graph->AddNode<StructSpreaderNode>("swapchain_spreader");
 *
 * // Pre-register SwapChainPublicVariables members
 * spreader->PreRegisterMembers<SwapChainPublicVariables>({
 *     {"swapChainImageViews", offsetof(SwapChainPublicVariables, swapChainImageViews),
 *      ResourceType::ImageView, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE},
 *     {"swapChainImages", offsetof(SwapChainPublicVariables, swapChainImages),
 *      ResourceType::Image, VK_DESCRIPTOR_TYPE_MAX_ENUM}
 * });
 *
 * // Connect struct pointer
 * batch.Connect(swapChainNode, SwapChainNodeConfig::SWAPCHAIN_PUBLIC,
 *               spreader, StructSpreaderNodeConfig::STRUCT_PTR);
 *
 * // Connect spreader outputs to descriptor gatherer
 * batch.ConnectVariadic(gathererNode, ComputeTest::outputImage,
 *                       spreader, 0);  // Index 0 = first member output
 * ```
 */
class StructSpreaderNode : public VariadicTypedNode<StructSpreaderNodeConfig> {
public:
    StructSpreaderNode(
        const std::string& instanceName,
        NodeType* nodeType
    );
    ~StructSpreaderNode() override = default;

    /**
     * @brief Pre-register struct members for spreading
     *
     * Call during graph construction to define which struct members to expose.
     * Creates variadic output slots for each member.
     *
     * @tparam StructType Type of the struct being spread
     * @param members Vector of member metadata
     */
    template<typename StructType>
    void PreRegisterMembers(const std::vector<StructMemberMetadata>& members) {
        memberMetadata_ = members;

        std::cout << "[StructSpreaderNode::PreRegisterMembers] Registering " << members.size()
                  << " members for " << typeid(StructType).name() << "\n";

        // Create variadic output slots for each member
        for (size_t i = 0; i < members.size(); ++i) {
            const auto& member = members[i];

            VariadicSlotInfo slotInfo;
            slotInfo.slotName = member.name;
            slotInfo.resourceType = member.resourceType;
            slotInfo.descriptorType = member.descriptorType;
            slotInfo.binding = static_cast<uint32_t>(i);  // Use index as binding
            slotInfo.state = SlotState::Tentative;
            slotInfo.resource = nullptr;  // Will be set during Compile

            RegisterVariadicSlot(slotInfo, 0);

            std::cout << "[StructSpreaderNode::PreRegisterMembers] Registered output slot " << i
                      << ": " << member.name << " (type=" << static_cast<int>(member.resourceType) << ")\n";
        }

        SetVariadicInputConstraints(members.size(), members.size());
    }

protected:
    void SetupImpl(Context& ctx) override;
    void CompileImpl(Context& ctx) override;
    void ExecuteImpl(Context& ctx) override;
    void CleanupImpl() override;

private:
    std::vector<StructMemberMetadata> memberMetadata_;
    void* structPtr_ = nullptr;
};

} // namespace Vixen::RenderGraph
