#include "CapabilityGraph.h"
#include "VulkanCapabilityConfig.h"
#ifndef VIXEN_CAPABILITY_CONFIGURE_PROBE
#include "CapabilityGraphBuildConfig.h"
#endif
#include <algorithm>
#include <charconv>
#include <limits>
#include <cstring>
#include <cctype>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <type_traits>

#ifndef VIXEN_CAPABILITY_CONFIGURE_PROBE
#define GLFW_INCLUDE_NONE   // don't pull in <GL/gl.h> (absent on headless/WSL); Vulkan-only below
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#endif

namespace Vixen {

namespace {

bool Contains(const std::vector<std::string>& haystack, const std::string& needle) {
    return std::find(haystack.begin(), haystack.end(), needle) != haystack.end();
}

std::string EscapeCppString(const std::string& value) {
    std::string escaped;
    escaped.reserve(value.size());
    for (const char ch : value) {
        switch (ch) {
        case '\\': escaped += "\\\\"; break;
        case '"': escaped += "\\\""; break;
        case '\n': escaped += "\\n"; break;
        case '\r': escaped += "\\r"; break;
        case '\t': escaped += "\\t"; break;
        default: escaped += ch; break;
        }
    }
    return escaped;
}

std::string CppIdentifier(const std::string& value) {
    std::string identifier;
    identifier.reserve(value.size());
    for (const unsigned char ch : value) {
        identifier += std::isalnum(ch) ? static_cast<char>(ch) : '_';
    }
    return identifier;
}

template<size_t N>
void EmitByteArray(std::ostream& output, const std::array<uint8_t, N>& bytes) {
    output << "{{";
    for (size_t i = 0; i < bytes.size(); ++i) {
        if (i != 0) output << ", ";
        output << "0x" << std::hex << static_cast<unsigned int>(bytes[i]) << std::dec;
    }
    output << "}}";
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

std::optional<CapabilityVersionValue> CapabilityVersionValue::Parse(const std::string& value) {
    CapabilityVersionValue parsed{};
    std::array<uint32_t*, 4> fields{&parsed.major, &parsed.minor, &parsed.patch, &parsed.revision};
    size_t start = 0;
    size_t count = 0;
    while (start <= value.size() && count < fields.size()) {
        const size_t end = value.find('.', start);
        const size_t tokenEnd = end == std::string::npos ? value.size() : end;
        if (tokenEnd == start) return std::nullopt;
        const char* first = value.data() + start;
        const char* last = value.data() + tokenEnd;
        const auto result = std::from_chars(first, last, *fields[count]);
        if (result.ec != std::errc{} || result.ptr != last) return std::nullopt;
        ++count;
        if (end == std::string::npos) {
            start = value.size() + 1;
            break;
        }
        start = end + 1;
    }
    if (start <= value.size() || count < 2) return std::nullopt;
    return parsed;
}

CapabilityVersionValue CapabilityVersionValue::FromVulkanApi(uint32_t value) noexcept {
    return {
        VK_API_VERSION_MAJOR(value),
        VK_API_VERSION_MINOR(value),
        VK_API_VERSION_PATCH(value),
        0u,
    };
}

std::string CapabilityVersionValue::ToString() const {
    return std::to_string(major) + "." + std::to_string(minor) + "." +
           std::to_string(patch) + "." + std::to_string(revision);
}

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
    return graph_ && graph_->IsDeviceFeatureAvailable(featureName_) && AreDependenciesSatisfied();
}

bool VersionInputCapability::CheckAvailability() const {
    return graph_ && graph_->GetVersion(source_).has_value();
}

bool VersionRequirementCapability::CheckAvailability() const {
    if (!graph_ || !AreDependenciesSatisfied()) return false;
    const auto version = graph_->GetVersion(source_);
    return version.has_value() && *version >= minimum_;
}

bool PromotedDeviceFeatureCapability::CheckAvailability() const {
    if (!graph_ || !graph_->IsDeviceFeatureAvailable(featureName_) ||
        !AreDependenciesSatisfied()) {
        return false;
    }
    const auto apiVersion = graph_->GetVersion(CapabilityVersionSource::VulkanApi);
    if (!apiVersion) return false;
    return *apiVersion >= coreVersion_ || graph_->IsDeviceExtensionAvailable(extensionName_);
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

void CapabilityGraph::RegisterBuildFact(const std::string& name, CapabilityBuildFact value,
                                        CapabilityBindingTime bindingTime) {
    RegisterCapability(std::make_shared<BuildFactCapability>(name, std::move(value), bindingTime));
}

void CapabilityGraph::SetAllBindingTimes(CapabilityBindingTime bindingTime) noexcept {
    for (auto& [name, capability] : capabilities_) {
        (void)name;
        capability->SetBindingTime(bindingTime);
    }
}

std::vector<std::string> CapabilityGraph::QuerySupportedDeviceFeatures(
    VkPhysicalDevice physicalDevice) const {
    if (physicalDevice == VK_NULL_HANDLE) return {};

    std::vector<std::string> supported;
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(physicalDevice, &properties);

    VkPhysicalDeviceVulkan12Features vulkan12{};
    vulkan12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    VkPhysicalDeviceVulkan13Features vulkan13{};
    vulkan13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    VkPhysicalDeviceSynchronization2FeaturesKHR synchronization2{};
    synchronization2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES_KHR;
    VkPhysicalDeviceFeatures2 features2{};
    features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;

    const bool apiSupportsVulkan12 = properties.apiVersion >= VK_API_VERSION_1_2;
    const bool apiSupportsSynchronization2Core = properties.apiVersion >= VK_API_VERSION_1_3;
    const bool extensionSupportsSynchronization2 =
        !apiSupportsSynchronization2Core && IsDeviceExtensionAvailable(
            VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME);
    if (apiSupportsVulkan12) {
        features2.pNext = &vulkan12;
        if (apiSupportsSynchronization2Core) {
            vulkan12.pNext = &vulkan13;
        } else if (extensionSupportsSynchronization2) {
            vulkan12.pNext = &synchronization2;
        }
    }

    VkPhysicalDeviceSubgroupProperties subgroup{};
    subgroup.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES;
    VkPhysicalDeviceProperties2 properties2{};
    properties2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
    properties2.pNext = &subgroup;
    vkGetPhysicalDeviceProperties2(physicalDevice, &properties2);
    vkGetPhysicalDeviceFeatures2(physicalDevice, &features2);

    if (apiSupportsVulkan12 && vulkan12.timelineSemaphore) {
        supported.emplace_back("timelineSemaphore");
    }
    if (apiSupportsVulkan12 && vulkan12.hostQueryReset) {
        supported.emplace_back("hostQueryReset");
    }
    if ((apiSupportsSynchronization2Core && vulkan13.synchronization2) ||
        (extensionSupportsSynchronization2 && synchronization2.synchronization2)) {
        supported.emplace_back("synchronization2");
    }
    if (features2.features.fragmentStoresAndAtomics) {
        supported.emplace_back("fragmentStoresAndAtomics");
    }
    if ((subgroup.supportedOperations & VK_SUBGROUP_FEATURE_BALLOT_BIT) != 0) {
        supported.emplace_back("subgroupComputeBallot");
    }
    if ((subgroup.supportedOperations & VK_SUBGROUP_FEATURE_ARITHMETIC_BIT) != 0) {
        supported.emplace_back("subgroupComputeArithmetic");
    }
    if ((subgroup.supportedOperations & VK_SUBGROUP_FEATURE_SHUFFLE_BIT) != 0) {
        supported.emplace_back("subgroupComputeShuffle");
    }
    return supported;
}

void CapabilityGraph::ObservePhysicalDevice(VkPhysicalDevice physicalDevice) {
    if (physicalDevice == VK_NULL_HANDLE) {
        return;
    }

    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(physicalDevice, &properties);
    SetVersion(CapabilityVersionSource::VulkanApi,
               CapabilityVersionValue::FromVulkanApi(properties.apiVersion));

    uint32_t extensionCount = 0;
    if (vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &extensionCount, nullptr) != VK_SUCCESS) {
        throw std::runtime_error("CapabilityGraph could not enumerate device extension count");
    }
    std::vector<VkExtensionProperties> extensionProperties(extensionCount);
    if (extensionCount != 0 && vkEnumerateDeviceExtensionProperties(
            physicalDevice, nullptr, &extensionCount, extensionProperties.data()) != VK_SUCCESS) {
        throw std::runtime_error("CapabilityGraph could not enumerate device extensions");
    }
    std::vector<std::string> extensions;
    extensions.reserve(extensionProperties.size());
    for (const auto& extension : extensionProperties) {
        extensions.emplace_back(extension.extensionName);
    }
    SetAvailableDeviceExtensions(std::move(extensions));
    SetAvailableDeviceFeatures(QuerySupportedDeviceFeatures(physicalDevice));

    VkPhysicalDeviceDriverProperties driverProperties{};
    driverProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES;
    VkPhysicalDeviceIDProperties idProperties{};
    idProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES;
    idProperties.pNext = &driverProperties;
    VkPhysicalDeviceProperties2 properties2{};
    properties2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
    properties2.pNext = &idProperties;
    vkGetPhysicalDeviceProperties2(physicalDevice, &properties2);

    DeviceDriverIdentityBuildFact identity{};
    identity.deviceName = properties2.properties.deviceName;
    identity.driverName = driverProperties.driverName;
    identity.driverInfo = driverProperties.driverInfo;
    identity.vendorId = properties2.properties.vendorID;
    identity.deviceId = properties2.properties.deviceID;
    identity.driverVersion = properties2.properties.driverVersion;
    identity.driverId = static_cast<uint32_t>(driverProperties.driverID);
    std::copy(std::begin(idProperties.deviceUUID), std::end(idProperties.deviceUUID),
              identity.deviceUuid.begin());
    std::copy(std::begin(idProperties.driverUUID), std::end(idProperties.driverUUID),
              identity.driverUuid.begin());
    RegisterBuildFact("BuildFact:ObservedDeviceDriverIdentity", std::move(identity));

    uint32_t queueCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueCount, nullptr);
    std::vector<VkQueueFamilyProperties> queueProperties(queueCount);
    if (queueCount != 0) {
        vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueCount, queueProperties.data());
    }
    bool graphicsFamilyRecorded = false;
    for (uint32_t i = 0; i < queueCount; ++i) {
        const auto& queue = queueProperties[i];
        RegisterBuildFact("BuildFact:QueueFamily:" + std::to_string(i),
                          QueueFamilyBuildFact{"family", i, queue.queueFlags, queue.queueCount});
        if (!graphicsFamilyRecorded && (queue.queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0) {
            RegisterBuildFact("BuildFact:QueueFamily:graphics",
                              QueueFamilyBuildFact{"graphics", i, queue.queueFlags, queue.queueCount});
            graphicsFamilyRecorded = true;
        }
    }
    InvalidateAll();
}

void CapabilityGraph::GenerateBuildConfigHeader(const std::string& path, bool personalized) const {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        throw std::runtime_error("CapabilityGraph could not write generated build config: " + path);
    }

