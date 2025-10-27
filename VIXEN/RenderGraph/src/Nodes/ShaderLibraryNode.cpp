#include "Nodes/ShaderLibraryNode.h"
#include "Core/RenderGraph.h"
#include "VulkanResources/VulkanDevice.h"
#include "Core/NodeLogging.h"
#include "error/VulkanError.h"

// MVP STUB: ShaderLibraryNode temporarily disabled pending ShaderManagement integration
// This file provides minimal stubs to allow compilation

namespace Vixen::RenderGraph {

// ====== ShaderLibraryNodeType ======

ShaderLibraryNodeType::ShaderLibraryNodeType(const std::string& typeName) : NodeType(typeName) {
    pipelineType = PipelineType::None;
    requiredCapabilities = DeviceCapability::None;
    supportsInstancing = false;
    maxInstances = 1;

    ShaderLibraryNodeConfig config;
    inputSchema = config.GetInputVector();
    outputSchema = config.GetOutputVector();

    workloadMetrics.estimatedMemoryFootprint = 0;
    workloadMetrics.estimatedComputeCost = 0.0f;
    workloadMetrics.estimatedBandwidthCost = 0.0f;
    workloadMetrics.canRunInParallel = false;
}

std::unique_ptr<NodeInstance> ShaderLibraryNodeType::CreateInstance(
    const std::string& instanceName
) const {
    return std::make_unique<ShaderLibraryNode>(
        instanceName,
        const_cast<ShaderLibraryNodeType*>(this)
    );
}

// ====== ShaderLibraryNode ======

ShaderLibraryNode::ShaderLibraryNode(
    const std::string& instanceName,
    NodeType* nodeType
)
    : TypedNode<ShaderLibraryNodeConfig>(instanceName, nodeType)
{
    // MVP STUB: No shader library initialized
}

ShaderLibraryNode::~ShaderLibraryNode() {
    Cleanup();
}

void ShaderLibraryNode::Setup() {
    NODE_LOG_DEBUG("Setup: ShaderLibraryNode (MVP stub)");
    
    vulkanDevice = In(ShaderLibraryNodeConfig::VULKAN_DEVICE_IN);
    
    if (!vulkanDevice) {
        std::string errorMsg = "ShaderLibraryNode: VulkanDevice input is null";
        NODE_LOG_ERROR(errorMsg);
        throw std::runtime_error(errorMsg);
    }
    
    NODE_LOG_INFO("Setup: Shader library node ready (MVP stub - no shaders)");
}

void ShaderLibraryNode::Compile() {
    NODE_LOG_INFO("Compile: ShaderLibraryNode (MVP stub - no shader compilation)");
    // MVP: Output nothing - shaders will be loaded directly in application
    
    // Pass through device
    Out(ShaderLibraryNodeConfig::VULKAN_DEVICE_OUT, vulkanDevice);

    // Register cleanup
    NodeInstance::RegisterCleanup();
}

void ShaderLibraryNode::Execute(VkCommandBuffer commandBuffer) {
    // MVP STUB: No-op - shaders loaded directly in application
}

void ShaderLibraryNode::CleanupImpl() {
    // MVP STUB: No resources to clean up
    NODE_LOG_DEBUG("Cleanup: ShaderLibraryNode (MVP stub - no cleanup needed)");
}

} // namespace Vixen::RenderGraph