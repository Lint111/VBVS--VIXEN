#pragma once

#include "Core/TypedNodeInstance.h"
#include "Core/NodeType.h"
#include "Nodes/DeviceNodeConfig.h"
#include "VulkanResources/VulkanDevice.h"
#include <memory>
#include <vector>

namespace Vixen::RenderGraph {

/**
 * @brief Node type for Vulkan device creation
 * Type ID: 112
 */
class DeviceNodeType : public NodeType {
public:
    DeviceNodeType();
    virtual ~DeviceNodeType() = default;

    std::unique_ptr<NodeInstance> CreateInstance(
        const std::string& instanceName
    ) const override;
};

/**
 * @brief Node instance for Vulkan device management
 *
 * Wraps VulkanDevice class to provide device functionality in graph.
 * Enumerates physical devices, selects one, and creates logical device.
 *
 * Parameters:
 * - gpu_index (uint32_t): Index of GPU to select (0 = first GPU, default)
 *
 * Outputs (auto-generated from DeviceNodeConfig):
 * - DEVICE: VulkanDevice* (index 0, required)
 *
 * The output VulkanDevice provides:
 * - VkPhysicalDevice* gpu - Physical device handle
 * - VkDevice device - Logical device handle
 * - VkPhysicalDeviceProperties gpuProperties
 * - VkPhysicalDeviceMemoryProperties gpuMemoryProperties
 * - VkPhysicalDeviceFeatures deviceFeatures
 * - Queue family information
 * - Memory type queries via MemoryTypeFromProperties()
 */
class DeviceNode : public TypedNode<DeviceNodeConfig> {
public:
    DeviceNode(
        const std::string& instanceName,
        NodeType* nodeType,
        Vixen::Vulkan::Resources::VulkanDevice* device
    );
    virtual ~DeviceNode();

    // Accessor for VulkanDevice wrapper
    Vixen::Vulkan::Resources::VulkanDevice* GetVulkanDevice() const {
        return vulkanDevice.get();
    }

protected:
	// Template method pattern - override *Impl() methods
	void SetupImpl() override;
	void CompileImpl() override;
	void ExecuteImpl(TaskContext& ctx) override;
	void CleanupImpl() override;

private:
    // VkInstance from global (Phase 1 temporary)
    VkInstance instance = VK_NULL_HANDLE;

    // GPU enumeration
    std::vector<VkPhysicalDevice> availableGPUs;
    uint32_t selectedGPUIndex = 0;
    VkPhysicalDevice selectedPhysicalDevice = VK_NULL_HANDLE; // Store selected GPU

    // VulkanDevice wrapper (owns logical device)
    std::shared_ptr<Vixen::Vulkan::Resources::VulkanDevice> vulkanDevice;

    // Extensions and layers to request
    std::vector<const char*> deviceExtensions;
    std::vector<const char*> deviceLayers;

    // Helper methods
    void EnumeratePhysicalDevices();
    void SelectPhysicalDevice();
    void CreateLogicalDevice();
    void PublishDeviceMetadata();  // Publish device capabilities via EventBus
};

} // namespace Vixen::RenderGraph