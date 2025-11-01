#include "Nodes/BoolOpNode.h"
#include "Core/NodeLogging.h"

namespace Vixen::RenderGraph {

// Type ID: 111
static constexpr NodeTypeId BOOL_OP_NODE_TYPE_ID = 111;

BoolOpNodeType::BoolOpNodeType(const std::string& typeName)
    : NodeType(BOOL_OP_NODE_TYPE_ID, typeName) {
    // Set node schema from config
    SetSchema(BoolOpNodeConfig());
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

void BoolOpNode::Setup() {
    NODE_LOG_DEBUG("BoolOpNode::Setup()");

    // Read OPERATION parameter
    operation = GetParameter<BoolOp>("OPERATION");
}

void BoolOpNode::Compile() {
    NODE_LOG_DEBUG("BoolOpNode::Compile()");

    // Validate operation type
    if (static_cast<uint8_t>(operation) > static_cast<uint8_t>(BoolOp::NOR)) {
        NODE_LOG_ERROR("Invalid BoolOp operation");
    }
}

void BoolOpNode::Execute(VkCommandBuffer commandBuffer) {
    // Get number of connections to INPUTS slot
    size_t inputCount = GetInputCount(BoolOpNodeConfig::INPUTS);

    if (inputCount == 0) {
        NODE_LOG_ERROR("BoolOpNode has no inputs connected");
        Out(BoolOpNodeConfig::OUTPUT, false);
        return;
    }

    // Read all inputs from the slot (multiple connections)
    std::vector<bool> inputs;
    inputs.reserve(inputCount);
    for (size_t i = 0; i < inputCount; ++i) {
        inputs.push_back(InArray(BoolOpNodeConfig::INPUTS, static_cast<uint32_t>(i), SlotRole::ExecuteOnly));
    }

    // Perform boolean operation across all inputs
    bool result = false;
    switch (operation) {
        case BoolOp::AND: {
            // All inputs must be true
            result = true;
            for (bool input : inputs) {
                result = result && input;
            }
            break;
        }

        case BoolOp::OR: {
            // At least one input must be true
            result = false;
            for (bool input : inputs) {
                result = result || input;
            }
            break;
        }

        case BoolOp::XOR: {
            // Exactly one input must be true
            int trueCount = 0;
            for (bool input : inputs) {
                if (input) trueCount++;
            }
            result = (trueCount == 1);
            break;
        }

        case BoolOp::NOT: {
            // Invert first input (ignore others)
            result = !inputs[0];
            break;
        }

        case BoolOp::NAND: {
            // Not all inputs true
            result = true;
            for (bool input : inputs) {
                result = result && input;
            }
            result = !result;
            break;
        }

        case BoolOp::NOR: {
            // No inputs true
            result = false;
            for (bool input : inputs) {
                result = result || input;
            }
            result = !result;
            break;
        }

        default:
            NODE_LOG_ERROR("Unknown BoolOp operation");
            result = false;
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