    std::vector<std::pair<std::string, std::shared_ptr<CapabilityNode>>> nodes;
    nodes.reserve(capabilities_.size());
    for (const auto& entry : capabilities_) nodes.push_back(entry);
    std::sort(nodes.begin(), nodes.end(), [](const auto& left, const auto& right) {
        return left.first < right.first;
    });

    const PlatformBuildFact* platform = nullptr;
    const ProductVariantBuildFact* productVariant = nullptr;
    const DeviceDriverIdentityBuildFact* identity = nullptr;
    std::vector<QueueFamilyBuildFact> queues;
    for (const auto& [name, node] : nodes) {
        const auto factNode = std::dynamic_pointer_cast<BuildFactCapability>(node);
        if (!factNode) continue;
        std::visit([&](const auto& fact) {
            using T = std::decay_t<decltype(fact)>;
            if constexpr (std::is_same_v<T, PlatformBuildFact>) platform = &fact;
            else if constexpr (std::is_same_v<T, ProductVariantBuildFact>) productVariant = &fact;
            else if constexpr (std::is_same_v<T, DeviceDriverIdentityBuildFact>) {
                if (name == "BuildFact:ObservedDeviceDriverIdentity") identity = &fact;
            } else if constexpr (std::is_same_v<T, QueueFamilyBuildFact>) {
                if (name.rfind("BuildFact:QueueFamily:", 0) == 0) queues.push_back(fact);
            }
        }, factNode->GetValue());
    }
    std::sort(queues.begin(), queues.end(), [](const auto& left, const auto& right) {
        const bool leftIsGraphics = left.role == "graphics";
        const bool rightIsGraphics = right.role == "graphics";
        if (leftIsGraphics != rightIsGraphics) return leftIsGraphics;
        return left.index < right.index;
    });

