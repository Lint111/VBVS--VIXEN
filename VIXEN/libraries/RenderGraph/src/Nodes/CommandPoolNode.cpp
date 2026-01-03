#include "Nodes/CommandPoolNode.h"
#include "Core/RenderGraph.h"
#include "VulkanDevice.h"
#include "Core/NodeLogging.h"
#include <stdexcept>

namespace Vixen::RenderGraph {

// ====== CommandPoolNodeType ======

CommandPoolNodeType::CommandPoolNodeType(const std::string& typeName)
    : TypedNodeType<CommandPoolNodeConfig>(typeName)
{
    requiredCapabilities = DeviceCapability::None;
    supportsInstancing = true;
    maxInstances = 0; // Unlimited command pools

    // Workload metrics
    workloadMetrics.estimatedMemoryFootprint = 1024; // Minimal - just pool structure
    workloadMetrics.estimatedComputeCost = 0.1f; // Very cheap to create
    workloadMetrics.estimatedBandwidthCost = 0.0f; // No bandwidth
    workloadMetrics.canRunInParallel = true;

    // Schema population now handled by TypedNodeType base class
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

void CommandPoolNode::SetupImpl(TypedSetupContext& ctx) {
    // Graph-scope initialization only (no input access)
    NODE_LOG_DEBUG("CommandPoolNode: Setup (graph-scope initialization)");
}

void CommandPoolNode::CompileImpl(TypedCompileContext& ctx) {
    // Access device input (compile-time dependency)
    VulkanDevice* devicePtr = ctx.In(CommandPoolNodeConfig::VULKAN_DEVICE_IN);

    if (devicePtr == nullptr) {
        std::string errorMsg = "CommandPoolNode: VkDevice input is null";
        NODE_LOG_ERROR(errorMsg);
        throw std::runtime_error(errorMsg);
    }

    // Set base class device member for cleanup tracking
    SetDevice(devicePtr);

    // Validate device pointer
    if (devicePtr == reinterpret_cast<VulkanDevice*>(0xFFFFFFFFFFFFFFFF)) {
        std::string errorMsg = "CommandPoolNode: VkDevice input is INVALID (0xFFFFFFFFFFFFFFFF)";
        NODE_LOG_ERROR(errorMsg);
        throw std::runtime_error(errorMsg);
    }

    NODE_LOG_DEBUG("CommandPoolNode: devicePtr = " + std::to_string(reinterpret_cast<uintptr_t>(devicePtr)));

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

    VkResult result = vkCreateCommandPool(device->device, &poolInfo, nullptr, &commandPool);
    if (result != VK_SUCCESS) {
        std::string errorMsg = "Failed to create command pool for node: " + instanceName;
        NODE_LOG_ERROR(errorMsg);
        throw std::runtime_error(errorMsg);
    }

    isCreated = true;

    // Store command pool handle in output resource (NEW VARIANT API)
    ctx.Out(CommandPoolNodeConfig::COMMAND_POOL, commandPool);
    ctx.Out(CommandPoolNodeConfig::VULKAN_DEVICE_OUT, device);

    NODE_LOG_INFO("Created command pool for queue family " + std::to_string(queueFamilyIndex));
}

void CommandPoolNode::ExecuteImpl(TypedExecuteContext& ctx) {
    // Command pool creation happens in Compile phase
    // Execute is a no-op
}

void CommandPoolNode::CleanupImpl(TypedCleanupContext& ctx) {
    // Free pre-allocated command buffers first
    if (!primaryBuffers_.empty() && commandPool != VK_NULL_HANDLE && device != nullptr) {
        vkFreeCommandBuffers(device->device, commandPool,
            static_cast<uint32_t>(primaryBuffers_.size()), primaryBuffers_.data());
        primaryBuffers_.clear();
    }
    if (!secondaryBuffers_.empty() && commandPool != VK_NULL_HANDLE && device != nullptr) {
        vkFreeCommandBuffers(device->device, commandPool,
            static_cast<uint32_t>(secondaryBuffers_.size()), secondaryBuffers_.data());
        secondaryBuffers_.clear();
    }
    primaryAcquireIndex_ = 0;
    secondaryAcquireIndex_ = 0;

    if (isCreated && commandPool != VK_NULL_HANDLE && device != nullptr && reinterpret_cast<uintptr_t>(device) != 0xFFFFFFFFFFFFFFFF) {
        vkDestroyCommandPool(device->device, commandPool, nullptr);
        commandPool = VK_NULL_HANDLE;
        isCreated = false;

        NODE_LOG_INFO("Destroyed command pool");
    }
}

// ============================================================================
// Pre-Allocation API (Sprint 5.5)
// ============================================================================

void CommandPoolNode::PreAllocateCommandBuffers(uint32_t primaryCount, uint32_t secondaryCount) {
    if (commandPool == VK_NULL_HANDLE || device == nullptr) {
        NODE_LOG_ERROR("Cannot pre-allocate: command pool not created");
        return;
    }

    // Pre-allocate primary command buffers
    if (primaryCount > 0) {
        primaryBuffers_.resize(primaryCount, VK_NULL_HANDLE);

        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool = commandPool;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = primaryCount;

        VkResult result = vkAllocateCommandBuffers(device->device, &allocInfo, primaryBuffers_.data());
        if (result != VK_SUCCESS) {
            NODE_LOG_ERROR("Failed to pre-allocate " + std::to_string(primaryCount) + " primary command buffers");
            primaryBuffers_.clear();
            return;
        }

        NODE_LOG_INFO("Pre-allocated " + std::to_string(primaryCount) + " primary command buffers");
    }

    // Pre-allocate secondary command buffers
    if (secondaryCount > 0) {
        secondaryBuffers_.resize(secondaryCount, VK_NULL_HANDLE);

        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool = commandPool;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_SECONDARY;
        allocInfo.commandBufferCount = secondaryCount;

        VkResult result = vkAllocateCommandBuffers(device->device, &allocInfo, secondaryBuffers_.data());
        if (result != VK_SUCCESS) {
            NODE_LOG_ERROR("Failed to pre-allocate " + std::to_string(secondaryCount) + " secondary command buffers");
            secondaryBuffers_.clear();
            return;
        }

        NODE_LOG_INFO("Pre-allocated " + std::to_string(secondaryCount) + " secondary command buffers");
    }

    primaryAcquireIndex_ = 0;
    secondaryAcquireIndex_ = 0;
}

VkCommandBuffer CommandPoolNode::AcquireCommandBuffer() {
    if (primaryAcquireIndex_ >= primaryBuffers_.size()) {
        // Pool exhausted - need to grow (should not happen if pre-allocation was correct)
        growthCount_++;
        NODE_LOG_WARNING("Command buffer pool exhausted, growing (growth count: " +
            std::to_string(growthCount_) + ")");

        // Allocate one more buffer
        VkCommandBuffer newBuffer = VK_NULL_HANDLE;
        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool = commandPool;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = 1;

        if (vkAllocateCommandBuffers(device->device, &allocInfo, &newBuffer) != VK_SUCCESS) {
            NODE_LOG_ERROR("Failed to allocate additional command buffer");
            return VK_NULL_HANDLE;
        }

        primaryBuffers_.push_back(newBuffer);
    }

    return primaryBuffers_[primaryAcquireIndex_++];
}

VkCommandBuffer CommandPoolNode::AcquireSecondaryCommandBuffer() {
    if (secondaryAcquireIndex_ >= secondaryBuffers_.size()) {
        // Pool exhausted - need to grow
        growthCount_++;
        NODE_LOG_WARNING("Secondary command buffer pool exhausted, growing (growth count: " +
            std::to_string(growthCount_) + ")");

        VkCommandBuffer newBuffer = VK_NULL_HANDLE;
        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool = commandPool;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_SECONDARY;
        allocInfo.commandBufferCount = 1;

        if (vkAllocateCommandBuffers(device->device, &allocInfo, &newBuffer) != VK_SUCCESS) {
            NODE_LOG_ERROR("Failed to allocate additional secondary command buffer");
            return VK_NULL_HANDLE;
        }

        secondaryBuffers_.push_back(newBuffer);
    }

    return secondaryBuffers_[secondaryAcquireIndex_++];
}

void CommandPoolNode::ReleaseAllCommandBuffers() {
    primaryAcquireIndex_ = 0;
    secondaryAcquireIndex_ = 0;
}

CommandPoolNode::PoolStats CommandPoolNode::GetPoolStats() const {
    PoolStats stats;
    stats.primaryCapacity = static_cast<uint32_t>(primaryBuffers_.size());
    stats.secondaryCapacity = static_cast<uint32_t>(secondaryBuffers_.size());
    stats.primaryAcquired = primaryAcquireIndex_;
    stats.secondaryAcquired = secondaryAcquireIndex_;
    stats.growthCount = growthCount_;
    return stats;
}

} // namespace Vixen::RenderGraph