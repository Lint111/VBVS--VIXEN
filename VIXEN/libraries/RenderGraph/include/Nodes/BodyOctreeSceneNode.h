#pragma once

#include "Core/TypedNodeInstance.h"
#include "Core/NodeType.h"
#include "Core/PerFrameResources.h"
#include "Data/Nodes/BodyOctreeSceneNodeConfig.h"

#include "ShellOctree.h"      // Vixen::SVO::ShellOctree, BuildShellOctree
#include "ShellOctreeGpu.h"   // Vixen::SVO::{Concatenate, ConcatenatedOctrees, BodyInstanceGpu, PackInstances}
#include "ShellDerive.h"      // Vixen::SVO::{DeriveShell, RevalidateShellBricks, ShellDeriveResult}
#include "Recipe/SdfInstruction.h"  // Vixen::SVO::Recipe::SdfInstruction
#include "InstanceSort.h"     // Vixen::SVO::SortInstancesFrontToBack (Inc1 M4b)
#include "Memory/BatchedUploader.h"  // ResourceManagement::UploadHandle/InvalidUploadHandle (Inc1 M4c)

#include <glm/glm.hpp>
#include <cstdint>
#include <memory>
#include <vector>

// Forward declarations
namespace Vixen::Vulkan::Resources {
    class VulkanDevice;
}

namespace Vixen::RenderGraph {

/**
 * @brief Node type for BodyOctreeSceneNode (SP2 Task 5b).
 */
class BodyOctreeSceneNodeType : public TypedNodeType<BodyOctreeSceneNodeConfig> {
public:
    BodyOctreeSceneNodeType(const std::string& typeName = "BodyOctreeScene")
        : TypedNodeType<BodyOctreeSceneNodeConfig>(typeName) {}
    virtual ~BodyOctreeSceneNodeType() = default;

    std::unique_ptr<NodeInstance> CreateInstance(
        const std::string& instanceName
    ) const override;
};

/**
 * @brief Uploads the (<=3) per-kind sparse SHELL octrees + a per-body instance SSBO
 *        for the SP2 LOD body renderer (instanced multi-octree ray march).
 *
 * On Compile the node:
 *   1. builds the <=3 per-kind ShellOctrees once (cached as members; they OWN their
 *      world + registry + octree, so they stay alive for the node's lifetime),
 *   2. serializes + concatenates them via Vixen::SVO::Concatenate (cached bytes),
 *   3. creates 4 GPU octree buffers (nodes / bricks / materials SSBOs + config UBO),
 *      host-visible/host-coherent, filled at create time,
 *   4. allocates a RING of kRingSize (= MAX_FRAMES_IN_FLIGHT) instance SSBOs (once,
 *      persistent across recompile; grown with vkDeviceWaitIdle on capacity overflow),
 *   5. publishes the four octree slots plus INSTANCE_BUFFER (ring[0] as a compile-time
 *      placeholder) + INSTANCE_COUNT.
 *
 * Instance seam: the host calls SetInstances(...) which ONLY stashes the vector and
 * sets a dirty flag — it NO LONGER triggers recompile. ExecuteImpl uploads the current
 * instances into ring[frameIndex % kRingSize] every frame and re-emits that buffer on
 * INSTANCE_BUFFER so the descriptor binds the freshly-written, not-in-flight slot.
 *
 * Lifecycle (CRITICAL — see the WSL/Dozen recompile-frees-in-flight VM-panic trap):
 *   - Octree buffers: created once, persist across recompile, destroyed at FinalTeardown.
 *   - Instance ring: allocated once, persists across recompile; grown (behind
 *     vkDeviceWaitIdle) only on capacity overflow; destroyed at FinalTeardown.
 *   - No GPU resource is ever freed on the per-tick / per-recompile path.
 *
 * Mirrors DynamicInstanceBufferNode's FR-7 ring pattern exactly.
 */
class BodyOctreeSceneNode : public TypedNode<BodyOctreeSceneNodeConfig> {
public:
    using Base = TypedNode<BodyOctreeSceneNodeConfig>;

