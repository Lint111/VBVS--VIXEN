#include "VulkanSwapChain.h"

#define GLFW_INCLUDE_NONE   // don't pull in <GL/gl.h> (absent on headless/WSL); Vulkan-only below
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

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

    DestroyImageViewsOnly(device);

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

void VulkanSwapChain::DestroyImageViewsOnly(VkDevice device)
{
    if (device == VK_NULL_HANDLE) {
        return;
    }

    for (uint32_t i = 0; i < scPublicVars.colorBuffers.size(); i++) {
        if(scPublicVars.colorBuffers[i].view != VK_NULL_HANDLE) {
            vkDestroyImageView(device, scPublicVars.colorBuffers[i].view, nullptr);
            scPublicVars.colorBuffers[i].view = VK_NULL_HANDLE;
        }
    }
    scPublicVars.colorBuffers.clear();
    scPrivateVars.swapChainImages.clear();
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

    // Dozen/D3D12 swapchain-STORAGE format fix (enables WSL2 GPU rendering) ------------------------
    // Dozen (Mesa's Vulkan-over-D3D12 "microsoft" driver, used on WSL2 to reach the real GPU) maps a
    // STORAGE swapchain image to a D3D12 UAV. D3D12 forbids UAVs on *_SRGB formats — but the X11/XCB
    // surface Dozen exposes lists VK_FORMAT_B8G8R8A8_SRGB *first*, so the "select surfaceFormats[0]"
    // rule above picks SRGB. The STORAGE guard below then keeps the bit (Dozen reports SRGB as
    // storage-capable), the compute pipeline builds, and the GPU silently removes the device
    // ("D3D12: Removing Device.") the moment a STORAGE image view is created over the SRGB swapchain
    // image. The UNORM sibling (e.g. VK_FORMAT_B8G8R8A8_UNORM) IS a legal D3D12 UAV — a direct
    // SRGB→UNORM swapchain reproducer on this box renders fine. So when STORAGE usage is requested on
    // a Dozen device and the chosen format is *_SRGB, swap to the matching non-SRGB (UNORM) surface
    // format if the surface offers one. Gated on the Dozen-only VK_MSFT_layered_driver device
    // extension → native-GPU behaviour is unchanged. The only visible change on Dozen is that the
    // compositor no longer applies a linear→sRGB encode on present; the ray-marcher already writes
    // display-ready colour, so UNORM (raw bytes) is in fact the correct target here.
    if (imageUsageFlags & VK_IMAGE_USAGE_STORAGE_BIT) {
        bool isLayeredDriver = false;
        uint32_t devExtCount = 0;
        if (vkEnumerateDeviceExtensionProperties(gpu, nullptr, &devExtCount, nullptr) == VK_SUCCESS && devExtCount > 0) {
            std::vector<VkExtensionProperties> devExts(devExtCount);
            if (vkEnumerateDeviceExtensionProperties(gpu, nullptr, &devExtCount, devExts.data()) == VK_SUCCESS) {
                for (const auto& e : devExts) {
                    if (std::strcmp(e.extensionName, "VK_MSFT_layered_driver") == 0) { isLayeredDriver = true; break; }
                }
            }
        }
        // Map an SRGB surface format to its UNORM sibling (D3D12 UAVs reject SRGB; UNORM is legal).
        auto srgbToUnorm = [](VkFormat f) -> VkFormat {
            switch (f) {
                case VK_FORMAT_B8G8R8A8_SRGB: return VK_FORMAT_B8G8R8A8_UNORM;
                case VK_FORMAT_R8G8B8A8_SRGB: return VK_FORMAT_R8G8B8A8_UNORM;
                case VK_FORMAT_A8B8G8R8_SRGB_PACK32: return VK_FORMAT_A8B8G8R8_UNORM_PACK32;
                default: return f;
            }
        };
        const VkFormat unormWanted = srgbToUnorm(scPublicVars.Format);
        if (isLayeredDriver && unormWanted != scPublicVars.Format) {
            for (const auto& sf : scPrivateVars.surfaceFormats) {
                if (sf.format == unormWanted) {
                    LOG_INFO("[GetSupportedFormats] Dozen/D3D12 (VK_MSFT_layered_driver) + STORAGE swapchain: "
                             "swapping chosen SRGB surface format to its UNORM sibling (SRGB is not a legal "
                             "D3D12 UAV; UNORM is).");
                    scPublicVars.Format = unormWanted;
                    break;
                }
            }
        }
    }
    // (end Dozen/D3D12 swapchain-STORAGE format fix)

    // Portability guard: keep STORAGE swapchain usage only when the chosen surface format actually
    // supports the STORAGE_IMAGE feature. Software rasterizers (e.g. llvmpipe on WSLg) expose BGRA
    // surface formats WITHOUT storage support; requesting STORAGE anyway produces an invalid swapchain
    // (VUID-VkSwapchainCreateInfoKHR-imageFormat-01778) whose handle then faults inside
    // vkQueuePresentKHR. Dropping it lets present succeed everywhere; compute-to-swapchain paths simply
    // require a storage-capable GPU format (real GPUs report STORAGE on their swapchain formats).
    if (imageUsageFlags & VK_IMAGE_USAGE_STORAGE_BIT) {
        VkFormatProperties fmtProps{};
        vkGetPhysicalDeviceFormatProperties(gpu, scPublicVars.Format, &fmtProps);
        if (!(fmtProps.optimalTilingFeatures & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT)) {
            imageUsageFlags &= ~VK_IMAGE_USAGE_STORAGE_BIT;
            LOG_INFO("[GetSupportedFormats] Surface format lacks STORAGE_IMAGE support - dropping STORAGE "
                     "from swapchain usage (software-rasterizer / cross-platform portability).");
        }
    }
}

