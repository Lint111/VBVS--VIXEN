#pragma once

#include "Headers.h"

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
    std::vector<VkPresentModeKHR> presentModes;

    // Size of the swwapChain color images
    VkExtent2D swapChainExtent;

    // Number of color images supported
    uint32_t desiredNumberOfSwapChainImages;
    VkSurfaceTransformFlagBitsKHR preTransform;
    
    // Store present mode bitwise flag
    VkPresentModeKHR swapChainPresentMode;

    // The retrived drawing color swapchain images
    std::vector<VkImage> swapChainImages;

    std::vector<VkSurfaceFormatKHR> surfaceFormats;
};

struct SwapChainPublicVariables {
    // The logical platform dependent surface object
    VkSurfaceKHR surface;

    // Number of buffer image used for swapchain
    uint32_t swapChainImageCount;

    // SwapChain object
    VkSwapchainKHR swapChain;

    // List of color swapchain images
    std::vector<SwapChainBuffer> colorBuffers;

    // Current drawing surface index in use
    uint32_t currentColorBuffer;

    // Format of the color image
    VkFormat Format;

};

class VulkanSwapChain {
    public:
    VulkanSwapChain(VulkanRenderer* renderer);
    ~VulkanSwapChain();


    public:
    void Initialize();
    void CreateSwapChain(const VkCommandBuffer& cmd);
    void DestroySwapChain();
    void SetSwapChainExtent(uint32_t width, uint32_t height);

    private:
    VkResult CreateSwapChainExtensions();
    void GetSupportedFormats();
    VkResult CreateSurface();
    void DestroySurface();
    uint32_t GetGraphicsQueueWithPresentationSupport();
    void GetSurfaceCapabilitiesAndPresentMode();
    void ManagePresentMode();
    void CreateSwapChainColorImages();
    void CreateColorImageView(const VkCommandBuffer& cmd);

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
    VulkanRenderer* rendererObj;
    VulkanApplicationBase* appObj;

    bool supportsScalingExtension = false;
};