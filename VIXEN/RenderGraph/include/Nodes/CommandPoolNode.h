#pragma once

#include "RenderGraph/TypedNodeInstance.h"
#include "RenderGraph/Nodes/CommandPoolNodeConfig.h"

namespace Vixen::RenderGraph {

/**
 * @brief CommandPoolNodeType - Defines command pool creation node
 */
class CommandPoolNodeType : public NodeType {
public:
    CommandPoolNodeType();

    std::unique_ptr<NodeInstance> CreateInstance(
        const std::string& instanceName,
        Vixen::Vulkan::Resources::VulkanDevice* device
    ) const override;
};

/**
 * @brief CommandPoolNode - Creates Vulkan command pool
 *
 * Inputs:
 *  - DeviceObj (VkDevice): Device to create pool on
 *
 * Outputs:
 *  - COMMAND_POOL (VkCommandPool): Created command pool
 *
 * Parameters:
 *  - queue_family_index (uint32_t): Queue family index for the pool
 */
class CommandPoolNode : public TypedNode<CommandPoolNodeConfig> {
public:
    CommandPoolNode(
        const std::string& instanceName,
        NodeType* nodeType,
        Vixen::Vulkan::Resources::VulkanDevice* device
    );

    ~CommandPoolNode() override;

    // Node lifecycle
    void Setup() override;
    void Compile() override;
    void Execute(VkCommandBuffer commandBuffer) override;
    void Cleanup() override;

private:
    VkCommandPool commandPool = VK_NULL_HANDLE;
    bool isCreated = false;
};

} // namespace Vixen::RenderGraph