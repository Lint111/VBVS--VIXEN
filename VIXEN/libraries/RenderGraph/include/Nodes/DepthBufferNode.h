#pragma once

#include "Core/NodeType.h"
#include "Core/TypedNodeInstance.h"
#include "Data/Nodes/DepthBufferNodeConfig.h"
#include "VulkanDevice.h"  // Full definition needed for inline member access

namespace Vixen::RenderGraph {

/**
 * @brief Depth buffer creation node
 *
 * Responsibilities:
 * - Create depth/stencil image
 * - Allocate device memory
 * - Create image view
 * - Transition to depth-stencil attachment optimal layout
 *
 * Uses TypedNode with DepthBufferNodeConfig for compile-time type safety.
 *
 * Type ID: 101
 */
class DepthBufferNode : public TypedNode<DepthBufferNodeConfig> {
public:

    DepthBufferNode(
        const std::string& instanceName,
        NodeType* nodeType
    );

    ~DepthBufferNode() override = default;

    // Accessor for depth image view
    VkImageView GetDepthImageView() const { return depthImage.view; }

protected:
	// Template method pattern - override *Impl() methods
	void SetupImpl(TypedSetupContext& ctx) override;
	void CompileImpl(TypedCompileContext& ctx) override;
	void ExecuteImpl(TypedExecuteContext& ctx) override;
	void CleanupImpl(TypedCleanupContext& ctx) override;

private:
    struct DepthImage {
        VkFormat format = VK_FORMAT_UNDEFINED;
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory mem = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
    } depthImage;

    VulkanDevice* vulkanDevice = nullptr;
    VulkanDevice* deviceHandle = nullptr; // Alias for vulkanDevice (for legacy code)
    VkCommandPool commandPool = VK_NULL_HANDLE;
    bool isCreated = false;

    // Task profile for compile-time cost estimation (Sprint 6.5)
    ITaskProfile* compileProfile_ = nullptr;

    // Helper to get VkDevice from VulkanDevice pointer
    VkDevice GetVkDevice() const {
        return vulkanDevice ? vulkanDevice->device : VK_NULL_HANDLE;
    }

    VkFormat ConvertDepthFormat(DepthFormat format);
    void CreateDepthImageAndView(uint32_t width, uint32_t height, VkFormat format);
    void TransitionDepthImageLayout(VkCommandPool cmdPool);
    void TransitionImageLayout(VkCommandBuffer cmdBuffer, VkImage image,
                               VkImageLayout oldLayout, VkImageLayout newLayout);
};

/**
 * @brief Type definition for DepthBufferNode
 */
class DepthBufferNodeType : public TypedNodeType<DepthBufferNodeConfig> {
public:
    DepthBufferNodeType(const std::string& typeName = "DepthBuffer")
        : TypedNodeType<DepthBufferNodeConfig>(typeName) {}

    std::unique_ptr<NodeInstance> CreateInstance(
        const std::string& instanceName
    ) const override;
};

} // namespace Vixen::RenderGraph
