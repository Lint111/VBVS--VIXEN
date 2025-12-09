#include "VulkanSwapChain.h"

#define INSTANCE_FUNC_PTR(instance, entrypoint){                \
    fp##entrypoint = (PFN_vk##entrypoint) vkGetInstanceProcAddr \
    (instance, "vk"#entrypoint);                                \
    if (fp##entrypoint == nullptr) {                            \
        throw std::runtime_error("Failed to load instance function: vk"#entrypoint); \
    }                                                           \
}

#define DEVICE_FUNC_PTR(dev, entrypoint){                       \
    fp##entrypoint = (PFN_vk##entrypoint)vkGetDeviceProcAddr    \
    (dev, "vk"#entrypoint);                                     \
    if (fp##entrypoint == nullptr) {                            \
        throw std::runtime_error("Failed to load device function: vk"#entrypoint); \
    }                                                           \
}

void VulkanSwapChain::Destroy(VkDevice device, VkInstance instance) {
    LOG_INFO("[VulkanSwapChain::Destroy] Called with device=" + std::to_string(reinterpret_cast<uint64_t>(device))
              + ", instance=" + std::to_string(reinterpret_cast<uint64_t>(instance)));
    LOG_INFO("[VulkanSwapChain::Destroy] Current surface=" + std::to_string(reinterpret_cast<uint64_t>(scPublicVars.surface))
              + ", swapchain=" + std::to_string(reinterpret_cast<uint64_t>(scPublicVars.swapChain)));

    // Ensure extension pointers are loaded before attempting destruction
    // This is safe to call multiple times - it just reloads the function pointers
    if (instance != VK_NULL_HANDLE && device != VK_NULL_HANDLE) {
        // Only load if not already loaded (check one representative pointer)
        if (fpDestroySwapchainKHR == nullptr || fpDestroySurfaceKHR == nullptr) {
            LOG_INFO("[VulkanSwapChain::Destroy] Loading extension pointers");
            CreateSwapChainExtensions(instance, device);
        }
    }

    // Destroy swapchain and image views
    if (device != VK_NULL_HANDLE) {
        DestroySwapChain(device);
    }

    // Destroy surface
    if (instance != VK_NULL_HANDLE) {
        DestroySurface(instance);
    }

    // Clear internal vectors
    CleanUp();

    LOG_INFO("[VulkanSwapChain::Destroy] Cleanup complete");
}

void VulkanSwapChain::CleanUp() {
    scPrivateVars.swapChainImages.clear();
    scPrivateVars.surfaceFormats.clear();
	scPrivateVars.presentModes.clear();
}

void VulkanSwapChain::Initialize() {
    // Initialize all Vulkan handles to VK_NULL_HANDLE
    scPublicVars.surface = VK_NULL_HANDLE;
    scPublicVars.swapChain = VK_NULL_HANDLE;
    scPublicVars.swapChainImageCount = 0;
    scPublicVars.currentColorBuffer = 0;
    scPublicVars.Format = VK_FORMAT_UNDEFINED;

    // Initialize private variables
    memset(&scPrivateVars.surfCapabilities, 0, sizeof(scPrivateVars.surfCapabilities));
    scPrivateVars.presentModeCount = 0;
    scPrivateVars.swapChainExtent = { 0, 0 };
    scPrivateVars.desiredNumberOfSwapChainImages = 0;
    scPrivateVars.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    scPrivateVars.swapChainPresentMode = VK_PRESENT_MODE_FIFO_KHR;

    // Initialize function pointers to nullptr
    fpQueuePresentKHR = nullptr;
    fpAcquireNextImageKHR = nullptr;
    fpGetPhysicalDeviceSurfaceSupportKHR = nullptr;
    fpGetPhysicalDeviceSurfaceCapabilitiesKHR = nullptr;
    fpGetPhysicalDeviceSurfaceFormatsKHR = nullptr;
    fpGetPhysicalDeviceSurfacePresentModesKHR = nullptr;
    fpDestroySurfaceKHR = nullptr;
    fpCreateSwapchainKHR = nullptr;
    fpDestroySwapchainKHR = nullptr;
    fpGetSwapchainImagesKHR = nullptr;
}

void VulkanSwapChain::DestroySwapChain(VkDevice device)
{
    if (device == VK_NULL_HANDLE) {
        return;
    }

    // Destroy image views
    for (uint32_t i = 0; i < scPublicVars.colorBuffers.size(); i++) {
        if(scPublicVars.colorBuffers[i].view != VK_NULL_HANDLE) {
            vkDestroyImageView(device, scPublicVars.colorBuffers[i].view, nullptr);
            scPublicVars.colorBuffers[i].view = VK_NULL_HANDLE;
        }
    }
    scPublicVars.colorBuffers.clear();
    scPrivateVars.swapChainImages.clear();

    // Destroy swap chain (but not the surface - it stays alive)
    if(scPublicVars.swapChain != VK_NULL_HANDLE) {
        if (fpDestroySwapchainKHR) {
            fpDestroySwapchainKHR(device, scPublicVars.swapChain, nullptr);
            scPublicVars.swapChain = VK_NULL_HANDLE;
            scPublicVars.swapChainImageCount = 0;
            scPublicVars.currentColorBuffer = 0;
        }
    }
}

void VulkanSwapChain::DestroySurface(VkInstance instance)
{
    // Destroy surface (only called during final cleanup)
    if(scPublicVars.surface != VK_NULL_HANDLE) {
        LOG_INFO("Destroying VkSurfaceKHR");
        if (fpDestroySurfaceKHR) {
            fpDestroySurfaceKHR(instance, scPublicVars.surface, nullptr);
            scPublicVars.surface = VK_NULL_HANDLE;
        } else {
            LOG_ERROR("ERROR: fpDestroySurfaceKHR is null!");
        }
    }
}

void VulkanSwapChain::SetSwapChainExtent(uint32_t width, uint32_t height)
{
    scPrivateVars.swapChainExtent.width = width;
    scPrivateVars.swapChainExtent.height = height;

	scPublicVars.Extent = { width, height };
}

VkResult VulkanSwapChain::CreateSwapChainExtensions(VkInstance instance, VkDevice device)
{
    // Get Instance based swapchain extension function pointers
    INSTANCE_FUNC_PTR(instance, GetPhysicalDeviceSurfaceSupportKHR);
    INSTANCE_FUNC_PTR(instance, GetPhysicalDeviceSurfaceCapabilitiesKHR);
    INSTANCE_FUNC_PTR(instance, GetPhysicalDeviceSurfaceFormatsKHR);
    INSTANCE_FUNC_PTR(instance, GetPhysicalDeviceSurfacePresentModesKHR);
    INSTANCE_FUNC_PTR(instance, DestroySurfaceKHR);

    // Get device based swapchain extension function pointers
    DEVICE_FUNC_PTR(device, CreateSwapchainKHR);
    DEVICE_FUNC_PTR(device, DestroySwapchainKHR);
    DEVICE_FUNC_PTR(device, GetSwapchainImagesKHR);
    DEVICE_FUNC_PTR(device, AcquireNextImageKHR);
    DEVICE_FUNC_PTR(device, QueuePresentKHR);

    return VK_SUCCESS;
}

void VulkanSwapChain::GetSupportedFormats(VkPhysicalDevice gpu)
{
    // Get the number of supported surface formats
    uint32_t formatCount;
    fpGetPhysicalDeviceSurfaceFormatsKHR(gpu, scPublicVars.surface, &formatCount, nullptr);
    scPrivateVars.surfaceFormats.clear();
    scPrivateVars.surfaceFormats.resize(formatCount);

    // Get the supported surface formats
    VkResult result = fpGetPhysicalDeviceSurfaceFormatsKHR(gpu, scPublicVars.surface, &formatCount, scPrivateVars.surfaceFormats.data());

    if(formatCount == 1 && scPrivateVars.surfaceFormats[0].format == VK_FORMAT_UNDEFINED) {
        // There is no preferred format, so we assume that
        // VK_FORMAT_B8G8R8A8_UNORM is supported
        scPublicVars.Format = VK_FORMAT_B8G8R8A8_UNORM;
    } else {
        // Always select the first available color format
        scPublicVars.Format = scPrivateVars.surfaceFormats[0].format;
    }
}

VkResult VulkanSwapChain::CreateSurface(VkInstance instance, HWND hwnd, HINSTANCE hinstance)
{
    // Construct the surface description structure
    VkWin32SurfaceCreateInfoKHR createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    createInfo.pNext = nullptr;
    createInfo.hinstance = hinstance;
    createInfo.hwnd = hwnd;

    return vkCreateWin32SurfaceKHR(instance, &createInfo, nullptr, &scPublicVars.surface);
}

uint32_t VulkanSwapChain::GetGraphicsQueueWithPresentationSupport(VkPhysicalDevice gpu, uint32_t queueFamilyCount, const std::vector<VkQueueFamilyProperties>& queueProps)
{
    LOG_INFO("[GetGraphicsQueue] ENTRY - surface = " + std::to_string(reinterpret_cast<uint64_t>(scPublicVars.surface)));
    LOG_INFO("[GetGraphicsQueue] fpGetPhysicalDeviceSurfaceSupportKHR = " + std::to_string(reinterpret_cast<uint64_t>(fpGetPhysicalDeviceSurfaceSupportKHR)));

    // Iterate each queue and get presentation status for each
    VkBool32* supportPresent = (VkBool32*)malloc(sizeof(VkBool32) * queueFamilyCount);
    for (uint32_t i = 0; i < queueFamilyCount; i++) {
        LOG_INFO("[GetGraphicsQueue] Checking queue " + std::to_string(i) + " - surface = " + std::to_string(reinterpret_cast<uint64_t>(scPublicVars.surface)));
        fpGetPhysicalDeviceSurfaceSupportKHR(gpu, i, scPublicVars.surface, &supportPresent[i]);
    }

    // Search for a graphics queue that supports presentation
    uint32_t graphicsQueueNodeIndex = UINT32_MAX;
    uint32_t presentQueueNodeIndex = UINT32_MAX;
    for (uint32_t i = 0; i < queueFamilyCount; i++) {
        if((queueProps[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0) {
            if(graphicsQueueNodeIndex == UINT32_MAX) {
                graphicsQueueNodeIndex = i;
            }

            if(supportPresent[i] == VK_TRUE) {
                graphicsQueueNodeIndex = i;
                presentQueueNodeIndex = i;
                break;
            }
        }
    }

    if(presentQueueNodeIndex == UINT32_MAX) {
        // If there is no queue that supports both graphics and presentation
        // search for a separate presentation queue
        for (uint32_t i = 0; i < queueFamilyCount; i++) {
            if(supportPresent[i] == VK_TRUE) {
                presentQueueNodeIndex = i;
                break;
            }
        }
    }

    free(supportPresent);

    // Generate error if could not find queue with present queue
    if(graphicsQueueNodeIndex == UINT32_MAX ||
       presentQueueNodeIndex == UINT32_MAX) {
        return UINT32_MAX;
    }

    return graphicsQueueNodeIndex;
}

void VulkanSwapChain::GetSurfaceCapabilitiesAndPresentMode(VkPhysicalDevice gpu, uint32_t width, uint32_t height)
{
    fpGetPhysicalDeviceSurfaceCapabilitiesKHR(gpu, scPublicVars.surface, &scPrivateVars.surfCapabilities);

    // If surface capabilities returned zeros, the window might not be ready yet
    if (scPrivateVars.surfCapabilities.maxImageExtent.width == 0 ||
        scPrivateVars.surfCapabilities.maxImageExtent.height == 0) {
        LOG_ERROR("ERROR: Surface capabilities returned invalid dimensions!");
        LOG_ERROR("Window dimensions: " + std::to_string(width) + "x" + std::to_string(height));
        LOG_ERROR("Surface capabilities: " + std::to_string(scPrivateVars.surfCapabilities.maxImageExtent.width)
                  + "x" + std::to_string(scPrivateVars.surfCapabilities.maxImageExtent.height));
        exit(-1);
    }

    fpGetPhysicalDeviceSurfacePresentModesKHR(gpu, scPublicVars.surface, &scPrivateVars.presentModeCount, nullptr);

    scPrivateVars.presentModes.clear();
    scPrivateVars.presentModes.resize(scPrivateVars.presentModeCount);
    assert(scPrivateVars.presentModes.size() >= 1);

    VkResult result = fpGetPhysicalDeviceSurfacePresentModesKHR(
        gpu,
        scPublicVars.surface,
        &scPrivateVars.presentModeCount,
        scPrivateVars.presentModes.data()
    );

    if(scPrivateVars.surfCapabilities.currentExtent.width == (uint32_t)-1) {
        // If the surface size is undefined, the size is set to image size
        scPrivateVars.swapChainExtent.width = width;
        scPrivateVars.swapChainExtent.height = height;
    } else {
        // If the surface size is defined, the swap chain size must match
        scPrivateVars.swapChainExtent = scPrivateVars.surfCapabilities.currentExtent;
    }
}

void VulkanSwapChain::ManagePresentMode()
{
    // Prioritize IMMEDIATE for maximum uncapped FPS
    // Then MAILBOX for low-latency triple buffering
    // Fallback to FIFO which is guaranteed to be supported
    scPrivateVars.swapChainPresentMode = VK_PRESENT_MODE_FIFO_KHR;

    std::string modesStr = "[ManagePresentMode] Available present modes: ";
    for (size_t i = 0; i < scPrivateVars.presentModeCount; i++) {
        modesStr += std::to_string(scPrivateVars.presentModes[i]) + " ";
    }
    LOG_INFO(modesStr);

    for (size_t i = 0; i < scPrivateVars.presentModeCount; i++) {
        if(scPrivateVars.presentModes[i] == VK_PRESENT_MODE_IMMEDIATE_KHR) {
            scPrivateVars.swapChainPresentMode = VK_PRESENT_MODE_IMMEDIATE_KHR;
            LOG_INFO("[ManagePresentMode] Selected IMMEDIATE mode (uncapped FPS)");
            break;
        }

        if(scPrivateVars.presentModes[i] == VK_PRESENT_MODE_MAILBOX_KHR) {
            scPrivateVars.swapChainPresentMode = VK_PRESENT_MODE_MAILBOX_KHR;
            LOG_INFO("[ManagePresentMode] Selected MAILBOX mode");
        }
    }

    if (scPrivateVars.swapChainPresentMode == VK_PRESENT_MODE_FIFO_KHR) {
        LOG_INFO("[ManagePresentMode] Using FIFO mode (V-Sync enabled)");
    }

    // Determine the number of images
    scPrivateVars.desiredNumberOfSwapChainImages = scPrivateVars.surfCapabilities.minImageCount + 1;
    if((scPrivateVars.surfCapabilities.maxImageCount > 0) &&
       (scPrivateVars.desiredNumberOfSwapChainImages > scPrivateVars.surfCapabilities.maxImageCount)) {
        // Application must settle for fewer images than desired
        scPrivateVars.desiredNumberOfSwapChainImages = scPrivateVars.surfCapabilities.maxImageCount;
    }

    if(scPrivateVars.surfCapabilities.supportedTransforms &
       VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR) {
        scPrivateVars.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    } else {
        scPrivateVars.preTransform = scPrivateVars.surfCapabilities.currentTransform;
    }
}

void VulkanSwapChain::CreateSwapChainColorImages(VkDevice device)
{
    VkResult result;

    // If scaling extension is available, configure it for live resize
    VkSwapchainPresentScalingCreateInfoEXT scalingInfo = {};
    if (supportsScalingExtension) {
        scalingInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_SCALING_CREATE_INFO_EXT;
        scalingInfo.pNext = nullptr;
        scalingInfo.scalingBehavior = VK_PRESENT_SCALING_STRETCH_BIT_EXT;
        scalingInfo.presentGravityX = VK_PRESENT_GRAVITY_CENTERED_BIT_EXT;
        scalingInfo.presentGravityY = VK_PRESENT_GRAVITY_CENTERED_BIT_EXT;
    }

    VkSwapchainCreateInfoKHR scInfo = {};
    scInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    scInfo.pNext = supportsScalingExtension ? &scalingInfo : nullptr;
    scInfo.surface = scPublicVars.surface;
    scInfo.minImageCount = scPrivateVars.desiredNumberOfSwapChainImages;
    scInfo.imageFormat = scPublicVars.Format;
    scInfo.imageExtent.width = scPrivateVars.swapChainExtent.width;
    scInfo.imageExtent.height = scPrivateVars.swapChainExtent.height;
    scInfo.preTransform = scPrivateVars.preTransform;
    scInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    scInfo.imageArrayLayers = 1;
    scInfo.presentMode = scPrivateVars.swapChainPresentMode;
    scInfo.oldSwapchain = VK_NULL_HANDLE;
    scInfo.clipped = VK_TRUE;
    scInfo.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    scInfo.imageUsage = imageUsageFlags;
    scInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    scInfo.queueFamilyIndexCount = 0;
    scInfo.pQueueFamilyIndices = nullptr;

    // Create the swapchain object
    result = fpCreateSwapchainKHR(device, &scInfo, nullptr, &scPublicVars.swapChain);
    if (result != VK_SUCCESS) {
        throw std::runtime_error(std::string("VulkanSwapChain::CreateSwapChainColorImages - fpCreateSwapchainKHR failed: ") + std::to_string(static_cast<int>(result)));
    }

    // Get the number of swapchain images
    result = fpGetSwapchainImagesKHR(device, scPublicVars.swapChain, &scPublicVars.swapChainImageCount, nullptr);
    if (result != VK_SUCCESS) {
        throw std::runtime_error(std::string("VulkanSwapChain::CreateSwapChainColorImages - fpGetSwapchainImagesKHR failed (count): ") + std::to_string(static_cast<int>(result)));
    }

    scPrivateVars.swapChainImages.clear();
    scPrivateVars.swapChainImages.resize(scPublicVars.swapChainImageCount);
    if (scPrivateVars.swapChainImages.size() < 1) {
        throw std::runtime_error("VulkanSwapChain::CreateSwapChainColorImages - no swapchain images returned");
    }

    // Retrieve the swapchain image surfaces
    result = fpGetSwapchainImagesKHR(device, scPublicVars.swapChain, &scPublicVars.swapChainImageCount, scPrivateVars.swapChainImages.data());
    if (result != VK_SUCCESS) {
        throw std::runtime_error(std::string("VulkanSwapChain::CreateSwapChainColorImages - fpGetSwapchainImagesKHR failed: ") + std::to_string(static_cast<int>(result)));
    }
}

void VulkanSwapChain::SetImageUsageFlags(VkImageUsageFlags flags) {
    imageUsageFlags = flags;
}

void VulkanSwapChain::CreateColorImageView(VkDevice device, const VkCommandBuffer &cmd)
{
    VkResult result;

    for(uint32_t i = 0; i < scPublicVars.swapChainImageCount; i++) {
        SwapChainBuffer sc_buffer;
        VkImageViewCreateInfo imgViewInfo = {};
        imgViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        imgViewInfo.pNext = nullptr;
        imgViewInfo.format = scPublicVars.Format;
        imgViewInfo.components.r = VK_COMPONENT_SWIZZLE_R;
        imgViewInfo.components.g = VK_COMPONENT_SWIZZLE_G;
        imgViewInfo.components.b = VK_COMPONENT_SWIZZLE_B;
        imgViewInfo.components.a = VK_COMPONENT_SWIZZLE_A;
        imgViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        imgViewInfo.subresourceRange.baseMipLevel = 0;
        imgViewInfo.subresourceRange.levelCount = 1;
        imgViewInfo.subresourceRange.baseArrayLayer = 0;
        imgViewInfo.subresourceRange.layerCount = 1;
        imgViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        imgViewInfo.flags = 0;

        sc_buffer.image = scPrivateVars.swapChainImages[i];
        imgViewInfo.image = sc_buffer.image;

        result = vkCreateImageView(device, &imgViewInfo, nullptr, &sc_buffer.view);

        if (result != VK_SUCCESS) {
            // Clean up any image views already created for this swapchain
            for (auto& buf : scPublicVars.colorBuffers) {
                if (buf.view != VK_NULL_HANDLE) {
                    vkDestroyImageView(device, buf.view, nullptr);
                    buf.view = VK_NULL_HANDLE;
                }
            }
            scPublicVars.colorBuffers.clear();

            throw std::runtime_error(std::string("VulkanSwapChain::CreateColorImageView - vkCreateImageView failed: ") + std::to_string(static_cast<int>(result)));
        }

        scPublicVars.colorBuffers.push_back(sc_buffer);
    }
    scPublicVars.currentColorBuffer = 0;
}
