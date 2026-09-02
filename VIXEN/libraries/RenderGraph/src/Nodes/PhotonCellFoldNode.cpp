// Copyright (C) 2025 Lior Yanai (eLiorg). Licensed under the MIT License.

#include "Nodes/PhotonCellPassNodes.h"

#include "Core/NodeRegistration.h"
#include "Nodes/PhotonCellShaderRegistration.h"
#include "Nodes/PhotonCellTableNode.h"

namespace Vixen::RenderGraph {

std::unique_ptr<NodeInstance> PhotonCellFoldNodeType::CreateInstance(
    const std::string& instanceName) const {
    return std::make_unique<PhotonCellFoldNode>(
        instanceName, const_cast<PhotonCellFoldNodeType*>(this));
}

PhotonCellFoldNode::PhotonCellFoldNode(const std::string& instanceName, NodeType* nodeType)
    : ComputeStageNode(instanceName, nodeType) {
    Detail::ConfigurePhotonProducer(*this, PhotonCellTableNode::kCapacity / 64u);
}

void PhotonCellFoldNode::RegisterShader(
    ShaderLibraryNode& shaderLibrary,
    ShaderManagement::ShaderCacheManager* cache) const {
    Detail::RegisterPhotonShader(shaderLibrary, cache,
                                 "PhotonCellFold.comp", Metadata::PROGRAM_NAME);
}

} // namespace Vixen::RenderGraph

VIXEN_REGISTER_NODE(Vixen::RenderGraph::PhotonCellFoldNodeType);
