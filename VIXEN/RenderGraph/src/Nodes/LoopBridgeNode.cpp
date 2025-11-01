#include "Nodes/LoopBridgeNode.h"
#include "Core/RenderGraph.h"
#include "Core/NodeLogging.h"

namespace Vixen::RenderGraph {

// Type ID: 110
static constexpr NodeTypeId LOOP_BRIDGE_NODE_TYPE_ID = 110;

LoopBridgeNodeType::LoopBridgeNodeType(const std::string& typeName)
    : NodeType(typeName) {
}

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

LoopBridgeNode::~LoopBridgeNode() {
    Cleanup();
}

void LoopBridgeNode::Setup() {
    NODE_LOG_DEBUG("LoopBridgeNode::Setup()");

    // Read LOOP_ID from input (connected to ConstantNode)
    loopID = In<uint32_t>(LoopBridgeNodeConfig::LOOP_ID);

    // Access graph-owned LoopManager
    RenderGraph* graph = GetOwningGraph();
    if (graph) {
        loopManager = &graph->GetLoopManager();
        NODE_LOG_DEBUG("Connected to graph LoopManager with LOOP_ID: " + std::to_string(loopID));
    } else {
        NODE_LOG_ERROR("LoopBridgeNode has no owning graph");
    }
}

void LoopBridgeNode::Compile() {
    NODE_LOG_DEBUG("LoopBridgeNode::Compile()");

    // Verify loop exists
    if (loopManager) {
        const LoopReference* loopRef = loopManager->GetLoopReference(loopID);
        if (!loopRef) {
            NODE_LOG_ERROR("Invalid LOOP_ID - loop not registered");
        }
    }
}

void LoopBridgeNode::Execute(VkCommandBuffer commandBuffer) {
    if (!loopManager) return;

    // Get current loop state
    const LoopReference* loopRef = loopManager->GetLoopReference(loopID);
    if (!loopRef) return;

    // Publish loop state to graph
    Out(LoopBridgeNodeConfig::LOOP_OUT, loopRef);
    Out(LoopBridgeNodeConfig::SHOULD_EXECUTE, loopRef->shouldExecuteThisFrame);
}

void LoopBridgeNode::CleanupImpl() {
    NODE_LOG_DEBUG("LoopBridgeNode::CleanupImpl()");

    // No resources to clean up - LoopManager owned by graph
    loopManager = nullptr;
}

} // namespace Vixen::RenderGraph
