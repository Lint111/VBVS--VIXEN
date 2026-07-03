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
class InstanceNodeType : public TypedNodeType<InstanceNodeConfig> {
public:
    InstanceNodeType(const std::string& typeName = "InstanceNode")
        : TypedNodeType<InstanceNodeConfig>(typeName) {}

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

    /// Get the VkInstance handle (for profiler integration)
    VkInstance GetVkInstance() const { return instance; }

protected:
    // TypedNode lifecycle overrides
    void SetupImpl(TypedSetupContext& ctx) override;
    void CompileImpl(TypedCompileContext& ctx) override;
    void ExecuteImpl(TypedExecuteContext& ctx) override;
    void CleanupImpl(TypedCleanupContext& ctx) override;

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
    void ValidateAndFilterExtensions();
    void ValidateAndFilterLayers();
    void DestroyVulkanInstance();

#if defined(VIXEN_FAIL_SCENARIOS) && VIXEN_FAIL_SCENARIOS
    // Fail-scenario validation-error counter (Task 4 Step 4): VK_EXT_debug_report is the only
    // way to observe validation-layer output programmatically. No production callback existed
    // before this — installed only when the extension was actually enabled (validation layer
    // present), so it is a no-op on lavapipe-without-SDK machines.
    VkDebugReportCallbackEXT debugReportCallback_ = VK_NULL_HANDLE;
    void CreateDebugReportCallback();
    void DestroyDebugReportCallback();
#endif
};

} // namespace Vixen::RenderGraph
