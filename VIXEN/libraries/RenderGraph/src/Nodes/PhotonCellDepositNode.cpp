// Copyright (C) 2025 Lior Yanai (eLiorg). Licensed under the MIT License.

#include "Nodes/PhotonCellPassNodes.h"

#include "Core/NodeRegistration.h"
#include "Nodes/PhotonCellShaderRegistration.h"

namespace Vixen::RenderGraph {

std::unique_ptr<NodeInstance> PhotonCellDepositNodeType::CreateInstance(
    const std::string& instanceName) const {
    return std::make_unique<PhotonCellDepositNode>(
        instanceName, const_cast<PhotonCellDepositNodeType*>(this));
}

PhotonCellDepositNode::PhotonCellDepositNode(const std::string& instanceName, NodeType* nodeType)
    : ComputeStageNode(instanceName, nodeType) {
    Detail::ConfigurePhotonProducer(*this, 0u);
}

void PhotonCellDepositNode::ConfigureForRecordCount(uint32_t recordCount) {
    const uint32_t dispatchX = recordCount / 64u + (recordCount % 64u != 0u ? 1u : 0u);
    Detail::ConfigurePhotonProducer(*this, dispatchX);
}

void PhotonCellDepositNode::RegisterShader(
    ShaderLibraryNode& shaderLibrary,
    ShaderManagement::ShaderCacheManager* cache) const {
    Detail::RegisterPhotonShader(shaderLibrary, cache,
                                 "PhotonDeposit.comp", Metadata::PROGRAM_NAME);
}

} // namespace Vixen::RenderGraph

VIXEN_REGISTER_NODE(Vixen::RenderGraph::PhotonCellDepositNodeType);
