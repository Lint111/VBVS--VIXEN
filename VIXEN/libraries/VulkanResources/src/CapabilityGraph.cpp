#include "CapabilityGraph.h"
#include <algorithm>
#include <limits>
#include <cstring>

#define GLFW_INCLUDE_NONE   // don't pull in <GL/gl.h> (absent on headless/WSL); Vulkan-only below
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

namespace Vixen {

namespace {

bool Contains(const std::vector<std::string>& haystack, const std::string& needle) {
    return std::find(haystack.begin(), haystack.end(), needle) != haystack.end();
}

// Instance-level availability is global to the loader/ICD set (no VkInstance handle required), so a
// per-device CapabilityGraph can populate it itself rather than receiving it from the InstanceNode
// via process-wide statics (AR#8).
std::vector<std::string> EnumerateAvailableInstanceExtensions() {
    uint32_t count = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr);
    std::vector<VkExtensionProperties> props(count);
    vkEnumerateInstanceExtensionProperties(nullptr, &count, props.data());

    std::vector<std::string> names;
    names.reserve(count);
    for (const auto& p : props) {
        names.emplace_back(p.extensionName);
    }
    return names;
}

std::vector<std::string> EnumerateAvailableInstanceLayers() {
    uint32_t count = 0;
    vkEnumerateInstanceLayerProperties(&count, nullptr);
    std::vector<VkLayerProperties> props(count);
    vkEnumerateInstanceLayerProperties(&count, props.data());

    std::vector<std::string> names;
    names.reserve(count);
    for (const auto& p : props) {
        names.emplace_back(p.layerName);
    }
    return names;
}

} // namespace

//==============================================================================
// Leaf capability availability checks — consult the owning graph's per-instance
// availability sets (AR#8: replaces the former process-wide static vectors).
//==============================================================================

bool InstanceExtensionCapability::CheckAvailability() const {
    return graph_ && graph_->IsInstanceExtensionAvailable(extensionName_);
}

bool InstanceLayerCapability::CheckAvailability() const {
    return graph_ && graph_->IsInstanceLayerAvailable(layerName_);
}

bool DeviceExtensionCapability::CheckAvailability() const {
    return graph_ && graph_->IsDeviceExtensionAvailable(extensionName_);
}

bool DeviceFeatureCapability::CheckAvailability() const {
    return graph_ && graph_->IsDeviceFeatureAvailable(featureName_);
}

bool BackgroundGpuCapability::CheckAvailability() const {
    return graph_ && graph_->HasBackgroundGpu();
}

//==============================================================================
// CapabilityGraph
//==============================================================================

