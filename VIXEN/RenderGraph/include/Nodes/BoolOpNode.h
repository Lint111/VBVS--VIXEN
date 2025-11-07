#pragma once
#include "Core/TypedNodeInstance.h"
#include "Core/NodeType.h"
#include "Data/Nodes/BoolOpNodeConfig.h"

namespace Vixen::RenderGraph {

/**
 * @brief Node type for boolean logic operations
 *
 * Performs boolean operations on input values (typically from loop SHOULD_EXECUTE outputs).
 * Supports AND, OR, XOR, NOT, NAND, NOR operations.
 *
 * Type ID: 111
 */
class BoolOpNodeType : public TypedNodeType<BoolOpNodeConfig> {
public:
    BoolOpNodeType(const std::string& typeName = "BoolOp")
        : TypedNodeType<BoolOpNodeConfig>(typeName) {}
    virtual ~BoolOpNodeType() = default;

    std::unique_ptr<NodeInstance> CreateInstance(
        const std::string& instanceName
    ) const override;
};

/**
 * @brief Node instance for boolean logic composition
 *
 * Uses TypedNode<BoolOpNodeConfig> for compile-time type safety.
 * Operation determined by OPERATION parameter.
 *
 * Parameters:
 * - OPERATION (BoolOp): Which boolean operation to perform
 *
 * Inputs:
 * - INPUT_A (bool): First operand
 * - INPUT_B (bool, nullable): Second operand (unused for NOT)
 *
 * Outputs:
 * - OUTPUT (bool): Result of boolean operation
 *
 * Examples:
 * - AND: Both loops must be active
 * - OR: At least one loop must be active
 * - NOT: Invert loop state
 */
class BoolOpNode : public TypedNode<BoolOpNodeConfig> {
public:
    BoolOpNode(
        const std::string& instanceName,
        NodeType* nodeType
    );
    ~BoolOpNode() override = default;

protected:
    // Template method pattern - override *Impl() methods
    void SetupImpl(SetupContext& ctx) override;
    void CompileImpl(CompileContext& ctx) override;
    void ExecuteImpl(ExecuteContext& ctx) override;
    void CleanupImpl(CleanupContext& ctx) override;

private:
    BoolOp operation = BoolOp::AND;
};

} // namespace Vixen::RenderGraph
