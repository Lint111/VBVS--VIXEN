#pragma once

#include "Core/TypedNodeInstance.h"
#include "Data/Nodes/CommandPoolNodeConfig.h"
#include "VulkanDevice.h"
#include <vector>

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
 * @brief CommandPoolNode - Creates Vulkan command pool with pre-allocation support
 *
 * Inputs:
 *  - DeviceObj (VkDevice): Device to create pool on
 *
 * Outputs:
 *  - COMMAND_POOL (VkCommandPool): Created command pool
 *
 * Parameters:
 *  - queue_family_index (uint32_t): Queue family index for the pool
 *
 * Pre-Allocation (Sprint 5.5):
 *  Call PreAllocateCommandBuffers() after Compile() to pre-allocate
 *  command buffers based on aggregated node requirements.
 *  Nodes can then request buffers via AcquireCommandBuffer().
 */
class CommandPoolNode : public TypedNode<CommandPoolNodeConfig> {
public:

    CommandPoolNode(
        const std::string& instanceName,
        NodeType* nodeType
    );

    ~CommandPoolNode() override = default;

    // ========================================================================
    // Pre-Allocation API (Sprint 5.5)
    // ========================================================================

    /**
     * @brief Pre-allocate command buffers for zero-allocation runtime
     *
     * Call after Compile() with the aggregated command buffer count
     * from all nodes that will use this pool.
     *
     * @param primaryCount Number of primary command buffers to pre-allocate
     * @param secondaryCount Number of secondary command buffers (default: 0)
     */
    void PreAllocateCommandBuffers(uint32_t primaryCount, uint32_t secondaryCount = 0);

    /**
     * @brief Acquire a pre-allocated primary command buffer
     *
     * Returns VK_NULL_HANDLE if no buffers available (pool exhausted).
     * Buffers are recycled when ReleaseAllCommandBuffers() is called.
     *
     * @return Pre-allocated command buffer, or VK_NULL_HANDLE
     */
    VkCommandBuffer AcquireCommandBuffer();

    /**
     * @brief Acquire a pre-allocated secondary command buffer
     *
     * @return Pre-allocated secondary command buffer, or VK_NULL_HANDLE
     */
    VkCommandBuffer AcquireSecondaryCommandBuffer();

    /**
     * @brief Release all acquired command buffers back to pool
     *
     * Call at end of frame to recycle command buffers for next frame.
     * Does NOT free the buffers, just resets acquisition indices.
     */
    void ReleaseAllCommandBuffers();

    /**
     * @brief Get pre-allocation statistics
     */
    struct PoolStats {
        uint32_t primaryCapacity = 0;
        uint32_t secondaryCapacity = 0;
        uint32_t primaryAcquired = 0;
        uint32_t secondaryAcquired = 0;
        uint32_t growthCount = 0;  // Times pool had to grow (should be 0)
    };
    PoolStats GetPoolStats() const;

protected:
    // Template method pattern - override *Impl() methods
    void SetupImpl(TypedSetupContext& ctx) override;
    void CompileImpl(TypedCompileContext& ctx) override;
    void ExecuteImpl(TypedExecuteContext& ctx) override;
    void CleanupImpl(TypedCleanupContext& ctx) override;

private:
    VkCommandPool commandPool = VK_NULL_HANDLE;
    VulkanDevice* vulkanDevice = nullptr;
    bool isCreated = false;

    // Pre-allocated command buffer pool (Sprint 5.5)
    std::vector<VkCommandBuffer> primaryBuffers_;
    std::vector<VkCommandBuffer> secondaryBuffers_;
    uint32_t primaryAcquireIndex_ = 0;
    uint32_t secondaryAcquireIndex_ = 0;
    uint32_t growthCount_ = 0;
};

} // namespace Vixen::RenderGraph