// Copyright (C) 2025 Lior Yanai (eLiorg). Licensed under the MIT License.
#pragma once

#include "Core/TypedNodeInstance.h"
#include "Data/Core/CompileTimeResourceSystem.h"

using VulkanDevice = Vixen::Vulkan::Resources::VulkanDevice;

namespace Vixen::RenderGraph {

// ============================================================================
// SLOT COUNTS
// ============================================================================

namespace ShellRevalidateNodeCounts {
    static constexpr size_t INPUTS  = 8;
    static constexpr size_t OUTPUTS = 1;
    static constexpr SlotArrayMode ARRAY_MODE = SlotArrayMode::Single;
}

// ============================================================================
// SHELL REVALIDATE NODE CONFIG
// ============================================================================

/**
 * @brief Config for ShellRevalidateNode — GPU compute dispatch of shaders/ShellDerive.comp
 * (Surface-Shell ESVO cache derivation, GPU mirror of Vixen::SVO::DeriveShell).
 *
 * Owns its own VkPipeline/VkDescriptorSetLayout/VkPipelineLayout built from the shipped
 * ShellDerive.comp SPIR-V (compiled at build time — see CMakeLists.txt), binds the 4
 * buffers ShellDerive.comp expects at bindings 0-3 (source channel pool, brick-grid
 * lookup, shell-flags output, OctreeConfig), and records mode-0 (surface classify)
 * followed by `shellDilationLayers` mode-1 (dilation) dispatches into ONE command buffer.
 *
 * This is intentionally a STANDALONE node (not wired into BodyOctreeSceneNode, which
 * already derives the shell on the CPU) — it exists to (a) prove the GPU dispatch
 * produces a membership mask matching the CPU DeriveShell classification, and (b) be
 * assembled as a ComputePassStep into a PassGroupNode alongside a disjoint "shell read"
 * pass to prove the scheduler bakes zero cross-pass barriers when the two passes touch
 * distinct Resource objects (double-buffered shell slots).
 */
CONSTEXPR_NODE_CONFIG(ShellRevalidateNodeConfig,
                      ShellRevalidateNodeCounts::INPUTS,
                      ShellRevalidateNodeCounts::OUTPUTS,
                      ShellRevalidateNodeCounts::ARRAY_MODE) {

    // ===== PARAMETER NAMES =====

    /** @brief Path to the compiled ShellDerive.spv (runtime parameter, NOT a compile-time
     *  macro — mirrors RayTracingPipelineNodeConfig::PARAM_RAYGEN_SHADER_PATH). Defaults to
     *  the shipped shader's conventional relative build output path; a host (e.g. a test
     *  harness that compiles the .comp itself) overrides via SetParameter. */
    static constexpr const char* PARAM_SPIRV_PATH = "spirvPath";

    // ===== INPUTS =====

    /** @brief Vulkan device for pipeline/buffer creation (compile-time dependency) */
    INPUT_SLOT(VULKAN_DEVICE_IN, VulkanDevice*, 0,
        SlotNullability::Required,
        SlotRole::Dependency,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    /** @brief Command pool for command buffer allocation */
    INPUT_SLOT(COMMAND_POOL, VkCommandPool, 1,
        SlotNullability::Required,
        SlotRole::Dependency,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    /** @brief Source full-interior channel pool SSBO (ShellDerive.comp binding 0) */
    INPUT_SLOT_SYNC(SOURCE_POOL_BUFFER, VkBuffer, 2,
        SlotNullability::Required,
        SlotRole::Execute,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel,
        ::Vixen::RenderGraph::AccessKind::ComputeStorageRead);

    /** @brief Dense grid->brick lookup SSBO for octree 0 (ShellDerive.comp binding 1) */
    INPUT_SLOT_SYNC(BRICK_LOOKUP_BUFFER, VkBuffer, 3,
        SlotNullability::Required,
        SlotRole::Execute,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel,
        ::Vixen::RenderGraph::AccessKind::ComputeStorageRead);

    /** @brief Octree-0 OctreeConfig SSBO, single element, 432B std430 (ShellDerive.comp binding 3) */
    INPUT_SLOT_SYNC(CONFIG_BUFFER, VkBuffer, 4,
        SlotNullability::Required,
        SlotRole::Execute,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel,
        ::Vixen::RenderGraph::AccessKind::ComputeStorageRead);

    /** @brief Number of source bricks in octree 0 (push constant: brickCount) */
    INPUT_SLOT(BRICK_COUNT, uint32_t, 5,
        SlotNullability::Required,
        SlotRole::Execute,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    /** @brief bricksPerAxis of octree 0 (push constant: bpa) */
    INPUT_SLOT(BRICKS_PER_AXIS, uint32_t, 6,
        SlotNullability::Required,
        SlotRole::Execute,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    /** @brief Number of mode-1 dilation dispatches to record after the mode-0 pass (mirrors
     *  BodyOctreeSceneNode::shellDilation_ / ShellDeriveParams::shellDilation). */
    INPUT_SLOT(SHELL_DILATION, uint32_t, 7,
        SlotNullability::Required,
        SlotRole::Execute,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    // ===== OUTPUTS =====

    /** @brief Per-brick shell membership flags SSBO (bit0=SURFACE, bit1=SHELL, bit2=FRONTIER) */
    OUTPUT_SLOT(SHELL_FLAGS_BUFFER, VkBuffer, 0,
        SlotNullability::Required,
        SlotMutability::WriteOnly);

    // ===== CONSTRUCTOR =====

    ShellRevalidateNodeConfig() {
        HandleDescriptor vulkanDeviceDesc{"VulkanDevice*"};
        INIT_INPUT_DESC(VULKAN_DEVICE_IN, "vulkan_device", ResourceLifetime::Persistent, vulkanDeviceDesc);

        HandleDescriptor commandPoolDesc{"VkCommandPool"};
        INIT_INPUT_DESC(COMMAND_POOL, "command_pool", ResourceLifetime::Persistent, commandPoolDesc);

        BufferDescriptor bufferDesc{};
        bufferDesc.usage = ResourceUsage::StorageBuffer | ResourceUsage::TransferDst;
        INIT_INPUT_DESC(SOURCE_POOL_BUFFER, "source_pool_buffer", ResourceLifetime::Persistent, bufferDesc);
        INIT_INPUT_DESC(BRICK_LOOKUP_BUFFER, "brick_lookup_buffer", ResourceLifetime::Persistent, bufferDesc);
        INIT_INPUT_DESC(CONFIG_BUFFER, "config_buffer", ResourceLifetime::Persistent, bufferDesc);

        INIT_INPUT_DESC(BRICK_COUNT, "brick_count", ResourceLifetime::Transient, BufferDescription{});
        INIT_INPUT_DESC(BRICKS_PER_AXIS, "bricks_per_axis", ResourceLifetime::Transient, BufferDescription{});
        INIT_INPUT_DESC(SHELL_DILATION, "shell_dilation", ResourceLifetime::Transient, BufferDescription{});

        BufferDescriptor shellFlagsDesc{};
        shellFlagsDesc.usage = ResourceUsage::StorageBuffer | ResourceUsage::TransferDst;
        INIT_OUTPUT_DESC(SHELL_FLAGS_BUFFER, "shell_flags_buffer", ResourceLifetime::Persistent, shellFlagsDesc);
    }

    // ===== COMPILE-TIME VALIDATIONS =====

    VALIDATE_NODE_CONFIG(ShellRevalidateNodeConfig, ShellRevalidateNodeCounts);
};

} // namespace Vixen::RenderGraph
