#pragma once
#include "Core/TypedNodeInstance.h"
#include "Core/NodeType.h"
#include "Nodes/PresentNodeConfig.h"
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
class PresentNodeType : public NodeType {
public:
    PresentNodeType(const std::string& typeName = "Present");
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
    virtual ~PresentNode();

    // Execute presentation
    VkResult Present();

protected:
	// Template method pattern - override *Impl() methods
	void SetupImpl() override;
	void CompileImpl() override;
	void ExecuteImpl() override;
	void CleanupImpl() override;

private:
    // Configuration from parameters
    bool waitForIdle = true;

    // State
    VkResult lastResult = VK_SUCCESS;
};

} // namespace Vixen::RenderGraph