void CapabilityGraph::RegisterCapability(std::shared_ptr<CapabilityNode> capability) {
    capability->SetOwningGraph(this);  // AR#8: node consults this graph's availability sets
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

void CapabilityGraph::SetAvailableInstanceExtensions(std::vector<std::string> extensions) {
    availableInstanceExtensions_ = std::move(extensions);
}

void CapabilityGraph::SetAvailableInstanceLayers(std::vector<std::string> layers) {
    availableInstanceLayers_ = std::move(layers);
}

void CapabilityGraph::SetAvailableDeviceExtensions(std::vector<std::string> extensions) {
    availableDeviceExtensions_ = std::move(extensions);
}

void CapabilityGraph::SetAvailableDeviceFeatures(std::vector<std::string> features) {
    availableDeviceFeatures_ = std::move(features);
}

bool CapabilityGraph::IsInstanceExtensionAvailable(const std::string& name) const {
    return Contains(availableInstanceExtensions_, name);
}

bool CapabilityGraph::IsInstanceLayerAvailable(const std::string& name) const {
    return Contains(availableInstanceLayers_, name);
}

bool CapabilityGraph::IsDeviceExtensionAvailable(const std::string& name) const {
    return Contains(availableDeviceExtensions_, name);
}

bool CapabilityGraph::IsDeviceFeatureAvailable(const std::string& name) const {
    return Contains(availableDeviceFeatures_, name);
}

PhysicalDeviceClass CapabilityGraph::ClassifyPhysicalDevice(VkPhysicalDeviceType type) noexcept {
    switch (type) {
        case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: return PhysicalDeviceClass::Integrated;
        case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:   return PhysicalDeviceClass::Discrete;
        case VK_PHYSICAL_DEVICE_TYPE_CPU:
        case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:
        case VK_PHYSICAL_DEVICE_TYPE_OTHER:         return PhysicalDeviceClass::Other;
        default:                                     return PhysicalDeviceClass::Unknown;
    }
}

std::optional<BackgroundGpuSelection> CapabilityGraph::SelectBackgroundGpu(
    const std::vector<PhysicalDeviceInfo>& devices) {
    const PhysicalDeviceInfo* best = nullptr;
    auto rank = [](PhysicalDeviceClass c) {
        // Integrated first: shell data is host-resident, so a unified-memory
        // adapter avoids a second copy across the PCIe boundary. Discrete is
        // the fallback when no integrated candidate is visible.
        switch (c) {
            case PhysicalDeviceClass::Integrated: return 0;
            case PhysicalDeviceClass::Discrete:   return 1;
            case PhysicalDeviceClass::Other:      return 2;
            default:                              return 3;
        }
    };
    for (const auto& device : devices) {
        if (device.classification == PhysicalDeviceClass::Unknown) continue;
        if (best == nullptr || rank(device.classification) < rank(best->classification) ||
            (rank(device.classification) == rank(best->classification) &&
             (device.deviceLocalBytes < best->deviceLocalBytes ||
              (device.deviceLocalBytes == best->deviceLocalBytes && device.index < best->index)))) {
            best = &device;
        }
    }
    if (best == nullptr) return std::nullopt;
    return BackgroundGpuSelection{best->index, best->classification};
}

void CapabilityGraph::EnumeratePhysicalDevices(VkInstance instance, VkPhysicalDevice primary) {
    physicalDevices_.clear();
    backgroundGpuSelection_.reset();
    if (instance == VK_NULL_HANDLE) {
        InvalidateAll();
        return;
    }

    uint32_t count = 0;
    if (vkEnumeratePhysicalDevices(instance, &count, nullptr) != VK_SUCCESS || count == 0u) {
        InvalidateAll();
        return;
    }
    std::vector<VkPhysicalDevice> handles(count);
    if (vkEnumeratePhysicalDevices(instance, &count, handles.data()) != VK_SUCCESS) {
        InvalidateAll();
        return;
    }

    for (uint32_t i = 0; i < count; ++i) {
        if (primary != VK_NULL_HANDLE && handles[i] == primary) continue;
        VkPhysicalDeviceProperties properties{};
        VkPhysicalDeviceMemoryProperties memory{};
        vkGetPhysicalDeviceProperties(handles[i], &properties);
        vkGetPhysicalDeviceMemoryProperties(handles[i], &memory);
        uint64_t localBytes = 0;
        for (uint32_t heap = 0; heap < memory.memoryHeapCount; ++heap) {
            if ((memory.memoryHeaps[heap].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) != 0u)
                localBytes += memory.memoryHeaps[heap].size;
        }
        physicalDevices_.push_back(PhysicalDeviceInfo{
            i, handles[i], properties.deviceType, ClassifyPhysicalDevice(properties.deviceType),
            localBytes, properties.deviceName});
    }
    backgroundGpuSelection_ = SelectBackgroundGpu(physicalDevices_);
    InvalidateAll();
}

void CapabilityGraph::BuildStandardCapabilities() {
    // AR#8: self-populate instance-level availability from the loader (globally queryable, no
    // VkInstance needed). Device-level sets are filled in later by the owning VulkanDevice.
    SetAvailableInstanceExtensions(EnumerateAvailableInstanceExtensions());
    SetAvailableInstanceLayers(EnumerateAvailableInstanceLayers());

    //==========================================================================
    // Base Device Extensions
    //==========================================================================

    auto swapchain = CreateCapability<DeviceExtensionCapability>(
        "DeviceExt:VK_KHR_swapchain", VK_KHR_SWAPCHAIN_EXTENSION_NAME);

    auto maintenance1 = CreateCapability<DeviceExtensionCapability>(
        "DeviceExt:VK_KHR_maintenance1", VK_KHR_MAINTENANCE_1_EXTENSION_NAME);

    // VK_EXT_swapchain_maintenance1 - specific extension for present fences and scaling
    auto swapchainMaintenance1Ext = CreateCapability<DeviceExtensionCapability>(
        "DeviceExt:VK_EXT_swapchain_maintenance1", VK_EXT_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME);

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
    // Device Features (non-concrete, queried via vkGetPhysicalDeviceFeatures2)
    //==========================================================================

    // timelineSemaphore (core Vulkan 1.2). Used by the BatchedUploader for timeline-based
    // upload synchronisation; gated through the graph so enablement only happens when the
    // physical device reports support (CapabilityGraph::SetAvailableDeviceFeatures).
    auto timelineSemaphore = CreateCapability<DeviceFeatureCapability>(
        "DeviceFeature:timelineSemaphore", "timelineSemaphore");

    // hostQueryReset (core Vulkan 1.2). Lets GPUTimestampQuery reset its per-frame timestamp query
    // pools on the host (vkResetQueryPool) at creation, so the first vkGetQueryPoolResults reads an
    // initialised pool instead of an unreset one (the VUID-vkGetQueryPoolResults-None-09401 startup
    // burst). Gated through the graph; enablement lives in VulkanDevice. Optional — the query code
    // falls back to GPU-side resets when unsupported.
    auto hostQueryReset = CreateCapability<DeviceFeatureCapability>(
        "DeviceFeature:hostQueryReset", "hostQueryReset");

    // synchronization2 (core Vulkan 1.3). REQUIRED: the renderer records all GPU barriers via
    // vkCmdPipelineBarrier2 (ComputeDispatchNode, MultiDispatchNode); without it every barrier2
    // call fails validation (VUID-vkCmdPipelineBarrier2-synchronization2-03848). Gated through the
    // graph like timelineSemaphore; enablement (and a hard-error-if-missing) lives in VulkanDevice.
    auto synchronization2 = CreateCapability<DeviceFeatureCapability>(
        "DeviceFeature:synchronization2", "synchronization2");

    // fragmentStoresAndAtomics (core Vulkan 1.0). B2's preferred proxy writer uses
    // fragment-shader SSBO atomics. The compute-writer twin is the capability-free
    // fallback, so this remains optional and is selected at runtime through the graph.
    auto fragmentStoresAndAtomics = CreateCapability<DeviceFeatureCapability>(
        "DeviceFeature:fragmentStoresAndAtomics", "fragmentStoresAndAtomics");

    //==========================================================================
    // Instance Extensions
    //==========================================================================

    auto surfaceExt = CreateCapability<InstanceExtensionCapability>(
        "InstanceExt:VK_KHR_surface", VK_KHR_SURFACE_EXTENSION_NAME);

    // Cross-platform surface extensions: GLFW reports exactly the instance extensions the current
    // platform needs to present (VK_KHR_surface + the platform surface, e.g. win32/xlib/wayland).
    // This replaces the hardcoded VK_KHR_WIN32_SURFACE / VK_USE_PLATFORM_WIN32_KHR logic.
    std::vector<std::shared_ptr<CapabilityNode>> platformSurfaceExts;
    {
        glfwInit();  // idempotent; required before glfwGetRequiredInstanceExtensions
        uint32_t glfwExtCount = 0;
        const char** glfwExts = glfwGetRequiredInstanceExtensions(&glfwExtCount);
        for (uint32_t i = 0; glfwExts && i < glfwExtCount; ++i) {
            // VK_KHR_surface is already registered above; register only the platform-specific ones.
            if (std::strcmp(glfwExts[i], VK_KHR_SURFACE_EXTENSION_NAME) == 0) {
                continue;
            }
            platformSurfaceExts.push_back(CreateCapability<InstanceExtensionCapability>(
                std::string("InstanceExt:") + glfwExts[i], glfwExts[i]));
        }
    }

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

    // SwapchainMaintenance1 - the VK_EXT_swapchain_maintenance1 extension for present fences
    // Note: This is VK_EXT_swapchain_maintenance1, NOT VK_KHR_maintenance1
    // The extension provides present fences via VkSwapchainPresentFenceInfoEXT
    auto swapchainMaint1 = std::make_shared<CompositeCapability>("SwapchainMaintenance1");
    swapchainMaint1->AddDependency(swapchain);
    swapchainMaint1->AddDependency(swapchainMaintenance1Ext);  // Correct: VK_EXT_swapchain_maintenance1
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
    for (auto& platformSurfaceExt : platformSurfaceExts) {
        basicRendering->AddDependency(platformSurfaceExt);
    }
    RegisterCapability(basicRendering);

    // Validation Support (validation layer + debug utils)
    auto validationSupport = std::make_shared<CompositeCapability>("ValidationSupport");
    validationSupport->AddDependency(validationLayer);
    validationSupport->AddDependency(debugUtils);
    RegisterCapability(validationSupport);

    // General BackgroundGpu lane capability. Selection is populated after the
    // instance and primary device are known; registration is deliberately
    // behavior-free until a later consumer opts in.
    RegisterCapability(std::make_shared<BackgroundGpuCapability>());
}

} // namespace Vixen