    BodyOctreeSceneNode(const std::string& instanceName, NodeType* nodeType);
    ~BodyOctreeSceneNode() override = default;

    /**
     * @brief Push the current per-body instance list (host -> node seam).
     *
     * Stashes the vector and marks a dirty flag. Does NOT trigger recompile.
     * ExecuteImpl uploads the current data into the current frame's ring buffer
     * on every frame — the per-tick recompile cascade is gone entirely.
     */
    void SetInstances(std::vector<Vixen::SVO::BodyInstanceGpu> instances);

    /**
     * @brief Reorder the current instance list front-to-back by distance from
     *        `cameraPos` (Sparse-Mip ESVO LOD Inc1 M4b).
     *
     * The shader's per-ray occlusion reject (BodyInstanceRayMarch.comp's
     * `gridT.x > bestT` check against the running nearest-hit) only saves
     * traversal work if closer instances are visited before farther ones in the
     * instance loop — i.e. the array itself must already be near-to-far ordered,
     * not sorted per-ray (that would defeat the point of a cheap, once-per-frame
     * CPU sort). Call after SetInstances (or whenever the camera moves enough to
     * change ordering) and before the next Execute uploads the ring slot.
     */
    void SortInstancesFrontToBack(const glm::vec3& cameraPos);

    /**
     * @brief Read back the current per-body instance list (Sparse-Mip ESVO LOD Inc1 M4c).
     *
     * Lets a host-side residency trigger (VulkanGraphApplication::UpdateBodySceneResidency)
     * evaluate distance/frustum/resolvability per instance without the host needing to
     * separately track whatever it last passed to SetInstances.
     */
    const std::vector<Vixen::SVO::BodyInstanceGpu>& GetInstances() const { return instances_; }

    /**
     * @brief Recipe-Live-App-Bucketed-Dispatch Inc4 M3: read the per-body instance ring
     * buffer's VkBuffer handle for a given ring slot (defaults to slot 0), for a caller
     * OUTSIDE the render graph's own per-frame Execute cycle (e.g. PreTick's specialized-
     * pipeline descriptor-set wiring, which runs before FrameSyncNode advances the live
     * frame index this frame). Same underlying accessor ExecuteImpl itself uses
     * (perFrame_.GetUniformBuffer(frameIndex)) — this is a read-only convenience, not a
     * new upload/write path; the ring slot's CONTENT is still written only by ExecuteImpl.
     */
    VkBuffer GetInstanceBufferHandle(uint32_t ringSlot = 0) const {
        return perFrame_.GetUniformBuffer(ringSlot % kRingSize);
    }

    /**
     * @brief Inject an SdfInstruction recipe for octree 0's bake.
     *
     * When non-empty and VIXEN_STORED_SDF_DEMO is set, octree 0 is baked via
     * BakeRecipeInstructionsToSdfWorld instead of the hardcoded analytic path.
     * Octrees 1/2 remain on the analytic path. Empty (default) = no change.
     * // ponytail: guard keeps analytic path byte-identical when recipe is absent
     */
    void SetBakeRecipe(std::vector<Vixen::SVO::Recipe::SdfInstruction> prog);

    /**
     * @brief Consume a pre-baked recipe pool (I4.1).
     *
     * When set, EnsureOctreesBuilt bypasses the hardcoded shell/SDF archetypes
     * and uses this pool directly.  Parallels SetBakeRecipe/Rematerialize:
     *   - called pre-Compile  → pool is used on next Compile;
     *   - called post-Compile → sets recipeDirty_ so ExecuteImpl re-materializes.
     */
    void SetRecipePool(Vixen::SVO::ConcatenatedOctrees pool);

