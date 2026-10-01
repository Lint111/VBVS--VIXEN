#pragma once
#include <vulkan/vulkan.h>
#include <vector>

namespace Vixen::Vulkan::Resources {

// Window images use binary acquire/present handoffs. Offscreen image rings use only the
// graph's internal dependencies and frame completion fence; empty arrays never define this.
enum class TargetSynchronization { Offscreen, WsiAcquirePresent };

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

    // ACTUAL usage flags these images were created with. For most render targets this is exactly
    // what was requested at creation; the swapchain is the notable exception -- its
    // negotiated-format logic can silently drop VK_IMAGE_USAGE_STORAGE_BIT on devices/formats that
    // don't support it (see SwapChainPublicVariables::ImageUsageFlags), so this is NOT guaranteed
    // to match whatever usage flags a caller originally asked for. Consumers that intend to bind
    // one of these images as a specific descriptor type (e.g. VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
    // must check the corresponding usage bit here first -- see the SupportsStorageImage() helper
    // below -- rather than assume the request succeeded. Binding an image lacking the matching
    // usage bit is a Vulkan spec violation (e.g. VUID-VkWriteDescriptorSet-descriptorType-00339)
    // that some drivers segfault on instead of erroring cleanly.
    virtual VkImageUsageFlags GetImageUsageFlags() const = 0;
    virtual TargetSynchronization GetSynchronization() const = 0;
    bool UsesWsiSynchronization() const { return GetSynchronization() == TargetSynchronization::WsiAcquirePresent; }

    // Last layout recorded by the graph for each physical image. Resource-access dependencies
    // order writers; capture reads this only between completed frames. Recreated buffers start
    // UNDEFINED, so handle reuse cannot inherit a previous allocation's layout.
    virtual VkImageLayout GetImageLayout(uint32_t i) const = 0;
    virtual void SetImageLayout(uint32_t i, VkImageLayout layout) = 0;

    VkImage     GetCurrentImage() const { return GetImage(GetCurrentIndex()); }
    VkImageView GetCurrentView()  const { return GetView(GetCurrentIndex()); }
    VkImageLayout GetCurrentLayout() const { return GetImageLayout(GetCurrentIndex()); }

    // Easy check: can this render target's images legally be bound as VK_DESCRIPTOR_TYPE_STORAGE_IMAGE?
    bool SupportsStorageImage() const { return (GetImageUsageFlags() & VK_IMAGE_USAGE_STORAGE_BIT) != 0; }

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
    VkImageLayout  layout = VK_IMAGE_LAYOUT_UNDEFINED;
};

/// Offscreen render target produced by RenderTargetNode.
class RenderTargetData : public IRenderTarget {
public:
    static constexpr TargetSynchronization Synchronization = TargetSynchronization::Offscreen;
    std::vector<RenderTargetBuffer> buffers;
    uint32_t   currentIndex = 0;
    VkFormat   format = VK_FORMAT_UNDEFINED;
    VkExtent2D extent = {0, 0};
    // Usage flags these images were actually created with. Unlike the swapchain, RenderTargetNode
    // passes the graph-derived usage straight into VkImageCreateInfo with no silent-stripping, so this
    // always matches what was requested -- set it to that same value when populating this struct.
    VkImageUsageFlags imageUsageFlags = 0;

    uint32_t    GetImageCount()   const override { return static_cast<uint32_t>(buffers.size()); }
    uint32_t    GetCurrentIndex() const override { return currentIndex; }
    VkImage     GetImage(uint32_t i) const override { return i < buffers.size() ? buffers[i].image : VK_NULL_HANDLE; }
    VkImageView GetView(uint32_t i)  const override { return i < buffers.size() ? buffers[i].view  : VK_NULL_HANDLE; }
    VkFormat    GetFormat()   const override { return format; }
    VkExtent2D  GetExtent()   const override { return extent; }
    VkImageUsageFlags GetImageUsageFlags() const override { return imageUsageFlags; }
    TargetSynchronization GetSynchronization() const override { return Synchronization; }
    VkImageLayout GetImageLayout(uint32_t i) const override { return buffers.at(i).layout; }
    void SetImageLayout(uint32_t i, VkImageLayout layout) override { buffers.at(i).layout = layout; }
};

} // namespace Vixen::Vulkan::Resources