    output << "#pragma once\n#include <vulkan/vulkan.h>\n#include <array>\n#include <cstdint>\n#include <string_view>\n\n"
              "namespace Vixen::BuildCapabilities {\n"
           << "inline constexpr bool kPersonalizedMode = " << (personalized ? "true" : "false") << ";\n"
           << "inline constexpr bool kFullMode = " << (personalized ? "false" : "true") << ";\n"
           << "inline constexpr std::string_view kPlatform = \""
           << (platform ? EscapeCppString(platform->systemName) : "") << "\";\n"
           << "inline constexpr std::string_view kArchitecture = \""
           << (platform ? EscapeCppString(platform->architecture) : "") << "\";\n"
           << "inline constexpr std::string_view kCompilerId = \""
           << (platform ? EscapeCppString(platform->compilerId) : "") << "\";\n"
           << "inline constexpr std::string_view kCompilerVersion = \""
           << (platform ? EscapeCppString(platform->compilerVersion) : "") << "\";\n"
           << "inline constexpr std::string_view kProductVariant = \""
           << (productVariant ? EscapeCppString(productVariant->variant) : "") << "\";\n"
           << "struct QueueFamilyValue { std::string_view role; uint32_t index; uint32_t flags; uint32_t queueCount; };\n"
           << "inline constexpr std::array<QueueFamilyValue, " << queues.size() << "> kQueueFamilies{{\n";
    for (const auto& queue : queues) {
        output << "    {\"" << EscapeCppString(queue.role) << "\", " << queue.index << "u, "
               << queue.flags << "u, " << queue.queueCount << "u},\n";
    }
    output << "}};\n";

