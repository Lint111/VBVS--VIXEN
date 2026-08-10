#pragma once

#define NOMINMAX

#include "IDebugBuffer.h"
#include "IDebugCapture.h"
#include "DebugRaySample.h"
#include <vulkan/vulkan.h>
#include <vector>
#include <span>
#include <any>
#include <cstdint>
#include <string>
#include <utility>

namespace Vixen::RenderGraph::Debug {

/**
 * @brief Round-6 DDA-threshold-degeneracy probe readback (see
 * TraceBufferHeader's farFieldLhs/RhsMin/MaxBits): min/max of both operands
 * compared at the far-field gate, across the whole boot.
 */
struct FarFieldRanges {
    float lhsMin = 0.0f;
    float lhsMax = 0.0f;
    float rhsMin = 0.0f;
    float rhsMax = 0.0f;
};

/**
 * @brief Round-7 blocker-1 probe readback (see TraceBufferHeader's
 * farFieldMipSuccess/farFieldMipFail): does descendToNodeOrdinal +
 * shadeFromMipSample resolve a real mip sample, or fall through to the
 * vec3(0.5) placeholder, across the whole boot.
 */
struct FarFieldMipStats {
    uint32_t success = 0;
    uint32_t fail = 0;
};

/**
 * @brief Round-17 probe: octree-3-only breakdown of traverseRayQueryWorld's
 * candidate loop (see rtLoopEntriesOct3/farFieldGateRejectOct3/
 * farFieldCandidatesOct3 in DebugRaySample.h). Discriminates which stage
 * thins octree 3's candidates: loopEntries (raw AABB candidates from the
 * TLAS), gateReject (tCellEnter>=bestT continue), candidatesReachingGate.
 */
struct FarFieldOct3Stats {
    uint32_t loopEntries = 0;
    uint32_t gateReject = 0;
    uint32_t candidatesReachingGate = 0;
};

/**
 * @brief Batch-10 readback (see TraceBufferHeader's farFieldColorResolved/
 * farFieldColorFallback): splits shadeFromMipSample's success into "a real
 * SEM_COLOR mip sample was resolved" vs "fell through to the flat grey
 * vec3(0.5) placeholder" -- farFieldMipSuccess above cannot distinguish these.
 */
struct FarFieldColorStats {
    uint32_t resolved = 0;
    uint32_t fallback = 0;
};

/**
 * @brief Round-11 readback (see TraceBufferHeader's *ByTag arrays):
 * far-field candidates/count/colorResolved/colorFallback split by which of
 * TraceWorld's two callers is executing -- index 0 = primary camera march
 * (BodyInstanceRayMarch.comp), index 1 = probe gather (ProbeGather.comp).
 * These are the ONLY two dispatches that can carry far-field firings on
 * either backend (TraceWorldShadow's AnyHit twins contain no far-field
 * logic at all).
 */
struct FarFieldByTagStats {
    uint32_t candidates[2] = {0, 0};
    uint32_t count[2] = {0, 0};
    uint32_t colorResolved[2] = {0, 0};
    uint32_t colorFallback[2] = {0, 0};
};

/**
 * @brief Batch-24 FARGEN readback (see rectRays/rectCellEntries/rectGateCross
 * in DebugRaySample.h): rect-scoped generation funnel over the far clusters
 * c1∪c2, isolating whether missing candidates die at entry, at the gate, or
 * after crossing it.
 */
struct FarFieldRectStats {
    uint32_t rays = 0;
    uint32_t cellEntries = 0;
    uint32_t gateCross = 0;
};

/**
 * @brief Batch-25 JOB 2: 8-bucket histogram of FarFieldGateLhs, rect-scoped
 * (see rectLhsHistogram in DebugRaySample.h). Bucket edges around the
 * 0.9375 gate threshold: <0.25,<0.5,<0.75,<0.9375,<1.25,<2,<4,>=4.
 */
struct FarFieldRectLhsHistogram {
    uint32_t buckets[8] = {};
};

/**
 * @brief Batch-27 JOB 2: ESVO's own cutoff criterion (tv_max*coef+bias vs
 * scale_exp2, traverseOctreeInstancedOnce), rect-scoped, same rays as
 * FarFieldRanges/FarFieldRectLhsHistogram above -- side-by-side comparison.
 * lhs/rhs are LOCAL/NORMALIZED octree-space (no world scale).
 */
struct EsvoCutoffOperands {
    float lhsMin = 0.0f;
    float lhsMax = 0.0f;
    float rhsMin = 0.0f;
    float rhsMax = 0.0f;
    uint32_t histogram[8] = {};  // same 8 edges as FarFieldRectLhsHistogram
    uint32_t crossLevelMin = 0;
    uint32_t crossLevelMax = 0;
};

/**
 * @brief Batch-29 JOB 3 (deep-field mip-accessor policy): 8-bucket histogram
 * of the LEVEL mipPolicyLevel (SVOTypes.glsl) resolves to at every
 * descendToNodeOrdinal call, VIXEN_MIP_POLICY only, rect-agnostic
 * (whole-frame). Buckets 0-6 = levels 0..6 above brick, bucket 7 = level>=7.
 */
struct PolicyLevelHistogram {
    uint32_t buckets[8] = {};
};

/**
 * @brief Batch-29 JOB 4: rect-scoped attribution for ESVO's five
 * shadeFromMipSample call sites (SceneBindings.glsl) -- see
 * recordEsvoMipArm's header comment for the arm index -> call site mapping.
 * Batch-30 stream B adds index 5 (policy-level arm, VIXEN_MIP_POLICY only).
 */
struct EsvoMipArmStats {
    uint32_t hits[6] = {};
};

/**
 * @brief Batch-32 JOB 1: min/max/mean-ish of the LEVEL that actually fed a
 * shaded far-field pixel, recorded at the mip-sample call site (see
 * farFieldSampledLevelMin/Max/Sum/Count field comment, DebugRaySample.h).
 */
struct FarFieldSampledLevelStats {
    uint32_t min = 0;
    uint32_t max = 0;
    uint32_t sum = 0;
    uint32_t count = 0;
};

/**
 * @brief Batch-33 JOB 2: [FarFieldSampleIntensity] -- luminance of the shaded
 * mip color at the same call site as FarFieldSampledLevel (shares its
 * count). min/max are decoded floatBitsToUint bits; mean divides the
 * fixed-point sum by kIntensityFixedPointScale (SceneBindings.glsl) * count.
 * See DebugRaySample.h's farFieldSampleIntensity* field comment.
 */
struct FarFieldSampleIntensityStats {
    float min = 0.0f;
    float max = 0.0f;
    float mean = 0.0f;
};

/**
 * @brief Batch-35: [PolicyEntryDispatch] -- counts how many instance rays
 * took the entry-point mip path (no march) vs fell through to the exact
 * per-cell march, at the DDA's traverseCoarseGridInstancedSdf entry. See
 * policyEntryDispatchMip/March field comment, DebugRaySample.h.
 */
struct PolicyEntryDispatchStats {
    uint32_t mip = 0;
    uint32_t march = 0;
    // Batch-39: subset of `march` where the ray WAS policy-admitted but its
    // entry cell held no brick (entryLocalBrickIdx == 0xFFFFFFFF) -- distinct
    // from the genuine detail-regime population also folded into `march`.
    // Additive: mip/march are unchanged, this is a breakdown on top.
    uint32_t emptyEntry = 0;
};

/**
 * @brief Regime-3 (cosmic accumulation) first slice readback (see
 * regime3EntryCount/regime3EarlyOutCount, DebugRaySample.h). entry = rays
 * that took the accumulation walk instead of a single mip-hit commit;
 * earlyOut = subset that hit the T~eps early-out (rather than exhausting the
 * walk's cell budget).
 */
struct Regime3Stats {
    uint32_t entry = 0;
    uint32_t earlyOut = 0;
};

/**
 * @brief Compositing-slice part 1 (walkCov source audit): min/max of walkCov
 * (readMipSample(SEM_SDF).y clamped [0,1]) and walkSampledLevel at the
 * regime-3 walk's sample call site (see walkCovMinBits/walkSampledLevelMin
 * field comment, DebugRaySample.h). min/max are decoded floatBitsToUint bits
 * for cov (non-negative by construction, same encoding as EntryGateRange).
 */
struct WalkCovStats {
    float covMin = 0.0f;
    float covMax = 0.0f;
    uint32_t levelMin = 0;
    uint32_t levelMax = 0;
};

/**
 * @brief B50-T1 follow-up (C3 gate probe): how many times the composite blend
 * actually executed, and the largest max(behindColor.rgb) seen over exactly
 * those executions. blends>0 with behindMax==0 is the decisive "the blend runs
 * but behindColor is black" reading (see compositeBlends field comment,
 * DebugRaySample.h).
 */
struct CompositeBlendStats {
    uint32_t blends = 0;
    float behindMax = 0.0f;
};

/** E1-T1 stencil slice 0 composition census. Indices are
 * [regime: surface,mip,cosmic][source: virtual,materialized,mixed]. */
struct CompositionCounterStats {
    uint32_t pixels[3][3] = {};
    // wave 0 = queued ShadowRayTrace; wave 1 = derived ShadowVisibilityWave
    // (analytic and reservoir dispatches are the same wave type).
    uint32_t shadowWaveEntries[2][3][3] = {};
    // Rays for which the blend-interval-gated policy admitted at least one
    // candidate that the former nearest-hit entry-cull would have rejected.
    uint32_t relaxedRays = 0;
};

/** E7-T1 policy-stencil materialization and shadow-word preservation gate. */
struct PolicyStencilStats {
    uint32_t primaryMaterializations = 0;
    uint32_t analyticPreservationChecks = 0;
    uint32_t reservoirWrites = 0;
    uint8_t primaryStencilOr = 0;
    uint8_t analyticStencilOr = 0;
    uint8_t reservoirStencilOr = 0;
    uint8_t analyticShadowOr = 0;
    bool reservoirVisible = false;
    bool primaryLowBitsNonzero = false;
    bool analyticStencilMismatch = false;
    bool reservoirPreservationMismatch = false;
};

/**
 * @brief GPU buffer for capturing per-ray traversal traces
 *
 * This class implements IDebugBuffer for ray trace data, enabling
 * polymorphic buffer handling in the render graph.
 *
 * Buffer layout:
 * - [0..sizeof(TraceBufferHeader)-1]: TraceBufferHeader (writeIndex, capacity, counters, padding)
 * - [sizeof(TraceBufferHeader)..]: RayTrace[] array (header + MAX_TRACE_STEPS * TraceStep each)
 *
 * Usage:
 * @code
 * RayTraceBuffer buffer(1024);  // 1024 rays max
 * buffer.Create(device, physicalDevice);
 *
 * // Bind to descriptor set at binding 4
 * buffer.Reset(device);
 * // ... dispatch compute shader ...
 * uint32_t count = buffer.Read(device);
 * auto traces = buffer.GetRayTraces();
 * @endcode
 */
// RayTraceBuffer implements BOTH IDebugBuffer (the polymorphic buffer-data interface) and
// IDebugCapture (the "this resource is debug-capturable, here's its identity" marker
// DescriptorResourceGathererNode::ProcessSlot looks for via GetInterface<IDebugCapture>()).
// Finishes the migration DebugCaptureResource's deprecation comment called for ("use
// RayTraceBuffer... directly instead") — that migration moved VoxelGridNode off the wrapper
// class but never updated RayTraceBuffer itself to implement IDebugCapture, so the gatherer's
// debug-flagged-slot detection has been silently finding nothing (WARNING: Debug-flagged slot N
// does not implement IDebugCapture) ever since; the whole ray-trace debug-export pipeline
// (DebugBufferReaderNode's autoExport) was dead as a result.
class RayTraceBuffer : public IDebugBuffer, public IDebugCapture {
public:
    /**
     * @brief Conversion type declaration for compile-time type system
     *
     * Enables the RenderGraph type system to recognize RayTraceBuffer
     * as a wrapper around VkBuffer without explicit registration.
     * See CompileTimeResourceSystem.h for the conversion_type pattern.
     */
    using conversion_type = VkBuffer;

