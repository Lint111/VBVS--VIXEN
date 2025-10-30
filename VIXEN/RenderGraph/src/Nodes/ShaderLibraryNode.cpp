#include "Nodes/ShaderLibraryNode.h"
#include "Core/RenderGraph.h"
#include "VulkanResources/VulkanDevice.h"
#include "Core/NodeLogging.h"
#include "error/VulkanError.h"
#include "CashSystem/MainCacher.h"
#include "CashSystem/ShaderModuleCacher.h"

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

    VulkanDevicePtr devicePtr = In(ShaderLibraryNodeConfig::VULKAN_DEVICE_IN);

    if (devicePtr == nullptr) {
        std::string errorMsg = "ShaderLibraryNode: VulkanDevice input is null";
        NODE_LOG_ERROR(errorMsg);
        throw std::runtime_error(errorMsg);
    }

    // Set base class device member for cleanup tracking
    SetDevice(devicePtr);

    NODE_LOG_INFO("Setup: Shader library node ready (MVP stub - no shaders)");
}

void ShaderLibraryNode::Compile() {
    NODE_LOG_INFO("Compile: ShaderLibraryNode - initializing shader module cache");

    // Get MainCacher from owning graph
    auto& mainCacher = GetOwningGraph()->GetMainCacher();

    // Register ShaderModuleCacher (idempotent - safe to call multiple times during recompile)
    if (!mainCacher.IsRegistered(typeid(CashSystem::ShaderModuleWrapper))) {
        mainCacher.RegisterCacher<
            CashSystem::ShaderModuleCacher,
            CashSystem::ShaderModuleWrapper,
            CashSystem::ShaderModuleCreateParams
        >(
            typeid(CashSystem::ShaderModuleWrapper),
            "ShaderModule",
            true  // device-dependent
        );
        NODE_LOG_DEBUG("ShaderLibraryNode: Registered ShaderModuleCacher");
    }

    // Cache the cacher reference for use throughout node lifetime
    shaderModuleCacher = mainCacher.GetCacher<
        CashSystem::ShaderModuleCacher,
        CashSystem::ShaderModuleWrapper,
        CashSystem::ShaderModuleCreateParams
    >(typeid(CashSystem::ShaderModuleWrapper), device);

    if (shaderModuleCacher) {
        NODE_LOG_INFO("ShaderLibraryNode: Shader module cache ready - loading shaders");

        try {
            // Load vertex shader using cacher (GetOrCreate checks cache first)
            vertexShader = shaderModuleCacher->GetOrCreateShaderModule(
                "../BuiltAssets/CompiledShaders/Draw.vert.spv",
                "main",
                {},  // no macros
                VK_SHADER_STAGE_VERTEX_BIT,
                "Draw_Vertex"
            );

            NODE_LOG_INFO("ShaderLibraryNode: Vertex shader loaded (VkShaderModule: " +
                         std::to_string(reinterpret_cast<uint64_t>(vertexShader->shaderModule)) + ")");

            // Load fragment shader using cacher
            fragmentShader = shaderModuleCacher->GetOrCreateShaderModule(
                "../BuiltAssets/CompiledShaders/Draw.frag.spv",
                "main",
                {},  // no macros
                VK_SHADER_STAGE_FRAGMENT_BIT,
                "Draw_Fragment"
            );

            NODE_LOG_INFO("ShaderLibraryNode: Fragment shader loaded (VkShaderModule: " +
                         std::to_string(reinterpret_cast<uint64_t>(fragmentShader->shaderModule)) + ")");

            NODE_LOG_INFO("ShaderLibraryNode: Both shaders successfully loaded via cacher");
        } catch (const std::exception& e) {
            NODE_LOG_ERROR("ShaderLibraryNode: Failed to load shaders: " + std::string(e.what()));
            throw;
        }
    } else {
        NODE_LOG_WARNING("ShaderLibraryNode: Shader module cache not available");
    }

    // Output device (matches original behavior)
    Out(ShaderLibraryNodeConfig::VULKAN_DEVICE_OUT, device);

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