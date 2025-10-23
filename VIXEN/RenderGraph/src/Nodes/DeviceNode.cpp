#include "Nodes/DeviceNode.h"
#include <iostream>
#include "Core/NodeLogging.h"

extern VkInstance g_VulkanInstance;
extern std::vector<const char*> deviceExtensionNames;
extern std::vector<const char*> layerNames;

namespace Vixen::RenderGraph {

using namespace Vixen::Vulkan::Resources;

// ============================================================================
// DeviceNodeType
// ============================================================================

DeviceNodeType::DeviceNodeType()
    : NodeType("Device")
{
    pipelineType = PipelineType::Graphics;
    requiredCapabilities = DeviceCapability::Graphics;
    supportsInstancing = false;
    maxInstances = 1;

    workloadMetrics.estimatedMemoryFootprint = 1024;
    workloadMetrics.estimatedComputeCost = 0.0f;
    workloadMetrics.estimatedBandwidthCost = 0.0f;
    workloadMetrics.canRunInParallel = false;

    // Populate schema from config
    DeviceNodeConfig config;
    inputSchema = config.GetInputVector();
    outputSchema = config.GetOutputVector();
}

std::unique_ptr<NodeInstance> DeviceNodeType::CreateInstance(
    const std::string& instanceName
) const {
    return std::make_unique<DeviceNode>(instanceName, const_cast<DeviceNodeType*>(this), nullptr);
}

// ============================================================================
// DeviceNode
// ============================================================================

DeviceNode::DeviceNode(
    const std::string& instanceName,
    NodeType* nodeType,
    VulkanDevice* device
)
    : TypedNode<DeviceNodeConfig>(instanceName, nodeType)
{
}

DeviceNode::~DeviceNode() {
    Cleanup();
}

void DeviceNode::Setup() {
    NODE_LOG_INFO("[DeviceNode] Setup: Preparing device creation");

    // Get VkInstance from global (Phase 1 temporary solution)
    instance = g_VulkanInstance;

    if (instance == VK_NULL_HANDLE) {
        NODE_LOG_ERROR("[DeviceNode] ERROR: VkInstance is null!");
        return;
    }

    // Use global extension/layer lists
    deviceExtensions = deviceExtensionNames;
    deviceLayers = layerNames;

    NODE_LOG_INFO("[DeviceNode] Requested " + std::to_string(deviceExtensions.size()) + " device extensions");
    NODE_LOG_INFO("[DeviceNode] Requested " + std::to_string(deviceLayers.size()) + " device layers");

    NODE_LOG_INFO("[DeviceNode] Setup complete");
}

void DeviceNode::Compile() {
    NODE_LOG_INFO("[DeviceNode] Compile: Creating Vulkan device");

    // Get gpu_index parameter (default to 0 if not set)
    selectedGPUIndex = GetParameterValue<uint32_t>(
        DeviceNodeConfig::PARAM_GPU_INDEX,
        0  // Default: first GPU
    );

    // Enumerate all available physical devices
    EnumeratePhysicalDevices();

    // Select the physical device
    SelectPhysicalDevice();

    // Create logical device using VulkanDevice wrapper
    CreateLogicalDevice();

    // Store VulkanDevice* in output slot (type-punned as VkDevice)
    // Downstream nodes will cast this back to VulkanDevice*
    Out(DeviceNodeConfig::DEVICE) = reinterpret_cast<VkDevice>(vulkanDevice.get());

    NODE_LOG_INFO("[DeviceNode] Compile complete");
}

void DeviceNode::Execute(VkCommandBuffer commandBuffer) {
    // DeviceNode doesn't record commands - it just provides the device
}

void DeviceNode::Cleanup() {
    NODE_LOG_INFO("[DeviceNode] Cleanup");

    // VulkanDevice destructor handles device destruction
    vulkanDevice.reset();

    availableGPUs.clear();
    instance = VK_NULL_HANDLE;
}

// ============================================================================
// Helper Methods
// ============================================================================

void DeviceNode::EnumeratePhysicalDevices() {
    uint32_t deviceCount = 0;

    // Get device count
    VkResult result = vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);
    if (result != VK_SUCCESS) {
        // log error

        NODE_LOG_ERROR("[DeviceNode] ERROR: Failed to enumerate physical device count: " + std::to_string(result));
        return;
    }

