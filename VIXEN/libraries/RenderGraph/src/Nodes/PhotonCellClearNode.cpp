// Copyright (C) 2025 Lior Yanai (eLiorg). Licensed under the MIT License.

#include "Nodes/PhotonCellPassNodes.h"

#include "Core/NodeRegistration.h"
#include "Nodes/PhotonCellShaderRegistration.h"
#include "Nodes/PhotonCellTableNode.h"

namespace Vixen::RenderGraph {

std::unique_ptr<NodeInstance> PhotonCellClearNodeType::CreateInstance(
    const std::string& instanceName) const {
    return std::make_unique<PhotonCellClearNode>(
        instanceName, const_cast<PhotonCellClearNodeType*>(this));
}

PhotonCellClearNode::PhotonCellClearNode(const std::string& instanceName, NodeType* nodeType)
    : ComputeStageNode(instanceName, nodeType) {
    Detail::ConfigurePhotonProducer(*this, PhotonCellTableNode::kCapacity / 64u);
}

void PhotonCellClearNode::RegisterShader(
    ShaderLibraryNode& shaderLibrary,
    ShaderManagement::ShaderCacheManager* cache) const {
    Detail::RegisterPhotonShader(shaderLibrary, cache,
                                 "PhotonCellClear.comp", Metadata::PROGRAM_NAME);
}

void PhotonCellClearNode::ExecuteImpl(TypedExecuteContext& ctx) {
    // VIXEN_PHOTON_CELLS_CLEAR is an explicit reset request, not a steady-state
    // table clear.  Keep this state in the node so the application/graph builder
    // never carries clear lifecycle logic.  ComputeStageNode still submits the
    // normal near-zero bookkeeping path after the one real dispatch, preserving
    // scheduler and GPU-query invariants without touching the table.
    if (clearPending_) {
        clearPending_ = false;
        SetParameter(ComputeStageNodeConfig::PARAM_DISPATCH_ENABLED, true);
    } else {
        SetParameter(ComputeStageNodeConfig::PARAM_DISPATCH_ENABLED, false);
    }
    ComputeStageNode::ExecuteImpl(ctx);
}

} // namespace Vixen::RenderGraph

VIXEN_REGISTER_NODE(Vixen::RenderGraph::PhotonCellClearNodeType);