    const auto graphics = std::find_if(queues.begin(), queues.end(), [](const auto& queue) {
        return queue.role == "graphics";
    });
    output << "inline constexpr uint32_t kGraphicsQueueFamily = "
           << (graphics == queues.end() ? "UINT32_MAX" : std::to_string(graphics->index) + "u") << ";\n"
           << "inline constexpr bool kHasDeviceIdentity = " << (identity ? "true" : "false") << ";\n"
           << "inline constexpr std::string_view kDeviceName = \""
           << (identity ? EscapeCppString(identity->deviceName) : "") << "\";\n"
           << "inline constexpr std::string_view kDriverName = \""
           << (identity ? EscapeCppString(identity->driverName) : "") << "\";\n"
           << "inline constexpr std::string_view kDriverInfo = \""
           << (identity ? EscapeCppString(identity->driverInfo) : "") << "\";\n"
           << "inline constexpr uint32_t kVendorId = " << (identity ? identity->vendorId : 0u) << "u;\n"
           << "inline constexpr uint32_t kDeviceId = " << (identity ? identity->deviceId : 0u) << "u;\n"
           << "inline constexpr uint32_t kDriverVersion = " << (identity ? identity->driverVersion : 0u) << "u;\n"
           << "inline constexpr uint32_t kDriverId = " << (identity ? identity->driverId : 0u) << "u;\n"
           << "inline constexpr std::array<uint8_t, VK_UUID_SIZE> kDeviceUuid = ";
    if (identity) EmitByteArray(output, identity->deviceUuid);
    else output << "{}";
    output << ";\ninline constexpr std::array<uint8_t, VK_UUID_SIZE> kDriverUuid = ";
    if (identity) EmitByteArray(output, identity->driverUuid);
    else output << "{}";
    output << ";\n";