    if (deviceCount == 0) {
        NODE_LOG_ERROR("[DeviceNode] ERROR: No Vulkan-capable GPUs found!");
        return;
    }

    NODE_LOG_INFO("[DeviceNode] Found " + std::to_string(deviceCount) + " physical device(s)");

    // Get all devices
    availableGPUs.resize(deviceCount);
    result = vkEnumeratePhysicalDevices(instance, &deviceCount, availableGPUs.data());
    if (result != VK_SUCCESS) {
        NODE_LOG_ERROR("[DeviceNode] ERROR: Failed to enumerate physical devices: " + std::to_string(result));
        availableGPUs.clear();
        return;
    }

    // Log all available GPUs
    for (uint32_t i = 0; i < deviceCount; i++) {
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(availableGPUs[i], &props);
        NODE_LOG_INFO("[DeviceNode]   GPU " + std::to_string(i) + ": " + props.deviceName);
    }
}

void DeviceNode::SelectPhysicalDevice() {
    // Validate selected index
    if (selectedGPUIndex >= availableGPUs.size()) {
        NODE_LOG_WARNING("[DeviceNode] WARNING: Requested GPU index " + std::to_string(selectedGPUIndex)
                    + " but only " + std::to_string(availableGPUs.size()) + " GPUs available. Using GPU 0.");
        selectedGPUIndex = 0;
    }

    VkPhysicalDevice selectedGPU = availableGPUs[selectedGPUIndex];

    // Log selected device
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(selectedGPU, &props);
    NODE_LOG_INFO("[DeviceNode] Selected GPU " + std::to_string(selectedGPUIndex) + ": " + props.deviceName);
    NODE_LOG_INFO("[DeviceNode]   Vendor ID: 0x" + std::to_string(props.vendorID));
    NODE_LOG_INFO("[DeviceNode]   Device ID: 0x" + std::to_string(props.deviceID));
    NODE_LOG_INFO("[DeviceNode]   Driver Version: " + std::to_string(props.driverVersion));
    NODE_LOG_INFO("[DeviceNode]   API Version: " + std::to_string(VK_VERSION_MAJOR(props.apiVersion)) + "."
              + std::to_string(VK_VERSION_MINOR(props.apiVersion)) + "."
              + std::to_string(VK_VERSION_PATCH(props.apiVersion)));
}

void DeviceNode::CreateLogicalDevice() {
    VkPhysicalDevice selectedGPU = availableGPUs[selectedGPUIndex];

    // Create VulkanDevice wrapper
    vulkanDevice = std::make_unique<VulkanDevice>(&selectedGPU);

    // Query queue families and properties
    vulkanDevice->GetPhysicalDeviceQueuesAndProperties();

    // Get graphics queue handle
    auto queueResult = vulkanDevice->GetGraphicsQueueHandle();
    if (!queueResult) {
        NODE_LOG_ERROR("[DeviceNode] ERROR: Failed to find graphics queue: " + queueResult.error().toString());
        return;
    }

    NODE_LOG_INFO("[DeviceNode] Graphics queue family index: " + std::to_string(queueResult.value()));

    // Get physical device properties and memory properties
    vkGetPhysicalDeviceProperties(selectedGPU, &vulkanDevice->gpuProperties);
    vkGetPhysicalDeviceMemoryProperties(selectedGPU, &vulkanDevice->gpuMemoryProperties);

    NODE_LOG_INFO("[DeviceNode] Memory heaps: " + std::to_string(vulkanDevice->gpuMemoryProperties.memoryHeapCount));
    NODE_LOG_INFO("[DeviceNode] Memory types: " + std::to_string(vulkanDevice->gpuMemoryProperties.memoryTypeCount));

    // Create logical device
    auto createResult = vulkanDevice->CreateDevice(deviceLayers, deviceExtensions);
    if (!createResult) {
        NODE_LOG_ERROR("[DeviceNode] ERROR: Failed to create logical device: " + createResult.error().toString());
        return;
    }

    // Get device queue
    vulkanDevice->GetDeviceQueue();

    NODE_LOG_INFO("[DeviceNode] Logical device created successfully");
    NODE_LOG_INFO("[DeviceNode] Device handle: " + std::to_string(reinterpret_cast<uint64_t>(vulkanDevice->device)));
    NODE_LOG_INFO("[DeviceNode] Queue handle: " + std::to_string(reinterpret_cast<uint64_t>(vulkanDevice->queue)));
}

} // namespace Vixen::RenderGraph