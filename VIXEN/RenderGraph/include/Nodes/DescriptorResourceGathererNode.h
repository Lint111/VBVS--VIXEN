#pragma once

#include "Core/VariadicTypedNode.h"
#include "Core/NodeType.h"
#include "Data/Nodes/DescriptorResourceGathererNodeConfig.h"
#include "ShaderManagement/ShaderDataBundle.h"
#include <memory>
#include <vector>

namespace Vixen::RenderGraph {

/**
 * @brief Node type for gathering descriptor resources based on shader metadata
 *
 * This node reads shader SDI files to discover descriptor requirements and
 * creates dynamic input slots for each resource. It then gathers these
 * resources into a single output array for DescriptorSetNode.
 *
 * Type ID: 114
 */
class DescriptorResourceGathererNodeType : public TypedNodeType<DescriptorResourceGathererNodeConfig> {
public:
    DescriptorResourceGathererNodeType(const std::string& typeName = "DescriptorResourceGatherer")
        : TypedNodeType<DescriptorResourceGathererNodeConfig>(typeName) {}
    virtual ~DescriptorResourceGathererNodeType() = default;

    std::unique_ptr<NodeInstance> CreateInstance(
        const std::string& instanceName
    ) const override;

    // Override to account for variadic input slots (binding indices)
    // Returns: base input count + max variadic binding index + 1
    size_t GetInputCount() const override {
        // Base implementation returns inputSchema.size() = 1 (SHADER_DATA_BUNDLE)
        // Variadic nodes need to account for dynamic binding indices
        // Since we don't know max binding at type-level, return a large value
        // Validation will be skipped for indices >= base count
        return 256;  // Support up to 256 descriptor bindings (Vulkan max per set)
    }
};

/**
 * @brief Descriptor information for dynamic slot creation
 */
struct DescriptorSlotInfo {
    uint32_t binding;
    VkDescriptorType descriptorType;
    std::string slotName;  // e.g., "input_image", "uniform_buffer"
    size_t dynamicInputIndex;  // Index in dynamic input array
};

/**
 * @brief Variadic node instance for gathering descriptor resources
 *
 * Workflow:
 * 1. Setup: Read ShaderDataBundle to discover descriptor requirements
 * 2. Compile: Validate variadic inputs against shader's descriptor layout
 * 3. Execute: Gather validated resources into output array (binding order)
 * 4. Execute: Pass shader bundle through to downstream nodes
 *
 * Users connect arbitrary number of resources via variadic inputs.
 * Node validates count and types match shader requirements during compile.
 */
class DescriptorResourceGathererNode : public VariadicTypedNode<DescriptorResourceGathererNodeConfig> {
public:

    DescriptorResourceGathererNode(
        const std::string& instanceName,
        NodeType* nodeType
    );
    ~DescriptorResourceGathererNode() override = default;

    /**
     * @brief Pre-register variadic slots using shader metadata from Names.h
     *
     * Call this during graph construction to enable ConnectVariadic() before Setup phase.
     * Accepts variadic list of binding refs (e.g., ComputeTest::outputImage).
     *
     * Example:
     *   gatherer->PreRegisterVariadicSlots(ComputeTest::outputImage, ComputeTest::uniformBuffer);
     */
    template<typename... BindingRefs>
    void PreRegisterVariadicSlots(BindingRefs... bindingRefs) {
        PreRegisterVariadicSlotsImpl({bindingRefs...});
    }

    // No GraphCompileSetup override - descriptor discovery happens in SetupImpl
    // when connected inputs are available via Context

protected:
    // Variadic lifecycle overrides with phase-specific contexts
    void SetupImpl(VariadicSetupContext& ctx) override;
    void CompileImpl(VariadicCompileContext& ctx) override;
    void ExecuteImpl(VariadicExecuteContext& ctx) override;
    void CleanupImpl(VariadicCleanupContext& ctx) override;

    // Variadic validation - shader metadata-specific validation at Compile time
    // NOTE: Not an override - base class ValidateVariadicInputsImpl only exists for ExecuteContext
    // Context automatically handles bundle access via taskIndex
    bool ValidateVariadicInputsImpl(VariadicCompileContext& ctx);

