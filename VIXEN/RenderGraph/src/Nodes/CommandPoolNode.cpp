#include "Nodes/CommandPoolNode.h"
#include "Core/RenderGraph.h"
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

    // Populate schemas from Config
    CommandPoolNodeConfig config;
    inputSchema = config.GetInputVector();
    outputSchema = config.GetOutputVector();

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
    vulkanDevice = In(CommandPoolNodeConfig::VULKAN_DEVICE_IN);

    if (vulkanDevice == VK_NULL_HANDLE) {
        std::string errorMsg = "CommandPoolNode: VkDevice input is null";
        NODE_LOG_ERROR(errorMsg);
        throw std::runtime_error(errorMsg);
    }
}

void CommandPoolNode::Compile() {

    // Get queue family index parameter
    // TODO: Should get queue family index from DeviceNode output instead of parameter
    uint32_t queueFamilyIndex = GetParameterValue<uint32_t>(
        CommandPoolNodeConfig::PARAM_QUEUE_FAMILY_INDEX,
        0  // Default to index 0 (graphics queue)
    );

    // Create command pool
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = queueFamilyIndex;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

    VkResult result = vkCreateCommandPool(vulkanDevice->device, &poolInfo, nullptr, &commandPool);
    if (result != VK_SUCCESS) {
        std::string errorMsg = "Failed to create command pool for node: " + instanceName;
        NODE_LOG_ERROR(errorMsg);
        throw std::runtime_error(errorMsg);
    }

    isCreated = true;

    // Store command pool handle in output resource (NEW VARIANT API)
    Out(CommandPoolNodeConfig::COMMAND_POOL, commandPool);
    Out(CommandPoolNodeConfig::VULKAN_DEVICE_OUT, vulkanDevice);

    NODE_LOG_INFO("Created command pool for queue family " + std::to_string(queueFamilyIndex));

    // === REGISTER CLEANUP ===
    // Automatically resolves dependency on DeviceNode via input slot 0
    RegisterCleanup();
}

void CommandPoolNode::Execute(VkCommandBuffer commandBuffer) {
    // Command pool creation happens in Compile phase
    // Execute is a no-op
}

void CommandPoolNode::Cleanup() {
    if (isCreated && commandPool != VK_NULL_HANDLE && vulkanDevice != VK_NULL_HANDLE) {
        vkDestroyCommandPool(vulkanDevice->device, commandPool, nullptr);
        commandPool = VK_NULL_HANDLE;
        isCreated = false;

        NODE_LOG_INFO("Destroyed command pool");
    }
}

} // namespace Vixen::RenderGraph