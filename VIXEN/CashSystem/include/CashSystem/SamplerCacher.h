#pragma once

#include "Headers.h"
#include "TypedCacher.h"
#include <cstdint>
#include <string>
#include <memory>

namespace CashSystem {

/**
 * @brief Resource wrapper for VkSampler
 *
 * Stores VkSampler and associated metadata for debugging.
 */
struct SamplerWrapper {
    VkSampler resource = VK_NULL_HANDLE;

    // Cache identification metadata (for debugging/logging)
    VkFilter minFilter = VK_FILTER_LINEAR;
    VkFilter magFilter = VK_FILTER_LINEAR;
    VkSamplerAddressMode addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    VkSamplerAddressMode addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    VkSamplerAddressMode addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    float maxAnisotropy = 16.0f;
    VkBool32 compareEnable = VK_FALSE;
    VkCompareOp compareOp = VK_COMPARE_OP_NEVER;
};

/**
 * @brief Creation parameters for VkSampler
 *
 * All parameters that affect sampler creation.
 * Used to generate cache keys and create samplers.
 */
struct SamplerCreateParams {
    VkFilter minFilter = VK_FILTER_LINEAR;
    VkFilter magFilter = VK_FILTER_LINEAR;
    VkSamplerAddressMode addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    VkSamplerAddressMode addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    VkSamplerAddressMode addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    float maxAnisotropy = 16.0f;
    VkBool32 compareEnable = VK_FALSE;
    VkCompareOp compareOp = VK_COMPARE_OP_NEVER;
    float mipLodBias = 0.0f;
    float minLod = 0.0f;
    float maxLod = 1000.0f;
    VkBorderColor borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    VkBool32 unnormalizedCoordinates = VK_FALSE;
};

/**
 * @brief TypedCacher for VkSampler resources
 *
 * Caches samplers based on filter modes, address modes, and anisotropy settings.
 * Samplers are small resources but frequently reused with limited unique combinations.
 *
 * Usage:
 * ```cpp
 * auto& mainCacher = GetOwningGraph()->GetMainCacher();
 *
 * // Register if needed (done in node)
 * if (!mainCacher.IsRegistered(std::type_index(typeid(SamplerWrapper)))) {
 *     mainCacher.RegisterCacher<SamplerCacher, SamplerWrapper, SamplerCreateParams>(
 *         std::type_index(typeid(SamplerWrapper)),
 *         "Sampler",
 *         true  // device-dependent
 *     );
 * }
 *
 * // Get cacher
 * auto* cacher = mainCacher.GetCacher<SamplerCacher, SamplerWrapper, SamplerCreateParams>(
 *     std::type_index(typeid(SamplerWrapper)), device
 * );
 *
 * // Create parameters
 * SamplerCreateParams params{};
 * params.minFilter = VK_FILTER_LINEAR;
 * params.magFilter = VK_FILTER_LINEAR;
 * // ... set other params
 *
 * // Get or create cached sampler
 * auto wrapper = cacher->GetOrCreate(params);
 * VkSampler sampler = wrapper->resource;
 * ```
 */
class SamplerCacher : public TypedCacher<SamplerWrapper, SamplerCreateParams> {
public:
    SamplerCacher() = default;
    ~SamplerCacher() override = default;

    // Override to add cache hit/miss logging
    std::shared_ptr<SamplerWrapper> GetOrCreate(const SamplerCreateParams& ci);

protected:
    // TypedCacher implementation
    std::shared_ptr<SamplerWrapper> Create(const SamplerCreateParams& ci) override;
    std::uint64_t ComputeKey(const SamplerCreateParams& ci) const override;

    // Resource cleanup
    void Cleanup() override;

    // Serialization
    bool SerializeToFile(const std::filesystem::path& path) const override;
    bool DeserializeFromFile(const std::filesystem::path& path, void* device) override;
    std::string_view name() const noexcept override { return "SamplerCacher"; }
};

} // namespace CashSystem
