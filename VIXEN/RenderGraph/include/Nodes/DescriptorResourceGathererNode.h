#pragma once

#include "Core/VariadicTypedNode.h"
#include "Core/NodeType.h"
#include "DescriptorResourceGathererNodeConfig.h"
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

    /**
     * @brief IGraphCompilable override - discover variadic slots from shader bundle
     *
     * Reads the SHADER_DATA_BUNDLE input and registers variadic slots based on
     * descriptor bindings found in the shader metadata. This happens during
     * graph compilation, before ConnectVariadic connections are processed.
     */
    void GraphCompileSetup() override;

protected:
    // Template method pattern - override *Impl() methods
    void SetupImpl(Context& ctx) override;
    void CompileImpl(Context& ctx) override;
    void ExecuteImpl(Context& ctx) override;
    void CleanupImpl() override;

    // Variadic validation override - shader metadata-specific validation
    bool ValidateVariadicInputsImpl(Context& ctx, size_t bundleIndex = 0) override;

private:
    // Discovered descriptor metadata from shader
    std::vector<DescriptorSlotInfo> descriptorSlots_;

    // Output resource array (indexed by binding)
    std::vector<ResourceHandleVariant> resourceArray_;

    // Helpers
    void DiscoverDescriptors(Context& ctx);
    void GatherResources(Context& ctx);

    // Shader-specific type validation helpers
    bool ValidateResourceType(Resource* res, VkDescriptorType expectedType);
    VkDescriptorType InferDescriptorType(Resource* res);

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
