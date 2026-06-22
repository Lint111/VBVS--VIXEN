// Copyright (C) 2025 Lior Yanai (eLiorg). Licensed under the MIT License.
// auto-sync FrameGraph P5b M2: generic single-compute-pass submit node config.
#pragma once

#include "Core/TypedNodeInstance.h"
#include "Data/Core/CompileTimeResourceSystem.h"
#include "ShaderDataBundle.h"

using VulkanDevice = Vixen::Vulkan::Resources::VulkanDevice;

namespace Vixen::Vulkan::Resources {
    struct IRenderTarget;  // AR#28: abstract render target interface
}

namespace Vixen::RenderGraph {

// ============================================================================
// SLOT COUNTS
// ============================================================================

namespace ComputeStageNodeCounts {
    static constexpr size_t INPUTS  = 19;
    static constexpr size_t OUTPUTS = 3;
    static constexpr SlotArrayMode ARRAY_MODE = SlotArrayMode::Single;
}

// ============================================================================
// COMPUTE STAGE NODE CONFIG
// ============================================================================

/**
 * @brief Generic single-compute-pass submit node (auto-sync P5b M2).
 *
 * One node = one compute dispatch = one vkQueueSubmit2 = its OWN SubmitGroup
 * (the FrameSyncScheduler makes one group per node in the execution order). Unlike
 * the swapchain-specific ComputeDispatchNode, this node is configurable for ANY of
 * three roles in the multi-submit fan-in proof:
 *
 *   - PRODUCER: writes a storage buffer (BUFFER_WRITE, ComputeStorageWrite); no
 *     WSI, no fence, no binary handoff — just compute + a timeline SIGNAL of its
 *     group's deduped signalEdges.
 *   - CONSUMER: reads storage buffers (BUFFER_READ_A/B, ComputeStorageRead) and
 *     writes the swapchain image (SWAPCHAIN_INFO, ComputeStorageWrite); waits the
 *     binary imageAvailable (WSI acquire) + per-edge timeline WAITS, signals the
 *     binary renderComplete (for Present), owns the in-flight fence, and transitions
 *     the swapchain image GENERAL→PRESENT_SRC.
 *
 * The producer→consumer ordering is SOLELY the baked timeline edges — there is NO
 * binary semaphore between producers and the consumer. The scheduler bakes one
 * SyncEdge per (writer-group → reader-group) hazard on a shared buffer Resource*:
 * wiring a StorageBufferNode's STORAGE_BUFFER output into a producer's BUFFER_WRITE
 * (ComputeStorageWrite) AND the consumer's BUFFER_READ_x (ComputeStorageRead) makes
 * both nodes' bundles reference the SAME Resource*, so the tracker records a
 * write-then-read on it and bakes the edge. Two such buffers → 2 waitEdges on the
 * consumer → the genuine 2-wait timeline fan-in.
 *
 * The buffer sync slots here are the DECLARATION of intent for the scheduler; the
 * actual descriptor binding still flows through the DescriptorResourceGathererNode.
 * Both reference the same StorageBufferNode output, so they stay consistent.
 *
 * Role is selected by PARAM_IS_CONSUMER (default false ⇒ producer) plus which slots
 * are wired (a producer leaves the buffer-read + swapchain slots unconnected).
 */
CONSTEXPR_NODE_CONFIG(ComputeStageNodeConfig,
                      ComputeStageNodeCounts::INPUTS,
                      ComputeStageNodeCounts::OUTPUTS,
                      ComputeStageNodeCounts::ARRAY_MODE) {

    // ===== PARAMETER NAMES =====
    // When true, this stage is the consumer/swapchain-adjacent submit: it waits the
    // binary imageAvailable (acquire), signals the binary renderComplete (for Present),
    // owns the in-flight fence, and transitions the swapchain image to PRESENT_SRC.
    // When false (default), it is a producer: compute + timeline signal only — no WSI,
    // no fence, no binary handoff.
    static constexpr const char* PARAM_IS_CONSUMER = "isConsumer";

    // Explicit dispatch group counts (vkCmdDispatch X/Y/Z). A producer has no
    // swapchain input, so it cannot derive dims from the extent — the host sets these
    // (typically ceil(extent/8) for an 8x8 tiling). The consumer, if these are 0,
    // falls back to the connected swapchain extent.
    static constexpr const char* PARAM_DISPATCH_X = "dispatchX";
    static constexpr const char* PARAM_DISPATCH_Y = "dispatchY";
    static constexpr const char* PARAM_DISPATCH_Z = "dispatchZ";

    // Host-provided {width,height} push constant (uint32 x2). When the wired shader
    // reflects a push-constant block and no gathered PUSH_CONSTANT_DATA is connected,
    // the node pushes these two values through the reflected range. 0 ⇒ not pushed
    // (the shader either has no PC block or it is fed via PUSH_CONSTANT_DATA).
    static constexpr const char* PARAM_PC_WIDTH  = "pcWidth";
    static constexpr const char* PARAM_PC_HEIGHT = "pcHeight";

    // ===== INPUTS (19) =====

    /** @brief Vulkan device for command-buffer allocation. */
    INPUT_SLOT(VULKAN_DEVICE_IN, VulkanDevice*, 0,
        SlotNullability::Required,
        SlotRole::Dependency,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    /** @brief Command pool for command-buffer allocation. */
    INPUT_SLOT(COMMAND_POOL, VkCommandPool, 1,
        SlotNullability::Required,
        SlotRole::Dependency,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    /** @brief Compute pipeline to bind (from ComputePipelineNode). */
    INPUT_SLOT(COMPUTE_PIPELINE, VkPipeline, 2,
        SlotNullability::Required,
        SlotRole::Dependency,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    /** @brief Pipeline layout for descriptor sets + push constants. */
    INPUT_SLOT(PIPELINE_LAYOUT, VkPipelineLayout, 3,
        SlotNullability::Required,
        SlotRole::Dependency,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    /** @brief Per-image descriptor sets (from DescriptorSetNode). */
    INPUT_SLOT(DESCRIPTOR_SETS, const std::vector<VkDescriptorSet>&, 4,
        SlotNullability::Required,
        SlotRole::Dependency,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    /**
     * @brief Swapchain render target (consumer role only).
     * Auto-sync: ComputeStorageWrite — the consumer compute storage-writes the
     * swapchain image. ReadWrite so the tracker records this node as a writer.
     * Optional: a producer leaves this unconnected.
     */
    INPUT_SLOT_SYNC(SWAPCHAIN_INFO, Vixen::Vulkan::Resources::IRenderTarget*, 5,
        SlotNullability::Optional,
        SlotRole::Execute,
        SlotMutability::ReadWrite,
        SlotScope::NodeLevel,
        ::Vixen::RenderGraph::AccessKind::ComputeStorageWrite);

    /** @brief Current swapchain image index (per-image cmd buffer + descriptor selection). */
    INPUT_SLOT(IMAGE_INDEX, uint32_t, 6,
        SlotNullability::Required,
        SlotRole::Execute,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    /** @brief Current frame-in-flight index (semaphore array indexing). */
    INPUT_SLOT(CURRENT_FRAME_INDEX, uint32_t, 7,
        SlotNullability::Required,
        SlotRole::Execute,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    /** @brief In-flight fence (consumer owns + resets it; producer ignores it). */
    INPUT_SLOT(IN_FLIGHT_FENCE, VkFence, 8,
        SlotNullability::Required,
        SlotRole::Execute,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    /** @brief imageAvailable (acquire) binary semaphore array, indexed by frame. */
    INPUT_SLOT(IMAGE_AVAILABLE_SEMAPHORES_ARRAY, const std::vector<VkSemaphore>&, 9,
        SlotNullability::Required,
        SlotRole::Dependency,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    /** @brief renderComplete binary semaphore array, indexed by image (consumer → Present). */
    INPUT_SLOT(RENDER_COMPLETE_SEMAPHORES_ARRAY, const std::vector<VkSemaphore>&, 10,
        SlotNullability::Required,
        SlotRole::Dependency,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    /** @brief Shader bundle with reflection metadata (push-constant detection). */
    INPUT_SLOT(SHADER_DATA_BUNDLE, const std::shared_ptr<ShaderManagement::ShaderDataBundle>&, 11,
        SlotNullability::Required,
        SlotRole::Dependency,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    /** @brief Push-constant data bytes (from PushConstantGathererNode). */
    INPUT_SLOT(PUSH_CONSTANT_DATA, std::vector<uint8_t>, 12,
        SlotNullability::Optional,
        SlotRole::Execute,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    /** @brief Push-constant ranges from shader reflection. */
    INPUT_SLOT(PUSH_CONSTANT_RANGES, std::vector<VkPushConstantRange>, 13,
        SlotNullability::Optional,
        SlotRole::Execute,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    /**
     * @brief Storage buffer this stage WRITES (producer role).
     * Auto-sync: ComputeStorageWrite — makes the scheduler record this node as a
     * writer of the buffer Resource* so a downstream reader bakes a SyncEdge.
     * Optional: the consumer leaves this unconnected.
     */
    INPUT_SLOT_SYNC(BUFFER_WRITE, VkBuffer, 14,
        SlotNullability::Optional,
        SlotRole::Execute,
        SlotMutability::ReadWrite,
        SlotScope::NodeLevel,
        ::Vixen::RenderGraph::AccessKind::ComputeStorageWrite);

    /**
     * @brief First storage buffer this stage READS (consumer role).
     * Auto-sync: ComputeStorageRead — paired with a producer's ComputeStorageWrite
     * on the SAME buffer Resource*, this bakes the first fan-in SyncEdge.
     * Optional: a producer leaves this unconnected.
     */
    INPUT_SLOT_SYNC(BUFFER_READ_A, VkBuffer, 15,
        SlotNullability::Optional,
        SlotRole::Execute,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel,
        ::Vixen::RenderGraph::AccessKind::ComputeStorageRead);

    /**
     * @brief Second storage buffer this stage READS (consumer role).
     * Auto-sync: ComputeStorageRead — the SECOND fan-in edge. Two distinct read
     * buffers, each written by a distinct producer group, give the consumer group
     * 2 waitEdges → the genuine 2-wait timeline fan-in.
     * Optional: a producer leaves this unconnected.
     */
    INPUT_SLOT_SYNC(BUFFER_READ_B, VkBuffer, 16,
        SlotNullability::Optional,
        SlotRole::Execute,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel,
        ::Vixen::RenderGraph::AccessKind::ComputeStorageRead);

    /**
     * @brief Timeline semaphore from FrameSyncNode (P5b). vkQueueSubmit2 signals
     * (producer) / waits (consumer) absolute timeline values for the baked edges.
     */
    INPUT_SLOT(TIMELINE_SEMAPHORE_IN, VkSemaphore, 17,
        SlotNullability::Optional,
        SlotRole::Execute,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    /**
     * @brief Per-frame timeline base offset from FrameSyncNode (P5b). Added to each
     * SyncEdge::timelineOffset to form the absolute signal/wait value.
     */
    INPUT_SLOT(TIMELINE_FRAME_BASE_IN, uint64_t, 18,
        SlotNullability::Optional,
        SlotRole::Execute,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    // ===== OUTPUTS (2) =====

    /** @brief renderComplete semaphore for Present to wait on (consumer role). */
    OUTPUT_SLOT(RENDER_COMPLETE_SEMAPHORE, VkSemaphore, 0,
        SlotNullability::Required,
        SlotMutability::WriteOnly);

    /** @brief Pass-through device for downstream nodes. */
    OUTPUT_SLOT(VULKAN_DEVICE_OUT, VulkanDevice*, 1,
        SlotNullability::Required,
        SlotMutability::WriteOnly);

    /**
     * @brief Pass-through of the VkBuffer this stage WROTE (producer role).
     * Re-publishes the BUFFER_WRITE handle VALUE so a downstream consumer can bind it
     * via its descriptor gatherer AND so the connection establishes a topological
     * producer→consumer ordering (the scheduler indexes groups by execution order, so
     * a producer must sort before the consumer for the baked edge to point the right
     * way). The hazard edge itself is baked off the SHARED StorageBufferNode Resource*
     * wired into both stages' buffer sync slots — this passthrough only carries the
     * handle value + the ordering edge, never a tracked-Resource identity.
     */
    OUTPUT_SLOT(BUFFER_OUT, VkBuffer, 2,
        SlotNullability::Optional,
        SlotMutability::WriteOnly);

    // ===== CONSTRUCTOR (runtime descriptor initialization) =====

    ComputeStageNodeConfig() {
        HandleDescriptor vulkanDeviceDesc{"VulkanDevice*"};
        INIT_INPUT_DESC(VULKAN_DEVICE_IN, "vulkan_device", ResourceLifetime::Persistent, vulkanDeviceDesc);

        HandleDescriptor commandPoolDesc{"VkCommandPool"};
        INIT_INPUT_DESC(COMMAND_POOL, "command_pool", ResourceLifetime::Persistent, commandPoolDesc);

        HandleDescriptor pipelineDesc{"VkPipeline"};
        INIT_INPUT_DESC(COMPUTE_PIPELINE, "compute_pipeline", ResourceLifetime::Persistent, pipelineDesc);

        HandleDescriptor layoutDesc{"VkPipelineLayout"};
        INIT_INPUT_DESC(PIPELINE_LAYOUT, "pipeline_layout", ResourceLifetime::Persistent, layoutDesc);

        HandleDescriptor descSetsDesc{"std::vector<VkDescriptorSet>"};
        INIT_INPUT_DESC(DESCRIPTOR_SETS, "descriptor_sets", ResourceLifetime::Persistent, descSetsDesc);

        HandleDescriptor swapchainDesc{"IRenderTarget*"};
        INIT_INPUT_DESC(SWAPCHAIN_INFO, "swapchain_info", ResourceLifetime::Persistent, swapchainDesc);

        HandleDescriptor uint32Desc{"uint32_t"};
        INIT_INPUT_DESC(IMAGE_INDEX, "image_index", ResourceLifetime::Transient, uint32Desc);
        INIT_INPUT_DESC(CURRENT_FRAME_INDEX, "current_frame_index", ResourceLifetime::Transient, uint32Desc);

        HandleDescriptor fenceDesc{"VkFence"};
        INIT_INPUT_DESC(IN_FLIGHT_FENCE, "in_flight_fence", ResourceLifetime::Transient, fenceDesc);

        HandleDescriptor semaphoreArrayDesc{"std::vector<VkSemaphore>"};
        INIT_INPUT_DESC(IMAGE_AVAILABLE_SEMAPHORES_ARRAY, "image_available_semaphores", ResourceLifetime::Persistent, semaphoreArrayDesc);
        INIT_INPUT_DESC(RENDER_COMPLETE_SEMAPHORES_ARRAY, "render_complete_semaphores", ResourceLifetime::Persistent, semaphoreArrayDesc);

        HandleDescriptor shaderBundleDesc{"ShaderDataBundle"};
        INIT_INPUT_DESC(SHADER_DATA_BUNDLE, "shader_data_bundle", ResourceLifetime::Persistent, shaderBundleDesc);

        HandleDescriptor pushConstDataDesc{"std::vector<uint8_t>"};
        INIT_INPUT_DESC(PUSH_CONSTANT_DATA, "push_constant_data", ResourceLifetime::Transient, pushConstDataDesc);

        HandleDescriptor pushConstRangesDesc{"std::vector<VkPushConstantRange>"};
        INIT_INPUT_DESC(PUSH_CONSTANT_RANGES, "push_constant_ranges", ResourceLifetime::Transient, pushConstRangesDesc);

        // Buffer sync slots — VkBuffer handles (their Resource* identity is what the
        // scheduler bakes edges on; Persistent so the buffer reference survives recompile).
        HandleDescriptor bufferDesc{"VkBuffer"};
        INIT_INPUT_DESC(BUFFER_WRITE, "buffer_write", ResourceLifetime::Persistent, bufferDesc);
        INIT_INPUT_DESC(BUFFER_READ_A, "buffer_read_a", ResourceLifetime::Persistent, bufferDesc);
        INIT_INPUT_DESC(BUFFER_READ_B, "buffer_read_b", ResourceLifetime::Persistent, bufferDesc);

        // Timeline primitives from FrameSyncNode.
        HandleDescriptor timelineSemDesc{"VkSemaphore"};
        INIT_INPUT_DESC(TIMELINE_SEMAPHORE_IN, "timeline_semaphore_in", ResourceLifetime::Persistent, timelineSemDesc);

        HandleDescriptor frameBaseDesc{"uint64_t"};
        INIT_INPUT_DESC(TIMELINE_FRAME_BASE_IN, "timeline_frame_base_in", ResourceLifetime::Transient, frameBaseDesc);

        // Outputs.
        HandleDescriptor semaphoreDesc{"VkSemaphore"};
        INIT_OUTPUT_DESC(RENDER_COMPLETE_SEMAPHORE, "render_complete_semaphore", ResourceLifetime::Transient, semaphoreDesc);

        HandleDescriptor deviceOutDesc{"VulkanDevice*"};
        INIT_OUTPUT_DESC(VULKAN_DEVICE_OUT, "vulkan_device_out", ResourceLifetime::Persistent, deviceOutDesc);

        HandleDescriptor bufferOutDesc{"VkBuffer"};
        INIT_OUTPUT_DESC(BUFFER_OUT, "buffer_out", ResourceLifetime::Persistent, bufferOutDesc);
    }

    // ===== COMPILE-TIME VALIDATIONS =====

    VALIDATE_NODE_CONFIG(ComputeStageNodeConfig, ComputeStageNodeCounts);

    static_assert(VULKAN_DEVICE_IN_Slot::index == 0, "VULKAN_DEVICE_IN must be at index 0");
    static_assert(!VULKAN_DEVICE_IN_Slot::nullable, "VULKAN_DEVICE_IN must not be nullable");
    static_assert(std::is_same_v<VULKAN_DEVICE_IN_Slot::Type, VulkanDevice*>);
    static_assert(std::is_same_v<BUFFER_WRITE_Slot::Type, VkBuffer>);
    static_assert(std::is_same_v<BUFFER_READ_A_Slot::Type, VkBuffer>);
    static_assert(std::is_same_v<BUFFER_READ_B_Slot::Type, VkBuffer>);
    static_assert(BUFFER_WRITE_Slot::accessKind == ::Vixen::RenderGraph::AccessKind::ComputeStorageWrite);
    static_assert(BUFFER_READ_A_Slot::accessKind == ::Vixen::RenderGraph::AccessKind::ComputeStorageRead);
    static_assert(BUFFER_READ_B_Slot::accessKind == ::Vixen::RenderGraph::AccessKind::ComputeStorageRead);
    static_assert(SWAPCHAIN_INFO_Slot::accessKind == ::Vixen::RenderGraph::AccessKind::ComputeStorageWrite);
};

} // namespace Vixen::RenderGraph
