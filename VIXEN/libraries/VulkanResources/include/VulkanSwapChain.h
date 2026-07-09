#pragma once

#include "Headers.h"
#include "ILoggable.h"
#include "error/VulkanError.h"  // VulkanStatus for de-fatalized surface-capability queries
#include "IRenderTarget.h"

struct GLFWwindow;  // cross-platform window handle (GLFW); real include only in the .cpp

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

struct SwapChainPublicVariables : public Vixen::Vulkan::Resources::IRenderTarget {
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

	// Extends of the swapchain images
	VkExtent2D Extent;

    // ACTUAL usage flags the swapchain images were created with (set right after
    // vkCreateSwapchainKHR succeeds -- see VulkanSwapChain::CreateSwapChainColorImages). May be a
    // strict subset of what was requested via SetImageUsageFlags(): GetSupportedFormats() drops
    // VK_IMAGE_USAGE_STORAGE_BIT when the negotiated surface format lacks
    // VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT (common on software rasterizers). Consumers that intend
    // to bind a swapchain image as VK_DESCRIPTOR_TYPE_STORAGE_IMAGE MUST check
    // (ImageUsageFlags & VK_IMAGE_USAGE_STORAGE_BIT) here -- not assume the request succeeded --
    // or risk VUID-VkWriteDescriptorSet-descriptorType-00339 (segfaults inside some drivers rather
    // than erroring cleanly).
    VkImageUsageFlags ImageUsageFlags = 0;

    // IRenderTarget interface implementation
    uint32_t    GetImageCount()   const override { return swapChainImageCount; }
    uint32_t    GetCurrentIndex() const override { return currentColorBuffer; }
    VkImage     GetImage(uint32_t i) const override { return i < colorBuffers.size() ? colorBuffers[i].image : VK_NULL_HANDLE; }
    VkImageView GetView(uint32_t i)  const override { return i < colorBuffers.size() ? colorBuffers[i].view  : VK_NULL_HANDLE; }
    VkFormat    GetFormat()   const override { return Format; }
    VkExtent2D  GetExtent()   const override { return Extent; }
    VkImageUsageFlags GetImageUsageFlags() const override { return ImageUsageFlags; }

    operator VkSurfaceKHR() const {
        return surface;
    }

    operator VkSwapchainKHR() const {
        return swapChain;
    }

    operator std::vector<SwapChainBuffer>() const {
        return colorBuffers;
    }

    operator VkFormat() const {
        return Format;
    }

    operator VkExtent2D() const {
        return Extent;
    }
    
};

class VulkanSwapChain : public ILoggable {
    public:
        VulkanSwapChain() { InitializeLogger("VulkanSwapChain"); };
        ~VulkanSwapChain() {};


    public:
    void Initialize();
	void CleanUp();
    void Destroy(VkDevice device, VkInstance instance);  // Proper cleanup with all resources
    void CreateSwapChain(const VkCommandBuffer& cmd);
    void DestroySwapChain(VkDevice device);
    // Destroys only the per-image VkImageViews (colorBuffers), NOT the VkSwapchainKHR handle itself.
    // Used ahead of an oldSwapchain-based recreation: the old views are tied to the old swapchain's
    // images and must go, but the old swapchain handle itself needs to survive to be passed as
    // scInfo.oldSwapchain (see CreateSwapChainColorImages) so the driver can recycle/hand over
    // presentation state instead of a full cold recreation.
    void DestroyImageViewsOnly(VkDevice device);
    void SetSwapChainExtent(uint32_t width, uint32_t height);

    // Swapchain creation methods (exposed for SwapChainNode)
    VkResult CreateSwapChainExtensions(VkInstance instance, VkDevice device);
    void GetSupportedFormats(VkPhysicalDevice gpu);
    VkResult CreateSurface(VkInstance instance, GLFWwindow* window);
    void DestroySurface(VkInstance instance);
    uint32_t GetGraphicsQueueWithPresentationSupport(VkPhysicalDevice gpu, uint32_t queueFamilyCount, const std::vector<VkQueueFamilyProperties>& queueProps);
    // Returns an error (instead of the old exit(-1)) when the surface reports zero extent -- e.g.
    // the window is minimized / not yet sized -- so the caller can defer + retry rather than die.
    VulkanStatus GetSurfaceCapabilitiesAndPresentMode(VkPhysicalDevice gpu, uint32_t width, uint32_t height);
    // Side-effect-free surface extent probe (no state mutation, unlike GetSurfaceCapabilitiesAndPresentMode).
    // Used by SwapChainNode's VK_SUBOPTIMAL_KHR handler to decide whether a recreation is actually
    // needed (extent changed) versus a same-extent recreation that can never clear SUBOPTIMAL.
    VkExtent2D QueryCurrentSurfaceExtent(VkPhysicalDevice gpu) const;
    void ManagePresentMode();
    // oldSwapchain (default VK_NULL_HANDLE): passed as VkSwapchainCreateInfoKHR::oldSwapchain so the
    // driver can reuse/hand over presentation state instead of a cold recreation. On success, the OLD
    // swapchain handle is destroyed here (after the new one exists, per the Vulkan spec's retirement
    // model) and scPublicVars.swapChain is overwritten with the new handle. Callers must have already
    // destroyed the old per-image views (DestroyImageViewsOnly) before calling this.
    void CreateSwapChainColorImages(VkDevice device, VkSwapchainKHR oldSwapchain = VK_NULL_HANDLE);
    void CreateColorImageView(VkDevice device, const VkCommandBuffer& cmd);

    // Image usage configuration
    void SetImageUsageFlags(VkImageUsageFlags flags);
    // Returns the ACTUAL usage flags the swapchain images were created with -- may differ from
    // what SetImageUsageFlags() requested. GetSupportedFormats() silently drops
    // VK_IMAGE_USAGE_STORAGE_BIT when the negotiated surface format lacks
    // VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT (software rasterizers / some layered drivers), to keep
    // swapchain creation itself valid. Callers that bind a swapchain image as a
    // VK_DESCRIPTOR_TYPE_STORAGE_IMAGE descriptor MUST check this (not the flags they requested)
    // before doing so -- binding a non-storage-capable image as STORAGE_IMAGE is
    // VUID-VkWriteDescriptorSet-descriptorType-00339 and segfaults inside some drivers rather than
    // erroring cleanly.
    VkImageUsageFlags GetImageUsageFlags() const { return imageUsageFlags; }

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