    /**
     * @brief Lazy-Procedural-Delta-Baseline Inc0 M6 Task 13 — upload the concatenated
     * per-recipe coarse occupancy grid blob (binding 16) built by
     * UberShaderSplice.h::SpliceProceduralRecipesIntoSource's out-param. Mirrors
     * SetRecipePool's pre/post-Compile duality: called pre-Compile → included in the
     * first CreateOctreeBuffers pass; called post-Compile → the buffer is recreated on
     * the next Rematerialize (recipeDirty_ already forces one whenever the shader itself
     * was re-spliced, which is the ONLY time this blob can change — no independent dirty
     * flag needed).
     */
    void SetOccupancyGrid(std::vector<float> concatenatedGrid);

    /**
     * @brief Surface-Shell ESVO cache — brick-layer dilation of the SURFACE set.
     *
     * Clamped to [1,3]; default 1 (minimal sound 26-neighbour invariant). Sizes
     * the derived reachable-shell cache, NOT SdfBake's bandVoxels. Re-derives both
     * cache slots on the next Compile/Rematerialize.
     */
    void SetShellThickness(uint32_t dilation);

    /**
     * @brief Surface-Shell §C dirty-path PRODUCER — value-edit one source brick's SDF lane.
     *
     * The ONE public way to value-edit octree 0's baked SDF post-Compile: writes the
     * source pool via Vixen::SVO::ApplyBrickSdfEdit AND appends the brick to the dirty
     * list in the same call, so the mark can never drift from the mutation. ExecuteImpl
     * consumes the list on the next frame — RevalidateShellBricks into the WRITE shell
     * slot + re-upload of that slot only (no full Rematerialize, no barrier vs the
     * render reading the other slot). Membership-changing edits (a brick entering or
     * leaving the shell set) are NOT detected here; per the §C increment-1 contract
     * those go through SetBakeRecipe/SetRecipePool (full re-derive). Proxy AABBs are
     * invariant under value edits (grid boxes don't move), so no proxy work is queued.
     *
     * @return false (nothing written, nothing marked) if the pool/brick/SDF channel is
     *         invalid or sdf512 holds fewer than kVoxelsPerBrick values.
     */
    bool EditSourceBrickSdf(uint32_t brickId, const std::vector<float>& sdf512);

    /// Accessors for verification/tests (no GPU needed). ShellCacheSlot returns
    /// octree 0's per-octree derivation (the primary Stored-SDF body); ShellPoolSlot
    /// returns the whole multi-octree compact pool. Slot 0/1 = CPU double buffer.
    [[nodiscard]] const Vixen::SVO::ShellDeriveResult& ShellCacheSlot(uint32_t i) const {
        static const Vixen::SVO::ShellDeriveResult kEmpty{};
        const auto& po = shellCache_[i & 1u].perOctree;
        return po.empty() ? kEmpty : po[0];
    }
    [[nodiscard]] const Vixen::SVO::ShellPool& ShellPoolSlot(uint32_t i) const {
        return shellCache_[i & 1u];
    }
    [[nodiscard]] uint32_t ShellDilation() const { return shellDilation_; }

    /// Lazy-Procedural-Delta-Baseline Inc0 M2 Task 4: current residency state (CPU-observable,
    /// no GPU needed) — reflects the capability-derived default once EnsureOctreesBuilt has run,
    /// or whatever RequestBrickResidency last explicitly set.
    [[nodiscard]] bool IsResidencyRequested() const { return residencyRequested_; }

    /// Lazy-Procedural-Delta-Baseline Inc1 M4 Task 6b: cumulative bytes pushed into GPU
    /// buffers via UploadBrickPool/PollBrickUploadCompletion, split boot (first residency
    /// grant) vs steady-state (any later re-upload, e.g. a residency toggle). Feeds the
    /// perf-CSV writer's byte-uploaded columns; CPU-observable, no GPU readback needed.
    [[nodiscard]] uint64_t BootBytesUploaded() const { return bootBytesUploaded_; }
    [[nodiscard]] uint64_t SteadyStateBytesUploaded() const { return steadyStateBytesUploaded_; }