    for (const auto& [name, node] : nodes) {
        const bool available = personalized && node->GetBindingTime() == CapabilityBindingTime::BuildTime &&
                               node->IsAvailable();
        const bool buildTime = personalized &&
                               node->GetBindingTime() == CapabilityBindingTime::BuildTime;
        output << "inline constexpr bool kBuildTime_" << CppIdentifier(name) << " = "
               << (buildTime ? "true" : "false") << ";\n"
               << "inline constexpr bool kCapability_" << CppIdentifier(name) << " = "
               << (available ? "true" : "false") << ";\n";
    }
    output << "} // namespace Vixen::BuildCapabilities\n";
    if (!output) {
        throw std::runtime_error("CapabilityGraph failed while writing generated build config: " + path);
    }
}

bool CapabilityGraph::MatchesConfiguredDeviceIdentity(
    const DeviceDriverIdentityBuildFact& actual, std::string* mismatchReason) {
#ifndef VIXEN_CAPABILITY_CONFIGURE_PROBE
    if constexpr (!BuildCapabilities::kPersonalizedMode || !BuildCapabilities::kHasDeviceIdentity) {
        return true;
    } else {
        const bool matches =
            actual.vendorId == BuildCapabilities::kVendorId &&
            actual.deviceId == BuildCapabilities::kDeviceId &&
            actual.driverVersion == BuildCapabilities::kDriverVersion &&
            actual.driverId == BuildCapabilities::kDriverId &&
            std::equal(actual.deviceUuid.begin(), actual.deviceUuid.end(),
                       BuildCapabilities::kDeviceUuid.begin()) &&
            std::equal(actual.driverUuid.begin(), actual.driverUuid.end(),
                       BuildCapabilities::kDriverUuid.begin());
        if (!matches && mismatchReason) {
            std::ostringstream reason;
            reason << "expected " << BuildCapabilities::kDeviceName << " / driver "
                   << BuildCapabilities::kDriverName << " " << BuildCapabilities::kDriverVersion
                   << ", detected " << actual.deviceName << " / driver "
                   << actual.driverName << " " << actual.driverVersion;
            *mismatchReason = reason.str();
        }
        return matches;
    }
#else
    (void)actual;
    if (mismatchReason) *mismatchReason = "configured identity is unavailable in the configure probe";
    return false;
#endif
}

bool CapabilityGraph::MatchesConfiguredPhysicalDevice(VkPhysicalDevice physicalDevice,
                                                       std::string* mismatchReason) {
#ifndef VIXEN_CAPABILITY_CONFIGURE_PROBE
    if constexpr (!BuildCapabilities::kPersonalizedMode || !BuildCapabilities::kHasDeviceIdentity) {
        return true;
    } else {
        if (physicalDevice == VK_NULL_HANDLE) {
            if (mismatchReason) *mismatchReason = "no Vulkan physical device was selected";
            return false;
        }
        VkPhysicalDeviceDriverProperties driverProperties{};
        driverProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES;
        VkPhysicalDeviceIDProperties idProperties{};
        idProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES;
        idProperties.pNext = &driverProperties;
        VkPhysicalDeviceProperties2 properties2{};
        properties2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
        properties2.pNext = &idProperties;
        vkGetPhysicalDeviceProperties2(physicalDevice, &properties2);

        DeviceDriverIdentityBuildFact actual{};
        actual.deviceName = properties2.properties.deviceName;
        actual.driverName = driverProperties.driverName;
        actual.driverInfo = driverProperties.driverInfo;
        actual.vendorId = properties2.properties.vendorID;
        actual.deviceId = properties2.properties.deviceID;
        actual.driverVersion = properties2.properties.driverVersion;
        actual.driverId = static_cast<uint32_t>(driverProperties.driverID);
        std::copy(std::begin(idProperties.deviceUUID), std::end(idProperties.deviceUUID),
                  actual.deviceUuid.begin());
        std::copy(std::begin(idProperties.driverUUID), std::end(idProperties.driverUUID),
                  actual.driverUuid.begin());
        return MatchesConfiguredDeviceIdentity(actual, mismatchReason);
    }
#else
    (void)physicalDevice;
    if (mismatchReason) *mismatchReason = "configured identity is unavailable in the configure probe";
    return false;
#endif
}

