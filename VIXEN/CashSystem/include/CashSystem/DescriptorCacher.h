#pragma once

#include "Headers.h"
#include "TypedCacher.h"
#include "MainCacher.h"
#include <cstdint>
#include <string>
#include <vector>
#include <memory>

// Forward declarations for ShaderManagement types
namespace ShaderManagement {
    struct DescriptorLayoutSpec;
}

namespace CashSystem {

/**
 * @brief Descriptor resource wrapper
 * 
 * Stores descriptor set layout, pool, and allocated sets.
 */
struct DescriptorWrapper {
    VkDescriptorSetLayout layout = VK_NULL_HANDLE;
    VkDescriptorPool pool = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> sets;
    
    // Cache identification
    std::string layoutHash;
    uint32_t maxSets = 1;
    uint32_t poolSizeCount = 0;
    
    // Layout specification
    const ShaderManagement::DescriptorLayoutSpec* layoutSpec = nullptr;
};

/**
 * @brief Descriptor creation parameters
 */
struct DescriptorCreateParams {
    const ShaderManagement::DescriptorLayoutSpec* layoutSpec = nullptr;
    uint32_t maxSets = 1;
    std::string layoutHash;
    
    // Pool configuration (derived from layout spec)
    std::vector<VkDescriptorPoolSize> poolSizes;
};

/**
 * @brief TypedCacher for descriptor resources
 * 
 * Caches descriptor set layouts, pools, and allocated sets based on:
 * - Layout specification
 * - Pool configuration
 * - Max sets count
 */
class DescriptorCacher : public TypedCacher<DescriptorWrapper, DescriptorCreateParams> {
public:
    DescriptorCacher() = default;
    ~DescriptorCacher() override = default;

    // Convenience API for descriptor creation
    std::shared_ptr<DescriptorWrapper> GetOrCreateDescriptors(
        const ShaderManagement::DescriptorLayoutSpec* layoutSpec,
        uint32_t maxSets = 1
    );

    // Serialization
    bool SerializeToFile(const std::filesystem::path& path) const override;
    bool DeserializeFromFile(const std::filesystem::path& path, void* device) override;
    std::string_view name() const noexcept override { return "DescriptorCacher"; }

protected:
    // TypedCacher implementation
    std::shared_ptr<DescriptorWrapper> Create(const DescriptorCreateParams& ci) override;
    std::uint64_t ComputeKey(const DescriptorCreateParams& ci) const override;

private:
    // Helper methods
    void CalculatePoolSizes(const ShaderManagement::DescriptorLayoutSpec* spec, 
                           std::vector<VkDescriptorPoolSize>& poolSizes) const;
    void CalculateLayoutHash(const ShaderManagement::DescriptorLayoutSpec* spec, 
                            std::string& hash) const;
};

} // namespace CashSystem