    /// True once PollBrickUploadCompletion has observed the GPU-side brick copy actually
    /// complete (BodyOctreeSceneNode.cpp:991) -- NOT the same moment as BootBytesUploaded()
    /// going non-zero, which reflects only that the upload was QUEUED (UploadBrickPool,
    /// same file ~967-968), not that it finished. A caller polling for "is the resident
    /// brick data actually visible to the shader yet" must check THIS, not BootBytesUploaded
    /// (root-caused 2026-07-12: a test that polled BootBytesUploaded() alone stopped
    /// polling the instant the upload was queued, before the GPU copy had landed, then
    /// rendered against still-stale brick data with no error).
    [[nodiscard]] bool IsBrickPoolUploaded() const { return brickPoolUploaded_; }

    /**
     * @brief Request (or release) brick-pool residency (Sparse-Mip ESVO LOD Inc1 M2).
     *
     * Per §0 scope this is per-tree binary: bricksBuffer_ is always allocated at full
     * capacity (CreateOctreeBuffers), but its contents are only populated once residency
     * is requested. Stashes the request and marks dirty — mirrors SetBakeRecipe/
     * SetRecipePool: ExecuteImpl performs the actual BatchedUploader call next frame,
     * never synchronously inside this setter.
     */
    void RequestBrickResidency(bool resident);

    /**
     * @brief Octree level at which this node's brick tier sits (Sparse-Mip ESVO LOD Inc1 M4c).
     *
     * Lets a host-side residency trigger compare minResolvableLevel(...) against the
     * actual brick depth without duplicating kShellDepth's value as a magic number at
     * the call site.
     */
    static constexpr int GetBrickTierLevel() { return kShellDepth; }

protected:
    void SetupImpl(TypedSetupContext& ctx) override;
    void CompileImpl(TypedCompileContext& ctx) override;
    void ExecuteImpl(TypedExecuteContext& ctx) override;
    void CleanupImpl(TypedCleanupContext& ctx) override;

private:
    // --- Octree build / serialization (octrees + concatenated bytes cached) ---
    void EnsureOctreesBuilt();                                        // build + concatenate once
    void CreateOctreeBuffers(Vixen::Vulkan::Resources::VulkanDevice* device);  // 4 octree buffers
    void EnsureRingAllocated(Vixen::Vulkan::Resources::VulkanDevice* device,
                             VkDeviceSize neededCapacity);            // allocate/grow instance ring
    void DestroyBuffers();
    void DestroyOctreeBuffers();   // P2.3: destroy ONLY the 6 octree/channel buffers (ring untouched)
    void Rematerialize();          // P2.3: re-bake octree 0 + recreate octree buffers (behind vkDeviceWaitIdle)
    void UploadBrickPool();        // Inc1 M2: BatchedUploader-driven brick population (ExecuteImpl-only)
    void PollBrickUploadCompletion();  // Inc1 M4c: non-blocking completion check (replaces WaitAllUploads)
    void DeriveResidencyDefaultIfUnset();  // Lazy-Procedural-Delta-Baseline Inc0 M2 Task 4

    // --- Surface-Shell ESVO cache ---
    // Derive the reachable shell of octree 0 from concatenated_ into BOTH CPU
    // double-buffer slots (bootstrap). No-op when octree 0 is not Stored-SDF.
    void DeriveShellCache();
    // Create/refresh the two GPU shell buffer pairs (double-buffered by distinct
    // object identity) from shellCache_[0]/[1]. Called from CreateOctreeBuffers.
    void CreateShellBuffers(Vixen::Vulkan::Resources::VulkanDevice* device);
    // Re-upload shellCache_[slot]'s compact pool + grid lookup into GPU slot `slot`.
    void UploadShellSlot(Vixen::Vulkan::Resources::VulkanDevice* device, uint32_t slot);

