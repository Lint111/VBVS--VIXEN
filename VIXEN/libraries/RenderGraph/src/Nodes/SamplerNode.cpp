#include "Nodes/SamplerNode.h"
#include "Core/NodeRegistration.h"
#include "VulkanDevice.h"
#include <stdexcept>

namespace Vixen::RenderGraph {

void SamplerNode::TypedCompileImpl(TypedCompileContext& ctx) {
    SetDevice(ctx.In(SamplerNodeConfig::VULKAN_DEVICE_IN));
    if (!GetDevice()) throw std::runtime_error("[SamplerNode] VULKAN_DEVICE_IN is null");
    // Persistent across recompile, like the images it samples.
    if (sampler_ == VK_NULL_HANDLE) {
        VkSamplerCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        info.magFilter = VK_FILTER_NEAREST;
        info.minFilter = VK_FILTER_NEAREST;
        info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        info.maxLod = 0.0f;
        info.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
        if (vkCreateSampler(GetDevice()->device, &info, nullptr, &sampler_) != VK_SUCCESS)
            throw std::runtime_error("[SamplerNode] vkCreateSampler failed");
    }
    ctx.Out(SamplerNodeConfig::SAMPLER, sampler_);
}

void SamplerNode::TypedDeviceReleaseImpl(TypedCleanupContext&) {
    if (sampler_ != VK_NULL_HANDLE && GetDevice()) vkDestroySampler(GetDevice()->device, sampler_, nullptr);
    sampler_ = VK_NULL_HANDLE;
}

} // namespace Vixen::RenderGraph

VIXEN_REGISTER_NODE(Vixen::RenderGraph::SamplerNodeType);