VkResult VulkanSwapChain::CreateSurface(VkInstance instance, GLFWwindow* window)
{
    // Cross-platform surface creation; GLFW selects the right platform surface internally
    // (Win32 on Windows, X11/Wayland on Linux). Replaces vkCreateWin32SurfaceKHR.
    return glfwCreateWindowSurface(instance, window, nullptr, &scPublicVars.surface);
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

VulkanStatus VulkanSwapChain::GetSurfaceCapabilitiesAndPresentMode(VkPhysicalDevice gpu, uint32_t width, uint32_t height)
{
    fpGetPhysicalDeviceSurfaceCapabilitiesKHR(gpu, scPublicVars.surface, &scPrivateVars.surfCapabilities);

    // If surface capabilities returned zeros, the window might not be ready yet (e.g. minimized).
    // Return a transient error so the caller can defer + retry instead of the process dying (was exit(-1)).
    if (scPrivateVars.surfCapabilities.maxImageExtent.width == 0 ||
        scPrivateVars.surfCapabilities.maxImageExtent.height == 0) {
        LOG_ERROR("ERROR: Surface capabilities returned invalid dimensions!");
        LOG_ERROR("Window dimensions: " + std::to_string(width) + "x" + std::to_string(height));
        LOG_ERROR("Surface capabilities: " + std::to_string(scPrivateVars.surfCapabilities.maxImageExtent.width)
                  + "x" + std::to_string(scPrivateVars.surfCapabilities.maxImageExtent.height));
        return std::unexpected(VulkanError{VK_ERROR_OUT_OF_DATE_KHR,
            "Surface reported zero extent (window not ready / minimized); defer swapchain setup"});
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

    // The public Extent MUST equal the extent the swapchain images are created at
    // (imageExtent = swapChainExtent, set below in CreateSwapChainColorImages). Framebuffers
    // and renderArea are built on those images, so if the public Extent desyncs from the image
    // extent — e.g. the requested window size differs from the surface's currentExtent — the
    // unrendered remainder of each image shows as uninitialized garbage (horizontal strips).
    scPublicVars.Extent = scPrivateVars.swapChainExtent;

    return {};  // success
}

VkExtent2D VulkanSwapChain::QueryCurrentSurfaceExtent(VkPhysicalDevice gpu) const
{
    VkSurfaceCapabilitiesKHR caps{};
    fpGetPhysicalDeviceSurfaceCapabilitiesKHR(gpu, scPublicVars.surface, &caps);
    return caps.currentExtent;
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

void VulkanSwapChain::CreateSwapChainColorImages(VkDevice device, VkSwapchainKHR oldSwapchain)
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
    scInfo.oldSwapchain = oldSwapchain;
    scInfo.clipped = VK_TRUE;
    scInfo.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    scInfo.imageUsage = imageUsageFlags;
    scInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    scInfo.queueFamilyIndexCount = 0;
    scInfo.pQueueFamilyIndices = nullptr;

    // Create the new swapchain. Per the Vulkan retirement model, oldSwapchain (if not VK_NULL_HANDLE)
    // becomes retired but remains a valid handle until explicitly destroyed -- destroy it only AFTER
    // this succeeds, so a failed recreation doesn't leave us without any swapchain at all.
    VkSwapchainKHR newSwapchain = VK_NULL_HANDLE;
    result = fpCreateSwapchainKHR(device, &scInfo, nullptr, &newSwapchain);
    if (result != VK_SUCCESS) {
        throw std::runtime_error(std::string("VulkanSwapChain::CreateSwapChainColorImages - fpCreateSwapchainKHR failed: ") + std::to_string(static_cast<int>(result)));
    }

    if (oldSwapchain != VK_NULL_HANDLE && fpDestroySwapchainKHR) {
        fpDestroySwapchainKHR(device, oldSwapchain, nullptr);
    }
    scPublicVars.swapChain = newSwapchain;

    // Mirror the ACTUAL negotiated usage flags (post STORAGE-bit-drop, see GetSupportedFormats)
    // onto the public struct so graph consumers (e.g. DescriptorSetNode::HandleStorageImage) can
    // check what the swapchain images were really created with, not what was requested.
    scPublicVars.ImageUsageFlags = imageUsageFlags;

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
