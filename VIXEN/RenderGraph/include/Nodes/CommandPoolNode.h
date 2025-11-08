#pragma once

#include "Core/TypedNodeInstance.h"
#include "Data/Nodes/CommandPoolNodeConfig.h"
#include "VulkanResources/VulkanDevice.h"

namespace Vixen::RenderGraph {

/**
 * @brief CommandPoolNodeType - Defines command pool creation node
 */
class CommandPoolNodeType : public TypedNodeType<CommandPoolNodeConfig> {
public:
    CommandPoolNodeType(const std::string& typeName = "CommandPool");

    std::unique_ptr<NodeInstance> CreateInstance(
        const std::string& instanceName
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
        NodeType* nodeType
    );

    ~CommandPoolNode() override = default;

protected:
    // Template method pattern - override *Impl() methods
    void SetupImpl(TypedSetupContext& ctx) override;
    void CompileImpl(TypedCompileContext& ctx) override;
    void ExecuteImpl(TypedExecuteContext& ctx) override;
    void CleanupImpl(TypedCleanupContext& ctx) override;

private:
    VkCommandPool commandPool = VK_NULL_HANDLE;
    VulkanDevicePtr vulkanDevice = VK_NULL_HANDLE;
    bool isCreated = false;
};

} // namespace Vixen::RenderGraph