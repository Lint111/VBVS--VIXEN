#include "Nodes/LoopBridgeNode.h"
#include "Core/RenderGraph.h"
#include "Core/NodeLogging.h"

namespace Vixen::RenderGraph {

// Type ID: 110
static constexpr NodeTypeId LOOP_BRIDGE_NODE_TYPE_ID = 110;

std::unique_ptr<NodeInstance> LoopBridgeNodeType::CreateInstance(
    const std::string& instanceName
) const {
    return std::make_unique<LoopBridgeNode>(instanceName, const_cast<LoopBridgeNodeType*>(this));
}

LoopBridgeNode::LoopBridgeNode(
    const std::string& instanceName,
    NodeType* nodeType
)
    : TypedNode<LoopBridgeNodeConfig>(instanceName, nodeType) {
}

void LoopBridgeNode::SetupImpl(TypedSetupContext& ctx) {
    NODE_LOG_DEBUG("LoopBridgeNode::SetupImpl()");
    
}

void LoopBridgeNode::CompileImpl(TypedCompileContext& ctx) {
    NODE_LOG_DEBUG("LoopBridgeNode::CompileImpl()");

    // Read LOOP_ID from input (connected to ConstantNode)
    loopID = ctx.In(LoopBridgeNodeConfig::LOOP_ID);

    // Access graph-owned LoopManager
    RenderGraph* graph = GetOwningGraph();
    if (graph) {
        loopManager = &graph->GetLoopManager();
        NODE_LOG_DEBUG("Connected to graph LoopManager with LOOP_ID: " + std::to_string(loopID));
    } else {
        NODE_LOG_ERROR("LoopBridgeNode has no owning graph");
    }

    // Verify loop exists
    if (loopManager) {
        const LoopReference* loopRef = loopManager->GetLoopReference(loopID);
        if (!loopRef) {
            NODE_LOG_ERROR("Invalid LOOP_ID - loop not registered");
        }
    }
}

void LoopBridgeNode::ExecuteImpl(TypedExecuteContext& ctx) {
    if (!loopManager) return;

    // Get current loop state
    const LoopReference* loopRef = loopManager->GetLoopReference(loopID);
    if (!loopRef) return;

    // Publish loop state to graph
    ctx.Out(LoopBridgeNodeConfig::LOOP_OUT, loopRef);
    ctx.Out(LoopBridgeNodeConfig::SHOULD_EXECUTE, loopRef->shouldExecuteThisFrame);

    // Debug logging for initial testing
    static uint64_t lastLoggedStep = 0;
    if (loopRef->stepCount != lastLoggedStep && loopRef->shouldExecuteThisFrame) {
        NODE_LOG_DEBUG("Loop " + std::to_string(loopID) + " executing step " +
                      std::to_string(loopRef->stepCount) + " (dt=" +
                      std::to_string(loopRef->deltaTime * 1000.0) + "ms)");
        lastLoggedStep = loopRef->stepCount;
    }
}

void LoopBridgeNode::CleanupImpl(TypedCleanupContext& ctx) {
    NODE_LOG_DEBUG("LoopBridgeNode::CleanupImpl(TypedCleanupContext& ctx)");

    // No resources to clean up - LoopManager owned by graph
    loopManager = nullptr;
}

} // namespace Vixen::RenderGraph
