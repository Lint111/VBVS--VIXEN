// Copyright (C) 2025 Lior Yanai (eLiorg). Licensed under the MIT License.

#include "Nodes/PhotonCellPassNodes.h"

#include "Core/NodeRegistration.h"
#include "Nodes/PhotonCellShaderRegistration.h"
#include "Nodes/PhotonCellTableNode.h"

namespace Vixen::RenderGraph {


PhotonCellFoldNode::PhotonCellFoldNode(const std::string& instanceName, NodeType* nodeType)
    : ComputeStageNode(instanceName, nodeType) {
    Detail::ConfigurePhotonProducer(*this, PhotonCellTableNode::kCapacity / 64u);
}

void PhotonCellFoldNode::RegisterShader(ShaderLibraryNode& shaderLibrary) const {
    Detail::RegisterPhotonShader(shaderLibrary,
                                 "PhotonCellFold.comp", Metadata::PROGRAM_NAME);
}

} // namespace Vixen::RenderGraph

VIXEN_REGISTER_NODE(Vixen::RenderGraph::PhotonCellFoldNodeType);
