// Copyright (C) 2025 Lior Yanai (eLiorg). Licensed under the MIT License.
#pragma once

#include "Core/TypedNodeInstance.h"
#include "Core/NodeType.h"
#include "Data/Nodes/ShellRevalidateNodeConfig.h"

#include <vulkan/vulkan.h>
#include <cstdint>

namespace Vixen::RenderGraph {

/**
 * @brief Node type for ShellRevalidateNode (Surface-Shell ESVO cache — GPU dispatch)
 */
class ShellRevalidateNodeType : public TypedNodeType<ShellRevalidateNodeConfig> {
public:
    ShellRevalidateNodeType(const std::string& typeName = "ShellRevalidate")
        : TypedNodeType<ShellRevalidateNodeConfig>(typeName) {}
    ~ShellRevalidateNodeType() override = default;

    std::unique_ptr<NodeInstance> CreateInstance(const std::string& instanceName) const override;
};

/**
 * @brief GPU compute dispatch of shaders/ShellDerive.comp (mode 0 = surface classify,
 * mode 1 = one dilation layer). Owns its own compute pipeline + descriptor set, built
 * from the shipped ShellDerive.comp SPIR-V at Compile time, and records the mode-0 pass
 * followed by SHELL_DILATION mode-1 passes into ONE command buffer at Execute time.
 *
 * This is a minimal, standalone node: it does NOT replace BodyOctreeSceneNode's existing
 * CPU-side DeriveShell path (which remains the committed cache); it exists to prove the
 * GPU mirror produces bit-identical SURFACE/SHELL classification, and to serve as a real
 * ComputePassStep source for PassGroupNode assembly (see test_shell_revalidate_node.cpp).
 */
class ShellRevalidateNode : public TypedNode<ShellRevalidateNodeConfig> {
public:
    using Base = TypedNode<ShellRevalidateNodeConfig>;

    ShellRevalidateNode(const std::string& instanceName, NodeType* nodeType);
    ~ShellRevalidateNode() override;

    /// Accessors for a test/host to assemble an EXTERNAL ComputePassStep (e.g. for a
    /// PassGroupNode) reusing this node's already-built pipeline/layout/descriptor set,
    /// instead of (or in addition to) this node's own Execute()-driven single dispatch.
    [[nodiscard]] VkPipeline       GetPipeline() const { return pipeline_; }
    [[nodiscard]] VkPipelineLayout GetPipelineLayout() const { return pipelineLayout_; }
    [[nodiscard]] VkDescriptorSet  GetDescriptorSet() const { return descriptorSet_; }

protected:
    void SetupImpl(TypedSetupContext& ctx) override;
    void CompileImpl(TypedCompileContext& ctx) override;
    void ExecuteImpl(TypedExecuteContext& ctx) override;
    void CleanupImpl(TypedCleanupContext& ctx) override;

private:
    struct PushConstants {
        uint32_t brickCount;
        uint32_t bpa;
        uint32_t mode;
        uint32_t sdfBase;
    };

    void BuildPipeline();
    void DestroyPipeline();
    void BuildDescriptorSet(VkBuffer sourcePool, VkBuffer brickLookup,
                             VkBuffer shellFlags, VkBuffer config);
    void EnsureShellFlagsBuffer(uint32_t brickCount);
    void RecordDispatch(VkCommandBuffer cmd, uint32_t brickCount, uint32_t bpa,
                         uint32_t dilationLayers);

    VulkanDevice*  vulkanDevice_ = nullptr;
    VkCommandPool  commandPool_  = VK_NULL_HANDLE;

    VkShaderModule        shaderModule_        = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptorSetLayout_ = VK_NULL_HANDLE;
    VkPipelineLayout      pipelineLayout_      = VK_NULL_HANDLE;
    VkPipeline            pipeline_            = VK_NULL_HANDLE;
    VkDescriptorPool      descriptorPool_      = VK_NULL_HANDLE;
    VkDescriptorSet       descriptorSet_       = VK_NULL_HANDLE;

    VkCommandBuffer commandBuffer_ = VK_NULL_HANDLE;

    // Output: per-brick shell membership flags (bit0=SURFACE, bit1=SHELL, bit2=FRONTIER).
    VkBuffer       shellFlagsBuffer_   = VK_NULL_HANDLE;
    VkDeviceMemory shellFlagsMemory_   = VK_NULL_HANDLE;
    VkDeviceSize   shellFlagsCapacity_ = 0;
};

} // namespace Vixen::RenderGraph
