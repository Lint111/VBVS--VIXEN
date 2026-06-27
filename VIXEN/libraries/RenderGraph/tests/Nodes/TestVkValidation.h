#pragma once
#include <string>
#include <vector>
#include <vulkan/vulkan.h>

// Returns {"VK_LAYER_KHRONOS_validation"} when the layer is installed, else {}.
// Render tests use validation as a debug aid; it is NOT required to render, so a
// machine with only the lavapipe ICD (no Vulkan SDK) still runs them. Mirrors
// BenchmarkRunner.cpp's IsLayerAvailable gate (which is .cpp-local).
inline std::vector<const char*> EnabledValidationLayers() {
    uint32_t count = 0;
    if (vkEnumerateInstanceLayerProperties(&count, nullptr) != VK_SUCCESS || count == 0) return {};
    std::vector<VkLayerProperties> props(count);
    if (vkEnumerateInstanceLayerProperties(&count, props.data()) != VK_SUCCESS) return {};
    for (const auto& p : props)
        if (std::string(p.layerName) == "VK_LAYER_KHRONOS_validation")
            return {"VK_LAYER_KHRONOS_validation"};
    return {};
}