    // Build constants (one shell per kind; depth/material chosen here).
    static constexpr int      kShellDepth = 6;   // 2^6 = 64 cells/axis
    static constexpr uint32_t kKindCount  = 3;

    // Ring size = frames-in-flight (same as DynamicInstanceBufferNode).
    // Defined in .cpp from FrameSyncNodeConfig::MAX_FRAMES_IN_FLIGHT.
    static const uint32_t kRingSize;

    // Per-kind owning shell octrees (built once, kept alive for the node's lifetime).
    std::vector<Vixen::SVO::ShellOctree>   shellOctrees_;
    Vixen::SVO::ConcatenatedOctrees        concatenated_;
    bool                                   octreesBuilt_ = false;
    bool                                   recipeDirty_  = false;  // P2.3: set by SetBakeRecipe post-Compile; re-materialize on next Execute

    // Sparse-Mip ESVO LOD Inc1 M2: per-tree binary brick residency (§0 scope — not
    // per-brick). bricksBuffer_ is always allocated at full capacity in CreateOctreeBuffers;
    // residencyRequested_ gates whether its contents have actually been populated.
    // brickResidencyDirty_ mirrors recipeDirty_'s pattern: RequestBrickResidency only stashes
    // the request; ExecuteImpl performs the BatchedUploader call next frame.
    //
    // Default TRUE (Inc1 M3 fix — was FALSE at M2 landing, which silently regressed every
    // caller that never calls RequestBrickResidency: no production call site does, so
    // brick population was skipped entirely for the app's default scene and any binary/
    // Procedural/Stored body until M3's shader change made the gap observable
    // (RenderMultiKindBodiesProvesStrideFix failing pre-M3-fix proved this against the real
    // shader). Mip-only ("streaming") behavior is now opt-in via RequestBrickResidency(false),
    // not the silent default — matches pre-Inc1 behavior for every existing caller.
    bool                                    residencyRequested_  = true;
    bool                                    brickPoolUploaded_   = false;
    bool                                    brickResidencyDirty_ = false;

    // Lazy-Procedural-Delta-Baseline Inc0 M2 Task 4: latch marking that residencyRequested_
    // was set EXPLICITLY (a real RequestBrickResidency call), as opposed to still holding its
    // constructor default. Set by RequestBrickResidency; cleared by SetRecipePool/SetBakeRecipe
    // (a genuinely new pool is staged — any previous explicit grant no longer applies to it).
    // DeriveResidencyDefaultIfUnset (called once, on the node's first-ever CompileImpl) skips
    // the capability derivation entirely whenever this is set, so a pre-Compile
    // RequestBrickResidency(false)/(true) call (VIXEN_TIER_CROSSING_NONRESIDENT,
    // VIXEN_TIER_ZOOM_DEMO, VIXEN_TIER_CROSSING_DEMO's eager pin) always wins. Deliberately
    // NOT re-evaluated on Rematerialize (SetBakeRecipe/SetRecipePool post-Compile) — Rematerialize
    // never touches residencyRequested_ itself, so a live grant from the app's per-frame
    // residency trigger (UpdateBodySceneResidency) survives an editor-toggle rebuild with the
    // camera unmoved, instead of being silently reset to lazy.
    bool                                    residencyExplicitlyRequested_ = false;

    // Inc1 M4c: async completion-tracking for the brick-pool upload. M2's UploadBrickPool
    // originally blocked on device->WaitAllUploads() every toggle — fine for a "rare,
    // explicit" residency change, but M4c's per-frame camera-driven re-check turns toggles
    // frequent enough that a synchronous wait-idle would hitch. Upload() now queues +
    // FlushUploads()es without blocking; ExecuteImpl polls IsUploadComplete() each frame
    // (cheap: a fence/timeline check, not a wait) and only flips brickPoolUploaded_ /
    // stamps brickResident=1 into configs once the GPU-side copy is actually visible.
    ResourceManagement::UploadHandle       pendingBrickUploadHandle_  = ResourceManagement::InvalidUploadHandle;
    ResourceManagement::UploadHandle       pendingConfigUploadHandle_ = ResourceManagement::InvalidUploadHandle;

