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
    NODE_LOG_DEBUG("ShaderLibraryNode::Setup: Called - graph-scope initialization");

    // Subscribe to DeviceMetadataEvent for shader version validation
    if (GetMessageBus()) {
        SubscribeToMessage(
            Vixen::EventBus::DeviceMetadataEvent::TYPE,
            [this](const Vixen::EventBus::BaseEventMessage& msg) {
                this->OnDeviceMetadata(msg);
                return true;
            }
        );
        NODE_LOG_INFO("ShaderLibraryNode: Subscribed to DeviceMetadataEvent");
    } else {
        NODE_LOG_WARNING("ShaderLibraryNode: MessageBus not available - cannot subscribe to device metadata");
    }

    RegisterShaderModuleCacher();
    NODE_LOG_DEBUG("ShaderLibraryNode::Setup: Complete");
}

void ShaderLibraryNode::RegisterShaderBuilder(
    std::function<ShaderManagement::ShaderBundleBuilder(int, int)> builderFunc
) {
    shaderBuilderFuncs.push_back(builderFunc);
    NODE_LOG_DEBUG("ShaderLibraryNode::RegisterShaderBuilder: Builder function registered (total: " +
                  std::to_string(shaderBuilderFuncs.size()) + ")");
}

void ShaderLibraryNode::CompileImpl(TypedCompileContext& ctx) {
    NODE_LOG_DEBUG("ShaderLibraryNode::Compile: START - Phase G shader builder");

    // Access VulkanDevice input (compile-time dependency)
    VulkanDevice* devicePtr = ctx.In(ShaderLibraryNodeConfig::VULKAN_DEVICE_IN);
    if (devicePtr == nullptr) {
        throw std::runtime_error("ShaderLibraryNode: VulkanDevice input is null during Compile");
    }

    NODE_LOG_DEBUG("ShaderLibraryNode::Compile: VulkanDevice retrieved: " + std::to_string(reinterpret_cast<uint64_t>(devicePtr)));
    SetDevice(devicePtr);

    InitializeShaderModuleCacher();

    // Determine target shader versions
    int targetVulkan = deviceVulkanVersion;
    int targetSpirv = deviceSpirvVersion;
    if (hasReceivedDeviceMetadata) {
        NODE_LOG_INFO("ShaderLibraryNode: Using device metadata (Vulkan " + std::to_string(targetVulkan) +
                      ", SPIR-V " + std::to_string(targetSpirv) + ")");
    } else {
        NODE_LOG_WARNING("ShaderLibraryNode: Using default versions (Vulkan " + std::to_string(targetVulkan) +
                         ", SPIR-V " + std::to_string(targetSpirv) + ")");
    }

    CompileShaderBundle(targetVulkan, targetSpirv);
    CreateShaderModules();

    // Output device and shader data bundle
    ctx.Out(ShaderLibraryNodeConfig::VULKAN_DEVICE_OUT, device);
    ctx.Out(ShaderLibraryNodeConfig::SHADER_DATA_BUNDLE, shaderBundle_);

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

    const auto& selectedDevice = metadataEvent->GetSelectedDevice();
    deviceVulkanVersion = selectedDevice.GetVulkanVersionShorthand();
    deviceSpirvVersion = selectedDevice.GetSpirvVersionShorthand();
    hasReceivedDeviceMetadata = true;

    NODE_LOG_INFO("ShaderLibraryNode: Device metadata received - " + selectedDevice.deviceName +
                  " (Vulkan " + std::to_string(deviceVulkanVersion) +
                  ", SPIR-V " + std::to_string(deviceSpirvVersion) + ")");
}

void ShaderLibraryNode::RegisterShaderModuleCacher() {
    auto& mainCacher = GetOwningGraph()->GetMainCacher();
    if (mainCacher.IsRegistered(typeid(CashSystem::ShaderModuleWrapper))) {
        NODE_LOG_DEBUG("ShaderLibraryNode: ShaderModuleCacher already registered");
        return;
    }

    NODE_LOG_DEBUG("ShaderLibraryNode: Registering ShaderModuleCacher...");
    mainCacher.RegisterCacher<
        CashSystem::ShaderModuleCacher,
        CashSystem::ShaderModuleWrapper,
        CashSystem::ShaderModuleCreateParams
    >(
        typeid(CashSystem::ShaderModuleWrapper),
        "ShaderModule",
        true  // device-dependent
    );
    NODE_LOG_DEBUG("ShaderLibraryNode: ShaderModuleCacher registered");
}

void ShaderLibraryNode::InitializeShaderModuleCacher() {
    auto& mainCacher = GetOwningGraph()->GetMainCacher();
    NODE_LOG_DEBUG("ShaderLibraryNode: Getting ShaderModuleCacher from MainCacher...");

    shaderModuleCacher = mainCacher.GetCacher<
        CashSystem::ShaderModuleCacher,
        CashSystem::ShaderModuleWrapper,
        CashSystem::ShaderModuleCreateParams
    >(typeid(CashSystem::ShaderModuleWrapper), device);

    if (!shaderModuleCacher) {
        throw std::runtime_error("ShaderLibraryNode: Failed to get ShaderModuleCacher (device=" +
                                 std::to_string(reinterpret_cast<uint64_t>(device)) + ")");
    }

    NODE_LOG_DEBUG("ShaderLibraryNode: ShaderModuleCacher initialized successfully");
}

void ShaderLibraryNode::CompileShaderBundle(int targetVulkan, int targetSpirv) {
    if (shaderBuilderFuncs.empty()) {
        throw std::runtime_error("ShaderLibraryNode: No shader builders registered. Call RegisterShaderBuilder() before compilation.");
    }

    // MVP: Compile only first registered shader program
    NODE_LOG_DEBUG("ShaderLibraryNode: Compiling shader bundle (first of " +
                   std::to_string(shaderBuilderFuncs.size()) + " registered)");

    ShaderManagement::ShaderBundleBuilder builder = shaderBuilderFuncs[0](targetVulkan, targetSpirv);
    auto result = builder.Build();

    if (!result.success) {
        throw std::runtime_error("ShaderLibraryNode: Shader compilation failed: " + result.errorMessage);
    }

    NODE_LOG_INFO("ShaderLibraryNode: Shader bundle built (compile: " +
                  std::to_string(result.compileTime.count()) + "ms, reflect: " +
                  std::to_string(result.reflectTime.count()) + "ms)");

    shaderBundle_ = std::move(result.bundle);
}

void ShaderLibraryNode::CreateShaderModules() {
    NODE_LOG_INFO("ShaderLibraryNode: Creating VkShaderModules via CashSystem");

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
            continue;
        }

        auto shaderWrapper = shaderModuleCacher->GetOrCreateFromSpirv(
            stage.spirvCode,
            stage.entryPoint,
            {},
            vkStage,
            std::string("Draw_") + stageName
        );

        if (stage.stage == ShaderManagement::ShaderStage::Vertex) {
            vertexShader = shaderWrapper;
        } else {
            fragmentShader = shaderWrapper;
        }

        NODE_LOG_INFO("ShaderLibraryNode: " + std::string(stageName) + " shader module created (VkShaderModule: " +
                      std::to_string(reinterpret_cast<uint64_t>(shaderWrapper->shaderModule)) + ")");
    }

    NODE_LOG_INFO("ShaderLibraryNode: All shader modules successfully created");
}

} // namespace Vixen::RenderGraph