#pragma once

#include "Core/TypedNodeInstance.h"
#include "Data/Nodes/ReconstructionHistoryStateNodeConfig.h"
#include "Data/ReconstructionHistory.h"

namespace Vixen::RenderGraph {

class ReconstructionHistoryStateNode final : public TypedNode<ReconstructionHistoryStateNodeConfig> {
public:
    ReconstructionHistoryStateNode(const std::string& name, NodeType* type) : TypedNode(name, type) {}
    void Invalidate(ReconstructionHistoryReset reason) { state_.Invalidate(reason); }
    void CommitHistoryFrame() { state_.CommitHistoryFrame(); }
protected:
    void TypedCompileImpl(TypedCompileContext& ctx) override;
    void TypedExecuteImpl(TypedExecuteContext& ctx) override;
private:
    ReconstructionHistoryState state_;
};

class ReconstructionHistoryStateNodeType final : public TypedNodeType<ReconstructionHistoryStateNodeConfig> {
public:
    ReconstructionHistoryStateNodeType() : TypedNodeType("ReconstructionHistoryState") {
        graphExecutionKind = GraphExecutionKind::Cpu;
    }
    std::unique_ptr<NodeInstance> CreateInstance(const std::string& name) const override;
};

} // namespace Vixen::RenderGraph