    // Inc1 M4 Task 6b: cumulative brick-pool bytes uploaded, split by whether the FIRST
    // (boot) upload has completed yet. bootBytesUploaded_ latches the size of that first
    // UploadBrickPool call; every subsequent one (a later residency toggle) accumulates
    // into steadyStateBytesUploaded_ instead.
    uint64_t                                bootBytesUploaded_        = 0;
    uint64_t                                steadyStateBytesUploaded_ = 0;

    // Optional recipe for octree 0 (P2.1 materialization). Empty = analytic path.
    std::vector<Vixen::SVO::Recipe::SdfInstruction> bakeRecipe_;

    // I4.1: pre-baked pool from BakeRegistryToPool. When set, EnsureOctreesBuilt
    // skips the hardcoded shell/SDF archetypes and uses this pool directly.
    Vixen::SVO::ConcatenatedOctrees providedPool_;
    bool                             poolProvided_ = false;

    // Lazy-Procedural-Delta-Baseline Inc0 M6 Task 13: concatenated per-recipe occupancy
    // grid, set by SetOccupancyGrid. CPU source of truth for CreateOctreeBuffers' upload —
    // mirrors providedPool_'s "stashed here, consumed at (re)Compile" shape.
    std::vector<float> occupancyGrid_;

    // Current instance list (set by SetInstances; uploaded in ExecuteImpl).
    std::vector<Vixen::SVO::BodyInstanceGpu> instances_;
    // int32_t (not uint32_t) to match the shader's reflected `int instanceCount` push-constant field
    // (BodyInstanceRayMarch.comp). The INSTANCE_COUNT slot, this member, and the shader field are all
    // int32_t so the gatherer's reflection-driven any_cast<int32_t> at Execute succeeds.
    int32_t                                  instanceCount_ = 0;

    // --- GPU resources (persistent across recompile; freed only at FinalTeardown) ---
    VkBuffer       nodesBuffer_          = VK_NULL_HANDLE;
    VkDeviceMemory nodesMemory_          = VK_NULL_HANDLE;
    VkBuffer       bricksBuffer_         = VK_NULL_HANDLE;
    VkDeviceMemory bricksMemory_         = VK_NULL_HANDLE;
    VkBuffer       materialsBuffer_      = VK_NULL_HANDLE;
    VkDeviceMemory materialsMemory_      = VK_NULL_HANDLE;
    VkBuffer       configBuffer_         = VK_NULL_HANDLE;
    VkDeviceMemory configMemory_         = VK_NULL_HANDLE;
    // Inc3 M2: generic channel pool buffer (shader binding 11) + brick-grid lookup (shader binding 12).
    // Created with a 1-byte placeholder when concatenated_.channelPool is empty
    // (binary/Procedural path — non-regression invariant: descriptor set always valid).
    VkBuffer       sdfBuffer_            = VK_NULL_HANDLE;
    VkDeviceMemory sdfMemory_            = VK_NULL_HANDLE;
    VkBuffer       brickLookupBuffer_    = VK_NULL_HANDLE;
    VkDeviceMemory brickLookupMemory_    = VK_NULL_HANDLE;
    // Sparse-Mip ESVO LOD Inc1 M3: mip sample pool buffer (shader binding 13).
    // Created with a 1-byte placeholder when concatenated_.mipPool is empty
    // (a tree that was never mip-baked — ConcatenateSdf's plain, non-mip sibling).
    VkBuffer       mipPoolBuffer_        = VK_NULL_HANDLE;
    VkDeviceMemory mipPoolMemory_        = VK_NULL_HANDLE;
    // Tiered-ESVO Inc2 M3: tier-crossing reference table buffer (shader binding 15).
    // Created with a 1-byte placeholder when concatenated_.tierRefTable is empty
    // (the common case — no tree in the scene has any tier-crossing leaves).
    VkBuffer       tierRefTableBuffer_   = VK_NULL_HANDLE;
    VkDeviceMemory tierRefTableMemory_   = VK_NULL_HANDLE;
    // Lazy-Procedural-Delta-Baseline Inc0 M6 Task 13: occupancy grid buffer (shader binding
    // 16). Created with a 1-byte placeholder when occupancyGrid_ is empty (no procedural
    // recipe with a derivable grid registered — the common case for a scene with no
    // recipes, or one with only non-whitelisted-opcode recipes).
    VkBuffer       occupancyGridBuffer_  = VK_NULL_HANDLE;
    VkDeviceMemory occupancyGridMemory_  = VK_NULL_HANDLE;