bool CapabilityGraph::IsConfiguredGraphicsQueueFamily(uint32_t queueFamily) noexcept {
#ifndef VIXEN_CAPABILITY_CONFIGURE_PROBE
    return !BuildCapabilities::kPersonalizedMode ||
           queueFamily == BuildCapabilities::kGraphicsQueueFamily;
#else
    (void)queueFamily;
    return true;
#endif
}

std::shared_ptr<CapabilityNode> CapabilityGraph::GetCapability(const std::string& name) const {
    auto it = capabilities_.find(name);
    return (it != capabilities_.end()) ? it->second : nullptr;
}

bool CapabilityGraph::IsCapabilityAvailable(const std::string& name) const {
    auto cap = GetCapability(name);
    return cap && cap->IsAvailable();
}

CapabilityPath CapabilityGraph::ResolveOptionalPath(
    const std::string& capabilityName, bool requested) const {
    if (!requested || !IsCapabilityAvailable(capabilityName)) {
        return CapabilityPath::CapabilityIndependent;
    }
    return CapabilityPath::CapabilityEnabled;
}

void CapabilityGraph::InvalidateAll() {
    for (auto& [name, cap] : capabilities_) {
        cap->Invalidate();
    }
}

void CapabilityGraph::SetAvailableInstanceExtensions(std::vector<std::string> extensions) {
    availableInstanceExtensions_ = std::move(extensions);
    InvalidateAll();
}

void CapabilityGraph::SetAvailableInstanceLayers(std::vector<std::string> layers) {
    availableInstanceLayers_ = std::move(layers);
    InvalidateAll();
}

void CapabilityGraph::SetAvailableDeviceExtensions(std::vector<std::string> extensions) {
    availableDeviceExtensions_ = std::move(extensions);
    InvalidateAll();
}

