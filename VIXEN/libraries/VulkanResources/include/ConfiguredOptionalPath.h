#pragma once

#include "CapabilityGraph.h"
#include "CapabilityGraphBuildConfig.h"

#include <string_view>
#include <utility>

namespace Vixen {

/// Stable, typed keys for production optional-path decisions. The generated availability values
/// themselves come from the registered CapabilityGraph nodes.
enum class BuildCapabilityId : uint8_t {
    RayQueryLighting,
    RTXSupport,
    FragmentStoresAndAtomics,
    SwapchainMaintenance1,
    HostQueryReset,
};

template<BuildCapabilityId Id>
struct BuildCapabilityTraits;

template<>
struct BuildCapabilityTraits<BuildCapabilityId::RayQueryLighting> {
    static constexpr std::string_view nodeName = "RayQueryLighting";
    static constexpr bool buildTime = BuildCapabilities::kBuildTime_RayQueryLighting;
    static constexpr bool available = BuildCapabilities::kCapability_RayQueryLighting;
};

template<>
struct BuildCapabilityTraits<BuildCapabilityId::RTXSupport> {
    static constexpr std::string_view nodeName = "RTXSupport";
    static constexpr bool buildTime = BuildCapabilities::kBuildTime_RTXSupport;
    static constexpr bool available = BuildCapabilities::kCapability_RTXSupport;
};

template<>
struct BuildCapabilityTraits<BuildCapabilityId::FragmentStoresAndAtomics> {
    static constexpr std::string_view nodeName = "DeviceFeature:fragmentStoresAndAtomics";
    static constexpr bool buildTime =
        BuildCapabilities::kBuildTime_DeviceFeature_fragmentStoresAndAtomics;
    static constexpr bool available = BuildCapabilities::kCapability_DeviceFeature_fragmentStoresAndAtomics;
};

template<>
struct BuildCapabilityTraits<BuildCapabilityId::SwapchainMaintenance1> {
    static constexpr std::string_view nodeName = "SwapchainMaintenance1";
    static constexpr bool buildTime = BuildCapabilities::kBuildTime_SwapchainMaintenance1;
    static constexpr bool available = BuildCapabilities::kCapability_SwapchainMaintenance1;
};

template<>
struct BuildCapabilityTraits<BuildCapabilityId::HostQueryReset> {
    static constexpr std::string_view nodeName = "DeviceFeature:hostQueryReset";
    static constexpr bool buildTime = BuildCapabilities::kBuildTime_DeviceFeature_hostQueryReset;
    static constexpr bool available = BuildCapabilities::kCapability_DeviceFeature_hostQueryReset;
};

/// The if-constexpr branch binds a personalized build to configure-time graph values. Full mode
/// retains CapabilityGraph::ResolveOptionalPath, so every implementation remains compiled and
/// selected from runtime device observations.
template<BuildCapabilityId Id>
[[nodiscard]] inline CapabilityPath ResolveConfiguredOptionalPath(
    const CapabilityGraph& graph, bool requested = true) {
    if constexpr (BuildCapabilities::kPersonalizedMode && BuildCapabilityTraits<Id>::buildTime) {
        if constexpr (BuildCapabilityTraits<Id>::available) {
            return requested ? CapabilityPath::CapabilityEnabled
                             : CapabilityPath::CapabilityIndependent;
        } else {
            return CapabilityPath::CapabilityIndependent;
        }
    } else {
        return graph.ResolveOptionalPath(std::string(BuildCapabilityTraits<Id>::nodeName), requested);
    }
}

/// Invoke one implementation body through the configured capability decision. Personalized
/// builds instantiate only the enabled or independent callback selected by the generated value;
/// full builds keep both callbacks and choose at runtime through CapabilityGraph.
template<BuildCapabilityId Id, typename EnabledPath, typename IndependentPath>
decltype(auto) WithConfiguredOptionalPath(
    const CapabilityGraph& graph, bool requested,
    EnabledPath&& enabledPath, IndependentPath&& independentPath) {
    if constexpr (BuildCapabilities::kPersonalizedMode && BuildCapabilityTraits<Id>::buildTime) {
        if constexpr (BuildCapabilityTraits<Id>::available) {
            if (requested) {
                return std::forward<EnabledPath>(enabledPath)();
            }
            return std::forward<IndependentPath>(independentPath)();
        } else {
            return std::forward<IndependentPath>(independentPath)();
        }
    } else {
        if (graph.ResolveOptionalPath(std::string(BuildCapabilityTraits<Id>::nodeName), requested) ==
            CapabilityPath::CapabilityEnabled) {
            return std::forward<EnabledPath>(enabledPath)();
        }
        return std::forward<IndependentPath>(independentPath)();
    }
}

} // namespace Vixen
