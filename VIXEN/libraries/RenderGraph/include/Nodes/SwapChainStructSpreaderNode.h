#pragma once

#include "Core/TypedNodeInstance.h"
#include "SwapChainStructSpreaderNodeConfig.h"
#include "VulkanSwapChain.h"

namespace Vixen::RenderGraph {

/**
 * @brief Node type for SwapChainStructSpreaderNode
 *
 * Spreads SwapChainPublicVariables struct members into individual outputs.
 * Type ID: 120
 */
class SwapChainStructSpreaderNodeType : public TypedNodeType<SwapChainStructSpreaderNodeConfig> {
public:
    SwapChainStructSpreaderNodeType(const std::string& typeName = "SwapChainStructSpreader")
        : TypedNodeType<SwapChainStructSpreaderNodeConfig>(typeName) {}
    virtual ~SwapChainStructSpreaderNodeType() = default;

    std::unique_ptr<NodeInstance> CreateInstance(
        const std::string& instanceName
    ) const override;
};

/**
 * @brief Spreads SwapChainPublicVariables into typed outputs
 *
 * Simple passthrough node that exposes struct members as individual outputs.
 * No Vulkan resource creation - just pointer manipulation.
 */
class SwapChainStructSpreaderNode : public TypedNode<SwapChainStructSpreaderNodeConfig> {
public:

    SwapChainStructSpreaderNode(
        const std::string& instanceName,
        NodeType* nodeType
    );
    ~SwapChainStructSpreaderNode() override = default;

protected:
    void SetupImpl(TypedSetupContext& ctx) override;
    void CompileImpl(TypedCompileContext& ctx) override;
    void ExecuteImpl(TypedExecuteContext& ctx) override;
    void CleanupImpl(TypedCleanupContext& ctx) override;
};

} // namespace Vixen::RenderGraph