    /**
     * @brief Implicit conversion to VkBuffer for descriptor binding
     */
    operator VkBuffer() const { return buffer_; }

    /**
     * @brief Construct buffer with specified capacity
     * @param rayCapacity Maximum number of rays to capture
     */
    explicit RayTraceBuffer(uint32_t rayCapacity = 1024);

    ~RayTraceBuffer() override;

    // Non-copyable
    RayTraceBuffer(const RayTraceBuffer&) = delete;
    RayTraceBuffer& operator=(const RayTraceBuffer&) = delete;

    // Movable
    RayTraceBuffer(RayTraceBuffer&& other) noexcept;
    RayTraceBuffer& operator=(RayTraceBuffer&& other) noexcept;

    // =========================================================================
    // Lifecycle
    // =========================================================================

    /**
     * @brief Create Vulkan buffer with HOST_VISIBLE | HOST_COHERENT memory
     * @param device Vulkan device
     * @param physicalDevice Physical device for memory type selection
     * @return true if creation succeeded
     */
    bool Create(VkDevice device, VkPhysicalDevice physicalDevice);

    /**
     * @brief Destroy Vulkan resources
     * @param device Vulkan device used in Create()
     */
    void Destroy(VkDevice device);

    // =========================================================================
    // IDebugBuffer interface
    // =========================================================================

