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
    std::cout << "[ShaderLibraryNode::Setup] Called" << std::endl;

    VulkanDevicePtr devicePtr = In(ShaderLibraryNodeConfig::VULKAN_DEVICE_IN);

    if (devicePtr == nullptr) {
        std::string errorMsg = "ShaderLibraryNode: VulkanDevice input is null";
        std::cout << "[ShaderLibraryNode::Setup] ERROR: " << errorMsg << std::endl;
        throw std::runtime_error(errorMsg);
    }

    // Set base class device member for cleanup tracking
    SetDevice(devicePtr);

    std::cout << "[ShaderLibraryNode::Setup] Complete" << std::endl;
}

void ShaderLibraryNode::Compile() {
    std::cout << "[ShaderLibraryNode::Compile] START - Phase 1 integration" << std::endl;

    // Step 1: Build shader bundle from GLSL source using ShaderManagement
    // Check current working directory
    auto cwd = std::filesystem::current_path();
    std::cout << "[ShaderLibraryNode] Current working directory: " << cwd << std::endl;

    // Try multiple possible paths
    std::vector<std::filesystem::path> possibleVertPaths = {
        "Draw.vert",
        "Shaders/Draw.vert",
        "../Shaders/Draw.vert",
        "binaries/Draw.vert"
    };

    std::filesystem::path vertPath;
    std::filesystem::path fragPath;

    for (const auto& path : possibleVertPaths) {
        if (std::filesystem::exists(path)) {
            vertPath = path;
            // Assume frag is in same directory
            auto dir = path.parent_path();
            fragPath = dir / "Draw.frag";
            std::cout << "[ShaderLibraryNode] Found shaders at: " << dir << std::endl;
            break;
        }
    }

    std::cout << "[ShaderLibraryNode] Checking for shader files..." << std::endl;
    std::cout << "[ShaderLibraryNode] Vertex path: " << vertPath << " exists=" << std::filesystem::exists(vertPath) << std::endl;
    std::cout << "[ShaderLibraryNode] Fragment path: " << fragPath << " exists=" << std::filesystem::exists(fragPath) << std::endl;

    if (!std::filesystem::exists(vertPath)) {
        std::string errorMsg = "ShaderLibraryNode: Vertex shader not found: " + vertPath.string();
        std::cout << "[ShaderLibraryNode] ERROR: " << errorMsg << std::endl;
        throw std::runtime_error(errorMsg);
    }
    if (!std::filesystem::exists(fragPath)) {
        std::string errorMsg = "ShaderLibraryNode: Fragment shader not found: " + fragPath.string();
        std::cout << "[ShaderLibraryNode] ERROR: " << errorMsg << std::endl;
        throw std::runtime_error(errorMsg);
    }

    std::cout << "[ShaderLibraryNode] Creating ShaderBundleBuilder..." << std::endl;

    ShaderManagement::ShaderBundleBuilder builder;
    builder.SetProgramName("Draw_Shader")
           .SetUuid("Draw_Shader_UUID")
           .AddStageFromFile(
               ShaderManagement::ShaderStage::Vertex,
               vertPath,
               "main"
           )
           .AddStageFromFile(
               ShaderManagement::ShaderStage::Fragment,
               fragPath,
               "main"
           );

    std::cout << "[ShaderLibraryNode] Stages added, calling Build()..." << std::endl;

    auto result = builder.Build();
    if (!result.success) {
        std::string errorMsg = "ShaderLibraryNode: Shader compilation failed: " + result.errorMessage;
        std::cout << "[ShaderLibraryNode] ERROR: " << errorMsg << std::endl;
        throw std::runtime_error(errorMsg);
    }

    std::cout << "[ShaderLibraryNode] Shader bundle built successfully" << std::endl;
    NODE_LOG_INFO("ShaderLibraryNode: Shader bundle built successfully");
    NODE_LOG_INFO("  - Compile time: " + std::to_string(result.compileTime.count()) + "ms");
    NODE_LOG_INFO("  - Reflect time: " + std::to_string(result.reflectTime.count()) + "ms");

    // Store bundle for future descriptor automation (Phase 2)
    shaderBundle_ = std::move(result.bundle);

    // Use device from base class (set in Setup via SetDevice)
    if (!device) {
        std::string errorMsg = "ShaderLibraryNode: device member is null during Compile";
        std::cout << "[ShaderLibraryNode] ERROR: " << errorMsg << std::endl;
        NODE_LOG_ERROR(errorMsg);
        throw std::runtime_error(errorMsg);
    }
    std::cout << "[ShaderLibraryNode] Using device: " << device << std::endl;

    // Step 2: Create VkShaderModules using CashSystem caching
    std::cout << "[ShaderLibraryNode] Creating VkShaderModules via CashSystem..." << std::endl;
    NODE_LOG_INFO("ShaderLibraryNode: Creating VkShaderModules via CashSystem");

    // Get MainCacher from owning graph
    auto& mainCacher = GetOwningGraph()->GetMainCacher();

    // Register ShaderModuleCacher (idempotent)
    std::cout << "[ShaderLibraryNode] Checking if ShaderModuleCacher is registered..." << std::endl;
    bool wasRegistered = mainCacher.IsRegistered(typeid(CashSystem::ShaderModuleWrapper));
    std::cout << "[ShaderLibraryNode] Is registered: " << wasRegistered << std::endl;

    if (!wasRegistered) {
        std::cout << "[ShaderLibraryNode] Registering ShaderModuleCacher..." << std::endl;
        mainCacher.RegisterCacher<
            CashSystem::ShaderModuleCacher,
            CashSystem::ShaderModuleWrapper,
            CashSystem::ShaderModuleCreateParams
        >(
            typeid(CashSystem::ShaderModuleWrapper),
            "ShaderModule",
            true  // device-dependent
        );
        std::cout << "[ShaderLibraryNode] ShaderModuleCacher registered" << std::endl;
        NODE_LOG_DEBUG("ShaderLibraryNode: Registered ShaderModuleCacher");
    } else {
        std::cout << "[ShaderLibraryNode] ShaderModuleCacher already registered" << std::endl;
    }

    // Get cacher reference (using base class device member)
    std::cout << "[ShaderLibraryNode] Getting ShaderModuleCacher from MainCacher..." << std::endl;
    std::cout << "[ShaderLibraryNode] Device pointer value: " << device << std::endl;
    std::cout << "[ShaderLibraryNode] IsDeviceDependent: " << mainCacher.IsDeviceDependent(typeid(CashSystem::ShaderModuleWrapper)) << std::endl;

    shaderModuleCacher = mainCacher.GetCacher<
        CashSystem::ShaderModuleCacher,
        CashSystem::ShaderModuleWrapper,
        CashSystem::ShaderModuleCreateParams
    >(typeid(CashSystem::ShaderModuleWrapper), device);

    if (!shaderModuleCacher) {
        std::string errorMsg = "ShaderLibraryNode: Failed to get ShaderModuleCacher";
        std::cout << "[ShaderLibraryNode] ERROR: " << errorMsg << std::endl;
        std::cout << "[ShaderLibraryNode] ERROR: device=" << device << std::endl;
        NODE_LOG_ERROR(errorMsg);
        throw std::runtime_error(errorMsg);
    }
    std::cout << "[ShaderLibraryNode] Got ShaderModuleCacher successfully" << std::endl;
    std::cout << "[ShaderLibraryNode] ShaderModuleCacher->IsInitialized()=" << shaderModuleCacher->IsInitialized() << std::endl;
    std::cout << "[ShaderLibraryNode] ShaderModuleCacher->GetDevice()=" << shaderModuleCacher->GetDevice() << std::endl;

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

    // Step 4: Create VulkanShader wrapper for compatibility with GraphicsPipelineNode (Phase 1)
    NODE_LOG_INFO("ShaderLibraryNode: Creating VulkanShader compatibility wrapper");

    vulkanShader = new VulkanShader();
    vulkanShader->creationDevice = device;
    vulkanShader->stagesCount = 2;

    // Populate vertex shader stage
    vulkanShader->shaderStages[0] = {};
    vulkanShader->shaderStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vulkanShader->shaderStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    vulkanShader->shaderStages[0].module = vertexShader->shaderModule;
    vulkanShader->shaderStages[0].pName = "main";

    // Populate fragment shader stage
    vulkanShader->shaderStages[1] = {};
    vulkanShader->shaderStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vulkanShader->shaderStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    vulkanShader->shaderStages[1].module = fragmentShader->shaderModule;
    vulkanShader->shaderStages[1].pName = "main";

    vulkanShader->initialized = true;

    NODE_LOG_INFO("ShaderLibraryNode: VulkanShader wrapper created successfully");

    // Output device (matches original behavior)
    Out(ShaderLibraryNodeConfig::VULKAN_DEVICE_OUT, device);

    // Output shader modules for downstream nodes (Phase 1)
    Out(ShaderLibraryNodeConfig::VERTEX_MODULE, vertexShader->shaderModule);
    Out(ShaderLibraryNodeConfig::FRAGMENT_MODULE, fragmentShader->shaderModule);

    // Output VulkanShader wrapper for GraphicsPipelineNode (Phase 1)
    Out(ShaderLibraryNodeConfig::VULKAN_SHADER, vulkanShader);

    NODE_LOG_INFO("ShaderLibraryNode: All outputs set - ready for pipeline node");

    // Register cleanup
    NodeInstance::RegisterCleanup();
}

void ShaderLibraryNode::Execute(VkCommandBuffer commandBuffer) {
    // MVP STUB: No-op - shaders loaded directly in application
}

void ShaderLibraryNode::CleanupImpl() {
    NODE_LOG_DEBUG("Cleanup: ShaderLibraryNode - releasing resources");

    // Cleanup VulkanShader wrapper (don't destroy modules - they're in cacher)
    if (vulkanShader) {
        // Note: Don't call DestroyShader() - modules are managed by CashSystem cacher
        delete vulkanShader;
        vulkanShader = nullptr;
        NODE_LOG_DEBUG("ShaderLibraryNode: VulkanShader wrapper destroyed");
    }

    // Release shared_ptrs (cacher owns the VkShaderModule handles and will destroy them)
    std::cout << "[ShaderLibraryNode::CleanupImpl] Releasing shader module references (cacher owns resources)" << std::endl;
    vertexShader.reset();
    fragmentShader.reset();

    NODE_LOG_DEBUG("Cleanup: ShaderLibraryNode complete");
}

} // namespace Vixen::RenderGraph