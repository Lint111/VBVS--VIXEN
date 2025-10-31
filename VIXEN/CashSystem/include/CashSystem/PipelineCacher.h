#pragma once

#include "Headers.h"
#include "TypedCacher.h"
#include "MainCacher.h"
#include "PipelineLayoutCacher.h"  // Need full definition for std::shared_ptr<PipelineLayoutWrapper>
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
 * @brief Pipeline resource wrapper
 *
 * Stores Vulkan pipeline objects and associated metadata.
 * Pipeline layout is shared via PipelineLayoutCacher.
 */
struct PipelineWrapper {
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkPipelineCache cache = VK_NULL_HANDLE;

    // Shared pipeline layout (from PipelineLayoutCacher)
    std::shared_ptr<PipelineLayoutWrapper> pipelineLayoutWrapper;

    // Cache identification
    std::string vertexShaderKey;
    std::string fragmentShaderKey;
    std::string layoutKey;
    std::string renderPassKey;

    // Pipeline configuration
    bool enableDepthTest = true;
    bool enableDepthWrite = true;
    VkCullModeFlags cullMode = VK_CULL_MODE_BACK_BIT;
    VkPolygonMode polygonMode = VK_POLYGON_MODE_FILL;
    VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
};

/**
 * @brief Pipeline creation parameters
 *
 * Supports two modes:
 * 1. Explicit: Provide pipelineLayoutWrapper from PipelineLayoutCacher (transparent, efficient)
 * 2. Convenience: Provide descriptorSetLayout, PipelineCacher creates layout internally
 */
struct PipelineCreateParams {
    // ===== Sub-cacher Resources (Explicit Dependencies) =====
    // If provided, use directly (recommended for transparency)
    std::shared_ptr<PipelineLayoutWrapper> pipelineLayoutWrapper;

    // ===== Convenience Fallbacks =====
    // If pipelineLayoutWrapper not provided, create from these:
    VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
    std::vector<VkPushConstantRange> pushConstantRanges;  // Extracted from reflection (Phase 5)

    // ===== Direct Pipeline Resources =====
    // Shader stages (dynamic - supports all stage types)
    std::vector<VkPipelineShaderStageCreateInfo> shaderStages;

    // Render pass (NOT owned by PipelineWrapper)
    VkRenderPass renderPass = VK_NULL_HANDLE;

    // Keys for cache lookup
    std::string vertexShaderKey;
    std::string fragmentShaderKey;
    std::string layoutKey;
    std::string renderPassKey;

    // Pipeline state
    bool enableDepthTest = true;
    bool enableDepthWrite = true;
    VkCullModeFlags cullMode = VK_CULL_MODE_BACK_BIT;
    VkPolygonMode polygonMode = VK_POLYGON_MODE_FILL;
    VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    // Vertex input description (if needed)
    std::vector<VkVertexInputAttributeDescription> vertexAttributes;
    std::vector<VkVertexInputBindingDescription> vertexBindings;

    // Shader specialization constants (if needed)
    std::vector<VkSpecializationMapEntry> specMapEntries;
    std::vector<uint8_t> specData;
};

/**
 * @brief TypedCacher for pipeline resources
 * 
 * Caches compiled pipelines based on:
 * - Shader module keys
 * - Pipeline layout
 * - Render pass compatibility
 * - Pipeline state configuration
 */
class PipelineCacher : public TypedCacher<PipelineWrapper, PipelineCreateParams> {
public:
    PipelineCacher() = default;
    ~PipelineCacher() override = default;

    // Override to add cache hit/miss logging
    std::shared_ptr<PipelineWrapper> GetOrCreate(const PipelineCreateParams& ci);

    // Convenience API for pipeline creation
    std::shared_ptr<PipelineWrapper> GetOrCreatePipeline(
        const std::string& vertexShaderKey,
        const std::string& fragmentShaderKey,
        const std::string& layoutKey,
        const std::string& renderPassKey,
        bool enableDepthTest = true,
        VkCullModeFlags cullMode = VK_CULL_MODE_BACK_BIT,
        VkPolygonMode polygonMode = VK_POLYGON_MODE_FILL
    );

protected:
    // TypedCacher implementation
    std::shared_ptr<PipelineWrapper> Create(const PipelineCreateParams& ci) override;
    std::uint64_t ComputeKey(const PipelineCreateParams& ci) const override;

    // Resource cleanup
    void Cleanup() override;

    // Serialization
    bool SerializeToFile(const std::filesystem::path& path) const override;
    bool DeserializeFromFile(const std::filesystem::path& path, void* device) override;
    std::string_view name() const noexcept override { return "PipelineCacher"; }

private:
    // Helper methods
    void CreatePipeline(const PipelineCreateParams& ci, PipelineWrapper& wrapper);
    void CreatePipelineLayout(const PipelineCreateParams& ci, PipelineWrapper& wrapper);
    void CreatePipelineCache(const PipelineCreateParams& ci, PipelineWrapper& wrapper);
};

} // namespace CashSystem
