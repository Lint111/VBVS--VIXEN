#include "Nodes/ShaderLibraryNode.h"
#include "Core/RenderGraph.h"
#include "VulkanResources/VulkanDevice.h"
#include "Core/NodeLogging.h"
#include "error/VulkanError.h"
#include "CashSystem/MainCacher.h"
#include "CashSystem/ShaderModuleCacher.h"
#include <ShaderManagement/ShaderBundleBuilder.h>
#include <ShaderManagement/ShaderCompiler.h>
#include "EventBus/Message.h"
#include "EventBus/MessageBus.h"

namespace Vixen::RenderGraph {

// ====== ShaderLibraryNodeType ======

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

void ShaderLibraryNode::SetupImpl(TypedSetupContext& ctx) {
    std::cout << "[ShaderLibraryNode::Setup] Called - graph-scope initialization" << std::endl;

    // Subscribe to DeviceMetadataEvent for shader version validation
    auto* messageBus = GetOwningGraph()->GetMessageBus();
    if (messageBus) {
        messageBus->Subscribe(
            Vixen::EventBus::DeviceMetadataEvent::TYPE,
            [this](const Vixen::EventBus::BaseEventMessage& msg) {
                this->OnDeviceMetadata(msg);
                return true; // Handled
            }
        );
        NODE_LOG_INFO("ShaderLibraryNode: Subscribed to DeviceMetadataEvent");
    } else {
        NODE_LOG_WARNING("ShaderLibraryNode: MessageBus not available - cannot subscribe to device metadata");
    }

    // Register ShaderModuleCacher (idempotent) - graph-scope setup
    auto& mainCacher = GetOwningGraph()->GetMainCacher();
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

    std::cout << "[ShaderLibraryNode::Setup] Complete" << std::endl;
}

void ShaderLibraryNode::RegisterShaderBuilder(
    std::function<ShaderManagement::ShaderBundleBuilder(int, int)> builderFunc
) {
    shaderBuilderFuncs.push_back(builderFunc);
    std::cout << "[ShaderLibraryNode::RegisterShaderBuilder] Builder function registered (total: "
              << shaderBuilderFuncs.size() << ")" << std::endl;
}

void ShaderLibraryNode::CompileImpl(TypedCompileContext& ctx) {
    std::cout << "[ShaderLibraryNode::Compile] START - Phase G shader builder" << std::endl;

    // Access VulkanDevice input (compile-time dependency)
    std::cout << "[ShaderLibraryNode::Compile] Retrieving VulkanDevice input..." << std::endl;
    VulkanDevicePtr devicePtr = In(ShaderLibraryNodeConfig::VULKAN_DEVICE_IN);

    if (devicePtr == nullptr) {
        std::string errorMsg = "ShaderLibraryNode: VulkanDevice input is null during Compile";
        std::cout << "[ShaderLibraryNode] ERROR: " << errorMsg << std::endl;
        throw std::runtime_error(errorMsg);
    }

    std::cout << "[ShaderLibraryNode::Compile] VulkanDevice retrieved: " << devicePtr << std::endl;
    SetDevice(devicePtr);

    // Get ShaderModuleCacher (registered during Setup)
    auto& mainCacher = GetOwningGraph()->GetMainCacher();
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

    // Check if any builder functions were registered
    if (shaderBuilderFuncs.empty()) {
        std::string errorMsg = "ShaderLibraryNode: No shader builders registered. Call RegisterShaderBuilder() before compilation.";
        std::cout << "[ShaderLibraryNode] ERROR: " << errorMsg << std::endl;
        throw std::runtime_error(errorMsg);
    }

    // Use device metadata for shader compilation if available
    int targetVulkan = deviceVulkanVersion;
    int targetSpirv = deviceSpirvVersion;

    if (hasReceivedDeviceMetadata) {
        NODE_LOG_INFO("ShaderLibraryNode: Using device metadata for shader compilation");
        NODE_LOG_INFO("  - Target Vulkan: " + std::to_string(targetVulkan));
        NODE_LOG_INFO("  - Target SPIR-V: " + std::to_string(targetSpirv));
    } else {
        NODE_LOG_WARNING("ShaderLibraryNode: Device metadata not received, using default versions");
        NODE_LOG_WARNING("  - Default Vulkan: " + std::to_string(targetVulkan));
        NODE_LOG_WARNING("  - Default SPIR-V: " + std::to_string(targetSpirv));
    }

    // MVP: Compile only first registered shader program
    // TODO Phase G.1: Support multiple programs with indexed outputs
    std::cout << "[ShaderLibraryNode] Registered builders: " << shaderBuilderFuncs.size()
              << " (compiling first only)" << std::endl;

    std::cout << "[ShaderLibraryNode] Calling shader builder function..." << std::endl;
    ShaderManagement::ShaderBundleBuilder builder = shaderBuilderFuncs[0](targetVulkan, targetSpirv);

    std::cout << "[ShaderLibraryNode] Builder configured, calling Build()..." << std::endl;

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

    // Step 2: Create VkShaderModules using CashSystem caching
    std::cout << "[ShaderLibraryNode] Creating VkShaderModules via CashSystem..." << std::endl;
    NODE_LOG_INFO("ShaderLibraryNode: Creating VkShaderModules via CashSystem");

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

    // Output device and shader data bundle
    Out(ShaderLibraryNodeConfig::VULKAN_DEVICE_OUT, device);
    Out(ShaderLibraryNodeConfig::SHADER_DATA_BUNDLE, shaderBundle_);

    NODE_LOG_INFO("ShaderLibraryNode: All outputs set - ready for downstream nodes");
}

void ShaderLibraryNode::ExecuteImpl(TypedExecuteContext& ctx) {
    // MVP STUB: No-op - shaders loaded directly in application
}

void ShaderLibraryNode::CleanupImpl(TypedCleanupContext& ctx) {
    NODE_LOG_DEBUG("Cleanup: ShaderLibraryNode - releasing resources");

    // Release shared_ptrs (cacher owns the VkShaderModule handles and will destroy them)
    NODE_LOG_DEBUG("ShaderLibraryNode: Releasing shader module references (cacher owns resources)");
    vertexShader.reset();
    fragmentShader.reset();

    NODE_LOG_DEBUG("Cleanup: ShaderLibraryNode complete");
}

void ShaderLibraryNode::OnDeviceMetadata(const Vixen::EventBus::BaseEventMessage& message) {
    auto* metadataEvent = static_cast<const Vixen::EventBus::DeviceMetadataEvent*>(&message);

    if (!metadataEvent) {
        NODE_LOG_WARNING("ShaderLibraryNode: Received invalid DeviceMetadataEvent");
        return;
    }

    // Get selected device info
    const auto& selectedDevice = metadataEvent->GetSelectedDevice();

    // Store device capabilities
    deviceVulkanVersion = selectedDevice.GetVulkanVersionShorthand();
    deviceSpirvVersion = selectedDevice.GetSpirvVersionShorthand();
    hasReceivedDeviceMetadata = true;

    NODE_LOG_INFO("ShaderLibraryNode: Received device metadata from EventBus");
    NODE_LOG_INFO("  - Total devices detected: " + std::to_string(metadataEvent->availableDevices.size()));
    NODE_LOG_INFO("  - Selected device index: " + std::to_string(metadataEvent->selectedDeviceIndex));
    NODE_LOG_INFO("  - Selected device: " + selectedDevice.deviceName);
    NODE_LOG_INFO("  - Vulkan API version: " + std::to_string(deviceVulkanVersion) + " (shorthand)");
    NODE_LOG_INFO("  - Max SPIR-V version: " + std::to_string(deviceSpirvVersion) + " (shorthand)");
    NODE_LOG_INFO("  - Discrete GPU: " + std::string(selectedDevice.isDiscreteGPU ? "Yes" : "No"));
    NODE_LOG_INFO("  - Dedicated memory: " + std::to_string(selectedDevice.dedicatedMemoryMB) + " MB");

    // Log all available devices
    NODE_LOG_INFO("ShaderLibraryNode: Available devices:");
    for (size_t i = 0; i < metadataEvent->availableDevices.size(); ++i) {
        const auto& dev = metadataEvent->availableDevices[i];
        NODE_LOG_INFO("  [" + std::to_string(i) + "] " + dev.deviceName +
                      " (Vulkan " + std::to_string(dev.GetVulkanVersionShorthand()) +
                      ", SPIR-V " + std::to_string(dev.GetSpirvVersionShorthand()) + ")");
    }
}

} // namespace Vixen::RenderGraph