    DebugBufferType GetType() const override { return DebugBufferType::RayTrace; }
    const char* GetTypeName() const override { return "RayTrace"; }

    VkBuffer GetVkBuffer() const override { return buffer_; }
    VkDeviceSize GetBufferSize() const override { return bufferSize_; }
    bool IsValid() const override { return buffer_ != VK_NULL_HANDLE && memory_ != VK_NULL_HANDLE; }
    bool IsHostVisible() const override { return isHostVisible_; }

    bool Reset(VkDevice device) override;
    uint32_t Read(VkDevice device) override;

    /**
     * @brief Read the accumulated far-field-cutoff counter (round-3 fix item 3).
     * Header-only map, independent of Read()/IDebugCapture wiring; Reset()
     * never clears this field, so it accumulates for the boot's lifetime.
     */
    uint32_t ReadFarFieldCount(VkDevice device) const;

    /**
     * @brief Read the accumulated far-field-candidates counter (round-5
     * diagnostics). Same header-only, never-reset discipline as
     * ReadFarFieldCount above.
     */
    uint32_t ReadFarFieldCandidates(VkDevice device) const;

    /**
     * @brief Read the round-6 far-field gate operand min/max ranges (see
     * FarFieldRanges above). Same header-only, never-reset discipline.
     */
    FarFieldRanges ReadFarFieldRanges(VkDevice device) const;

