#include "CapabilityGraph.h"
#include <algorithm>

namespace Vixen {

//==============================================================================
// Static member initialization
//==============================================================================

std::vector<std::string> InstanceExtensionCapability::availableExtensions_;
std::vector<std::string> InstanceLayerCapability::availableLayers_;
std::vector<std::string> DeviceExtensionCapability::availableExtensions_;

//==============================================================================
// InstanceExtensionCapability
//==============================================================================

void InstanceExtensionCapability::SetAvailableExtensions(const std::vector<std::string>& extensions) {
    availableExtensions_ = extensions;
}

bool InstanceExtensionCapability::CheckAvailability() const {
    return std::find(availableExtensions_.begin(), availableExtensions_.end(), extensionName_)
           != availableExtensions_.end();
}

//==============================================================================
// InstanceLayerCapability
//==============================================================================

void InstanceLayerCapability::SetAvailableLayers(const std::vector<std::string>& layers) {
    availableLayers_ = layers;
}

bool InstanceLayerCapability::CheckAvailability() const {
    return std::find(availableLayers_.begin(), availableLayers_.end(), layerName_)
           != availableLayers_.end();
}

//==============================================================================
// DeviceExtensionCapability
//==============================================================================

void DeviceExtensionCapability::SetAvailableExtensions(const std::vector<std::string>& extensions) {
    availableExtensions_ = extensions;
}

bool DeviceExtensionCapability::CheckAvailability() const {
    return std::find(availableExtensions_.begin(), availableExtensions_.end(), extensionName_)
           != availableExtensions_.end();
}

//==============================================================================
// CapabilityGraph
//==============================================================================

void CapabilityGraph::RegisterCapability(std::shared_ptr<CapabilityNode> capability) {
    capabilities_[capability->GetName()] = capability;
}

std::shared_ptr<CapabilityNode> CapabilityGraph::GetCapability(const std::string& name) const {
    auto it = capabilities_.find(name);
    return (it != capabilities_.end()) ? it->second : nullptr;
}

bool CapabilityGraph::IsCapabilityAvailable(const std::string& name) const {
    auto cap = GetCapability(name);
    return cap && cap->IsAvailable();
}

void CapabilityGraph::InvalidateAll() {
    for (auto& [name, cap] : capabilities_) {
        cap->Invalidate();
    }
}

void CapabilityGraph::BuildStandardCapabilities() {
    //==========================================================================
    // Base Device Extensions
    //==========================================================================

    auto swapchain = CreateCapability<DeviceExtensionCapability>(
        "DeviceExt:VK_KHR_swapchain", VK_KHR_SWAPCHAIN_EXTENSION_NAME);

    auto maintenance1 = CreateCapability<DeviceExtensionCapability>(
        "DeviceExt:VK_KHR_maintenance1", VK_KHR_MAINTENANCE_1_EXTENSION_NAME);

    auto maintenance2 = CreateCapability<DeviceExtensionCapability>(
        "DeviceExt:VK_KHR_maintenance2", VK_KHR_MAINTENANCE_2_EXTENSION_NAME);

    auto maintenance3 = CreateCapability<DeviceExtensionCapability>(
        "DeviceExt:VK_KHR_maintenance3", VK_KHR_MAINTENANCE_3_EXTENSION_NAME);

    auto maintenance4 = CreateCapability<DeviceExtensionCapability>(
        "DeviceExt:VK_KHR_maintenance4", VK_KHR_MAINTENANCE_4_EXTENSION_NAME);

    auto maintenance5 = CreateCapability<DeviceExtensionCapability>(
        "DeviceExt:VK_KHR_maintenance5", VK_KHR_MAINTENANCE_5_EXTENSION_NAME);

    auto maintenance6 = CreateCapability<DeviceExtensionCapability>(
        "DeviceExt:VK_KHR_maintenance6", VK_KHR_MAINTENANCE_6_EXTENSION_NAME);

    auto swapchainMutableFormat = CreateCapability<DeviceExtensionCapability>(
        "DeviceExt:VK_KHR_swapchain_mutable_format", VK_KHR_SWAPCHAIN_MUTABLE_FORMAT_EXTENSION_NAME);

    //==========================================================================
    // RTX Extensions
    //==========================================================================

    auto rayTracingPipeline = CreateCapability<DeviceExtensionCapability>(
        "DeviceExt:VK_KHR_ray_tracing_pipeline", VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME);

    auto accelerationStructure = CreateCapability<DeviceExtensionCapability>(
        "DeviceExt:VK_KHR_acceleration_structure", VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME);

    auto rayQuery = CreateCapability<DeviceExtensionCapability>(
        "DeviceExt:VK_KHR_ray_query", VK_KHR_RAY_QUERY_EXTENSION_NAME);

    auto deferredHostOps = CreateCapability<DeviceExtensionCapability>(
        "DeviceExt:VK_KHR_deferred_host_operations", VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);

    auto bufferDeviceAddress = CreateCapability<DeviceExtensionCapability>(
        "DeviceExt:VK_KHR_buffer_device_address", VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME);

    //==========================================================================
    // Instance Extensions
    //==========================================================================

    auto surfaceExt = CreateCapability<InstanceExtensionCapability>(
        "InstanceExt:VK_KHR_surface", VK_KHR_SURFACE_EXTENSION_NAME);

#ifdef VK_USE_PLATFORM_WIN32_KHR
    auto win32Surface = CreateCapability<InstanceExtensionCapability>(
        "InstanceExt:VK_KHR_win32_surface", VK_KHR_WIN32_SURFACE_EXTENSION_NAME);
#endif

    auto debugUtils = CreateCapability<InstanceExtensionCapability>(
        "InstanceExt:VK_EXT_debug_utils", VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

    //==========================================================================
    // Instance Layers
    //==========================================================================

    auto validationLayer = CreateCapability<InstanceLayerCapability>(
        "InstanceLayer:VK_LAYER_KHRONOS_validation", "VK_LAYER_KHRONOS_validation");

    //==========================================================================
    // Composite Capabilities
    //==========================================================================

    // RTX Support (requires all RT extensions)
    auto rtxSupport = std::make_shared<CompositeCapability>("RTXSupport");
    rtxSupport->AddDependency(rayTracingPipeline);
    rtxSupport->AddDependency(accelerationStructure);
    rtxSupport->AddDependency(rayQuery);
    rtxSupport->AddDependency(deferredHostOps);
    rtxSupport->AddDependency(bufferDeviceAddress);
    RegisterCapability(rtxSupport);

    // Swapchain Maintenance 1 (swapchain + maintenance1)
    auto swapchainMaint1 = std::make_shared<CompositeCapability>("SwapchainMaintenance1");
    swapchainMaint1->AddDependency(swapchain);
    swapchainMaint1->AddDependency(maintenance1);
    RegisterCapability(swapchainMaint1);

    // Swapchain Maintenance 2 (swapchain + maintenance1 + maintenance2)
    auto swapchainMaint2 = std::make_shared<CompositeCapability>("SwapchainMaintenance2");
    swapchainMaint2->AddDependency(swapchain);
    swapchainMaint2->AddDependency(maintenance1);
    swapchainMaint2->AddDependency(maintenance2);
    RegisterCapability(swapchainMaint2);

    // Swapchain Maintenance 3 (swapchain + maintenance1 + maintenance2 + maintenance3)
    auto swapchainMaint3 = std::make_shared<CompositeCapability>("SwapchainMaintenance3");
    swapchainMaint3->AddDependency(swapchain);
    swapchainMaint3->AddDependency(maintenance1);
    swapchainMaint3->AddDependency(maintenance2);
    swapchainMaint3->AddDependency(maintenance3);
    RegisterCapability(swapchainMaint3);

    // Full Swapchain Support (all maintenance + mutable format)
    auto fullSwapchain = std::make_shared<CompositeCapability>("FullSwapchainSupport");
    fullSwapchain->AddDependency(swapchain);
    fullSwapchain->AddDependency(maintenance1);
    fullSwapchain->AddDependency(maintenance2);
    fullSwapchain->AddDependency(maintenance3);
    fullSwapchain->AddDependency(maintenance4);
    fullSwapchain->AddDependency(maintenance5);
    fullSwapchain->AddDependency(maintenance6);
    fullSwapchain->AddDependency(swapchainMutableFormat);
    RegisterCapability(fullSwapchain);

    // Basic Rendering Support (swapchain + surface + platform surface)
    auto basicRendering = std::make_shared<CompositeCapability>("BasicRenderingSupport");
    basicRendering->AddDependency(swapchain);
    basicRendering->AddDependency(surfaceExt);
#ifdef VK_USE_PLATFORM_WIN32_KHR
    basicRendering->AddDependency(win32Surface);
#endif
    RegisterCapability(basicRendering);

    // Validation Support (validation layer + debug utils)
    auto validationSupport = std::make_shared<CompositeCapability>("ValidationSupport");
    validationSupport->AddDependency(validationLayer);
    validationSupport->AddDependency(debugUtils);
    RegisterCapability(validationSupport);
}

} // namespace Vixen
