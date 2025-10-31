#pragma once

#include "Headers.h"
#include "TypedCacher.h"
#include <vulkan/vulkan.h>
#include <memory>
#include <vector>

namespace CashSystem {

// Forward declarations
namespace Vixen::Vulkan::Resources {
    class VulkanDevice;
}

/**
 * @brief Pipeline layout resource wrapper
 */
struct PipelineLayoutWrapper {
    VkPipelineLayout layout = VK_NULL_HANDLE;

    // Source descriptor set layout (NOT owned - just for reference)
    VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;

    // Push constant ranges (optional)
    std::vector<VkPushConstantRange> pushConstantRanges;
};

/**
 * @brief Pipeline layout creation parameters
 */
struct PipelineLayoutCreateParams {
    // Descriptor set layout (NOT owned by this cacher)
    VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;

    // Push constant ranges (optional)
    std::vector<VkPushConstantRange> pushConstantRanges;

    // Cache key
    std::string layoutKey;
};

/**
 * @brief TypedCacher for pipeline layout resources
 *
 * Enables sharing of VkPipelineLayout across multiple pipelines with same descriptor layout.
 * Key: Hash of descriptor set layout handle + push constant configuration
 */
class PipelineLayoutCacher : public TypedCacher<PipelineLayoutWrapper, PipelineLayoutCreateParams> {
public:
    PipelineLayoutCacher() = default;
    ~PipelineLayoutCacher() override = default;

    // Override to add cache hit/miss logging
    std::shared_ptr<PipelineLayoutWrapper> GetOrCreate(const PipelineLayoutCreateParams& ci);

protected:
    // TypedCacher implementation
    std::shared_ptr<PipelineLayoutWrapper> Create(const PipelineLayoutCreateParams& ci) override;
    std::uint64_t ComputeKey(const PipelineLayoutCreateParams& ci) const override;

    // Resource cleanup
    void Cleanup() override;

    // Serialization (not implemented for layouts)
    bool SerializeToFile(const std::filesystem::path& path) const override;
    bool DeserializeFromFile(const std::filesystem::path& path, void* device) override;
    std::string_view name() const noexcept override { return "PipelineLayoutCacher"; }
};

} // namespace CashSystem
