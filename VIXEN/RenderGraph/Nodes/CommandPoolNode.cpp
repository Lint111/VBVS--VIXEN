#include "RenderGraph/Nodes/CommandPoolNode.h"
#include "VulkanResources/VulkanDevice.h"
#include <stdexcept>

namespace Vixen::RenderGraph {

// ====== CommandPoolNodeType ======

CommandPoolNodeType::CommandPoolNodeType() {
    typeId = 101; // Unique ID (DeviceNode is 100, TextureLoader is 100, need to reorganize)
    typeName = "CommandPool";
    pipelineType = PipelineType::None; // No graphics/compute pipeline
    requiredCapabilities = DeviceCapability::None;
    supportsInstancing = true;
    maxInstances = 0; // Unlimited command pools

    // Input: Device object
    DeviceObjectDescription deviceInput{};
    inputSchema.push_back(ResourceDescriptor(
        "device_obj",
        ResourceType::Buffer, // Using Buffer as placeholder for device objects
        ResourceLifetime::Persistent,
        deviceInput
    ));

    // Output: Command pool
    BufferDescription poolOutput{};
    poolOutput.size = 0;
    poolOutput.usage = ResourceUsage::CommandPool;
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
    const std::string& instanceName,
    Vixen::Vulkan::Resources::VulkanDevice* device
) const {
    return std::make_unique<CommandPoolNode>(
        instanceName,
        const_cast<CommandPoolNodeType*>(this),
        device
    );
}

// ====== CommandPoolNode ======

CommandPoolNode::CommandPoolNode(
    const std::string& instanceName,
    NodeType* nodeType,
    Vixen::Vulkan::Resources::VulkanDevice* device
)
    : TypedNode<CommandPoolNodeConfig>(instanceName, nodeType, device)
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
        NODE_LOG_ERROR(this, errorMsg);
        throw std::runtime_error(errorMsg);
    }

    isCreated = true;

    // Create output resource and set the command pool handle
    Resource* outputResource = GetOutput(CommandPoolNodeConfig::COMMAND_POOL);
    if (!outputResource) {
        std::string errorMsg = "CommandPoolNode output resource not allocated for node: " + instanceName;
        NODE_LOG_ERROR(this, errorMsg);
        throw std::runtime_error(errorMsg);
    }

    outputResource->SetCommandPool(commandPool);
    outputResource->SetDeviceDependency(device);

    NODE_LOG(this, "Created command pool for queue family " + std::to_string(queueFamilyIndex));
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

        NODE_LOG(this, "Destroyed command pool");
    }
}

} // namespace Vixen::RenderGraph