    // Single input validation helpers (extracted from ValidateVariadicInputsImpl)
    bool ValidateSingleInput(VariadicCompileContext& ctx, size_t slotIndex);
    bool ShouldSkipTransientSlot(const VariadicSlotInfo* slotInfo, size_t slotIndex);
    bool ShouldSkipFieldExtractionSlot(const VariadicSlotInfo* slotInfo, size_t slotIndex);
    void LogTypeValidationError(size_t slotIndex, const VariadicSlotInfo* slotInfo, VkDescriptorType expectedType);

private:
    // Discovered descriptor metadata from shader
    std::vector<DescriptorSlotInfo> descriptorSlots_;

    // Output resource array (indexed by binding)
    std::vector<ResourceVariant> resourceArray_;
    std::vector<SlotRole> slotRoleArray_;  // Parallel array: slot role for each binding

    // Primary workflow helpers
    void DiscoverDescriptors(VariadicCompileContext& ctx);
    void GatherResources(VariadicCompileContext& ctx);
    void ValidateTentativeSlotsAgainstShader(VariadicCompileContext& ctx, const ShaderManagement::ShaderDataBundle* shaderBundle);

    // Validation helpers (extracted from ValidateTentativeSlotsAgainstShader)
    void ValidateSingleSlotAgainstShader(VariadicCompileContext& ctx, size_t slotIndex, const VariadicSlotInfo* slotInfo, const ShaderManagement::DescriptorLayoutSpecification* layoutSpec);
    void UpdateSlotWithShaderBinding(VariadicCompileContext& ctx, size_t slotIndex, const VariadicSlotInfo* slotInfo, const ShaderManagement::DescriptorBindingInfo& shaderBinding);
    void MarkSlotAsInvalid(VariadicCompileContext& ctx, size_t slotIndex, const VariadicSlotInfo* slotInfo);

    // Resource gathering helpers (extracted from GatherResources)
    bool ProcessSlot(size_t slotIndex, const VariadicSlotInfo* slotInfo);
    void InitializeExecuteOnlySlot(size_t slotIndex, uint32_t binding, SlotRole role);
    void StoreFieldExtractionResource(size_t slotIndex, uint32_t binding, size_t fieldOffset, const ResourceVariant& variant);
    void StoreRegularResource(size_t slotIndex, uint32_t binding, const std::string& slotName, SlotRole role, const ResourceVariant& variant);

    // Pointer extraction helper for field extraction
    template<typename T>
    const void* ExtractRawPointerFromVariant(T&& structPtr);

    // Shader-specific type validation helpers
    bool ValidateResourceType(Resource* res, VkDescriptorType expectedType);
    VkDescriptorType InferDescriptorType(Resource* res);  // Deprecated - use IsResourceCompatibleWithDescriptorType

    // Generic descriptor type compatibility (visitor pattern)
    bool IsResourceCompatibleWithDescriptorType(Resource* res, VkDescriptorType descriptorType);
    std::optional<ResourceUsage> ExtractResourceUsage(Resource* res);
    bool CheckUsageCompatibility(ResourceUsage usage, ResourceType resType, VkDescriptorType descriptorType);
    bool IsResourceTypeCompatibleWithDescriptor(ResourceType resType, VkDescriptorType descriptorType);

    // Pre-registration helper (implementation detail)
    template<typename BindingRef>
    void PreRegisterVariadicSlotsImpl(std::initializer_list<BindingRef> bindingRefs) {
        for (const auto& ref : bindingRefs) {
            // Create descriptor slot info
            DescriptorSlotInfo slotInfo;
            slotInfo.binding = ref.binding;
            slotInfo.descriptorType = ref.type;
            slotInfo.slotName = ref.name;
            slotInfo.dynamicInputIndex = descriptorSlots_.size();

            descriptorSlots_.push_back(slotInfo);

            // Register variadic slot with bundle 0
            VariadicSlotInfo variadicSlot;
            variadicSlot.resource = nullptr;
            variadicSlot.resourceType = ResourceType::Image;  // Default
            variadicSlot.slotName = slotInfo.slotName;
            variadicSlot.binding = ref.binding;
            variadicSlot.descriptorType = ref.type;

            RegisterVariadicSlot(variadicSlot, 0);

            std::cout << "[DescriptorResourceGathererNode::PreRegister] Registered slot for binding "
                      << ref.binding << ": " << ref.name << " (type=" << ref.type << ")\n";
        }

        // Set variadic input constraints
        if (!descriptorSlots_.empty()) {
            SetVariadicInputConstraints(descriptorSlots_.size(), descriptorSlots_.size());
        }
    }
};

} // namespace Vixen::RenderGraph
