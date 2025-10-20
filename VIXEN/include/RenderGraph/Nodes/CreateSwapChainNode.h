#pragma once

#include "RenderGraph/NodeInstance.h"
#include "RenderGraph/NodeType.h"
#include "VulkanSwapChain.h"
#include "RenderGraph/Nodes/SwapChainNodeConfig.h"
#include <memory>

namespace Vixen::RenderGraph {

class CreateSwapChainNodeType : public NodeType {
public:
    CreateSwapChainNodeType();
    virtual ~CreateSwapChainNodeType() = default;

    std::unique_ptr<NodeInstance> CreateInstance(
        const std::string& instanceName,
        Vixen::Vulkan::Resources::VulkanDevice* device
    ) const override;
};

class CreateSwapChainNode : public TypedNode<SwapChainNodeConfig> {
public:
    CreateSwapChainNode(
        const std::string& instanceName,
        NodeType* nodeType,
        Vixen::Vulkan::Resources::VulkanDevice* device
    );
    virtual ~CreateSwapChainNode();

    void Setup() override;
    void Compile() override;
    void Execute(VkCommandBuffer commandBuffer) override;
    void Cleanup() override;

    VulkanSwapChain* GetSwapchain() const { return swapchain.get(); }
    uint32_t GetCurrentImageIndex() const { return swapchain ? swapchain->scPublicVars.currentColorBuffer : 0; }

private:
    std::unique_ptr<VulkanSwapChain> swapchain;
};

} // namespace Vixen::RenderGraph