    // BATCH 38: the ENTRY dispatch gate's own LHS range. Distinct from
    // ReadFarFieldRanges, whose probe is blind to the entry decision (its call
    // sites are the mid-march safety net + the RT twin) — batch-37 finding.
    struct EntryGateRange { float lhsMin; float lhsMax; };
    EntryGateRange ReadEntryGateRange(VkDevice device) const;

    /**
     * @brief Read the round-6 blocker-2 RT TLAS candidate-loop-entry counter
     * (see rtLoopEntries in DebugRaySample.h). Same never-reset discipline.
     */
    uint32_t ReadRtLoopEntries(VkDevice device) const;

    /**
     * @brief Read the round-7 blocker-1 far-field mip-resolve success/fail
     * counters (see FarFieldMipStats above). Same never-reset discipline.
     */
    FarFieldMipStats ReadFarFieldMipStats(VkDevice device) const;

    /**
     * @brief Read the round-13 far-field descent-fail counter (see
     * farFieldDescentFail in DebugRaySample.h): splits farFieldMipFail into
     * "descendToNodeOrdinal never reached the brick level" vs "reached it but
     * shadeFromMipSample found no coverage". Same never-reset discipline.
     */
    uint32_t ReadFarFieldDescentFail(VkDevice device) const;

    /**
     * @brief Read the round-17 octree-3-only candidate-loop breakdown (see
     * FarFieldOct3Stats above). Same header-only, never-reset discipline.
     */
    FarFieldOct3Stats ReadFarFieldOct3Stats(VkDevice device) const;

    /// Read the per-frame fixed-capacity E6-T2 body x mip-level byte ledger.
    MipReadByteCounters ReadMipReadByteCounters(VkDevice device) const;
    [[nodiscard]] const std::vector<MipReadByteCounters>& GetMipReadSnapshots() const {
        return mipReadSnapshots_;
    }

    /**
     * @brief Read the round-13 probe #2 descent-fail level min/max (see
     * farFieldDescentFailLevelMin/Max in DebugRaySample.h). Same never-reset
     * discipline. Returns {min, max} as a pair (hops = depth - level).
     */
    std::pair<uint32_t, uint32_t> ReadFarFieldDescentFailLevelRange(VkDevice device) const;

    /**
     * @brief Read the round-7 blocker-1 probe #2 tight-bounds far-hit-
     * rejection discard count. Same never-reset discipline.
     */
    uint32_t ReadFarFieldRejectedByBounds(VkDevice device) const;

    /**
     * @brief Read the round-7 blocker-1 probe #3 far-field-firings-that-won
     * TraceWorld's isCloserHit count. Same never-reset discipline.
     */
    uint32_t ReadFarFieldWon(VkDevice device) const;

