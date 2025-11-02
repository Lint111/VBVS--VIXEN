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
 * Stores VkComputePipeline and associated metadata.
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

    // Equality and hash functions for cache lookup
    bool operator==(const ComputePipelineCreateParams& other) const {
        return shaderKey == other.shaderKey &&
               layoutKey == other.layoutKey &&
               workgroupSizeX == other.workgroupSizeX &&
               workgroupSizeY == other.workgroupSizeY &&
               workgroupSizeZ == other.workgroupSizeZ;
    }

    size_t Hash() const {
        size_t hash = std::hash<std::string>{}(shaderKey);
        hash ^= std::hash<std::string>{}(layoutKey) << 1;
        hash ^= std::hash<uint32_t>{}(workgroupSizeX) << 2;
        hash ^= std::hash<uint32_t>{}(workgroupSizeY) << 3;
        hash ^= std::hash<uint32_t>{}(workgroupSizeZ) << 4;
        return hash;
    }
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
class ComputePipelineCacher : public TypedCacher<ComputePipelineWrapper> {
public:
    ComputePipelineCacher(Vixen::Vulkan::Resources::VulkanDevice* device);
    ~ComputePipelineCacher() override;

    /**
     * @brief Get or create a compute pipeline
     *
     * @param params Pipeline creation parameters
     * @return Shared pointer to ComputePipelineWrapper (cached or newly created)
     */
    std::shared_ptr<ComputePipelineWrapper> GetOrCreate(
        const ComputePipelineCreateParams& params);

    /**
     * @brief Get name for cache file identification
     */
    const char* name() const override { return "ComputePipelineCacher"; }

    /**
     * @brief Cleanup all cached pipelines (called on device destruction)
     */
    void Cleanup() override;

private:
    /**
     * @brief Create a new compute pipeline
     *
     * @param params Pipeline creation parameters
     * @return Shared pointer to newly created ComputePipelineWrapper
     */
    std::shared_ptr<ComputePipelineWrapper> CreatePipeline(
        const ComputePipelineCreateParams& params);

    /**
     * @brief Generate cache key from parameters
     *
     * @param params Pipeline creation parameters
     * @return Unique cache key string
     */
    std::string GenerateCacheKey(const ComputePipelineCreateParams& params) const;

    Vixen::Vulkan::Resources::VulkanDevice* device_;
    VkPipelineCache pipelineCache_ = VK_NULL_HANDLE;

    // Cache tracking
    size_t cacheHits_ = 0;
    size_t cacheMisses_ = 0;
};

} // namespace CashSystem
