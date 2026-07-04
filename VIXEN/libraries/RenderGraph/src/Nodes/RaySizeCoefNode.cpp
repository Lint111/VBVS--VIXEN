#include "Nodes/RaySizeCoefNode.h"
#include "Core/NodeRegistration.h"
#include "Core/NodeLogging.h"
#include <cmath>

namespace Vixen::RenderGraph {

std::unique_ptr<NodeInstance> RaySizeCoefNodeType::CreateInstance(const std::string& n) const {
    return std::make_unique<RaySizeCoefNode>(n, const_cast<RaySizeCoefNodeType*>(this));
}

RaySizeCoefNode::RaySizeCoefNode(const std::string& n, NodeType* t)
    : TypedNode<RaySizeCoefNodeConfig>(n, t)
{
}

void RaySizeCoefNode::SetupImpl(TypedSetupContext& ctx) {
    fovDegrees_ = GetParameterValue<float>(RaySizeCoefNodeConfig::PARAM_FOV_DEGREES, 45.0f);
}

void RaySizeCoefNode::CompileImpl(TypedCompileContext& ctx) {
    // Recomputed every Compile — this node is a transitive dependent of whatever publishes
    // HEIGHT (RenderTargetNode::HEIGHT_OUT), so a resize drives this via the standard
    // recompile cascade with no per-frame checks anywhere.
    uint32_t height = ctx.In(RaySizeCoefNodeConfig::HEIGHT);
    if (height == 0) {
        throw std::runtime_error("[RaySizeCoefNode] HEIGHT is 0");
    }

    const float fovYRadians = fovDegrees_ * (3.14159265358979323846f / 180.0f);
    const float raySizeCoef = 2.0f * std::tan((fovYRadians / static_cast<float>(height)) * 0.5f);

    if (raySizeCoef != lastComputed_) {
        NODE_LOG_INFO("[LOD] raySizeCoef recomputed for height " + std::to_string(height) +
                      ": " + std::to_string(raySizeCoef));
        lastComputed_ = raySizeCoef;
    }

    ctx.Out(RaySizeCoefNodeConfig::RAY_SIZE_COEF, raySizeCoef);
}

void RaySizeCoefNode::ExecuteImpl(TypedExecuteContext& ctx) {
    // Value is Compile-derived and stable across a frame's Executes; nothing to do here.
}

void RaySizeCoefNode::CleanupImpl(TypedCleanupContext& ctx) {
    // No resources to release.
}

} // namespace Vixen::RenderGraph

// Self-registration (M3): registrar kept in this TU; RenderGraphNodes is whole-archived so it is not stripped.
VIXEN_REGISTER_NODE(Vixen::RenderGraph::RaySizeCoefNodeType);
