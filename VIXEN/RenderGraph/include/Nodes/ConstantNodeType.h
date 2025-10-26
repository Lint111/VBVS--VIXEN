#pragma once

#include "Core/NodeType.h"
#include "ConstantNode.h"
#include "ConstantNodeConfig.h"

namespace Vixen::RenderGraph {

/**
 * @brief Typed NodeType for ConstantNode<VulkanShader*>
 *
 * Creates constant nodes that output VulkanShader* pointers.
 * The output slot is properly typed as VulkanShaderPtr for validation.
 */
class ShaderConstantNodeType : public NodeType {
public:
    ShaderConstantNodeType() : NodeType("ShaderConstant") {
        // Populate schema from config
        ConstantNodeConfig config;
        inputSchema = config.GetInputVector();
        outputSchema = config.GetOutputVector();
    }

    std::unique_ptr<NodeInstance> CreateInstance(const std::string& instanceName) const override {
        return std::make_unique<ConstantNode>(instanceName, const_cast<ShaderConstantNodeType*>(this));
    }
};

/**
 * @brief Generic ConstantNodeType for other resource types
 *
 * Creates constant nodes with dynamically-typed output.
 * Use SetValue<T>() to inject the constant value and determine the output type.
 */
class ConstantNodeType : public NodeType {
public:
    ConstantNodeType() : NodeType("ConstantNode") {
        // Populate schema from config
        ConstantNodeConfig config;
        inputSchema = config.GetInputVector();
        outputSchema = config.GetOutputVector();
    }

    std::unique_ptr<NodeInstance> CreateInstance(const std::string& instanceName) const override {
        return std::make_unique<ConstantNode>(instanceName, const_cast<ConstantNodeType*>(this));
    }
};

} // namespace Vixen::RenderGraph
