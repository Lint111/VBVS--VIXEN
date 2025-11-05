#pragma once

#include "Headers.h"
#include "TypedCacher.h"
#include "MainCacher.h"
#include "PipelineLayoutCacher.h"
#include <cstdint>
#include <string>
#include <vector>
#include <memory>

namespace CashSystem {

// Forward declarations
namespace Vixen::Vulkan::Resources {
    class VulkanDevice;
}

/**
 * @brief Compute pipeline resource wrapper
 *
 * Stores VkPipeline (compute) and associated metadata.
 * Pipeline layout is shared via PipelineLayoutCacher.
 */
struct ComputePipelineWrapper {
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkPipelineCache cache = VK_NULL_HANDLE;

    // Shared pipeline layout (from PipelineLayoutCacher)
    std::shared_ptr<PipelineLayoutWrapper> pipelineLayoutWrapper;

    // Cache identification
    std::string shaderKey;
    std::string layoutKey;

    // Workgroup size (metadata for dispatch calculations)
    uint32_t workgroupSizeX = 8;
    uint32_t workgroupSizeY = 8;
    uint32_t workgroupSizeZ = 1;
};

/**
 * @brief Compute pipeline creation parameters
 *
 * Supports two modes:
 * 1. Explicit: Provide pipelineLayoutWrapper from PipelineLayoutCacher (transparent, efficient)
 * 2. Convenience: Provide descriptorSetLayout, ComputePipelineCacher creates layout internally
 */
struct ComputePipelineCreateParams {
    // ===== Sub-cacher Resources (Explicit Dependencies) =====
    // If provided, use directly (recommended for transparency)
    std::shared_ptr<PipelineLayoutWrapper> pipelineLayoutWrapper;

    // ===== Convenience Fallbacks =====
    // If pipelineLayoutWrapper not provided, create from these:
    VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
    std::vector<VkPushConstantRange> pushConstantRanges;

    // ===== Direct Pipeline Resources =====
    VkShaderModule shaderModule = VK_NULL_HANDLE;
    const char* entryPoint = "main";

    // Keys for cache lookup
    std::string shaderKey;
    std::string layoutKey;

    // Workgroup size (for dispatch calculations)
    uint32_t workgroupSizeX = 8;
    uint32_t workgroupSizeY = 8;
    uint32_t workgroupSizeZ = 1;

    // Shader specialization constants (if needed)
    std::vector<VkSpecializationMapEntry> specMapEntries;
    std::vector<uint8_t> specData;
};

/**
 * @brief TypedCacher for compute pipeline resources
 *
 * Caches compiled compute pipelines based on:
 * - Shader module key
 * - Pipeline layout
 * - Workgroup size (metadata only)
 * - Specialization constants
 */
class ComputePipelineCacher : public TypedCacher<ComputePipelineWrapper, ComputePipelineCreateParams> {
public:
    ComputePipelineCacher() = default;
    ~ComputePipelineCacher() override = default;

    // Override to add cache hit/miss logging
    std::shared_ptr<ComputePipelineWrapper> GetOrCreate(const ComputePipelineCreateParams& ci);

    // Convenience: Get the global pipeline cache (shared with graphics)
    VkPipelineCache GetPipelineCache() const { return m_globalCache; }

    // Serialization
    bool SerializeToFile(const std::filesystem::path& path) const override;
    bool DeserializeFromFile(const std::filesystem::path& path, void* device) override;
    std::string_view name() const noexcept override { return "ComputePipelineCacher"; }

protected:
    // TypedCacher implementation
    std::shared_ptr<ComputePipelineWrapper> Create(const ComputePipelineCreateParams& ci) override;
    std::uint64_t ComputeKey(const ComputePipelineCreateParams& ci) const override;

    // Resource cleanup
    void Cleanup() override;

private:
    // Helper methods
    void CreateComputePipeline(const ComputePipelineCreateParams& ci, ComputePipelineWrapper& wrapper);
    void CreatePipelineLayout(const ComputePipelineCreateParams& ci, ComputePipelineWrapper& wrapper);

    // Global pipeline cache (shared with graphics pipelines)
    VkPipelineCache m_globalCache = VK_NULL_HANDLE;
};

} // namespace CashSystem
