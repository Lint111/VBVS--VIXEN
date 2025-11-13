#pragma once

#include <stdexcept>
#include <string>
#include <typeindex>
#include <memory>

namespace RenderGraph::NodeHelpers {

/// Registers a cacher with MainCacher if not already registered.
/// Returns the registered or existing cacher.
///
/// Usage:
///   auto* cacher = RegisterCacherIfNeeded<
///       CashSystem::RenderPassCacher,
///       CashSystem::RenderPassWrapper,
///       CashSystem::RenderPassCreateParams
///   >(graph, device, "RenderPass", /* isDeviceDependent */ true);
template <typename CacherType, typename WrapperType, typename ParamsType>
CacherType* RegisterCacherIfNeeded(
    auto* graph,
    auto device,
    const std::string& cacherName,
    bool isDeviceDependent = true
) {
    auto& mainCacher = graph->GetMainCacher();
    const auto wrapperTypeIndex = std::type_index(typeid(WrapperType));

    if (!mainCacher.IsRegistered(wrapperTypeIndex)) {
        mainCacher.RegisterCacher<CacherType, WrapperType, ParamsType>(
            wrapperTypeIndex,
            cacherName,
            isDeviceDependent
        );
    }

    auto* cacher = mainCacher.GetCacher<CacherType, WrapperType, ParamsType>(
        wrapperTypeIndex,
        device
    );

    if (!cacher) {
        throw std::runtime_error(
            "Failed to get " + cacherName + " cacher from MainCacher"
        );
    }

    return cacher;
}

/// Gets or creates a cached resource. Throws if cacher unavailable or creation fails.
///
/// Usage:
///   auto cached = GetOrCreateCached<
///       CashSystem::RenderPassCacher,
///       CashSystem::RenderPassWrapper
///   >(cacher, params, "render pass");
template <typename CacherType, typename WrapperType, typename ParamsType>
std::shared_ptr<WrapperType> GetOrCreateCached(
    CacherType* cacher,
    const ParamsType& params,
    const std::string& resourceName
) {
    if (!cacher) {
        throw std::runtime_error(
            "Cacher for " + resourceName + " is null"
        );
    }

    auto cached = cacher->GetOrCreate(params);
    if (!cached) {
        throw std::runtime_error(
            "Failed to get or create " + resourceName + " from cache"
        );
    }

    return cached;
}

/// Validates that a cached wrapper contains a valid Vulkan handle.
/// Throws with descriptive error if invalid.
///
/// Usage:
///   ValidateCachedHandle(wrapper->renderPass, "VkRenderPass", "render pass");
template <typename HandleType>
void ValidateCachedHandle(
    HandleType handle,
    const std::string& handleTypeName,
    const std::string& resourceName
) {
    if (handle == VK_NULL_HANDLE) {
        throw std::runtime_error(
            "Invalid " + handleTypeName + " for " + resourceName + " from cache"
        );
    }
}

} // namespace RenderGraph::NodeHelpers
