#include "Nodes/ShaderLibraryNode.h"
#include "Core/RenderGraph.h"
#include "VulkanResources/VulkanDevice.h"
#include "Core/NodeLogging.h"
#include "error/VulkanError.h"
#include "CashSystem/MainCacher.h"
#include "CashSystem/ShaderModuleCacher.h"
#include <ShaderManagement/ShaderBundleBuilder.h>

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
    NODE_LOG_INFO("Compile: ShaderLibraryNode - Phase 1 ShaderManagement integration");

    // Step 1: Build shader bundle from GLSL source using ShaderManagement
    NODE_LOG_INFO("ShaderLibraryNode: Building shader bundle from GLSL source");

    ShaderManagement::ShaderBundleBuilder builder;
    builder.SetProgramName("Draw_Shader")
           .SetUuid("Draw_Shader_UUID")
           .AddStageFromFile(
               ShaderManagement::ShaderStage::Vertex,
               "Shaders/Draw.vert",
               "main"
           )
           .AddStageFromFile(
               ShaderManagement::ShaderStage::Fragment,
               "Shaders/Draw.frag",
               "main"
           );

    auto result = builder.Build();
    if (!result.success) {
        std::string errorMsg = "ShaderLibraryNode: Shader compilation failed: " + result.errorMessage;
        NODE_LOG_ERROR(errorMsg);
        throw std::runtime_error(errorMsg);
    }

    NODE_LOG_INFO("ShaderLibraryNode: Shader bundle built successfully");
    NODE_LOG_INFO("  - Compile time: " + std::to_string(result.compileTime.count()) + "ms");
    NODE_LOG_INFO("  - Reflect time: " + std::to_string(result.reflectTime.count()) + "ms");

    // Store bundle for future descriptor automation (Phase 2)
    shaderBundle_ = std::move(result.bundle);

    // Step 2: Create VkShaderModules using CashSystem caching
    NODE_LOG_INFO("ShaderLibraryNode: Creating VkShaderModules via CashSystem");

    // Get MainCacher from owning graph
    auto& mainCacher = GetOwningGraph()->GetMainCacher();

    // Register ShaderModuleCacher (idempotent)
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

    // Get cacher reference
    shaderModuleCacher = mainCacher.GetCacher<
        CashSystem::ShaderModuleCacher,
        CashSystem::ShaderModuleWrapper,
        CashSystem::ShaderModuleCreateParams
    >(typeid(CashSystem::ShaderModuleWrapper), device);

    if (!shaderModuleCacher) {
        std::string errorMsg = "ShaderLibraryNode: Failed to get ShaderModuleCacher";
        NODE_LOG_ERROR(errorMsg);
        throw std::runtime_error(errorMsg);
    }

    // Step 3: Create shader modules from ShaderDataBundle SPIR-V
    try {
        for (const auto& stage : shaderBundle_->program.stages) {
            VkShaderStageFlagBits vkStage;
            const char* stageName;

            if (stage.stage == ShaderManagement::ShaderStage::Vertex) {
                vkStage = VK_SHADER_STAGE_VERTEX_BIT;
                stageName = "Vertex";
            } else if (stage.stage == ShaderManagement::ShaderStage::Fragment) {
                vkStage = VK_SHADER_STAGE_FRAGMENT_BIT;
                stageName = "Fragment";
            } else {
                continue; // Skip other stages for now
            }

            // Use cacher to create/retrieve shader module from SPIR-V
            auto shaderWrapper = shaderModuleCacher->GetOrCreateFromSpirv(
                stage.spirvCode,
                stage.entryPoint,
                {},  // no macros
                vkStage,
                std::string("Draw_") + stageName
            );

            if (stage.stage == ShaderManagement::ShaderStage::Vertex) {
                vertexShader = shaderWrapper;
                NODE_LOG_INFO("ShaderLibraryNode: Vertex shader module created (VkShaderModule: " +
                             std::to_string(reinterpret_cast<uint64_t>(vertexShader->shaderModule)) + ")");
            } else {
                fragmentShader = shaderWrapper;
                NODE_LOG_INFO("ShaderLibraryNode: Fragment shader module created (VkShaderModule: " +
                             std::to_string(reinterpret_cast<uint64_t>(fragmentShader->shaderModule)) + ")");
            }
        }

        NODE_LOG_INFO("ShaderLibraryNode: All shader modules successfully created via CashSystem");
    } catch (const std::exception& e) {
        NODE_LOG_ERROR("ShaderLibraryNode: Failed to create shader modules: " + std::string(e.what()));
        throw;
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