#include "Nodes/CommandPoolNode.h"
#include "VulkanResources/VulkanDevice.h"
#include "Core/NodeLogging.h"
#include <stdexcept>

namespace Vixen::RenderGraph {

// ====== CommandPoolNodeType ======

CommandPoolNodeType::CommandPoolNodeType(const std::string& typeName)
    : NodeType(typeName)
{
    requiredCapabilities = DeviceCapability::None;
    supportsInstancing = true;
    maxInstances = 0; // Unlimited command pools

    // Input: Device object (uses HandleDescriptor for VkDevice)
    HandleDescriptor deviceInput{"VkDevice"};
    inputSchema.push_back(ResourceDescriptor(
        "device_obj",
        ResourceType::Buffer, // Using Buffer as placeholder for device objects
        ResourceLifetime::Persistent,
        deviceInput
    ));

    // Output: Command pool
    CommandPoolDescriptor poolOutput{};
    poolOutput.flags = 0;
    poolOutput.queueFamilyIndex = 0;
    outputSchema.push_back(ResourceDescriptor(
        "command_pool",
        ResourceType::Buffer, // Using Buffer as placeholder for command pool
        ResourceLifetime::Persistent,
        poolOutput
    ));

    // Workload metrics
    workloadMetrics.estimatedMemoryFootprint = 1024; // Minimal - just pool structure
    workloadMetrics.estimatedComputeCost = 0.1f; // Very cheap to create
    workloadMetrics.estimatedBandwidthCost = 0.0f; // No bandwidth
    workloadMetrics.canRunInParallel = true;
}

std::unique_ptr<NodeInstance> CommandPoolNodeType::CreateInstance(
    const std::string& instanceName
) const {
    return std::make_unique<CommandPoolNode>(
        instanceName,
        const_cast<NodeType*>(static_cast<const NodeType*>(this))
    );
}

// ====== CommandPoolNode ======

CommandPoolNode::CommandPoolNode(
    const std::string& instanceName,
    NodeType* nodeType
)
    : TypedNode<CommandPoolNodeConfig>(instanceName, nodeType)
{
}

CommandPoolNode::~CommandPoolNode() {
    Cleanup();
}

void CommandPoolNode::Setup() {
    // No setup needed for command pool
}

void CommandPoolNode::Compile() {
    // Get queue family index parameter
    uint32_t queueFamilyIndex = GetParameterValue<uint32_t>(
        CommandPoolNodeConfig::PARAM_QUEUE_FAMILY_INDEX,
        device->graphicsQueueIndex // Default to graphics queue
    );

    // Get device from input
    // Note: Input is VulkanDevice pointer type-punned as VkDevice in the resource
    // We use the device pointer from NodeInstance instead
    VkDevice vkDevice = device->device;

    // Create command pool
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = queueFamilyIndex;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

    VkResult result = vkCreateCommandPool(vkDevice, &poolInfo, nullptr, &commandPool);
    if (result != VK_SUCCESS) {
        std::string errorMsg = "Failed to create command pool for node: " + instanceName;
        NODE_LOG_ERROR(errorMsg);
        throw std::runtime_error(errorMsg);
    }

    isCreated = true;

    // Create output resource and set the command pool handle
    Resource* outputResource = GetOutput(CommandPoolNodeConfig::COMMAND_POOL_Slot::index);
    if (!outputResource) {
        std::string errorMsg = "CommandPoolNode output resource not allocated for node: " + instanceName;
        NODE_LOG_ERROR(errorMsg);
        throw std::runtime_error(errorMsg);
    }

    // Store command pool handle in resource (NEW VARIANT API)
    outputResource->SetHandle<VkCommandPool>(commandPool);
    // TODO: Track device dependency through graph connections instead of storing in resource

    NODE_LOG_INFO("Created command pool for queue family " + std::to_string(queueFamilyIndex));
}

void CommandPoolNode::Execute(VkCommandBuffer commandBuffer) {
    // Command pool creation happens in Compile phase
    // Execute is a no-op
}

void CommandPoolNode::Cleanup() {
    if (isCreated && commandPool != VK_NULL_HANDLE) {
        vkDestroyCommandPool(device->device, commandPool, nullptr);
        commandPool = VK_NULL_HANDLE;
        isCreated = false;

        NODE_LOG_INFO("Destroyed command pool");
    }
}

} // namespace Vixen::RenderGraph