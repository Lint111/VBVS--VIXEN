#pragma once

#include "Data/Nodes/InstanceNodeConfig.h"
#include "Core/TypedNodeInstance.h"
#include <vulkan/vulkan.h>
#include <vector>
#include <string>

namespace Vixen::RenderGraph {

/**
 * @brief NodeType for InstanceNode
 */
class InstanceNodeType : public NodeType {
public:
    InstanceNodeType() : NodeType("InstanceNode", InstanceNodeConfig{}) {}

    std::unique_ptr<NodeInstance> CreateInstance(const std::string& instanceName) const override;
};

/**
 * @brief InstanceNode - Creates and manages Vulkan instance
 *
 * Responsible for:
 * - Creating VkInstance with validation layers and extensions
 * - Managing instance lifetime
 * - Supporting multi-device scenarios by separating instance from device
 *
 * Outputs:
 * - INSTANCE: VkInstance handle
 */
class InstanceNode : public TypedNode<InstanceNodeConfig> {
public:
    InstanceNode(const std::string& instanceName, NodeType* nodeType);
    ~InstanceNode() override;

protected:
    // TypedNode lifecycle overrides
    void SetupImpl(TypedSetupContext& ctx) override;
    void CompileImpl(TypedCompileContext& ctx) override;
    void ExecuteImpl(TypedExecuteContext& ctx) override;
    void CleanupImpl() override;

private:
    // Vulkan instance
    VkInstance instance = VK_NULL_HANDLE;

    // Instance configuration
    std::vector<const char*> enabledLayers;
    std::vector<const char*> enabledExtensions;
    std::string appName;
    std::string engineName;
    bool validationEnabled = false;

    // Helper methods
    void CreateVulkanInstance();
    void EnumerateAvailableLayers();
    void EnumerateAvailableExtensions();
    void DestroyVulkanInstance();
};

} // namespace Vixen::RenderGraph