    /**
     * @brief Read the round-9 per-pixel TERMINAL far-field count -- pixels
     * whose FINAL rendered HitRecord carries HITRECORD_FLAG_FAR_FIELD, not
     * just "won its own per-instance-loop compare" (see farFieldTerminal in
     * DebugRaySample.h). Same never-reset discipline.
     */
    uint32_t ReadFarFieldTerminal(VkDevice device) const;

    /**
     * @brief Read the batch-10 far-field color-resolved/fallback split (see
     * FarFieldColorStats above). Same never-reset discipline.
     */
    FarFieldColorStats ReadFarFieldColorStats(VkDevice device) const;

    /**
     * @brief Read the round-11 per-dispatch-tag far-field split (see
     * FarFieldByTagStats above). Same never-reset discipline.
     */
    FarFieldByTagStats ReadFarFieldByTagStats(VkDevice device) const;

    /**
     * @brief Read the batch-24 FARGEN rect-scoped generation funnel (see
     * FarFieldRectStats above). Same header-only, never-reset discipline.
     */
    FarFieldRectStats ReadFarFieldRectStats(VkDevice device) const;

    /**
     * @brief Batch-25 JOB 2: read the rect-scoped FarFieldGateLhs histogram
     * (see FarFieldRectLhsHistogram above). Same header-only, never-reset
     * discipline.
     */
    FarFieldRectLhsHistogram ReadFarFieldRectLhsHistogram(VkDevice device) const;

    /**
     * @brief Batch-27 JOB 2: read ESVO's own cutoff criterion operand log
     * (see EsvoCutoffOperands above). Same header-only, never-reset discipline.
     */
    EsvoCutoffOperands ReadEsvoCutoffOperands(VkDevice device) const;

    /**
     * @brief Batch-29 JOB 3: read the rect-agnostic policy-level histogram
     * (see PolicyLevelHistogram above). Same header-only, never-reset
     * discipline. Zero across the board when VIXEN_MIP_POLICY is unset (the
     * shader never calls recordPolicyLevel in that build).
     */
    PolicyLevelHistogram ReadPolicyLevelHistogram(VkDevice device) const;

    /**
     * @brief Batch-29 JOB 4: read the rect-scoped ESVO shadeFromMipSample
     * arm attribution (see EsvoMipArmStats above). Same header-only,
     * never-reset discipline.
     */
    EsvoMipArmStats ReadEsvoMipArmStats(VkDevice device) const;

    /**
     * @brief Batch-32 JOB 1: read the level-sensitive far-field counter (see
     * FarFieldSampledLevelStats above). Same header-only, never-reset
     * discipline.
     */
    FarFieldSampledLevelStats ReadFarFieldSampledLevelStats(VkDevice device) const;

    /**
     * @brief Batch-33 JOB 2: read the far-field sample-intensity stats (see
     * FarFieldSampleIntensityStats above). Same header-only, never-reset
     * discipline.
     */
    FarFieldSampleIntensityStats ReadFarFieldSampleIntensityStats(VkDevice device) const;

    /**
     * @brief Batch-35: read the entry-point dispatch counters (see
     * PolicyEntryDispatchStats above). Same header-only, never-reset
     * discipline.
     */
    PolicyEntryDispatchStats ReadPolicyEntryDispatchStats(VkDevice device) const;

    /**
     * @brief Regime-3 (cosmic accumulation) first slice: read the entry/
     * early-out counters (see Regime3Stats above). Same header-only,
     * never-reset discipline.
     */
    Regime3Stats ReadRegime3Stats(VkDevice device) const;

    /**
     * @brief Compositing-slice part 1: read the walkCov/walkSampledLevel
     * min/max probe (see WalkCovStats above). Same header-only, never-reset
     * discipline.
     */
    WalkCovStats ReadWalkCovStats(VkDevice device) const;

    /**
     * @brief B50-T1 follow-up C3 probe: read the composite-blend execution
     * count + max behindColor magnitude (see CompositeBlendStats above).
     * Same header-only, never-reset discipline.
     */
    CompositeBlendStats ReadCompositeBlendStats(VkDevice device) const;

    /** Read the env-gated slice-0 histogram stored in the terminal-pixel ring. */
    CompositionCounterStats ReadCompositionCounterStats(VkDevice device) const;

