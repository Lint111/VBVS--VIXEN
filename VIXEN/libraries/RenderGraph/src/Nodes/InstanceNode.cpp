#include "Nodes/InstanceNode.h"
#include "Core/RenderGraph.h"
#include "Core/NodeLogging.h"
#include <iostream>
#include <stdexcept>

// Include central header that provides inline/selectany globals so this TU
// does not require or create a strong definition. Use the public include
// provided by the `VulkanResources` target so consumers can `#include
// <VulkanGlobalNames.h>` directly.
#include <VulkanGlobalNames.h>

// NOTE: globals `instanceExtensionNames` and `layerNames` are provided
// as inline/selectany variables by `VulkanGlobalNames.h`.

namespace Vixen::RenderGraph {

// ============================================================================
// InstanceNodeType
// ============================================================================

std::unique_ptr<NodeInstance> InstanceNodeType::CreateInstance(
    const std::string& instanceName
) const {
    return std::make_unique<InstanceNode>(instanceName, const_cast<InstanceNodeType*>(this));
}

// ============================================================================
// InstanceNode
// ============================================================================

InstanceNode::InstanceNode(
    const std::string& instanceName,
    NodeType* nodeType
)
    : TypedNode<InstanceNodeConfig>(instanceName, nodeType)
{
}

InstanceNode::~InstanceNode() {
    DestroyVulkanInstance();
}

void InstanceNode::SetupImpl(TypedSetupContext& ctx) {
    NODE_LOG_INFO("[InstanceNode] Setup: Preparing instance creation");

    // Get parameters
    validationEnabled = GetParameterValue<bool>(
        InstanceNodeConfig::PARAM_ENABLE_VALIDATION,
        true  // Default: enable validation in debug builds
    );

    appName = GetParameterValue<std::string>(
        InstanceNodeConfig::PARAM_APP_NAME,
        "VIXEN Application"
    );

    engineName = GetParameterValue<std::string>(
        InstanceNodeConfig::PARAM_ENGINE_NAME,
        "VIXEN Engine"
    );

    // Use global extension/layer lists from main.cpp
    enabledExtensions = instanceExtensionNames;
    enabledLayers = layerNames;

    NODE_LOG_INFO("[InstanceNode] Requested " + std::to_string(enabledExtensions.size()) + " instance extensions");
    NODE_LOG_INFO("[InstanceNode] Requested " + std::to_string(enabledLayers.size()) + " instance layers");

    NODE_LOG_INFO("[InstanceNode] Setup complete");
}

void InstanceNode::CompileImpl(TypedCompileContext& ctx) {
    NODE_LOG_INFO("[InstanceNode] Compile: Creating Vulkan instance");

    // Destroy existing instance if recompiling
    if (instance != VK_NULL_HANDLE) {
        NODE_LOG_INFO("[InstanceNode] Destroying existing instance for recompilation");
        DestroyVulkanInstance();
    }

    // Create Vulkan instance
    CreateVulkanInstance();

    // Output the instance handle
    ctx.Out(InstanceNodeConfig::INSTANCE, instance);

    NODE_LOG_INFO("[InstanceNode] Instance created and output set");
}

void InstanceNode::ExecuteImpl(TypedExecuteContext& ctx) {
    // InstanceNode has no per-frame execution logic
    // Instance is created during Compile and remains valid
}

void InstanceNode::CleanupImpl() {
    NODE_LOG_INFO("[InstanceNode] Cleanup: Destroying Vulkan instance");
    DestroyVulkanInstance();
}

// ============================================================================
// Private Helper Methods
// ============================================================================

void InstanceNode::CreateVulkanInstance() {
    // Define Vulkan application info
    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pNext = nullptr;
    appInfo.pApplicationName = appName.c_str();
    appInfo.applicationVersion = 1;
    appInfo.pEngineName = engineName.c_str();
    appInfo.engineVersion = 1;
    appInfo.apiVersion = VK_API_VERSION_1_3;  // Request Vulkan 1.3

    // Define Vulkan instance create info
    VkInstanceCreateInfo instInfo{};
    instInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instInfo.pNext = nullptr;
    instInfo.flags = 0;
    instInfo.pApplicationInfo = &appInfo;

    // Specify enabled layers
    instInfo.enabledLayerCount = static_cast<uint32_t>(enabledLayers.size());
    instInfo.ppEnabledLayerNames = enabledLayers.data();

    // Specify enabled extensions
    instInfo.enabledExtensionCount = static_cast<uint32_t>(enabledExtensions.size());
    instInfo.ppEnabledExtensionNames = enabledExtensions.data();

    // Create instance
    VkResult result = vkCreateInstance(&instInfo, nullptr, &instance);
    if (result != VK_SUCCESS) {
        NODE_LOG_ERROR("[InstanceNode] Failed to create Vulkan instance: " + std::to_string(result));
        throw std::runtime_error("Failed to create Vulkan instance");
    }

    NODE_LOG_INFO("[InstanceNode] Vulkan instance created successfully");
    NODE_LOG_INFO("[InstanceNode] VkInstance handle: " + std::to_string(reinterpret_cast<uint64_t>(instance)));
}

void InstanceNode::DestroyVulkanInstance() {
    if (instance != VK_NULL_HANDLE) {
        NODE_LOG_INFO("[InstanceNode] Destroying Vulkan instance");
        vkDestroyInstance(instance, nullptr);
        instance = VK_NULL_HANDLE;
    }
}

void InstanceNode::EnumerateAvailableLayers() {
    // TODO: Implement layer enumeration for validation
}

void InstanceNode::EnumerateAvailableExtensions() {
    // TODO: Implement extension enumeration for validation
}

} // namespace Vixen::RenderGraph
