#pragma once
#include <vulkan/vulkan.h>
#include <vector>

namespace Vixen::Vulkan::Resources {

/// Abstract render target: a set of color images (one per in-flight frame) the recording nodes
/// draw into / sample. Both the swapchain (SwapChainPublicVariables) and offscreen targets
/// (RenderTargetData) implement it, so recording nodes depend on this, not the swapchain (AR#28).
struct IRenderTarget {
    virtual ~IRenderTarget() = default;

    virtual uint32_t    GetImageCount()   const = 0;  ///< number of color images (>=1)
    virtual uint32_t    GetCurrentIndex() const = 0;  ///< index of the image in use this frame
    virtual VkImage     GetImage(uint32_t i) const = 0;
    virtual VkImageView GetView(uint32_t i)  const = 0;
    virtual VkFormat    GetFormat()   const = 0;
    virtual VkExtent2D  GetExtent()   const = 0;

    VkImage     GetCurrentImage() const { return GetImage(GetCurrentIndex()); }
    VkImageView GetCurrentView()  const { return GetView(GetCurrentIndex()); }

    // Ergonomic conversions (preserve call sites that relied on the swapchain's implicit
    // conversions). Resolve to the CURRENT image/view.
    operator VkImageView() const { return GetCurrentView(); }
    operator VkImage()     const { return GetCurrentImage(); }
};

/// One offscreen color buffer: image + its backing memory + a view.
struct RenderTargetBuffer {
    VkImage        image  = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView    view   = VK_NULL_HANDLE;
};

/// Offscreen render target produced by RenderTargetNode.
class RenderTargetData : public IRenderTarget {
public:
    std::vector<RenderTargetBuffer> buffers;
    uint32_t   currentIndex = 0;
    VkFormat   format = VK_FORMAT_UNDEFINED;
    VkExtent2D extent = {0, 0};

    uint32_t    GetImageCount()   const override { return static_cast<uint32_t>(buffers.size()); }
    uint32_t    GetCurrentIndex() const override { return currentIndex; }
    VkImage     GetImage(uint32_t i) const override { return i < buffers.size() ? buffers[i].image : VK_NULL_HANDLE; }
    VkImageView GetView(uint32_t i)  const override { return i < buffers.size() ? buffers[i].view  : VK_NULL_HANDLE; }
    VkFormat    GetFormat()   const override { return format; }
    VkExtent2D  GetExtent()   const override { return extent; }
};

} // namespace Vixen::Vulkan::Resources
