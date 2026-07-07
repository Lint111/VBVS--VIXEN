#pragma once
// SkyProjectionNode.h — Tiered ESVO Observer Addressing, Inc1 M3 (Tasks 5-7).
//
// Consumes M1/M2's pure-CPU TierAddress/TierDirection/TierMagnitude math (libraries/SVO)
// to build a small "sky point" fixture — (direction, magnitude) tuples for a handful of
// synthetic candidate objects observed from a synthetic observer address — and composites
// them as point sprites into the existing swapchain image, layered OVER the voxel compute
// render exactly like the existing ui_composite_render_pass/UIRenderNode pattern (a
// RenderPassNode set to Load instead of Clear + a FramebufferNode + this node instead of
// UIRenderNode). This is the ONLY GPU-touching piece of the whole Tiered ESVO Inc1
// increment (plan §"M3 — SkyProjectionNode + live composite gate").
//
// SCOPE NOTE (Tiered-ESVO-Inc1-Plan-2026-07.md §0 — read this before touching this file):
// this node builds NO TierRef/TierRefTable GPU data, does NO tier-crossing traversal, and
// does not touch ChildDescriptor/farBit/SVORebuild.cpp/LaineKarrasOctree's traversal code.
// It is purely: CPU-side synthetic TierAddress pairs -> M1/M2's math -> a small SSBO -> a
// point-sprite composite draw. Extending this node to consume real octree/TierRef data is
// explicitly a separate, unscheduled future increment.
//
// SYNTHETIC FIXTURE — NOT THE PRODUCTION DATA PATH: BuildSyntheticFixture() (in the .cpp)
// hand-constructs one observer TierAddress + a handful of candidate object TierAddress/
// TierHopFrame pairs at plausible tier depths/angular offsets, exactly as the plan's Task 5
// describes ("a small synthetic/hardcoded test fixture ... not a real undertow-fed data
// source"). A future increment wiring real candidate data (e.g. from undertow's tracked
// fleet/body set) MUST REPLACE BuildSyntheticFixture() with a real data source, not extend
// it in place — mirrors the scope-note convention TierDirection.h/TierAddress.h already use
// for their own synthetic-vs-production-data boundaries.
//
// GRAPH POSITION: this node sits BETWEEN the voxel compute dispatch and the UI/HUD composite
// pass (compute -> sky-projection -> UI), so its config slot shape mirrors
// ComputeDispatchNodeConfig's "middle pass" shape, not UIRenderNodeConfig's "last pass, owns
// the frame fence + present semaphore" shape — see SkyProjectionNodeConfig.h's doc comment.

#include "Core/TypedNodeInstance.h"
#include "Core/NodeType.h"
#include "Core/GPUPerformanceLogger.h"
#include "Data/Nodes/SkyProjectionNodeConfig.h"

#include <vulkan/vulkan.h>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace Vixen::RenderGraph {

/**
 * @brief Node type for SkyProjectionNode.
 */
class SkyProjectionNodeType : public TypedNodeType<SkyProjectionNodeConfig> {
public:
    SkyProjectionNodeType(const std::string& typeName = "SkyProjection")
        : TypedNodeType<SkyProjectionNodeConfig>(typeName) {}
    ~SkyProjectionNodeType() override = default;

    std::unique_ptr<NodeInstance> CreateInstance(const std::string& instanceName) const override;
};

/**
 * @brief Composites a handful of address-derived sky points (direction + magnitude) as
 * point sprites over the existing voxel/UI render.
 *
 * Two distinct roles (see SkyProjectionNodeConfig.h's fuller doc comment):
 *   1. DATA: builds the synthetic fixture via M1/M2's math and uploads it into a small
 *      host-visible SSBO once at Compile (mirrors InstanceBufferNode's "small CPU-side
 *      per-instance dataset -> SSBO" idiom; no per-frame re-upload).
 *   2. DRAW: owns its own graphics pipeline (built directly with raw Vulkan calls, mirroring
 *      VixenRmlRenderInterface's own hand-rolled pipeline construction) and records/submits a
 *      point-list vkCmdDraw each frame via the SAME timeline-semaphore vkQueueSubmit2 skeleton
 *      UIRenderNode uses (FrameSyncSchedule::waitEdges/signalEdges via FindGroupForNode) — this
 *      node WAITS the upstream compute's signal AND SIGNALS its own completion value for the
 *      downstream UI composite pass to wait (unlike UIRenderNode, which only waits — it is last
 *      in the chain and has no timeline consumer). A live-gate-caught bug during this milestone:
 *      omitting the signal half left the UI's baked waitEdge waiting a timeline value nothing
 *      ever signalled, hanging/erroring at present (VUID-vkQueuePresentKHR-pWaitSemaphores-03268)
 *      — fixed by mirroring ComputeDispatchNode's signalEdges loop exactly. This node does NOT
 *      maintain its own binary "composite complete" semaphore array (a second live-gate-caught
 *      bug: an owned-but-never-waited binary semaphore signalled every frame double-signals,
 *      VUID-vkQueueSubmit2-semaphore-03868) — its RENDER_COMPLETE_SEMAPHORE output is a plain
 *      VK_NULL_HANDLE passthrough purely for the topology edge (mirrors how UIRenderNode's own
 *      COMPOSITE_WAIT_SEMAPHORE input is never actually read/waited — the real ordering is 100%
 *      carried by the timeline edges).
 *
 * The render pass/framebuffers this node consumes are built externally (BuildRenderGraph.cpp
 * creates a dedicated RenderPassNode with PARAM_COLOR_LOAD_OP=Load + a FramebufferNode,
 * exactly mirroring ui_composite_render_pass/ui_composite_framebuffer) — this node does not
 * own their lifecycle, matching GeometryRenderNode/UIRenderNode's own convention.
 */