    /** Read the env-gated E7-T1 policy-stencil parity summary. */
    PolicyStencilStats ReadPolicyStencilStats(VkDevice device) const;

    std::any GetData() const override;

protected:
    std::any GetDataPtr() const override;

public:
    // =========================================================================
    // IDebugCapture interface
    // =========================================================================

    IDebugBuffer* GetBuffer() override { return this; }
    const IDebugBuffer* GetBuffer() const override { return this; }

    std::string GetDebugName() const override { return debugName_; }
    uint32_t GetBindingIndex() const override { return bindingIndex_; }

    bool IsCaptureEnabled() const override { return captureEnabled_; }
    void SetCaptureEnabled(bool enabled) override { captureEnabled_ = enabled; }

    // Set once by the owning node (VoxelGridNode etc.) after construction — not part of the
    // constructor since callers build the buffer with just its capacity, then separately
    // know their own instance name/binding index.
    void SetDebugName(std::string name) { debugName_ = std::move(name); }
    void SetBindingIndex(uint32_t binding) { bindingIndex_ = binding; }

    // =========================================================================
    // RayTrace-specific accessors
    // =========================================================================

    /**
     * @brief Get read-only view of ray traces
     * @return Span of RayTrace (empty if no data read yet)
     */
    std::span<const RayTrace> GetRayTraces() const {
        return std::span<const RayTrace>(rayTraces_.data(), rayTraces_.size());
    }

    /**
     * @brief Get the configured capacity (max rays)
     */
    uint32_t GetCapacity() const { return capacity_; }

    /**
     * @brief Get number of rays read in last Read() call
     */
    uint32_t GetCapturedCount() const { return capturedCount_; }

    /**
     * @brief Get total writes since last reset (may exceed capacity if wrapped)
     */
    uint32_t GetTotalWrites() const { return totalWrites_; }

    /**
     * @brief Check if buffer has wrapped (more writes than capacity)
     */
    bool HasWrapped() const { return totalWrites_ > capacity_; }

    /**
     * @brief Calculate required buffer size for given ray count
     */
    static VkDeviceSize CalculateBufferSize(uint32_t rayCapacity) {
        return sizeof(TraceBufferHeader) + TRACE_RAY_SIZE * rayCapacity;
    }

private:
    // Vulkan resources
    VkBuffer buffer_ = VK_NULL_HANDLE;
    VkDeviceMemory memory_ = VK_NULL_HANDLE;
    VkDeviceSize bufferSize_ = 0;

    // Configuration
    uint32_t capacity_ = 0;
    bool isHostVisible_ = true;

    // CPU-side data after readback
    std::vector<RayTrace> rayTraces_;
    uint32_t capturedCount_ = 0;
    uint32_t totalWrites_ = 0;
    std::vector<MipReadByteCounters> mipReadSnapshots_;

    // IDebugCapture identity/state (see SetDebugName/SetBindingIndex above)
    std::string debugName_ = "RayTraceBuffer";
    uint32_t bindingIndex_ = 0;
    // Task 0.2 (Baked-Content Perf Audit D2): default OFF -- when true, DebugBufferReaderNode's
    // every-Nth-frame ExecuteImpl does a blocking vkWaitForFences(UINT64_MAX) pipeline drain +
    // JSON export (see DebugBufferReaderNode.cpp's IsCaptureEnabled() gate), which perturbs
    // every perf bench unless explicitly opted into via VIXEN_DEBUG_CAPTURE=1 (see
    // BuildRenderGraph.cpp's debugCapture PARAM_AUTO_EXPORT wiring, which mirrors this same
    // knob for the companion CPU-side auto-export toggle).
    bool captureEnabled_ = false;

    // Helper to find suitable memory type
    static uint32_t FindMemoryType(
        VkPhysicalDevice physicalDevice,
        uint32_t typeFilter,
        VkMemoryPropertyFlags properties
    );
};

/**
 * @brief Factory function for creating RayTraceBuffer
 *
 * @param device Vulkan device
 * @param physicalDevice Physical device
 * @param rayCapacity Maximum number of rays (default: 1024)
 * @return Configured buffer (check IsValid() for success)
 */
inline RayTraceBuffer CreateRayTraceBuffer(
    VkDevice device,
    VkPhysicalDevice physicalDevice,
    uint32_t rayCapacity = 1024
) {
    RayTraceBuffer buffer(rayCapacity);
    buffer.Create(device, physicalDevice);
    return buffer;
}

} // namespace Vixen::RenderGraph::Debug
