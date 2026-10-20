#pragma once
#include "Core/TypedNodeInstance.h"
#include "Core/NodeType.h"
#include "VulkanDeviceFwd.h"
#include <vulkan/vulkan.h>

namespace Vixen::RenderGraph {

CONSTEXPR_NODE_CONFIG(SamplerNodeConfig, 1, 1, SlotArrayMode::Single) {
    INPUT_SLOT(VULKAN_DEVICE_IN, Vixen::Vulkan::Resources::VulkanDevice*, 0, SlotNullability::Required,
        SlotRole::Dependency, SlotMutability::ReadOnly, SlotScope::NodeLevel);
    OUTPUT_SLOT(SAMPLER, VkSampler, 0, SlotNullability::Required, SlotMutability::WriteOnly);
    SamplerNodeConfig() {
        INIT_INPUT_DESC(VULKAN_DEVICE_IN, "vulkan_device", ResourceLifetime::Persistent, HandleDescriptor{"VulkanDevice*"});
        INIT_OUTPUT_DESC(SAMPLER, "sampler", ResourceLifetime::Persistent, HandleDescriptor{"VkSampler"});
    }
};

// The separate sampler a generated system's sampled view reads through (manifest form
// `sampled` + `samplerBinding`). Nearest filtering at lod 0 with clamp-to-edge addressing:
// a texel-centre read returns the stored texel and a read past the frame edge returns the edge.
class SamplerNode final : public TypedNode<SamplerNodeConfig> {
public:
    SamplerNode(const std::string& name, NodeType* type) : TypedNode(name, type) {}
protected:
    void TypedCompileImpl(TypedCompileContext& ctx) override;
    void TypedCleanupImpl(TypedCleanupContext& ctx) override;
private:
    VkSampler sampler_ = VK_NULL_HANDLE;
};

class SamplerNodeType final : public TypedNodeType<SamplerNodeConfig> {
public:
    SamplerNodeType() : TypedNodeType("Sampler") {}
    std::unique_ptr<NodeInstance> CreateInstance(const std::string& name) const override {
        return std::make_unique<SamplerNode>(name, const_cast<SamplerNodeType*>(this));
    }
};

} // namespace Vixen::RenderGraph
