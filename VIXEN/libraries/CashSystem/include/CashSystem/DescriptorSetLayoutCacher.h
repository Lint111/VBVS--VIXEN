#pragma once

#include "Headers.h"
#include "TypedCacher.h"
#include <ShaderManagement/ShaderDataBundle.h>
#include <vulkan/vulkan.h>
#include <memory>
#include <vector>

// Forward declarations
namespace Vixen::Vulkan::Resources {
    class VulkanDevice;
}

namespace CashSystem {

/**
 * @brief Descriptor set layout resource wrapper
 */
struct DescriptorSetLayoutWrapper {
    VkDescriptorSetLayout layout = VK_NULL_HANDLE;

    // Source reflection data (for debugging/validation)
    std::vector<VkDescriptorSetLayoutBinding> bindings;

    // Cache identification
    std::string layoutKey;  // Typically descriptorInterfaceHash from ShaderDataBundle
};

/**
 * @brief Descriptor set layout creation parameters
 *
 * Supports two modes:
 * 1. **From ShaderDataBundle** (recommended): Pass bundle, automatically extracts descriptors
 * 2. **Manual bindings**: Pass explicit VkDescriptorSetLayoutBinding array
 */
struct DescriptorSetLayoutCreateParams {
    // ===== Mode 1: From ShaderDataBundle (Automatic) =====
    // If provided, reflection data is extracted automatically
    std::shared_ptr<ShaderManagement::ShaderDataBundle> shaderBundle;
    uint32_t descriptorSetIndex = 0;  // Which set to extract (default: set 0)

    // ===== Mode 2: Manual Bindings (Explicit) =====
    // If shaderBundle is null, use these bindings directly
    std::vector<VkDescriptorSetLayoutBinding> manualBindings;

    // ===== Common Parameters =====
    std::string layoutKey;  // Cache key (auto-generated from bundle or manual hash)

    // Vulkan device (required for VkDescriptorSetLayout creation)
    ::Vixen::Vulkan::Resources::VulkanDevice* device = nullptr;
};

/**
 * @brief TypedCacher for descriptor set layout resources
 *
 * Automatically creates VkDescriptorSetLayout from ShaderDataBundle reflection data.
 * Eliminates manual descriptor configuration - just pass the bundle!
 *
 * **Key Benefits:**
 * - Automatic extraction from SPIR-V reflection
 * - Content-based caching (same descriptor layout = same cache entry)
 * - Works seamlessly with shader hot-reload
 * - Supports descriptor set sharing across different shaders with identical layouts
 *
 * **Usage:**
 * @code
 * // Get cacher from MainCacher
 * auto* cacher = mainCacher->GetCacher<DescriptorSetLayoutCacher>(...);
 *
 * // Create from shader bundle (automatic)
 * DescriptorSetLayoutCreateParams params;
 * params.shaderBundle = myShaderBundle;
 * params.descriptorSetIndex = 0;
 * params.device = vulkanDevice;
 *
 * auto layout = cacher->GetOrCreate(params);
 * // Use: layout->layout (VkDescriptorSetLayout)
 * @endcode
 */
class DescriptorSetLayoutCacher : public TypedCacher<DescriptorSetLayoutWrapper, DescriptorSetLayoutCreateParams> {
public:
    DescriptorSetLayoutCacher() = default;
    ~DescriptorSetLayoutCacher() override = default;

    // Override to add cache hit/miss logging
    std::shared_ptr<DescriptorSetLayoutWrapper> GetOrCreate(const DescriptorSetLayoutCreateParams& ci);

    // Serialization (not implemented for descriptor layouts)
    bool SerializeToFile(const std::filesystem::path& path) const override;
    bool DeserializeFromFile(const std::filesystem::path& path, void* device) override;
    std::string_view name() const noexcept override { return "DescriptorSetLayoutCacher"; }

protected:
    // TypedCacher implementation
    std::shared_ptr<DescriptorSetLayoutWrapper> Create(const DescriptorSetLayoutCreateParams& ci) override;
    std::uint64_t ComputeKey(const DescriptorSetLayoutCreateParams& ci) const override;

    // Resource cleanup
    void Cleanup() override;

private:
    /**
     * @brief Extract VkDescriptorSetLayoutBinding from ShaderDataBundle
     *
     * Converts SPIR-V reflection data to Vulkan descriptor bindings.
     *
     * @param bundle Shader bundle with reflection data
     * @param setIndex Which descriptor set to extract
     * @return Vector of Vulkan bindings
     */
    std::vector<VkDescriptorSetLayoutBinding> ExtractBindingsFromBundle(
        const ShaderManagement::ShaderDataBundle& bundle,
        uint32_t setIndex
    ) const;
};

/**
 * @brief Helper: Build VkDescriptorSetLayout directly from ShaderDataBundle
 *
 * Convenience function for one-off layout creation without caching.
 * For production use, prefer DescriptorSetLayoutCacher for caching benefits.
 *
 * @param device Vulkan device
 * @param bundle Shader data bundle with reflection
 * @param setIndex Descriptor set index to extract
 * @return Created VkDescriptorSetLayout (caller owns, must destroy)
 */
VkDescriptorSetLayout BuildDescriptorSetLayoutFromReflection(
    ::Vixen::Vulkan::Resources::VulkanDevice* device,
    const ShaderManagement::ShaderDataBundle& bundle,
    uint32_t setIndex = 0
);

/**
 * @brief Helper: Extract VkPushConstantRange from ShaderDataBundle
 *
 * Converts SPIR-V push constant reflection to Vulkan ranges.
 *
 * @param bundle Shader data bundle with reflection
 * @return Vector of Vulkan push constant ranges
 */
std::vector<VkPushConstantRange> ExtractPushConstantsFromReflection(
    const ShaderManagement::ShaderDataBundle& bundle
);

/**
 * @brief Helper: Calculate descriptor pool sizes from ShaderDataBundle
 *
 * Analyzes all descriptor bindings in the bundle and calculates the pool sizes
 * needed to allocate descriptor sets. Counts descriptors by type.
 *
 * @param bundle Shader data bundle with reflection
 * @param setIndex Descriptor set index to analyze (default 0)
 * @param maxSets Maximum number of descriptor sets to allocate from pool (default 1)
 * @return Vector of VkDescriptorPoolSize for pool creation
 */
std::vector<VkDescriptorPoolSize> CalculateDescriptorPoolSizes(
    const ShaderManagement::ShaderDataBundle& bundle,
    uint32_t setIndex = 0,
    uint32_t maxSets = 1
);

} // namespace CashSystem