class SkyProjectionNode : public TypedNode<SkyProjectionNodeConfig> {
public:
    SkyProjectionNode(const std::string& instanceName, NodeType* nodeType);
    ~SkyProjectionNode() override = default;

protected:
    void SetupImpl(TypedSetupContext& ctx) override;
    void CompileImpl(TypedCompileContext& ctx) override;
    void ExecuteImpl(TypedExecuteContext& ctx) override;
    void CleanupImpl(TypedCleanupContext& ctx) override;

private:
    // --- DATA role: synthetic fixture -> SSBO (Task 5) ---
    void BuildSyntheticFixture();  // fills skyPoints_ from M1/M2 math (SYNTHETIC — see file header)
    void CreateBuffer(Vixen::Vulkan::Resources::VulkanDevice* device);
    void DestroyBuffer();

    // --- DRAW role: pipeline + per-frame submit (Tasks 6-7) ---
    void CreatePipeline(Vixen::Vulkan::Resources::VulkanDevice* device, VkRenderPass renderPass);
    void DestroyPipeline();
    void RecordFrame(VkCommandBuffer cmd, VkFramebuffer framebuffer, VkExtent2D extent,
                      const CameraData& camera, uint32_t frameIndex);

    // std430-friendly per-point layout, BYTE-FOR-BYTE matching shaders/SkyProjection.vert's
    // SkyPoint struct. GLSL std430 packing gotcha (verified by hand, not assumed): a vec3
    // struct MEMBER (as opposed to a standalone/array vec3) has base alignment 16 but only
    // occupies 12 bytes — the NEXT member is placed at the smallest offset satisfying ITS
    // OWN alignment, so a scalar float immediately following a vec3 member packs TIGHTLY at
    // offset 12, not 16 (padding to 16 only happens at the very end of the struct, when the
    // struct's overall size rounds up to its base alignment). So: direction[0..11],
    // magnitude[12..15], appliedDelaySeconds[16..19], then 12 bytes of trailing pad so the
    // struct rounds up to the 32-byte std430 array stride (a multiple of 16).
    struct SkyPointGpu {
        float direction[3];         // offset 0..11
        float magnitude;            // offset 12..15 (packs right after vec3, NOT offset 16)
        float appliedDelaySeconds;  // offset 16..19
        float _pad[3];              // offset 20..31 — rounds struct to the 32 B std430 stride
    };
    static_assert(sizeof(SkyPointGpu) == 32, "SkyPointGpu must match shaders/SkyProjection.vert's SkyPoint stride");
    static_assert(offsetof(SkyPointGpu, magnitude) == 12, "must match GLSL std430 vec3-then-scalar packing");
    static_assert(offsetof(SkyPointGpu, appliedDelaySeconds) == 16, "must match GLSL std430 layout");

    std::vector<SkyPointGpu> skyPoints_;  // CPU-side fixture, built once by BuildSyntheticFixture

    // --- DATA role GPU resources (persistent across recompile; freed only at FinalTeardown) ---
    VkBuffer       buffer_ = VK_NULL_HANDLE;
    VkDeviceMemory memory_ = VK_NULL_HANDLE;
    uint32_t       pointCount_ = 0;

    // --- DRAW role GPU resources ---
    VkDescriptorSetLayout descriptorLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool      descriptorPool_   = VK_NULL_HANDLE;
    VkDescriptorSet       descriptorSet_    = VK_NULL_HANDLE;
    VkPipelineLayout      pipelineLayout_   = VK_NULL_HANDLE;
    VkPipeline            pipeline_         = VK_NULL_HANDLE;

    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue  queue_  = VK_NULL_HANDLE;
    PFN_vkQueueSubmit2KHR fpQueueSubmit2_ = nullptr;
    VkCommandPool commandPool_ = VK_NULL_HANDLE;

    VkRenderPass renderPass_ = VK_NULL_HANDLE;  // consumed, not owned (RenderPassNode)
    std::vector<VkCommandBuffer> commandBuffers_;  // one per swapchain image (owned)
    uint32_t syncImageCount_ = 0;

    bool loggedFixture_ = false;  // NODE_LOG_INFO the fixture once at first Compile, not every recompile
};

} // namespace Vixen::RenderGraph
