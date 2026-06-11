#include "Nodes/BoolOpNode.h"
#include "Core/NodeLogging.h"
#include "NodeHelpers/ValidationHelpers.h"

using namespace RenderGraph::NodeHelpers;

namespace Vixen::RenderGraph {

// Type ID: 111
static constexpr NodeTypeId BOOL_OP_NODE_TYPE_ID = 111;

std::unique_ptr<NodeInstance> BoolOpNodeType::CreateInstance(
    const std::string& instanceName
) const {
    return std::make_unique<BoolOpNode>(instanceName, const_cast<BoolOpNodeType*>(this));
}

BoolOpNode::BoolOpNode(
    const std::string& instanceName,
    NodeType* nodeType
)
    : TypedNode<BoolOpNodeConfig>(instanceName, nodeType) {
}

void BoolOpNode::SetupImpl(TypedSetupContext& ctx) {
    NODE_LOG_DEBUG("BoolOpNode setup");
}

void BoolOpNode::CompileImpl(TypedCompileContext& ctx) {
    NODE_LOG_DEBUG("BoolOpNode compile");

    // Read OPERATION from input
    operation = ctx.In(BoolOpNodeConfig::OPERATION);

    // Validate operation type
    if (static_cast<uint8_t>(operation) > static_cast<uint8_t>(BoolOp::NOR)) {
        throw std::runtime_error("Invalid BoolOp operation: " + std::to_string(static_cast<int>(operation)));
    }

    NODE_LOG_DEBUG("BoolOp operation set to: " + std::to_string(static_cast<int>(operation)));
}

void BoolOpNode::ExecuteImpl(TypedExecuteContext& ctx) {
    // Read vector of bools from INPUTS slot (accumulation slot returns std::vector<ElementType>)
    // Element type is bool, so we get std::vector<bool>
    const auto& inputs = ctx.In(BoolOpNodeConfig::INPUTS);

    if (inputs.empty()) {
        NODE_LOG_ERROR("BoolOpNode has no inputs");
        ctx.Out(BoolOpNodeConfig::OUTPUT, false);
        return;
    }

    // Accumulator for boolean operations
    bool result = false;
    int trueCount = 0;  // For XOR operation

    // Initialize accumulator based on operation
    switch (operation) {
        case BoolOp::AND:
        case BoolOp::NAND:
            result = true;  // AND starts with true
            break;
        case BoolOp::OR:
        case BoolOp::NOR:
        case BoolOp::XOR:
        case BoolOp::NOT:
        default:
            result = false;
            break;
    }

    // Process each input (std::vector<bool> returns proxy reference, convert to bool)
    for (size_t index = 0; index < inputs.size(); index++) {
        bool inputValue = static_cast<bool>(inputs[index]);

        switch (operation) {
            case BoolOp::AND:
            case BoolOp::NAND:
                result = result && inputValue;
                break;
            case BoolOp::OR:
            case BoolOp::NOR:
                result = result || inputValue;
                break;
            case BoolOp::XOR:
                if (inputValue) trueCount++;
                break;
            case BoolOp::NOT:
                // Only process first input
                if (index == 0) result = !inputValue;
                break;
        }
    }

    // Finalize result based on operation
    switch (operation) {
        case BoolOp::XOR:
            result = (trueCount == 1);
            break;
        case BoolOp::NAND:
        case BoolOp::NOR:
            result = !result;
            break;
        case BoolOp::AND:
        case BoolOp::OR:
        case BoolOp::NOT:
        default:
            // Result already computed
            break;
    }

    // Output result
    ctx.Out(BoolOpNodeConfig::OUTPUT, result);
}

void BoolOpNode::CleanupImpl(TypedCleanupContext& ctx) {
    NODE_LOG_DEBUG("BoolOpNode cleanup");
    // No resources to clean up
}

} // namespace Vixen::RenderGraph
