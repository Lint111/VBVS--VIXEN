#pragma once
#include "Core/TypedNodeInstance.h"
#include "Core/NodeType.h"
#include "Data/Nodes/LoopBridgeNodeConfig.h"
#include <memory>

namespace Vixen::RenderGraph {

/**
 * @brief Node type for accessing graph-owned loop system
 *
 * Bridge node that provides access to RenderGraph's LoopManager.
 * Similar to ShaderLibraryNode pattern - zero input slots, accesses
 * graph system directly via GetGraph()->GetLoopManager().
 *
 * Type ID: 110
 */
class LoopBridgeNode;

class LoopBridgeNodeType : public TypedNodeType<LoopBridgeNodeConfig, LoopBridgeNode> {
public:
    LoopBridgeNodeType(const std::string& typeName = "LoopBridge")
        : TypedNodeType<LoopBridgeNodeConfig, LoopBridgeNode>(typeName) {}
    virtual ~LoopBridgeNodeType() = default;

};

/**
 * @brief Node instance for publishing loop state to graph
 *
 * Uses TypedNode<LoopBridgeNodeConfig> for compile-time type safety.
 * Publishes LoopReference pointer and shouldExecuteThisFrame bool.
 *
 * Parameters:
 * - LOOP_ID (uint32_t): Which loop to bridge (from graph->RegisterLoop())
 *
 * Outputs:
 * - LOOP_OUT (const LoopReference*): Stable pointer to loop state
 * - SHOULD_EXECUTE (bool): Whether loop should execute this frame
 */
class LoopBridgeNode : public TypedNode<LoopBridgeNodeConfig> {
public:

    LoopBridgeNode(
        const std::string& instanceName,
        NodeType* nodeType
    );
    ~LoopBridgeNode() override = default;

protected:
    // Template method pattern - override *Impl() methods
    void TypedSetupImpl(TypedSetupContext& ctx) override;
    void TypedCompileImpl(TypedCompileContext& ctx) override;
    void TypedExecuteImpl(TypedExecuteContext& ctx) override;
    void TypedCleanupImpl(TypedCleanupContext& ctx) override;

private:
    uint32_t loopID = 0;
    LoopManager* loopManager = nullptr;  // Non-owning pointer to graph system
};

} // namespace Vixen::RenderGraph
