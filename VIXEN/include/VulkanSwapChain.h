#pragma once

#include "Headers.h"
#include "RenderGraph/include/Core/VulkanLimits.h"
#include <array>

class VulkanApplication;
class VulkanApplicationBase;
class VulkanRenderer;


struct SwapChainBuffer {
    VkImage image;
    VkImageView view;
};


struct SwapChainPrivateVariables {
    // Store image surface capabilities
    VkSurfaceCapabilitiesKHR surfCapabilities;

    // Store number of present modes
    uint32_t presentModeCount;

    // Array for retrived present modes
    std::vector<VkPresentModeKHR> presentModes;  // Variable size, keep as vector

    // Size of the swwapChain color images
    VkExtent2D swapChainExtent;

    // Number of color images supported
    uint32_t desiredNumberOfSwapChainImages;
    VkSurfaceTransformFlagBitsKHR preTransform;

    // Store present mode bitwise flag
    VkPresentModeKHR swapChainPresentMode;

    // Phase H: Convert to stack array (bounded by MAX_SWAPCHAIN_IMAGES)
    // The retrived drawing color swapchain images
    std::array<VkImage, Vixen::RenderGraph::MAX_SWAPCHAIN_IMAGES> swapChainImages{};
    uint32_t swapChainImageCount = 0;  // Track actual count

    std::vector<VkSurfaceFormatKHR> surfaceFormats;  // Variable size, keep as vector
};

struct SwapChainPublicVariables {
    // The logical platform dependent surface object
    VkSurfaceKHR surface;

    // Number of buffer image used for swapchain
    uint32_t swapChainImageCount;

    // SwapChain object
    VkSwapchainKHR swapChain;

    // Phase H: Convert to stack array (bounded by MAX_SWAPCHAIN_IMAGES)
    // List of color swapchain images
    std::array<SwapChainBuffer, Vixen::RenderGraph::MAX_SWAPCHAIN_IMAGES> colorBuffers{};

    // Current drawing surface index in use
    uint32_t currentColorBuffer;

    // Format of the color image
    VkFormat Format;

	// Extends of the swapchain images
	VkExtent2D Extent;

    // Implicit conversion operators for render graph connections
    // When nodes expect VkImageView, automatically provide current image view
    operator VkImageView() const {
        if (currentColorBuffer < colorBuffers.size()) {
            return colorBuffers[currentColorBuffer].view;
        }
        return VK_NULL_HANDLE;
    }

    // When nodes expect VkImage, automatically provide current image
    operator VkImage() const {
        if (currentColorBuffer < colorBuffers.size()) {
            return colorBuffers[currentColorBuffer].image;
        }
        return VK_NULL_HANDLE;
    }

    operator VkSurfaceKHR() const {
        return surface;
    }

    operator VkSwapchainKHR() const {
        return swapChain;
    }

    // Phase H: Convert array to vector for compatibility
    operator std::vector<SwapChainBuffer>() const {
        return std::vector<SwapChainBuffer>(colorBuffers.begin(), colorBuffers.begin() + swapChainImageCount);
    }

    operator VkFormat() const {
        return Format;
    }

    operator VkExtent2D() const {
        return Extent;
    }
    
};

class VulkanSwapChain {
    public:
        VulkanSwapChain() {};
        ~VulkanSwapChain() {};


    public:
    void Initialize();
	void CleanUp();
    void Destroy(VkDevice device, VkInstance instance);  // Proper cleanup with all resources
    void CreateSwapChain(const VkCommandBuffer& cmd);
    void DestroySwapChain(VkDevice device);
    void SetSwapChainExtent(uint32_t width, uint32_t height);

    // Swapchain creation methods (exposed for SwapChainNode)
    VkResult CreateSwapChainExtensions(VkInstance instance, VkDevice device);
    void GetSupportedFormats(VkPhysicalDevice gpu);
    VkResult CreateSurface(VkInstance instance, HWND hwnd, HINSTANCE hinstance);
    void DestroySurface(VkInstance instance);
    uint32_t GetGraphicsQueueWithPresentationSupport(VkPhysicalDevice gpu, uint32_t queueFamilyCount, const std::vector<VkQueueFamilyProperties>& queueProps);
    void GetSurfaceCapabilitiesAndPresentMode(VkPhysicalDevice gpu, uint32_t width, uint32_t height);
    void ManagePresentMode();
    void CreateSwapChainColorImages(VkDevice device);
    void CreateColorImageView(VkDevice device, const VkCommandBuffer& cmd);

    // Image usage configuration
    void SetImageUsageFlags(VkImageUsageFlags flags);

    private:

    public:
    // user defined structure containing public variables used by the swapchain
    // private and public functions.
    SwapChainPublicVariables scPublicVars;
    PFN_vkQueuePresentKHR fpQueuePresentKHR;
    PFN_vkAcquireNextImageKHR fpAcquireNextImageKHR;



    private:
    PFN_vkGetPhysicalDeviceSurfaceSupportKHR fpGetPhysicalDeviceSurfaceSupportKHR;
    PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR fpGetPhysicalDeviceSurfaceCapabilitiesKHR;
    PFN_vkGetPhysicalDeviceSurfaceFormatsKHR fpGetPhysicalDeviceSurfaceFormatsKHR;
    PFN_vkGetPhysicalDeviceSurfacePresentModesKHR fpGetPhysicalDeviceSurfacePresentModesKHR;
    PFN_vkDestroySurfaceKHR fpDestroySurfaceKHR;

    // Layer extension debugging
    PFN_vkCreateSwapchainKHR fpCreateSwapchainKHR;
    PFN_vkDestroySwapchainKHR fpDestroySwapchainKHR;
    PFN_vkGetSwapchainImagesKHR fpGetSwapchainImagesKHR;

    private:
    SwapChainPrivateVariables scPrivateVars;

    // Configurable image usage flags (default for graphics + compute)
    VkImageUsageFlags imageUsageFlags = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                                         VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                                         VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                                         VK_IMAGE_USAGE_STORAGE_BIT;

    bool supportsScalingExtension = false;
};