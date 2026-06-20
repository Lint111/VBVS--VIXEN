// ConstantNode self-registration TU (M3).
//
// ConstantNodeType.h defines two header-only NodeTypes — ShaderConstantNodeType
// ("ShaderConstant") and ConstantNodeType ("ConstantNode") — and there is no other
// ConstantNode source among the node sources. This small TU exists solely to
// self-register both. The former "register in application code (circular
// dependency)" note is moot now that registration lives in RenderGraphNodes, not
// the app.
#include "Core/RenderGraph.h"   // ConstantNode.h uses the complete RenderGraph type
#include "Nodes/ConstantNodeType.h"
#include "Core/NodeRegistration.h"

VIXEN_REGISTER_NODE(Vixen::RenderGraph::ShaderConstantNodeType);
VIXEN_REGISTER_NODE(Vixen::RenderGraph::ConstantNodeType);
