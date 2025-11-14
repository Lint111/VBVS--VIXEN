#pragma once

#include "Core/VariadicTypedNode.h"
#include "Core/NodeType.h"
#include "Data/Nodes/PushConstantGathererNodeConfig.h"
#include "ShaderManagement/ShaderDataBundle.h"
#include <memory>
#include <vector>

namespace Vixen::RenderGraph {

/**
 * @brief Node type for gathering push constant field values
 *
 * This node reads shader reflection to discover push constant requirements and
 * creates dynamic input slots for each field. It then packs these values into
 * a contiguous buffer for vkCmdPushConstants.
 *
 * Type ID: 120
 */
class PushConstantGathererNodeType : public TypedNodeType<PushConstantGathererNodeConfig> {
public:
    PushConstantGathererNodeType(const std::string& typeName = "PushConstantGatherer")
        : TypedNodeType<PushConstantGathererNodeConfig>(typeName)
        , defaultMinVariadicInputs(0)
        , defaultMaxVariadicInputs(64)  // Conservative limit - enough fields to fill 256 bytes (common max)
    {}
    virtual ~PushConstantGathererNodeType() = default;

    std::unique_ptr<NodeInstance> CreateInstance(
        const std::string& instanceName
    ) const override;

    // Variadic input constraints (for validation)
    size_t GetDefaultMinVariadicInputs() const { return defaultMinVariadicInputs; }
    size_t GetDefaultMaxVariadicInputs() const { return defaultMaxVariadicInputs; }

private:
    size_t defaultMinVariadicInputs;
    size_t defaultMaxVariadicInputs;
};

/**
 * @brief Push constant field information for dynamic slot creation
 */
struct PushConstantFieldSlotInfo {
    std::string fieldName;        // e.g., "cameraPos", "time"
    uint32_t offset;              // Byte offset in push constant block
    uint32_t size;                // Size in bytes
    ShaderManagement::SpirvTypeInfo::BaseType baseType;  // Float, Vec3, etc.
    uint32_t vecSize;             // 1 for scalar, 3 for vec3, etc.
    size_t dynamicInputIndex;     // Index in dynamic input array
};

/**
 * @brief Variadic node instance for gathering push constant field values
 *
 * Workflow:
 * 1. Setup: Read ShaderDataBundle to discover push constant fields
 * 2. Compile: Validate variadic inputs against shader's push constant layout
 * 3. Execute: Pack field values into contiguous buffer with proper alignment
 * 4. Execute: Pass shader bundle through to downstream nodes
 *
 * Users connect field values via variadic inputs in field order.
 * Node validates types and packing during compile phase.
 */
class PushConstantGathererNode : public VariadicTypedNode<PushConstantGathererNodeConfig> {
public:

    PushConstantGathererNode(
        const std::string& instanceName,
        NodeType* nodeType
    );
    ~PushConstantGathererNode() override = default;

    /**
     * @brief Pre-register variadic slots for push constant fields
     *
     * Call this during graph construction to enable ConnectVariadic() before Setup phase.
     * Accepts field information from shader metadata.
     *
     * Example:
     *   gatherer->PreRegisterPushConstantFields(shaderBundle);
     */
    void PreRegisterPushConstantFields(const ShaderManagement::ShaderDataBundle* shaderBundle);

protected:
    // Variadic lifecycle overrides with phase-specific contexts
    void SetupImpl(VariadicSetupContext& ctx) override;
    void CompileImpl(VariadicCompileContext& ctx) override;
    void ExecuteImpl(VariadicExecuteContext& ctx) override;
    void CleanupImpl(VariadicCleanupContext& ctx) override;

    // Variadic validation - validates against shader metadata at Compile time
    bool ValidateVariadicInputsImpl(VariadicCompileContext& ctx);

private:
    // Discovered push constant metadata from shader
    std::vector<PushConstantFieldSlotInfo> pushConstantFields_;

    // Output data
    std::vector<uint8_t> pushConstantData_;
    std::vector<VkPushConstantRange> pushConstantRanges_;

    // Helpers
    void DiscoverPushConstants(VariadicCompileContext& ctx);
    void PackPushConstantData(VariadicExecuteContext& ctx);
    bool ValidateFieldType(Resource* res, const PushConstantFieldSlotInfo& field);

    // Type conversion helpers
    void PackScalar(const Resource* res, uint8_t* dest, size_t size);
    void PackVector(const Resource* res, uint8_t* dest, size_t componentCount);
    void PackMatrix(const Resource* res, uint8_t* dest, size_t rows, size_t cols);

    // Resource type mapping
    ResourceType GetResourceTypeForField(const PushConstantFieldSlotInfo& field) const;
};

} // namespace Vixen::RenderGraph