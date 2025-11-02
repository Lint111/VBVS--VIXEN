#include "Nodes/BoolOpNode.h"
#include "Core/NodeLogging.h"

namespace Vixen::RenderGraph {

// Type ID: 111
static constexpr NodeTypeId BOOL_OP_NODE_TYPE_ID = 111;

BoolOpNodeType::BoolOpNodeType(const std::string& typeName)
    : NodeType(typeName) {
    // Populate schema from config
    BoolOpNodeConfig config;
    inputSchema = config.GetInputVector();
    outputSchema = config.GetOutputVector();
}

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

BoolOpNode::~BoolOpNode() {
    Cleanup();
}

void BoolOpNode::SetupImpl() {
    NODE_LOG_DEBUG("BoolOpNode::SetupImpl()");

    // Read OPERATION from input (connected to ConstantNode)
    operation = In(BoolOpNodeConfig::OPERATION);
}

void BoolOpNode::CompileImpl() {
    NODE_LOG_DEBUG("BoolOpNode::CompileImpl()");

    // Validate operation type
    if (static_cast<uint8_t>(operation) > static_cast<uint8_t>(BoolOp::NOR)) {
        NODE_LOG_ERROR("Invalid BoolOp operation");
    }
}

void BoolOpNode::ExecuteImpl() {
    // Read vector of bools from INPUTS slot
    std::vector<bool> inputs = In(BoolOpNodeConfig::INPUTS);

    if (inputs.empty()) {
        NODE_LOG_ERROR("BoolOpNode has no inputs");
        Out(BoolOpNodeConfig::OUTPUT, false);
        return;
    }

    // Accumulator for boolean operations (using ExecuteTasks API)
    struct BoolAccumulator {
        bool result = false;
        int trueCount = 0;  // For XOR operation
        BoolOp op;
    };

    BoolAccumulator acc;
    acc.op = operation;

    // Initialize accumulator based on operation
    switch (operation) {
        case BoolOp::AND:
        case BoolOp::NAND:
            acc.result = true;  // AND starts with true
            break;
        case BoolOp::OR:
        case BoolOp::NOR:
        case BoolOp::XOR:
        case BoolOp::NOT:
        default:
            acc.result = false;
            break;
    }

    // Process each input using ExecuteTasks (works for 1 or N inputs)
    auto processInput = [&acc, &inputs](SlotTaskContext& ctx) -> bool {
        uint32_t index = ctx.GetElementIndex();
        bool inputValue = inputs[index];

        switch (acc.op) {
            case BoolOp::AND:
            case BoolOp::NAND:
                acc.result = acc.result && inputValue;
                break;
            case BoolOp::OR:
            case BoolOp::NOR:
                acc.result = acc.result || inputValue;
                break;
            case BoolOp::XOR:
                if (inputValue) acc.trueCount++;
                break;
            case BoolOp::NOT:
                // Only process first input
                if (index == 0) acc.result = !inputValue;
                break;
        }
        return true;
    };

    // Execute tasks (automatically handles 1 or N inputs)
    ExecuteTasks(BoolOpNodeConfig::INPUTS_Slot::index, processInput, nullptr, true);

    // Finalize result based on operation
    bool result = acc.result;
    switch (operation) {
        case BoolOp::XOR:
            result = (acc.trueCount == 1);
            break;
        case BoolOp::NAND:
        case BoolOp::NOR:
            result = !acc.result;
            break;
        case BoolOp::AND:
        case BoolOp::OR:
        case BoolOp::NOT:
        default:
            // Result already computed
            break;
    }

    // Output result
    Out(BoolOpNodeConfig::OUTPUT, result);
}

void BoolOpNode::CleanupImpl() {
    NODE_LOG_DEBUG("BoolOpNode::CleanupImpl()");
    // No resources to clean up
}

} // namespace Vixen::RenderGraph
