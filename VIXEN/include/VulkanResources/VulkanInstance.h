#pragma once

#include <Headers.h>
#include <VulkanResources/VulkanLayerAndExtension.h>

namespace Vixen::Vulkan::Resources {

class VulkanInstance {
    public:
        VulkanInstance();
        ~VulkanInstance();
    public:
        VkInstance instance; // Vulkan instance object variable
        VulkanLayerAndExtension layerExtension; // Vulkan instance layer and extensions
        // Functions for creation and deletion of vulkan instance
        VkResult CreateInstance(std::vector<const char*>& layers,
                                std::vector<const char*>& extensions,
                                const char* applicationName);
        void DestroyInstance();
};

} // namespace Vixen::Vulkan::Resources
