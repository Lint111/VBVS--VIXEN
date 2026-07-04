#pragma once
#include "Core/TypedNodeInstance.h"
#include "Core/NodeType.h"
#include "Data/Nodes/RaySizeCoefNodeConfig.h"

namespace Vixen::RenderGraph {

/**
 * @brief Node type for RaySizeCoefNode (see RaySizeCoefNodeConfig for the math + rationale).
 * Type ID: 127
 */
class RaySizeCoefNodeType : public TypedNodeType<RaySizeCoefNodeConfig> {
public:
    RaySizeCoefNodeType(const std::string& typeName = "RaySizeCoef")
        : TypedNodeType<RaySizeCoefNodeConfig>(typeName) {}
    virtual ~RaySizeCoefNodeType() = default;

    std::unique_ptr<NodeInstance> CreateInstance(const std::string& instanceName) const override;
};

/**
 * @brief Derives the LOD ray-cone spread constant (raySizeCoef) from a live render-target height.
 * See RaySizeCoefNodeConfig for the formula and the resize-cascade mechanism.
 */
class RaySizeCoefNode : public TypedNode<RaySizeCoefNodeConfig> {
public:
    RaySizeCoefNode(const std::string& instanceName, NodeType* nodeType);
    ~RaySizeCoefNode() override = default;

protected:
    void SetupImpl(TypedSetupContext& ctx) override;
    void CompileImpl(TypedCompileContext& ctx) override;
    void ExecuteImpl(TypedExecuteContext& ctx) override;
    void CleanupImpl(TypedCleanupContext& ctx) override;

private:
    float fovDegrees_ = 45.0f;
    float lastComputed_ = 0.0f;
};

} // namespace Vixen::RenderGraph
