#include "Nodes/BoolOpNode.h"
#include "Core/NodeLogging.h"

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

void BoolOpNode::SetupImpl(SetupContext& ctx) {
    NODE_LOG_DEBUG("BoolOpNode::SetupImpl()");

    // Read OPERATION from input (connected to ConstantNode)
    operation = In(BoolOpNodeConfig::OPERATION);
}

void BoolOpNode::CompileImpl(CompileContext& ctx) {
    NODE_LOG_DEBUG("BoolOpNode::CompileImpl()");

    // Validate operation type
    if (static_cast<uint8_t>(operation) > static_cast<uint8_t>(BoolOp::NOR)) {
        NODE_LOG_ERROR("Invalid BoolOp operation");
    }
}

void BoolOpNode::ExecuteImpl(ExecuteContext& ctx) {
    // Read vector of bools from INPUTS slot
    std::vector<bool> inputs = ctx.In(BoolOpNodeConfig::INPUTS);

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

    // Process each input
    for (size_t index = 0; index < inputs.size(); index++) {
        bool inputValue = inputs[index];

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

void BoolOpNode::CleanupImpl(CleanupContext& ctx) {
    NODE_LOG_DEBUG("BoolOpNode::CleanupImpl(CleanupContext& ctx)");
    // No resources to clean up
}

} // namespace Vixen::RenderGraph
