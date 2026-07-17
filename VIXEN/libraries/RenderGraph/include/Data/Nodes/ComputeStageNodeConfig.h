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
    static constexpr size_t INPUTS  = 21;
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
 *   - PRODUCER: writes a set of storage buffers (BUFFER_WRITE_ARRAY, ComputeStorageWrite);
 *     no WSI, no fence, no binary handoff — just compute + a timeline SIGNAL of its
 *     group's deduped signalEdges.
 *   - CONSUMER: reads a set of storage buffers (BUFFER_READ_ARRAY, ComputeStorageRead)
 *     and writes the swapchain image (SWAPCHAIN_INFO, ComputeStorageWrite); waits the
 *     binary imageAvailable (WSI acquire) + per-edge timeline WAITS, signals the
 *     binary renderComplete (for Present), owns the in-flight fence, and transitions
 *     the swapchain image GENERAL→PRESENT_SRC.
 *
 * The producer→consumer ordering is SOLELY the baked timeline edges — there is NO
 * binary semaphore between producers and the consumer. The scheduler bakes one
 * SyncEdge per (writer-group → reader-group) hazard on a shared array-typed buffer
 * Resource*: wiring a BufferSyncGathererNode's BUFFER_ARRAY output into BOTH a
 * producer's BUFFER_WRITE_ARRAY (ComputeStorageWrite) AND a consumer's
 * BUFFER_READ_ARRAY (ComputeStorageRead) makes both nodes' bundles reference the
 * SAME Resource* (the gathered array VALUE, not any one buffer inside it), so the
 * tracker records a write-then-read on it and bakes the edge.
 *
 * SAMPLED LIGHTING INC3 M5 — GENERALIZED FROM FIXED NAMED SLOTS: this replaces the
 * PRE-M5 shape (three fixed named slots — BUFFER_WRITE/BUFFER_READ_A/BUFFER_READ_B,
 * one Resource* per physical buffer) with TWO array-typed slots (BUFFER_WRITE_ARRAY/
 * BUFFER_READ_ARRAY, each carrying std::vector<VkBuffer>). WHY: a fixed named slot
 * per buffer does not scale — every new multi-buffer hazard shape (the original 2-wait
 * fan-in proof's A/B pair; later, a reservoir ping-pong pair) meant adding MORE named
 * slots to this shared, load-bearing config. The array shape means an arbitrary
 * buffer COUNT (the fan-in demo's 2 producers -> 1 two-buffer-reading consumer; a
 * future N-buffer hazard) needs NO new named slots here, ever again — only a
 * BufferSyncGathererNode instance (see BufferSyngGathererNode.h) pre-registered with
 * the right count, upstream of whichever ComputeStageNode(s) need the hazard
 * declared. Each connecting side (producer vs consumer) uses its OWN
 * BufferSyncGathererNode instance (one gathers the write-side buffer(s), a SEPARATE
 * one gathers the read-side buffer(s)) — the SAME underlying StorageBufferNode
 * Resource* must reach BOTH gatherers so their respective array Resource*s end up
 * wired into the SAME producer-write-array/consumer-read-array Resource* pairing
 * the scheduler correlates on (mirrors the pre-M5 "same StorageBufferNode output
 * feeds both a WRITE and a READ slot" identity, just via an array now).
 *
 * The buffer sync slots here are the DECLARATION of intent for the scheduler; the
 * actual descriptor binding still flows through the DescriptorResourceGathererNode
 * (a SEPARATE, older gatherer for shader descriptor sets — not to be confused with
 * BufferSyncGathererNode, which exists purely to feed THESE sync slots).
 *
 * Role is selected by PARAM_IS_CONSUMER (default false ⇒ producer) plus which slots
 * are wired (a producer leaves the buffer-read + swapchain slots unconnected).
 *
 * IMAGE_WRITE (Sampled Lighting Inc3 M1) stays a SEPARATE, still-single (non-array)
 * slot — deliberately NOT generalized into the same array shape as the buffer slots
 * (M5 decision): images and buffers are genuinely different resource kinds with
 * different consumer needs (IMAGE_WRITE's own entry/exit barrier logic in
 * ComputeStageNode::RecordComputeCommands is per-IRenderTarget*, not something an
 * arbitrary-count array would simplify — every image-producing pass this codebase
 * has needed so far writes exactly ONE non-swapchain image, unlike buffers where
 * 2+ simultaneous ping-pong/fan-in buffers are the established multi-buffer case).
 * If a genuine multi-image-write need arises later, generalize IMAGE_WRITE THEN,
 * with its own concrete use case driving the design — not preemptively here. This
 * is the image-typed sibling of BUFFER_WRITE_ARRAY: transitions the wired
 * IRenderTarget*'s CURRENT image to GENERAL before dispatch (reusing
 * SwapchainBarriers::TransitionImageToGeneralBarrier2, the same generic-over-any-
 * VkImage helper SWAPCHAIN_INFO's consumer path already uses) and hazard-tracks the
 * write (ComputeStorageWrite), so a downstream reader/blit bakes a real SyncEdge.
 * Deliberately kept SEPARATE from SWAPCHAIN_INFO, not merged into it or gated by
 * PARAM_IS_CONSUMER: SWAPCHAIN_INFO OWNS the WSI contract (binary acquire-wait,
 * renderComplete-signal, fence ownership, PRESENT_SRC transition) — real semantics
 * that only apply to the actual swapchain image. IMAGE_WRITE NEVER touches WSI and
 * NEVER forces PRESENT_SRC; it leaves the image in GENERAL and lets whatever reads
 * it next (another IMAGE_WRITE producer, or a presentation-only blit) decide the
 * next transition. A future reader must not fold these two slots together — that
 * would silently reintroduce WSI coupling into every non-swapchain image-producing
 * pass.
 *
 * SAMPLED LIGHTING INC4 M1 — THE FLAGGED CONCRETE USE CASE ARRIVED: DDGI's
 * probe-update pass needs an irradiance atlas AND a separate Chebyshev-visibility
 * atlas written simultaneously (verified against real DDGI/RTXGI-reference atlas
 * layout — the two atlases use DIFFERENT per-probe texel resolutions, so they are
 * not even the same image dimensions and cannot be channel-packed into one image).
 * IMAGE_WRITE_ARRAY is the image-typed sibling of BUFFER_WRITE_ARRAY/BUFFER_READ_ARRAY,
 * added the SAME way: fed by an ImageSyncGathererNode's IMAGE_ARRAY output (mirrors
 * BufferSyncGathererNode exactly). Resource::hazardConstituents_ /
 * ResourceAccessTracker::AddNode (the mechanism that expands a gathered array into N
 * independent SyncEdges) is genuinely resource-type-agnostic already — verified by
 * direct reading, not assumed: zero ResourceType/VkBuffer-specific logic anywhere in
 * the tracker or FrameSyncScheduler — so no tracker/scheduler changes were needed,
 * only this new slot + ImageSyncGathererNode + the RecordComputeCommands barrier loop
 * (ComputeStageNode.cpp; imageWriteLayouts_ is already a std::unordered_map<VkImage,
 * VkImageLayout>, already multi-entry-capable with zero type changes).
 *
 * Deliberately ADDITIVE, NOT a replacement: the original single-image IMAGE_WRITE
 * slot (index 18) is completely untouched — Inc3's shipped single-IMAGE_WRITE
 * consumers (DirectLighting.comp, SpatialReuseShade.comp via BlitNode) keep using it
 * exactly as before, zero regression risk. IMAGE_WRITE_ARRAY (index 19) is opt-in,
 * wired only where a pass genuinely needs N simultaneous image outputs (M2's
 * ProbeAtlasNode(s) being the first real consumer).
 *
 * SAMPLED LIGHTING INC4 M5 -- IMAGE_READ_ARRAY, the concrete consumer arrived: the
 * shade pass (SpatialReuseShade.comp) needs to READ both DDGI atlases (irradiance +
 * visibility) written by the sibling probe-update pass. The image-typed sibling of
 * BUFFER_READ_ARRAY, added the identical way: fed by an ImageSyncGathererNode's
 * IMAGE_ARRAY output (a SEPARATE gatherer instance from the writer's own -- same
 * "same underlying node's output feeds both a WRITE-side and a READ-side gatherer"
 * shape BUFFER_WRITE_ARRAY/BUFFER_READ_ARRAY already established). AccessKind is
 * ComputeStorageRead so the tracker pairs it against IMAGE_WRITE_ARRAY's
 * ComputeStorageWrite on the SAME constituent Resource*s (via
 * Resource::hazardConstituents_) and bakes a real SyncEdge. No layout-transition work
 * needed in RecordComputeCommands: IMAGE_WRITE_ARRAY targets are left in GENERAL by
 * their writer (never transitioned on exit), and a storage-image descriptor read
 * requires no further transition -- this slot exists purely to declare the hazard for
 * the scheduler, mirroring BUFFER_READ_ARRAY's own "declaration only, the real
 * descriptor binding flows through DescriptorResourceGathererNode" split.
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

    // Baked-Perf M4 Task 4.3 (audit C8 + inventory #1/#2): generic no-op dispatch
    // guard. When false, ExecuteImpl returns before recording/submitting anything --
    // no command buffer, no vkQueueSubmit2, no timeline signal/wait. Default true
    // (backward-safe: every existing wiring keeps dispatching exactly as before
    // unless a caller explicitly opts a pass out). Intended for a pass whose SHADER
    // is a config-gated no-op on the current config (e.g. direct_lighting when
    // reservoirEnabled=0, probe_update when probeGridEnabled=0) -- today those
    // shaders still pay for a full dispatch + submit every frame even though their
    // own body-level gate (see SpatialReuseShade.comp's "reservoirEnabled==0 skips
    // this block entirely" / ProbeUpdate.comp's own byte-identity escape hatch)
    // makes the work provably a no-op. A CONSUMER (isConsumer=true) must never be
    // skipped this way -- it owns the in-flight fence reset + WSI present chain,
    // which downstream nodes unconditionally depend on; this guard is for
    // producer-role middle passes only (the caller is responsible for that
    // invariant -- see BuildRenderGraph.cpp's wiring for the two current uses).
    static constexpr const char* PARAM_DISPATCH_ENABLED = "dispatchEnabled";

    // ===== INPUTS (20, indices 0-19 — Sampled Lighting Inc3 M5 collapsed the old 3
    // fixed buffer slots [14,15,16] into 2 array slots [14,15], renumbering every slot
    // after it down by one to keep the index space contiguous — RenderGraph::Validate
    // walks every index up to INPUTS treating a gap as an undeclared-and-thus-
    // non-nullable slot, so a hole is not a legal way to "retire" an index. Sampled
    // Lighting Inc4 M1 appended IMAGE_WRITE_ARRAY at index 19 — purely additive, no
    // renumbering of any existing slot) =====

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
     * @brief Set of storage buffers this stage WRITES (producer role) — Sampled
     * Lighting Inc3 M5: generalized from the old fixed BUFFER_WRITE single-buffer
     * slot into an array. Fed by a BufferSyncGathererNode's BUFFER_ARRAY output
     * (see that node's own file header). Auto-sync: ComputeStorageWrite — makes the
     * scheduler record this node as a writer of the array Resource* so a downstream
     * reader (another ComputeStageNode's BUFFER_READ_ARRAY wired to the SAME
     * gathered-array Resource*) bakes a SyncEdge. Optional: the consumer leaves this
     * unconnected. An empty/unconnected array is a legitimate "writes nothing"
     * producer (e.g. a pass whose only hazard is an IMAGE_WRITE).
     */
    INPUT_SLOT_SYNC(BUFFER_WRITE_ARRAY, std::vector<VkBuffer>, 14,
        SlotNullability::Optional,
        SlotRole::Execute,
        SlotMutability::ReadWrite,
        SlotScope::NodeLevel,
        ::Vixen::RenderGraph::AccessKind::ComputeStorageWrite);

    /**
     * @brief Set of storage buffers this stage READS (consumer role) — Sampled
     * Lighting Inc3 M5: generalized from the old fixed BUFFER_READ_A/BUFFER_READ_B
     * two-slot pair into ONE array of arbitrary count. Fed by a
     * BufferSyncGathererNode's BUFFER_ARRAY output. Auto-sync: ComputeStorageRead —
     * paired with a producer's ComputeStorageWrite on the SAME array Resource*
     * (i.e. the SAME BufferSyncGathererNode's own output, or a separate producer-
     * side/consumer-side gatherer pair both fed from the SAME upstream buffer
     * nodes — see this config's own class doc for the two-gatherer-instances shape).
     * The fan-in demo's own "2 distinct read buffers -> 2 waitEdges" case is now
     * expressed as one 2-entry array rather than two named slots; N entries -> N
     * waitEdges generalizes with NO further slot additions. Optional: a producer
     * leaves this unconnected.
     */
    INPUT_SLOT_SYNC(BUFFER_READ_ARRAY, std::vector<VkBuffer>, 15,
        SlotNullability::Optional,
        SlotRole::Execute,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel,
        ::Vixen::RenderGraph::AccessKind::ComputeStorageRead);

    /**
     * @brief Timeline semaphore from FrameSyncNode (P5b). vkQueueSubmit2 signals
     * (producer) / waits (consumer) absolute timeline values for the baked edges.
     */
    INPUT_SLOT(TIMELINE_SEMAPHORE_IN, VkSemaphore, 16,
        SlotNullability::Optional,
        SlotRole::Execute,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    /**
     * @brief Per-frame timeline base offset from FrameSyncNode (P5b). Added to each
     * SyncEdge::timelineOffset to form the absolute signal/wait value.
     */
    INPUT_SLOT(TIMELINE_FRAME_BASE_IN, uint64_t, 17,
        SlotNullability::Optional,
        SlotRole::Execute,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    /**
     * @brief Non-swapchain image this stage WRITES (image-producer middle-pass role).
     * Auto-sync: ComputeStorageWrite — the image-typed sibling of BUFFER_WRITE. Makes
     * the scheduler record this node as a writer of the wired IRenderTarget*'s
     * Resource* so a downstream reader (another IMAGE_WRITE consumer, or a
     * presentation blit node) bakes a SyncEdge. Transitions the image to GENERAL
     * before dispatch; leaves it in GENERAL after (no WSI, no PRESENT_SRC — see the
     * IMAGE_WRITE vs SWAPCHAIN_INFO doc note above the class comment). Optional: a
     * pass with no non-swapchain image output leaves this unconnected.
     */
    INPUT_SLOT_SYNC(IMAGE_WRITE, Vixen::Vulkan::Resources::IRenderTarget*, 18,
        SlotNullability::Optional,
        SlotRole::Execute,
        SlotMutability::ReadWrite,
        SlotScope::NodeLevel,
        ::Vixen::RenderGraph::AccessKind::ComputeStorageWrite);

    /**
     * @brief Set of non-swapchain images this stage WRITES SIMULTANEOUSLY (Sampled
     * Lighting Inc4 M1) — the image-typed sibling of BUFFER_WRITE_ARRAY, additive
     * alongside (NOT replacing) the single-image IMAGE_WRITE slot above. Fed by an
     * ImageSyncGathererNode's IMAGE_ARRAY output (mirrors BufferSyncGathererNode's
     * own file header exactly). Auto-sync: ComputeStorageWrite — makes the scheduler
     * record this node as a writer of EACH constituent image's own Resource*
     * (via Resource::hazardConstituents_, same expansion BUFFER_WRITE_ARRAY already
     * uses) so a downstream reader bakes a real SyncEdge per image. Optional: a pass
     * with no multi-image-write need leaves this unconnected and uses the single
     * IMAGE_WRITE slot instead (or neither, for a pure buffer producer).
     */
    INPUT_SLOT_SYNC(IMAGE_WRITE_ARRAY, std::vector<Vixen::Vulkan::Resources::IRenderTarget*>, 19,
        SlotNullability::Optional,
        SlotRole::Execute,
        SlotMutability::ReadWrite,
        SlotScope::NodeLevel,
        ::Vixen::RenderGraph::AccessKind::ComputeStorageWrite);

    /**
     * @brief Set of non-swapchain images this stage READS (Sampled Lighting Inc4 M5) —
     * the image-typed sibling of BUFFER_READ_ARRAY, additive alongside IMAGE_WRITE/
     * IMAGE_WRITE_ARRAY above. Fed by an ImageSyncGathererNode's IMAGE_ARRAY output
     * (a separate gatherer instance from whichever node's IMAGE_WRITE_ARRAY writes the
     * SAME images — mirrors BUFFER_READ_ARRAY's own two-gatherer-instances shape).
     * Auto-sync: ComputeStorageRead — paired with a producer's ComputeStorageWrite on
     * the SAME constituent Resource*s so the scheduler bakes a real SyncEdge per image.
     * Optional: a pass that reads no non-swapchain images leaves this unconnected.
     */
    INPUT_SLOT_SYNC(IMAGE_READ_ARRAY, std::vector<Vixen::Vulkan::Resources::IRenderTarget*>, 20,
        SlotNullability::Optional,
        SlotRole::Execute,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel,
        ::Vixen::RenderGraph::AccessKind::ComputeStorageRead);

    // ===== OUTPUTS (3) =====

    /** @brief renderComplete semaphore for Present to wait on (consumer role). */
    OUTPUT_SLOT(RENDER_COMPLETE_SEMAPHORE, VkSemaphore, 0,
        SlotNullability::Required,
        SlotMutability::WriteOnly);

    /** @brief Pass-through device for downstream nodes. */
    OUTPUT_SLOT(VULKAN_DEVICE_OUT, VulkanDevice*, 1,
        SlotNullability::Required,
        SlotMutability::WriteOnly);

    /**
     * @brief Pass-through of the FIRST VkBuffer this stage WROTE (producer role).
     * Re-publishes element 0 of BUFFER_WRITE_ARRAY's handle VALUE so a downstream
     * consumer can bind it via its descriptor gatherer AND so the connection
     * establishes a topological producer→consumer ordering (the scheduler indexes
     * groups by execution order, so a producer must sort before the consumer for the
     * baked edge to point the right way). Sampled Lighting Inc3 M5: kept single-buffer
     * (not generalized to an array output) — its only consumer (BuildFanInDemoGraph.cpp)
     * always wires each producer stage with exactly ONE buffer in its write array. The
     * hazard edge itself is baked off the SHARED array Resource*'s per-entry
     * constituents (see Resource::hazardConstituents_) — this passthrough only carries
     * the handle value + the ordering edge, never a tracked-Resource identity.
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

        // Buffer sync slots — Sampled Lighting Inc3 M5: array-typed (std::vector<VkBuffer>),
        // fed by a BufferSyncGathererNode; the array Resource*'s IDENTITY (shared between a
        // producer's write-array and a consumer's read-array output) is what the scheduler
        // bakes edges on. Transient (a value type, like PUSH_CONSTANT_DATA/DESCRIPTOR_SETS
        // above — not a pointer/reference, so Persistent is compile-time-disallowed by
        // SlotValidator's own CanBePersistent check; the array VALUE is re-gathered fresh
        // by BufferSyncGathererNode::ExecuteImpl every frame anyway, so Transient is also
        // the semantically correct lifetime here, matching every other array-VALUE slot).
        HandleDescriptor bufferArrayDesc{"std::vector<VkBuffer>"};
        INIT_INPUT_DESC(BUFFER_WRITE_ARRAY, "buffer_write_array", ResourceLifetime::Transient, bufferArrayDesc);
        INIT_INPUT_DESC(BUFFER_READ_ARRAY, "buffer_read_array", ResourceLifetime::Transient, bufferArrayDesc);

        // Timeline primitives from FrameSyncNode.
        HandleDescriptor timelineSemDesc{"VkSemaphore"};
        INIT_INPUT_DESC(TIMELINE_SEMAPHORE_IN, "timeline_semaphore_in", ResourceLifetime::Persistent, timelineSemDesc);

        HandleDescriptor frameBaseDesc{"uint64_t"};
        INIT_INPUT_DESC(TIMELINE_FRAME_BASE_IN, "timeline_frame_base_in", ResourceLifetime::Transient, frameBaseDesc);

        // Image-write sync slot — same IRenderTarget* handle shape as SWAPCHAIN_INFO
        // (reuses swapchainDesc), but a DISTINCT slot/Resource* identity so the
        // scheduler tracks it as its own hazard, independent of any SWAPCHAIN_INFO
        // wiring on this or any other node.
        INIT_INPUT_DESC(IMAGE_WRITE, "image_write", ResourceLifetime::Persistent, swapchainDesc);

        // Image-write ARRAY sync slot (Sampled Lighting Inc4 M1) — array-typed sibling
        // of IMAGE_WRITE, mirrors BUFFER_WRITE_ARRAY's own Transient rationale exactly
        // (a value type re-gathered fresh by ImageSyncGathererNode::ExecuteImpl every
        // frame, so Persistent is both compile-time-disallowed and semantically wrong).
        HandleDescriptor imageArrayDesc{"std::vector<IRenderTarget*>"};
        INIT_INPUT_DESC(IMAGE_WRITE_ARRAY, "image_write_array", ResourceLifetime::Transient, imageArrayDesc);

        // Image-read ARRAY sync slot (Sampled Lighting Inc4 M5) — same Transient
        // rationale as IMAGE_WRITE_ARRAY (a value type re-gathered fresh every frame).
        INIT_INPUT_DESC(IMAGE_READ_ARRAY, "image_read_array", ResourceLifetime::Transient, imageArrayDesc);

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
    static_assert(std::is_same_v<BUFFER_WRITE_ARRAY_Slot::Type, std::vector<VkBuffer>>);
    static_assert(std::is_same_v<BUFFER_READ_ARRAY_Slot::Type, std::vector<VkBuffer>>);
    static_assert(BUFFER_WRITE_ARRAY_Slot::accessKind == ::Vixen::RenderGraph::AccessKind::ComputeStorageWrite);
    static_assert(BUFFER_READ_ARRAY_Slot::accessKind == ::Vixen::RenderGraph::AccessKind::ComputeStorageRead);
    static_assert(SWAPCHAIN_INFO_Slot::accessKind == ::Vixen::RenderGraph::AccessKind::ComputeStorageWrite);
    static_assert(IMAGE_WRITE_Slot::index == 18, "IMAGE_WRITE must be at index 18 (Sampled Lighting Inc3 M5: renumbered down by one after collapsing the 3 fixed buffer slots into 2 array slots)");
    static_assert(std::is_same_v<IMAGE_WRITE_Slot::Type, Vixen::Vulkan::Resources::IRenderTarget*>);
    static_assert(IMAGE_WRITE_Slot::accessKind == ::Vixen::RenderGraph::AccessKind::ComputeStorageWrite);
    static_assert(IMAGE_WRITE_ARRAY_Slot::index == 19, "IMAGE_WRITE_ARRAY must be at index 19 (Sampled Lighting Inc4 M1: purely additive, appended after IMAGE_WRITE)");
    static_assert(std::is_same_v<IMAGE_WRITE_ARRAY_Slot::Type, std::vector<Vixen::Vulkan::Resources::IRenderTarget*>>);
    static_assert(IMAGE_WRITE_ARRAY_Slot::accessKind == ::Vixen::RenderGraph::AccessKind::ComputeStorageWrite);
    static_assert(IMAGE_READ_ARRAY_Slot::index == 20, "IMAGE_READ_ARRAY must be at index 20 (Sampled Lighting Inc4 M5: purely additive, appended after IMAGE_WRITE_ARRAY)");
    static_assert(std::is_same_v<IMAGE_READ_ARRAY_Slot::Type, std::vector<Vixen::Vulkan::Resources::IRenderTarget*>>);
    static_assert(IMAGE_READ_ARRAY_Slot::accessKind == ::Vixen::RenderGraph::AccessKind::ComputeStorageRead);
};

} // namespace Vixen::RenderGraph
