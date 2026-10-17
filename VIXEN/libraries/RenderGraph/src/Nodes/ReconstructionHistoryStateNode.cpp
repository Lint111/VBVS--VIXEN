#include "Nodes/ReconstructionHistoryStateNode.h"
#include "Core/NodeRegistration.h"

namespace Vixen::RenderGraph {

std::unique_ptr<NodeInstance> ReconstructionHistoryStateNodeType::CreateInstance(const std::string& name) const {
    return std::make_unique<ReconstructionHistoryStateNode>(name,
        const_cast<ReconstructionHistoryStateNodeType*>(this));
}

void ReconstructionHistoryStateNode::TypedCompileImpl(TypedCompileContext& ctx) {
    state_.Invalidate(ReconstructionHistoryReset::Recompile);
    ctx.Out(ReconstructionHistoryStateNodeConfig::RESET, 1u);
    ctx.Out(ReconstructionHistoryStateNodeConfig::VALID, 0u);
    ctx.Out(ReconstructionHistoryStateNodeConfig::FRAME_INDEX, 0u);
    ctx.Out(ReconstructionHistoryStateNodeConfig::WRITE_INDEX, 0u);
    ctx.Out(ReconstructionHistoryStateNodeConfig::RESET_REASONS,
            static_cast<uint32_t>(ReconstructionHistoryReset::Recompile));
}

void ReconstructionHistoryStateNode::TypedExecuteImpl(TypedExecuteContext& ctx) {
    const auto& camera = ctx.In(ReconstructionHistoryStateNodeConfig::CAMERA);
    auto* internal = ctx.In(ReconstructionHistoryStateNodeConfig::INTERNAL_TARGET);
    auto* output = ctx.In(ReconstructionHistoryStateNodeConfig::OUTPUT_TARGET);
    const auto frame = state_.BeginFrame(camera.invProjection, internal->GetExtent(), output->GetExtent());
    ctx.Out(ReconstructionHistoryStateNodeConfig::RESET, frame.reset);
    ctx.Out(ReconstructionHistoryStateNodeConfig::VALID, frame.valid);
    ctx.Out(ReconstructionHistoryStateNodeConfig::FRAME_INDEX, frame.frameIndex);
    ctx.Out(ReconstructionHistoryStateNodeConfig::WRITE_INDEX, frame.writeIndex);
    ctx.Out(ReconstructionHistoryStateNodeConfig::RESET_REASONS, frame.resetReasons);
}

} // namespace Vixen::RenderGraph

VIXEN_REGISTER_NODE(Vixen::RenderGraph::ReconstructionHistoryStateNodeType);
