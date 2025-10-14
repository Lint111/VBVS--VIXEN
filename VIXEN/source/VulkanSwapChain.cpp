#include "VulkanSwapChain.h"
#include "VulkanApplication.h"
#include "VulkanRenderer.h"

#define INSTANCE_FUNC_PTR(instance, entrypoint){                \
    fp##entrypoint = (PFN_vk##entrypoint) vkGetInstanceProcAddr \
    (instance, "vk"#entrypoint);                                \
    if (fp##entrypoint == NULL) { exit(-1);                     \
    }                                                           \
}

#define DEVICE_FUNC_PTR(dev, entrypoint){                       \
    fp##entrypoint = (PFN_vk##entrypoint)vkGetDeviceProcAddr    \
    (dev, "vk"#entrypoint);                                     \
    if (fp##entrypoint == NULL) { exit(-1);                     \
    }                                                           \
}


VulkanSwapChain::VulkanSwapChain(VulkanRenderer* renderer) {
    rendererObj = renderer;
    appObj = VulkanApplication::GetInstance();

    // Initialize all Vulkan handles to VK_NULL_HANDLE
    scPublicVars.surface = VK_NULL_HANDLE;
    scPublicVars.swapChain = VK_NULL_HANDLE;
    scPublicVars.swapChainImageCount = 0;
    scPublicVars.currentColorBuffer = 0;
    scPublicVars.Format = VK_FORMAT_UNDEFINED;

    // Initialize private variables
    memset(&scPrivateVars.surfCapabilities, 0, sizeof(scPrivateVars.surfCapabilities));
    scPrivateVars.presentModeCount = 0;
    scPrivateVars.swapChainExtent = {0, 0};
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

VulkanSwapChain::~VulkanSwapChain() {
    // Destroy surface on final cleanup
    DestroySurface();

    scPrivateVars.swapChainImages.clear();
    scPrivateVars.surfaceFormats.clear();
    scPrivateVars.presentModes.clear();
}

void VulkanSwapChain::Initialize() {
    std::cout << "[Initialize] START - surface = " << std::hex << (uint64_t)scPublicVars.surface << std::dec << std::endl;

    // Only load function pointers once (first initialization)
    if (fpGetPhysicalDeviceSurfaceSupportKHR == nullptr) {
        std::cout << "[Initialize] Loading function pointers..." << std::endl;
        CreateSwapChainExtensions();
    }

    // Only create surface if it doesn't exist (during resize it stays alive)
    if (scPublicVars.surface == VK_NULL_HANDLE) {
        std::cout << "[Initialize] Creating surface..." << std::endl;
        CreateSurface();
        std::cout << "[Initialize] Surface created = " << std::hex << (uint64_t)scPublicVars.surface << std::dec << std::endl;
    }

    // Check if scaling extension was successfully enabled during device creation
    // (Extension is requested in main.cpp, but may not be available on all hardware)
    static bool extensionChecked = false;
    if (!extensionChecked) {
        // Check if extension is in the enabled list
        VulkanDevice* device = rendererObj->GetDevice();
        for (const char* extName : device->layerExtension.appRequestedExtensionNames) {
            if (strcmp(extName, VK_EXT_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME) == 0) {
                supportsScalingExtension = true;
                std::cout << "[Initialize] VK_EXT_swapchain_maintenance1 enabled - live resize scaling available" << std::endl;
                break;
            }
        }
        if (!supportsScalingExtension) {
            std::cout << "[Initialize] VK_EXT_swapchain_maintenance1 NOT available - resize will show frozen content" << std::endl;
        }
        extensionChecked = true;
    }

    std::cout << "[Initialize] Before GetGraphicsQueue - surface = " << std::hex << (uint64_t)scPublicVars.surface << std::dec << std::endl;
    uint32_t index = GetGraphicsQueueWithPresentationSupport();
    if(index == UINT32_MAX) {
        std::cerr << "Could not find a graphics and presentation queue" << std::endl;
        exit(-1);
    }

    rendererObj->GetDevice()->graphicsQueueIndex = index;

    GetSupportedFormats();
}

void VulkanSwapChain::CreateSwapChain(const VkCommandBuffer& cmd) {
    GetSurfaceCapabilitiesAndPresentMode();
    ManagePresentMode();
    CreateSwapChainColorImages();
    CreateColorImageView(cmd);
}

void VulkanSwapChain::DestroySwapChain()
{

    VulkanDevice* deviceObj = appObj->deviceObj;
    if (!deviceObj || deviceObj->device == VK_NULL_HANDLE) {
        return;
    }

    // Destroy image views
    for (uint32_t i = 0; i < scPublicVars.colorBuffers.size(); i++) {
        if(scPublicVars.colorBuffers[i].view != VK_NULL_HANDLE) {
            vkDestroyImageView(deviceObj->device, scPublicVars.colorBuffers[i].view, NULL);
            scPublicVars.colorBuffers[i].view = VK_NULL_HANDLE;
        }
    }
    scPublicVars.colorBuffers.clear();
    scPrivateVars.swapChainImages.clear();

    // Destroy swap chain (but not the surface - it stays alive)
    if(scPublicVars.swapChain != VK_NULL_HANDLE) {
        if (fpDestroySwapchainKHR) {
            fpDestroySwapchainKHR(deviceObj->device, scPublicVars.swapChain, NULL);
            scPublicVars.swapChain = VK_NULL_HANDLE;
            scPublicVars.swapChainImageCount = 0;
            scPublicVars.currentColorBuffer = 0;
        }
    }


}

void VulkanSwapChain::DestroySurface()
{
    // Destroy surface (only called during final cleanup)
    if(scPublicVars.surface != VK_NULL_HANDLE) {
        std::cout << "  Destroying VkSurfaceKHR" << std::endl;
        if (fpDestroySurfaceKHR) {
            fpDestroySurfaceKHR(appObj->instanceObj.instance, scPublicVars.surface, NULL);
            scPublicVars.surface = VK_NULL_HANDLE;
        } else {
            std::cerr << "ERROR: fpDestroySurfaceKHR is null!" << std::endl;
        }
    }
}

void VulkanSwapChain::SetSwapChainExtent(uint32_t width, uint32_t height)
{
    scPrivateVars.swapChainExtent.width = width;
    scPrivateVars.swapChainExtent.height = height;
}

VkResult VulkanSwapChain::CreateSwapChainExtensions()
{
    // Dependecy on CreatePresentationWindow() function
    if (!appObj) {
        std::cerr << "ERROR: appObj is null in CreateSwapChainExtensions!" << std::endl;
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (!appObj->deviceObj) {
        std::cerr << "ERROR: deviceObj is null in CreateSwapChainExtensions!" << std::endl;
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkInstance* instance = &appObj->instanceObj.instance;
    VkDevice* device = &appObj->deviceObj->device;

    // Get Instace based swapchain extension function pointer
    INSTANCE_FUNC_PTR(*instance, GetPhysicalDeviceSurfaceSupportKHR);
    INSTANCE_FUNC_PTR(*instance, GetPhysicalDeviceSurfaceCapabilitiesKHR);
    INSTANCE_FUNC_PTR(*instance, GetPhysicalDeviceSurfaceFormatsKHR);
    INSTANCE_FUNC_PTR(*instance, GetPhysicalDeviceSurfacePresentModesKHR);
    INSTANCE_FUNC_PTR(*instance, DestroySurfaceKHR);

    // Get device based swapchain extension function pointer
    DEVICE_FUNC_PTR(*device, CreateSwapchainKHR);
    DEVICE_FUNC_PTR(*device, DestroySwapchainKHR);
    DEVICE_FUNC_PTR(*device, GetSwapchainImagesKHR);
    DEVICE_FUNC_PTR(*device, AcquireNextImageKHR);
    DEVICE_FUNC_PTR(*device, QueuePresentKHR);

    return VK_SUCCESS;
}

void VulkanSwapChain::GetSupportedFormats()
{
    VkPhysicalDevice gpu = *rendererObj->GetDevice()->gpu;
    VkResult result;
    // Get the number of supported surface formats
    uint32_t formatCount;
    fpGetPhysicalDeviceSurfaceFormatsKHR(gpu, scPublicVars.surface, &formatCount, NULL);
    scPrivateVars.surfaceFormats.clear();
    scPrivateVars.surfaceFormats.resize(formatCount);

    // Get the supported surface formats
    result = fpGetPhysicalDeviceSurfaceFormatsKHR(gpu, scPublicVars.surface, &formatCount, scPrivateVars.surfaceFormats.data());
    
    if(formatCount == 1 && scPrivateVars.surfaceFormats[0].format == VK_FORMAT_UNDEFINED) {
        // There is no preferred format, so we assume that
        // VK_FORMAT_B8G8R8A8_UNORM is supported
        scPublicVars.Format = VK_FORMAT_B8G8R8A8_UNORM;
    } else {
        // Always select the first available color format
        scPublicVars.Format = scPrivateVars.surfaceFormats[0].format;
    }
}

VkResult VulkanSwapChain::CreateSurface()
{
    VkInstance* instance = &appObj->instanceObj.instance;

    // Construct the surface description structure
    VkWin32SurfaceCreateInfoKHR createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    createInfo.pNext = NULL;
    createInfo.hinstance = rendererObj->connection;
    createInfo.hwnd = rendererObj->window;

    return vkCreateWin32SurfaceKHR(*instance, &createInfo, NULL, &scPublicVars.surface);

}

uint32_t VulkanSwapChain::GetGraphicsQueueWithPresentationSupport()
{
    std::cout << "[GetGraphicsQueue] ENTRY - surface = " << std::hex << (uint64_t)scPublicVars.surface << std::dec << std::endl;
    std::cout << "[GetGraphicsQueue] fpGetPhysicalDeviceSurfaceSupportKHR = " << std::hex << (void*)fpGetPhysicalDeviceSurfaceSupportKHR << std::dec << std::endl;

    VulkanDevice* device = appObj->deviceObj;
    std::cout << "[GetGraphicsQueue] device = " << std::hex << (void*)device << std::dec << std::endl;

    if (!device) {
        std::cerr << "ERROR: deviceObj is null!" << std::endl;
        return UINT32_MAX;
    }

    uint32_t queueCount = device->queueFamilyCount;
    VkPhysicalDevice* gpu = device->gpu;

    std::cout << "[GetGraphicsQueue] gpu pointer = " << std::hex << (void*)gpu << std::dec << std::endl;

    if (!gpu) {
        std::cerr << "ERROR: gpu pointer is null!" << std::endl;
        return UINT32_MAX;
    }

    std::cout << "[GetGraphicsQueue] *gpu (VkPhysicalDevice) = " << std::hex << (uint64_t)(*gpu) << std::dec << std::endl;

    std::vector<VkQueueFamilyProperties>& queueProps = device->queueFamilyProperties;

    std::cout << "[GetGraphicsQueue] queueCount = " << queueCount << std::endl;
    std::cout << "[GetGraphicsQueue] About to iterate queues - surface = " << std::hex << (uint64_t)scPublicVars.surface << std::dec << std::endl;

    // Iterate each queue and get presentation status for each.
    VkBool32* supportPresent = (VkBool32*)malloc(sizeof(VkBool32) * queueCount);
    for (uint32_t i = 0; i < queueCount; i++) {
        std::cout << "[GetGraphicsQueue] Checking queue " << i << " - surface = " << std::hex << (uint64_t)scPublicVars.surface << std::dec << std::endl;
        fpGetPhysicalDeviceSurfaceSupportKHR(*gpu, i, scPublicVars.surface, &supportPresent[i]);
    }

    // Search for a graphics queues that supports presentation
    uint32_t graphicsQueueNodeIndex = UINT32_MAX;
    uint32_t presentQueueNodeIndex = UINT32_MAX;
    for (uint32_t i = 0; i < queueCount; i++) {
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
        for (uint32_t i = 0; i < queueCount; i++) {
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

void VulkanSwapChain::GetSurfaceCapabilitiesAndPresentMode()
{
    VkResult result;

    VkPhysicalDevice gpu = *appObj->deviceObj->gpu;
    fpGetPhysicalDeviceSurfaceCapabilitiesKHR(gpu, scPublicVars.surface, &scPrivateVars.surfCapabilities);

    // If surface capabilities returned zeros, the window might not be ready yet
    if (scPrivateVars.surfCapabilities.maxImageExtent.width == 0 ||
        scPrivateVars.surfCapabilities.maxImageExtent.height == 0) {
        std::cerr << "ERROR: Surface capabilities returned invalid dimensions!" << std::endl;
        std::cerr << "Window dimensions: " << rendererObj->width << "x" << rendererObj->height << std::endl;
        std::cerr << "Surface capabilities: " << scPrivateVars.surfCapabilities.maxImageExtent.width
                  << "x" << scPrivateVars.surfCapabilities.maxImageExtent.height << std::endl;
        exit(-1);
    }

    fpGetPhysicalDeviceSurfacePresentModesKHR(gpu, scPublicVars.surface, &scPrivateVars.presentModeCount, NULL);

    scPrivateVars.presentModes.clear();
    scPrivateVars.presentModes.resize(scPrivateVars.presentModeCount);
    assert(scPrivateVars.presentModes.size() >= 1);

    result = fpGetPhysicalDeviceSurfacePresentModesKHR(
        gpu, 
        scPublicVars.surface, 
        &scPrivateVars.presentModeCount, 
        &scPrivateVars.presentModes[0]
    );

    fpGetPhysicalDeviceSurfacePresentModesKHR(
        gpu, 
        scPublicVars.surface, 
        &scPrivateVars.presentModeCount, 
        scPrivateVars.presentModes.data()
    );

    if(scPrivateVars.surfCapabilities.currentExtent.width == (uint32_t)-1) {
        // If the surface size is undefined, the size is set to image size
        scPrivateVars.swapChainExtent.width = rendererObj->width;
        scPrivateVars.swapChainExtent.height = rendererObj->height;
    } else {
        // If the surface size is defined, the swap chain size must match
        scPrivateVars.swapChainExtent = scPrivateVars.surfCapabilities.currentExtent;
    }




}

void VulkanSwapChain::ManagePresentMode()
{
    // MAILBOX -lowest-latency non tearing mode.
    // If not try Immediate, the fastest (but tears).
    // Else FIFO which is guaranteed to be supported
    scPrivateVars.swapChainPresentMode = VK_PRESENT_MODE_FIFO_KHR;

    for (size_t i = 0; i < scPrivateVars.presentModeCount; i++) {
        if(scPrivateVars.presentModes[i] == VK_PRESENT_MODE_MAILBOX_KHR) {
            scPrivateVars.presentModes[i] = VK_PRESENT_MODE_MAILBOX_KHR;
            break;
        }

        if(scPrivateVars.presentModes[i] != VK_PRESENT_MODE_MAILBOX_KHR &&
           scPrivateVars.presentModes[i] == VK_PRESENT_MODE_IMMEDIATE_KHR) {
            scPrivateVars.swapChainPresentMode = VK_PRESENT_MODE_IMMEDIATE_KHR;
        }
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

void VulkanSwapChain::CreateSwapChainColorImages()
{
    VkResult result;

    // If scaling extension is available, configure it for live resize
    VkSwapchainPresentScalingCreateInfoEXT scalingInfo = {};
    if (supportsScalingExtension) {
        scalingInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_SCALING_CREATE_INFO_EXT;
        scalingInfo.pNext = NULL;
        scalingInfo.scalingBehavior = VK_PRESENT_SCALING_STRETCH_BIT_EXT;
        scalingInfo.presentGravityX = VK_PRESENT_GRAVITY_CENTERED_BIT_EXT;
        scalingInfo.presentGravityY = VK_PRESENT_GRAVITY_CENTERED_BIT_EXT;
    }

    VkSwapchainCreateInfoKHR scInfo = {};
    scInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    scInfo.pNext = supportsScalingExtension ? &scalingInfo : NULL;
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
    scInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                        VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    scInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    scInfo.queueFamilyIndexCount = 0;
    scInfo.pQueueFamilyIndices = NULL;

    // Create the swapchain object
    result = fpCreateSwapchainKHR(
        rendererObj->GetDevice()->device,
        &scInfo,
        NULL,
        &scPublicVars.swapChain
    );

    assert(result == VK_SUCCESS);


    // Get the number of swapchain images
    result = fpGetSwapchainImagesKHR(
        rendererObj->GetDevice()->device,
        scPublicVars.swapChain,
        &scPublicVars.swapChainImageCount,
        NULL
    );

    assert(result == VK_SUCCESS);

    scPrivateVars.swapChainImages.clear();
    scPrivateVars.swapChainImages.resize(scPublicVars.swapChainImageCount);
    assert(scPrivateVars.swapChainImages.size() >= 1);

    // Retrieve the swapchain image surfaces
    result = fpGetSwapchainImagesKHR(
        rendererObj->GetDevice()->device,
        scPublicVars.swapChain,
        &scPublicVars.swapChainImageCount,
        scPrivateVars.swapChainImages.data()
    );

    assert(result == VK_SUCCESS);
}

void VulkanSwapChain::CreateColorImageView(const VkCommandBuffer &cmd)
{
    VkResult result;    

    for(uint32_t i = 0; i < scPublicVars.swapChainImageCount; i++) {
        SwapChainBuffer sc_buffer;
        VkImageViewCreateInfo imgViewInfo = {};
        imgViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        imgViewInfo.pNext = NULL;
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

        result = vkCreateImageView(
            rendererObj->GetDevice()->device,
            &imgViewInfo,
            NULL,
            &sc_buffer.view
        );

        scPublicVars.colorBuffers.push_back(sc_buffer);
        assert(result == VK_SUCCESS);
    }
    scPublicVars.currentColorBuffer = 0;
}
