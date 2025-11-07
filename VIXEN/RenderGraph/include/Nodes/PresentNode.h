#pragma once
#include "Core/TypedNodeInstance.h"
#include "Core/NodeType.h"
#include "Data/Nodes/PresentNodeConfig.h"
#include <memory>

namespace Vixen::RenderGraph {

/**
 * @brief Node type for presenting rendered images to the swapchain
 *
 * Queues presentation operations with proper synchronization.
 * This is the final node in the rendering pipeline.
 *
 * Type ID: 110
 */
class PresentNodeType : public TypedNodeType<PresentNodeConfig> {
public:
    PresentNodeType(const std::string& typeName = "Present")
        : TypedNodeType<PresentNodeConfig>(typeName) {}
    virtual ~PresentNodeType() = default;

    std::unique_ptr<NodeInstance> CreateInstance(
        const std::string& instanceName
    ) const override;
};

/**
 * @brief Node instance for presentation operations
 * 
 * Now uses TypedNode<PresentNodeConfig> for compile-time type safety.
 * All inputs/outputs are accessed via the typed config slot API.
 * 
 * See PresentNodeConfig.h for slot definitions and parameters.
 */
class PresentNode : public TypedNode<PresentNodeConfig> {
public:
    PresentNode(
        const std::string& instanceName,
        NodeType* nodeType
    );
    ~PresentNode() override = default;

    // Execute presentation (helper called from ExecuteImpl)
    VkResult Present(Context& ctx);

protected:
	// Template method pattern - override *Impl() methods
	void SetupImpl(SetupContext& ctx) override;
	void CompileImpl(CompileContext& ctx) override;
	void ExecuteImpl(ExecuteContext& ctx) override;
	void CleanupImpl(CleanupContext& ctx) override;

private:
    // Configuration from parameters
    bool waitForIdle = true;

    // State
    VkResult lastResult = VK_SUCCESS;
};

} // namespace Vixen::RenderGraph
