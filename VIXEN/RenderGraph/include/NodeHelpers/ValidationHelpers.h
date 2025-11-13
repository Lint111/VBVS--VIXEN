#pragma once

#include <stdexcept>
#include <string>
#include <typeinfo>
#include <memory>

namespace RenderGraph::NodeHelpers {

/// Validates and sets device in node context. Throws if device is null.
/// Usage: ValidateAndSetDevice<MyNodeConfig>(ctx, this);
template <typename NodeConfig, typename NodeType>
void ValidateAndSetDevice(const typename NodeConfig::ContextType& ctx, NodeType* node) {
    using DevicePtr = typename NodeConfig::VulkanDeviceType;

    DevicePtr devicePtr = ctx.template In(NodeConfig::VULKAN_DEVICE_IN);
    if (!devicePtr) {
        throw std::runtime_error(
            std::string(typeid(NodeType).name()) + ": Device input is null"
        );
    }
    node->SetDevice(devicePtr);
}

/// Generic typed input validation. Throws with descriptive error.
/// Usage: auto ptr = ValidateInput<MyPtrType>(ctx, "MyInput", inputSlot);
template <typename T, typename ContextType, typename SlotType>
inline T ValidateInput(
    const ContextType& ctx,
    const std::string& inputName,
    const SlotType& inputSlot
) {
    auto ptr = ctx.In(inputSlot);  // Let auto deduce, then cast if needed
    if (!ptr) {
        throw std::runtime_error(
            "Required input '" + inputName + "' is null"
        );
    }
    return ptr;
}

/// Validates Vulkan result code. Throws with context if failed.
/// Usage: ValidateVulkanResult(vkCreateRenderPass(...), "RenderPass creation");
inline void ValidateVulkanResult(
    VkResult result,
    const std::string& operation
) {
    if (result != VK_SUCCESS) {
        throw std::runtime_error(
            operation + " failed with VkResult: " + std::to_string(result)
        );
    }
}

/// Safely retrieves optional input, returns default if null
/// Usage: auto ptr = GetOptionalInput<MyPtrType>(ctx, inputSlot, nullptr);
template <typename T, typename ContextType, typename SlotType>
inline T GetOptionalInput(
    const ContextType& ctx,
    const SlotType& inputSlot,
    T defaultValue = nullptr
) {
    auto ptr = ctx.In(inputSlot);  // Let auto deduce
    return ptr ? ptr : defaultValue;
}

} // namespace RenderGraph::NodeHelpers
