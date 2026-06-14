#include "Nodes/InstanceNode.h"
#include "Core/RenderGraph.h"
#include "Core/NodeLogging.h"
#include "CapabilityGraph.h"
#include <iostream>
#include <stdexcept>
#include <algorithm>
#include <cstring>

#define GLFW_INCLUDE_NONE   // don't pull in <GL/gl.h> (absent on headless/WSL); Vulkan-only below
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

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

    // Cross-platform surface extensions: merge in whatever instance extensions GLFW requires to
    // present on the current platform (VK_KHR_surface + the platform surface, e.g. win32/xlib/
    // wayland). This replaces the previously-hardcoded VK_KHR_WIN32_SURFACE entry so the same code
    // produces a valid instance on every OS. Deduplicated against the global list.
    glfwInit();  // idempotent; required before glfwGetRequiredInstanceExtensions
    uint32_t glfwExtCount = 0;
    const char** glfwExts = glfwGetRequiredInstanceExtensions(&glfwExtCount);
    for (uint32_t i = 0; glfwExts && i < glfwExtCount; ++i) {
        const bool alreadyPresent = std::any_of(
            enabledExtensions.begin(), enabledExtensions.end(),
            [&](const char* e) { return std::strcmp(e, glfwExts[i]) == 0; });
        if (!alreadyPresent) {
            enabledExtensions.push_back(glfwExts[i]);
        }
    }

    NODE_LOG_INFO("[InstanceNode] Requested " + std::to_string(enabledExtensions.size()) + " instance extensions");
    NODE_LOG_INFO("[InstanceNode] Requested " + std::to_string(enabledLayers.size()) + " instance layers");

    NODE_LOG_INFO("[InstanceNode] Setup complete");
}

void InstanceNode::CompileImpl(TypedCompileContext& ctx) {
    // Idempotent: the VkInstance is PERSISTENT across a recompile / device-loss rebuild (see CleanupImpl).
    // Only create it the first time; on a rebuild the existing instance is reused so downstream nodes —
    // and the WindowNode surface created from it — stay valid. Always (re)publish the handle.
    if (instance == VK_NULL_HANDLE) {
        NODE_LOG_INFO("[InstanceNode] Compile: Creating Vulkan instance");
        ValidateAndFilterExtensions();
        ValidateAndFilterLayers();
        CreateVulkanInstance();
    } else {
        NODE_LOG_INFO("[InstanceNode] Compile: reusing persistent VkInstance");
    }

    // Output the instance handle (every compile, so re-wiring picks it up)
    ctx.Out(InstanceNodeConfig::INSTANCE, instance);

    NODE_LOG_INFO("[InstanceNode] Instance output set");
}

void InstanceNode::ExecuteImpl(TypedExecuteContext& ctx) {
    // InstanceNode has no per-frame execution logic
    // Instance is created during Compile and remains valid
}

void InstanceNode::CleanupImpl(TypedCleanupContext& ctx) {
    // The VkInstance is instance-level and SURVIVES a device loss (only the device + its children are
    // rebuilt). Keep it across a recompile / device-loss rebuild — exactly like WindowNode keeps the
    // window+surface. The surface is created FROM this instance, so destroying it here while WindowNode
    // keeps the surface would dangle the surface and trip VUID-vkDestroyInstance-instance-00629. Release
    // only on final teardown.
    if (ctx.reason != CleanupReason::FinalTeardown) {
        NODE_LOG_INFO("[InstanceNode] Cleanup (recompile/device-loss): keeping the persistent VkInstance");
        return;
    }
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

    // AR#8: instance-extension availability is no longer pushed to a process-wide static here.
    // Each device's CapabilityGraph self-populates it from the loader (instance availability is
    // globally queryable via vkEnumerateInstanceExtensionProperties), so multiple instances/devices
    // in one process don't clobber shared capability state.
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

    // AR#8: instance-layer availability is no longer pushed to a process-wide static here (see the
    // instance-extension note above) — each device's CapabilityGraph self-populates from the loader.
}

} // namespace Vixen::RenderGraph