    // --- Surface-Shell ESVO cache GPU buffers (double-buffered by DISTINCT object
    //     identity). Render reads slot [N&1] (last committed); the ShellRevalidate
    //     compute pass writes slot [(N+1)&1]. Because the two GPU buffers are
    //     SEPARATE VkBuffer objects (distinct Resource* on the graph side), the
    //     FrameSyncScheduler — which keys hazards by pointer identity — never
    //     inserts a false barrier between a render reading slot N and a revalidate
    //     writing slot N+1 (proven: FrameSyncScheduler.cpp per-resource timelines).
    //     shellData[slot] holds the COMPACT pool (binding 11 replacement),
    //     shellLookup[slot] holds the grid->shellSlot remap (binding 12 replacement).
    VkBuffer       shellDataBuffer_[2]   = { VK_NULL_HANDLE, VK_NULL_HANDLE };
    VkDeviceMemory shellDataMemory_[2]   = { VK_NULL_HANDLE, VK_NULL_HANDLE };
    VkBuffer       shellLookupBuffer_[2] = { VK_NULL_HANDLE, VK_NULL_HANDLE };
    VkDeviceMemory shellLookupMemory_[2] = { VK_NULL_HANDLE, VK_NULL_HANDLE };
    VkDeviceSize   shellDataCapacity_[2]   = { 0, 0 };  // bytes allocated per slot
    VkDeviceSize   shellLookupCapacity_[2] = { 0, 0 };

    // Raster-proxy artifact (hybrid slice A): per-shell-brick template-local AABBs
    // (ShellProxyAabb, 32B), flattened across octree templates, on the SAME
    // two-distinct-VkBuffer double-buffer pattern as shellData/shellLookup (the
    // FrameSyncScheduler hazard-keys by Resource/pointer identity). No graph output
    // slot yet — the proxy raster pre-pass (slice B2) is its first binder.
    VkBuffer       proxyAabbBuffer_[2]   = { VK_NULL_HANDLE, VK_NULL_HANDLE };
    VkDeviceMemory proxyAabbMemory_[2]   = { VK_NULL_HANDLE, VK_NULL_HANDLE };
    VkDeviceSize   proxyAabbCapacity_[2] = { 0, 0 };

    // CPU double-buffer (source of truth; also what the tests inspect). Each slot
    // holds the multi-octree compact ShellPool (drop-in ConcatenatedOctrees +
    // per-octree ShellDeriveResults for the dirty-revalidate path).
    Vixen::SVO::ShellPool         shellCache_[2];
    uint32_t                      shellDilation_ = 1u;   // [1,3]; sound 26-neighbour default
    // CPU-owned dirty source-brick list (§C). A value edit pushes the affected
    // brick range here; ExecuteImpl revalidates only those bricks in the write slot.
    std::vector<uint32_t>         dirtyBricks_;

    // Instance SSBO ring (one buffer per frame-in-flight — never freed on the tick path).
    PerFrameResources perFrame_;
    VkDeviceSize      instanceRingCapacity_ = 0;  // bytes per ring slot (grow-only)
};

} // namespace Vixen::RenderGraph
