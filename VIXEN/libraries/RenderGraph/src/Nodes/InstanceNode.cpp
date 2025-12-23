#include "Nodes/InstanceNode.h"
#include "Core/RenderGraph.h"
#include "Core/NodeLogging.h"
#include "CapabilityGraph.h"
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

    // Validate and filter extensions/layers before creating instance
    ValidateAndFilterExtensions();
    ValidateAndFilterLayers();

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

void InstanceNode::ValidateAndFilterExtensions() {
    // Enumerate available instance extensions
    uint32_t extensionCount = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, nullptr);

    std::vector<VkExtensionProperties> availableExtensions(extensionCount);
    vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, availableExtensions.data());

    NODE_LOG_INFO("[InstanceNode] Found " + std::to_string(extensionCount) + " available instance extensions");

    // Filter requested extensions - only enable those that are available
    std::vector<const char*> validatedExtensions;
    validatedExtensions.reserve(enabledExtensions.size());

    for (const char* requestedExt : enabledExtensions) {
        bool found = false;
        for (const auto& availableExt : availableExtensions) {
            if (strcmp(requestedExt, availableExt.extensionName) == 0) {
                found = true;
                break;
            }
        }

        if (found) {
            validatedExtensions.push_back(requestedExt);
            NODE_LOG_DEBUG("[InstanceNode]   ✓ " + std::string(requestedExt) + " (available)");
        } else {
            NODE_LOG_WARNING("[InstanceNode]   ✗ " + std::string(requestedExt) + " (NOT AVAILABLE - skipping)");
        }
    }

    // Replace enabled extensions with validated list
    enabledExtensions = validatedExtensions;
    NODE_LOG_INFO("[InstanceNode] Enabled " + std::to_string(enabledExtensions.size()) + " instance extensions");

    // Populate capability graph with available instance extensions
    std::vector<std::string> availableExtStrings;
    availableExtStrings.reserve(availableExtensions.size());
    for (const auto& ext : availableExtensions) {
        availableExtStrings.emplace_back(ext.extensionName);
    }
    Vixen::InstanceExtensionCapability::SetAvailableExtensions(availableExtStrings);
}

void InstanceNode::ValidateAndFilterLayers() {
    // Enumerate available instance layers
    uint32_t layerCount = 0;
    vkEnumerateInstanceLayerProperties(&layerCount, nullptr);

    std::vector<VkLayerProperties> availableLayers(layerCount);
    vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

    NODE_LOG_INFO("[InstanceNode] Found " + std::to_string(layerCount) + " available instance layers");

    // Filter requested layers - only enable those that are available
    std::vector<const char*> validatedLayers;
    validatedLayers.reserve(enabledLayers.size());

    for (const char* requestedLayer : enabledLayers) {
        bool found = false;
        for (const auto& availableLayer : availableLayers) {
            if (strcmp(requestedLayer, availableLayer.layerName) == 0) {
                found = true;
                break;
            }
        }

        if (found) {
            validatedLayers.push_back(requestedLayer);
            NODE_LOG_DEBUG("[InstanceNode]   ✓ " + std::string(requestedLayer) + " (available)");
        } else {
            NODE_LOG_WARNING("[InstanceNode]   ✗ " + std::string(requestedLayer) + " (NOT AVAILABLE - skipping)");
        }
    }

    // Replace enabled layers with validated list
    enabledLayers = validatedLayers;
    NODE_LOG_INFO("[InstanceNode] Enabled " + std::to_string(enabledLayers.size()) + " instance layers");

    // Populate capability graph with available instance layers
    std::vector<std::string> availableLayerStrings;
    availableLayerStrings.reserve(availableLayers.size());
    for (const auto& layer : availableLayers) {
        availableLayerStrings.emplace_back(layer.layerName);
    }
    Vixen::InstanceLayerCapability::SetAvailableLayers(availableLayerStrings);
}

} // namespace Vixen::RenderGraph