void CapabilityGraph::SetAvailableDeviceFeatures(std::vector<std::string> features) {
    availableDeviceFeatures_ = std::move(features);
    InvalidateAll();
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

void CapabilityGraph::SetVersion(CapabilityVersionSource source, CapabilityVersionValue version) {
    const auto index = static_cast<size_t>(source);
    if (index >= versions_.size()) return;
    versions_[index] = version;
    InvalidateAll();
}

std::optional<CapabilityVersionValue> CapabilityGraph::GetVersion(
    CapabilityVersionSource source) const {
    const auto index = static_cast<size_t>(source);
    if (index >= versions_.size()) return std::nullopt;
    return versions_[index];
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
    // The build records the provisioned SDK and shader compiler versions in the generated config
    // header. The Vulkan API version remains a per-device input set by VulkanDevice.
    if (const auto version = CapabilityVersionValue::Parse(VIXEN_CONFIGURED_VULKAN_SDK_VERSION)) {
        SetVersion(CapabilityVersionSource::VulkanSdk, *version);
    }
    if (const auto version = CapabilityVersionValue::Parse(VIXEN_CONFIGURED_GLSLANG_VERSION)) {
        SetVersion(CapabilityVersionSource::Glslang, *version);
    }
    if (const auto version = CapabilityVersionValue::Parse(VIXEN_CONFIGURED_SPIRV_TARGET_VERSION)) {
        SetVersion(CapabilityVersionSource::SpirvTarget, *version);
    }

    // AR#8: self-populate instance-level availability from the loader (globally queryable, no
    // VkInstance needed). Device-level sets are filled in later by the owning VulkanDevice.
    SetAvailableInstanceExtensions(EnumerateAvailableInstanceExtensions());
    SetAvailableInstanceLayers(EnumerateAvailableInstanceLayers());

    auto vulkanApiVersion = CreateCapability<VersionInputCapability>(
        "Version:VulkanApi", "Version:VulkanApi", CapabilityVersionSource::VulkanApi);
    auto vulkanSdkVersion = CreateCapability<VersionInputCapability>(
        "Version:VulkanSdk", "Version:VulkanSdk", CapabilityVersionSource::VulkanSdk);
    auto glslangVersion = CreateCapability<VersionInputCapability>(
        "Version:Glslang", "Version:Glslang", CapabilityVersionSource::Glslang);
    auto spirvTargetVersion = CreateCapability<VersionInputCapability>(
        "Version:SpirvTarget", "Version:SpirvTarget", CapabilityVersionSource::SpirvTarget);

    const auto minimumApi = CapabilityVersionValue::Parse(VIXEN_MINIMUM_VULKAN_API_VERSION)
                                .value_or(CapabilityVersionValue{1u, 2u, 0u, 0u});
    auto vulkanApiRequirement = CreateCapability<VersionRequirementCapability>(
        "VulkanApi:RequiredFloor", "VulkanApi:RequiredFloor", CapabilityVersionSource::VulkanApi,
        minimumApi);
    vulkanApiRequirement->AddDependency(vulkanApiVersion);

    const auto minimumSpirv = CapabilityVersionValue::Parse(VIXEN_MINIMUM_SPIRV_TARGET_VERSION)
                                  .value_or(CapabilityVersionValue{1u, 4u, 0u, 0u});
    auto spirvRequirement = CreateCapability<VersionRequirementCapability>(
        "SpirvTarget:RayQueryFloor", "SpirvTarget:RayQueryFloor",
        CapabilityVersionSource::SpirvTarget, minimumSpirv);
    spirvRequirement->AddDependency(spirvTargetVersion);
    spirvRequirement->AddDependency(vulkanSdkVersion);
    spirvRequirement->AddDependency(glslangVersion);

    // The SPIR-V target requirement depends on a known SDK and glslang input as well as the target
    // itself, keeping this optional shader route tied to the configured toolchain.

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

    auto spirv14 = CreateCapability<DeviceExtensionCapability>(
        "DeviceExt:VK_KHR_spirv_1_4", VK_KHR_SPIRV_1_4_EXTENSION_NAME);

    auto shaderFloatControls = CreateCapability<DeviceExtensionCapability>(
        "DeviceExt:VK_KHR_shader_float_controls", VK_KHR_SHADER_FLOAT_CONTROLS_EXTENSION_NAME);

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
    const auto synchronization2CoreVersion =
        CapabilityVersionValue::Parse(VIXEN_SYNCHRONIZATION2_CORE_API_VERSION)
            .value_or(CapabilityVersionValue{1u, 3u, 0u, 0u});
    auto synchronization2 = CreateCapability<PromotedDeviceFeatureCapability>(
        "DeviceFeature:synchronization2", "DeviceFeature:synchronization2", "synchronization2",
        VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME, synchronization2CoreVersion);
    synchronization2->AddDependency(vulkanApiRequirement);

    // fragmentStoresAndAtomics (core Vulkan 1.0). B2's preferred proxy writer uses
    // fragment-shader SSBO atomics. The compute-writer twin is the capability-free
    // fallback, so this remains optional and is selected at runtime through the graph.
    auto fragmentStoresAndAtomics = CreateCapability<DeviceFeatureCapability>(
        "DeviceFeature:fragmentStoresAndAtomics", "fragmentStoresAndAtomics");

    // Subgroup properties are reported by VkPhysicalDeviceSubgroupProperties rather than
    // VkPhysicalDeviceFeatures2. They still belong in the same availability set so consumers
    // have one graph-owned answer for optional device behavior (rtperf S0/e2).
    auto subgroupComputeBallot = CreateCapability<DeviceFeatureCapability>(
        "DeviceFeature:subgroupComputeBallot", "subgroupComputeBallot");
    auto subgroupComputeArithmetic = CreateCapability<DeviceFeatureCapability>(
        "DeviceFeature:subgroupComputeArithmetic", "subgroupComputeArithmetic");
    auto subgroupComputeShuffle = CreateCapability<DeviceFeatureCapability>(
        "DeviceFeature:subgroupComputeShuffle", "subgroupComputeShuffle");

    //==========================================================================
    // Instance Extensions
    //==========================================================================

    auto surfaceExt = CreateCapability<InstanceExtensionCapability>(
        "InstanceExt:VK_KHR_surface", VK_KHR_SURFACE_EXTENSION_NAME);

    // Cross-platform surface extensions: GLFW reports exactly the instance extensions the current
    // platform needs to present (VK_KHR_surface + the platform surface, e.g. win32/xlib/wayland).
    // This replaces the hardcoded VK_KHR_WIN32_SURFACE / VK_USE_PLATFORM_WIN32_KHR logic.
    std::vector<std::shared_ptr<CapabilityNode>> platformSurfaceExts;
#ifndef VIXEN_CAPABILITY_CONFIGURE_PROBE
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
    rtxSupport->AddDependency(spirv14);
    rtxSupport->AddDependency(shaderFloatControls);
    rtxSupport->AddDependency(spirvRequirement);
    RegisterCapability(rtxSupport);

    // Tier-1 lighting ray queries intentionally do not depend on the RT-pipeline extension.
    // RTXSupport above remains the existing Tier-2 pipeline capability. The SPIR-V and shader
    // float-control extensions are also prerequisites because the selected shader bundle requests
    // them even though the lighting path does not require the full RT pipeline.
    auto rayQueryLighting = std::make_shared<CompositeCapability>("RayQueryLighting");
    rayQueryLighting->AddDependency(accelerationStructure);
    rayQueryLighting->AddDependency(rayQuery);
    rayQueryLighting->AddDependency(bufferDeviceAddress);
    rayQueryLighting->AddDependency(deferredHostOps);
    rayQueryLighting->AddDependency(spirv14);
    rayQueryLighting->AddDependency(shaderFloatControls);
    rayQueryLighting->AddDependency(spirvRequirement);
    RegisterCapability(rayQueryLighting);

    auto subgroupCoopTraversal = std::make_shared<CompositeCapability>("SubgroupCoopTraversal");
    subgroupCoopTraversal->AddDependency(subgroupComputeBallot);
    subgroupCoopTraversal->AddDependency(subgroupComputeArithmetic);
    subgroupCoopTraversal->AddDependency(subgroupComputeShuffle);
    RegisterCapability(subgroupCoopTraversal);

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

#ifndef VIXEN_CAPABILITY_CONFIGURE_PROBE
    RegisterBuildFact("BuildFact:ConfiguredPlatform",
        PlatformBuildFact{std::string(BuildCapabilities::kPlatform),
                          std::string(BuildCapabilities::kArchitecture),
                          std::string(BuildCapabilities::kCompilerId),
                          std::string(BuildCapabilities::kCompilerVersion)});
    RegisterBuildFact("BuildFact:ConfiguredProductVariant",
        ProductVariantBuildFact{std::string(BuildCapabilities::kProductVariant)});
    for (const auto& queue : BuildCapabilities::kQueueFamilies) {
        RegisterBuildFact("BuildFact:ConfiguredQueueFamily:" + std::string(queue.role),
                          QueueFamilyBuildFact{std::string(queue.role), queue.index,
                                               queue.flags, queue.queueCount});
    }
    if constexpr (BuildCapabilities::kHasDeviceIdentity) {
        DeviceDriverIdentityBuildFact expected{};
        expected.deviceName = BuildCapabilities::kDeviceName;
        expected.driverName = BuildCapabilities::kDriverName;
        expected.driverInfo = BuildCapabilities::kDriverInfo;
        expected.vendorId = BuildCapabilities::kVendorId;
        expected.deviceId = BuildCapabilities::kDeviceId;
        expected.driverVersion = BuildCapabilities::kDriverVersion;
        expected.driverId = BuildCapabilities::kDriverId;
        expected.deviceUuid = BuildCapabilities::kDeviceUuid;
        expected.driverUuid = BuildCapabilities::kDriverUuid;
        RegisterBuildFact("BuildFact:ConfiguredDeviceDriverIdentity", std::move(expected));
    }
    SetAllBindingTimes(BuildCapabilities::kPersonalizedMode
                           ? CapabilityBindingTime::BuildTime
                           : CapabilityBindingTime::Runtime);
#endif
}

} // namespace Vixen
