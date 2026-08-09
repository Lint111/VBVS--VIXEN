#include "Debug/RayTraceBuffer.h"
#include <cstring>
#include <algorithm>
#include <bit>

namespace Vixen::RenderGraph::Debug {

// ============================================================================
// Constructor / Destructor
// ============================================================================

RayTraceBuffer::RayTraceBuffer(uint32_t rayCapacity)
    : capacity_(rayCapacity)
    , bufferSize_(CalculateBufferSize(rayCapacity))
{
    rayTraces_.reserve(capacity_);
}

RayTraceBuffer::~RayTraceBuffer() {
    // Note: Destroy() must be called explicitly with VkDevice before destruction
}

// ============================================================================
// Move operations
// ============================================================================

RayTraceBuffer::RayTraceBuffer(RayTraceBuffer&& other) noexcept
    : buffer_(other.buffer_)
    , memory_(other.memory_)
    , bufferSize_(other.bufferSize_)
    , capacity_(other.capacity_)
    , isHostVisible_(other.isHostVisible_)
    , rayTraces_(std::move(other.rayTraces_))
    , capturedCount_(other.capturedCount_)
    , totalWrites_(other.totalWrites_)
    , debugName_(std::move(other.debugName_))
    , bindingIndex_(other.bindingIndex_)
    , captureEnabled_(other.captureEnabled_)
{
    other.buffer_ = VK_NULL_HANDLE;
    other.memory_ = VK_NULL_HANDLE;
    other.bufferSize_ = 0;
    other.capacity_ = 0;
    other.capturedCount_ = 0;
    other.totalWrites_ = 0;
}

RayTraceBuffer& RayTraceBuffer::operator=(RayTraceBuffer&& other) noexcept {
    if (this != &other) {
        buffer_ = other.buffer_;
        memory_ = other.memory_;
        bufferSize_ = other.bufferSize_;
        capacity_ = other.capacity_;
        isHostVisible_ = other.isHostVisible_;
        rayTraces_ = std::move(other.rayTraces_);
        capturedCount_ = other.capturedCount_;
        totalWrites_ = other.totalWrites_;
        debugName_ = std::move(other.debugName_);
        bindingIndex_ = other.bindingIndex_;
        captureEnabled_ = other.captureEnabled_;

        other.buffer_ = VK_NULL_HANDLE;
        other.memory_ = VK_NULL_HANDLE;
        other.bufferSize_ = 0;
        other.capacity_ = 0;
        other.capturedCount_ = 0;
        other.totalWrites_ = 0;
    }
    return *this;
}

// ============================================================================
// Lifecycle
// ============================================================================

bool RayTraceBuffer::Create(VkDevice device, VkPhysicalDevice physicalDevice) {
    if (device == VK_NULL_HANDLE || physicalDevice == VK_NULL_HANDLE) {
        return false;
    }

    if (capacity_ == 0) {
        return false;
    }

    // Create buffer
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = bufferSize_;
    bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(device, &bufferInfo, nullptr, &buffer_) != VK_SUCCESS) {
        return false;
    }

    // Get memory requirements
    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(device, buffer_, &memRequirements);

    // Find suitable memory type (HOST_VISIBLE | HOST_COHERENT)
    VkMemoryPropertyFlags memProperties =
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

    uint32_t memoryTypeIndex = FindMemoryType(physicalDevice, memRequirements.memoryTypeBits, memProperties);
    if (memoryTypeIndex == UINT32_MAX) {
        vkDestroyBuffer(device, buffer_, nullptr);
        buffer_ = VK_NULL_HANDLE;
        return false;
    }

    // Allocate memory
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = memoryTypeIndex;

    if (vkAllocateMemory(device, &allocInfo, nullptr, &memory_) != VK_SUCCESS) {
        vkDestroyBuffer(device, buffer_, nullptr);
        buffer_ = VK_NULL_HANDLE;
        return false;
    }

    // Bind memory to buffer
    if (vkBindBufferMemory(device, buffer_, memory_, 0) != VK_SUCCESS) {
        vkFreeMemory(device, memory_, nullptr);
        vkDestroyBuffer(device, buffer_, nullptr);
        buffer_ = VK_NULL_HANDLE;
        memory_ = VK_NULL_HANDLE;
        return false;
    }

    isHostVisible_ = true;

    // Initialize buffer header
    void* data = nullptr;
    if (vkMapMemory(device, memory_, 0, sizeof(TraceBufferHeader), 0, &data) == VK_SUCCESS) {
        auto* header = static_cast<TraceBufferHeader*>(data);
        header->writeIndex = 0;
        header->capacity = capacity_;
        header->farFieldCount = 0;
        header->farFieldCandidates = 0;
        // Round-6 min/max probe: seed min to all-ones (largest possible bit
        // pattern for a non-negative float == +inf-ish) and max to 0, so the
        // first atomicMin/atomicMax in the shader always wins.
        header->farFieldLhsMinBits = 0xFFFFFFFFu;
        header->farFieldLhsMaxBits = 0u;
        header->farFieldRhsMinBits = 0xFFFFFFFFu;
        header->farFieldRhsMaxBits = 0u;
        header->rtLoopEntries = 0;
        header->farFieldMipSuccess = 0;
        header->farFieldMipFail = 0;
        header->farFieldRejectedByBounds = 0;
        header->farFieldWon = 0;
        header->farFieldTerminal = 0;
        header->farFieldColorResolved = 0;
        header->farFieldColorFallback = 0;
        // Round 14: seed round-13's descent-fail level min/max the same way as
        // the round-6 lhs/rhs trackers above -- min to all-ones so the first
        // atomicMin always wins, max to 0. Standing rule per batch-13 postmortem:
        // every new min-tracker gets seeded here, or atomicMin against a
        // zero-initialized field silently reads back 0 (looks like a real
        // root-adjacent failure when it's actually "never written").
        header->farFieldDescentFailLevelMin = 0xFFFFFFFFu;
        header->farFieldDescentFailLevelMax = 0u;
        for (int i = 0; i < 2; ++i) {  // round 11: per-tag counters
            header->farFieldCandidatesByTag[i] = 0;
            header->farFieldCountByTag[i] = 0;
            header->farFieldColorResolvedByTag[i] = 0;
            header->farFieldColorFallbackByTag[i] = 0;
        }
        header->farFieldTerminalPixelWriteCount = 0;  // round 11: pixel ring
        for (int i = 0; i < 32; ++i) {
            header->farFieldTerminalPixels[i] = 0;
        }
        header->rtLoopEntriesOct3 = 0;      // round 17: octree-3-only probe
        header->farFieldGateRejectOct3 = 0;
        header->farFieldCandidatesOct3 = 0;
        // Batch-27 JOB 2: ESVO-criterion operand log. Min-trackers seeded per
        // the batch-13 standing rule (all-ones so the first atomicMin wins;
        // zero-init would silently read back as "0" and look like a real,
        // never-actually-observed root-adjacent crossing level).
        header->esvoLhsMinBits = 0xFFFFFFFFu;
        header->esvoLhsMaxBits = 0u;
        header->esvoRhsMinBits = 0xFFFFFFFFu;
        header->esvoRhsMaxBits = 0u;
        for (int i = 0; i < 8; ++i) header->esvoLhsHistogram[i] = 0u;
        header->esvoCrossLevelMin = 0xFFFFFFFFu;
        header->esvoCrossLevelMax = 0u;
        // Batch-32 JOB 1: level-sensitive far-field counter. Min-tracker
        // seeded per the batch-13 standing rule (all-ones so the first
        // atomicMin wins); sum/count are plain accumulators, zero is correct.
        header->farFieldSampledLevelMin = 0xFFFFFFFFu;
        header->farFieldSampledLevelMax = 0u;
        header->farFieldSampledLevelSum = 0u;
        header->farFieldSampledLevelCount = 0u;
        // Batch-33 JOB 2: far-field sample-intensity. Min-tracker seeded
        // per the batch-13 standing rule; luminance floats are >= 0 so
        // 0xFFFFFFFF (bit pattern of a large positive float) sorts correctly
        // above any real sample.
        header->entryGateLhsMinBits = 0xFFFFFFFFu;  // batch-38 probe (standing rule: seed min all-ones)
        header->entryGateLhsMaxBits = 0u;
        header->farFieldSampleIntensityMinBits = 0xFFFFFFFFu;
        header->farFieldSampleIntensityMaxBits = 0u;
        header->farFieldSampleIntensityFixedSum = 0u;
        header->_padBatch33 = 0u;
        // Batch-35: [PolicyEntryDispatch] mip/march counters. Plain
        // accumulators, zero is correct (no min-tracker to seed).
        header->policyEntryDispatchMip = 0u;
        header->policyEntryDispatchMarch = 0u;
        // Batch-39: [PolicyEntryDispatch] third bucket. Plain accumulator,
        // zero is correct (never-fired reads back as 0, same as mip/march).
        header->policyEntryDispatchEmptyEntry = 0u;
        header->_padBatch39 = 0u;
        // Regime-3 (cosmic accumulation) first slice: plain accumulators,
        // zero is correct (no min-tracker to seed -- same discipline as
        // policyEntryDispatchMip/March above).
        header->regime3EntryCount = 0u;
        header->regime3EarlyOutCount = 0u;
        // Compositing-slice part 1 (walkCov source audit): min-trackers seeded
        // per the batch-13 standing rule (all-ones so the first atomicMin
        // wins); cov is non-negative by construction.
        header->walkCovMinBits = 0xFFFFFFFFu;
        header->walkCovMaxBits = 0u;
        header->walkSampledLevelMin = 0xFFFFFFFFu;
        header->walkSampledLevelMax = 0u;
        header->_padWalkCov[0] = 0u;
        header->_padWalkCov[1] = 0u;
        vkUnmapMemory(device, memory_);
    }

    return true;
}

void RayTraceBuffer::Destroy(VkDevice device) {
    if (buffer_ != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, buffer_, nullptr);
        buffer_ = VK_NULL_HANDLE;
    }

    if (memory_ != VK_NULL_HANDLE) {
        vkFreeMemory(device, memory_, nullptr);
        memory_ = VK_NULL_HANDLE;
    }

    bufferSize_ = 0;
    rayTraces_.clear();
    capturedCount_ = 0;
    totalWrites_ = 0;
}

// ============================================================================
// IDebugBuffer interface implementation
// ============================================================================

bool RayTraceBuffer::Reset(VkDevice device) {
    if (!IsValid() || !isHostVisible_) {
        return false;
    }

    void* data = nullptr;
    if (vkMapMemory(device, memory_, 0, sizeof(TraceBufferHeader), 0, &data) != VK_SUCCESS) {
        return false;
    }

    // Reset write index
    auto* header = static_cast<TraceBufferHeader*>(data);
    header->writeIndex = 0;

    vkUnmapMemory(device, memory_);

    // Clear CPU-side data
    rayTraces_.clear();
    capturedCount_ = 0;
    totalWrites_ = 0;

    return true;
}

uint32_t RayTraceBuffer::Read(VkDevice device) {
    if (!IsValid() || !isHostVisible_) {
        return 0;
    }

    void* data = nullptr;
    if (vkMapMemory(device, memory_, 0, bufferSize_, 0, &data) != VK_SUCCESS) {
        return 0;
    }

    // Read header
    const auto* header = static_cast<const TraceBufferHeader*>(data);
    totalWrites_ = header->writeIndex;
    capturedCount_ = std::min(totalWrites_, capacity_);

    // Clear previous traces
    rayTraces_.clear();
    rayTraces_.reserve(capturedCount_);

    if (capturedCount_ > 0) {
        const uint8_t* rawData = static_cast<const uint8_t*>(data) + sizeof(TraceBufferHeader);

        for (uint32_t i = 0; i < capturedCount_; ++i) {
            // Handle ring buffer wrapping
            uint32_t slotIndex = (totalWrites_ > capacity_)
                ? ((totalWrites_ % capacity_) + i) % capacity_  // Wrapped: oldest first
                : i;                                             // Not wrapped: linear

            const uint8_t* rayData = rawData + (slotIndex * TRACE_RAY_SIZE);

            // Read ray header
            RayTrace trace;
            const auto* rayHeader = reinterpret_cast<const RayTraceHeader*>(rayData);
            trace.header = *rayHeader;

            // Read steps
            uint32_t stepCount = std::min(rayHeader->stepCount, MAX_TRACE_STEPS);
            trace.steps.reserve(stepCount);

            const uint8_t* stepData = rayData + sizeof(RayTraceHeader);
            for (uint32_t s = 0; s < stepCount; ++s) {
                const auto* step = reinterpret_cast<const TraceStep*>(stepData + s * sizeof(TraceStep));
                trace.steps.push_back(*step);
            }

            rayTraces_.push_back(std::move(trace));
        }
    }

    vkUnmapMemory(device, memory_);
    return capturedCount_;
}

// W-COMPOSED round-3 fix item 3: cheap, ALWAYS-available readback of the
// far-field-cutoff counter -- maps only the 16-byte header (unlike Read(),
// which maps the whole ring buffer and rewrites CPU-side trace state), so
// callers with no IDebugCapture wiring (VoxelGridNode::CleanupImpl, a plain
// boot-time summary) can read it with no dependency on VIXEN_DEBUG_CAPTURE.
uint32_t RayTraceBuffer::ReadFarFieldCount(VkDevice device) const {
    if (!IsValid() || !isHostVisible_) {
        return 0;
    }
    void* data = nullptr;
    if (vkMapMemory(device, memory_, 0, sizeof(TraceBufferHeader), 0, &data) != VK_SUCCESS) {
        return 0;
    }
    const auto* header = static_cast<const TraceBufferHeader*>(data);
    const uint32_t count = header->farFieldCount;
    vkUnmapMemory(device, memory_);
    return count;
}

// Round-5 far-field diagnostics: same cheap header-only readback as
// ReadFarFieldCount above, for the candidates-reaching-the-gate counter.
uint32_t RayTraceBuffer::ReadFarFieldCandidates(VkDevice device) const {
    if (!IsValid() || !isHostVisible_) {
        return 0;
    }
    void* data = nullptr;
    if (vkMapMemory(device, memory_, 0, sizeof(TraceBufferHeader), 0, &data) != VK_SUCCESS) {
        return 0;
    }
    const auto* header = static_cast<const TraceBufferHeader*>(data);
    const uint32_t count = header->farFieldCandidates;
    vkUnmapMemory(device, memory_);
    return count;
}

// Round-6 DDA-threshold-degeneracy probe: readback of the min/max ranges of
// both far-field gate operands (see DebugRaySample.h's TraceBufferHeader
// comment). bits->float via bit_cast (values are always non-negative
// distances, so raw uint order == float order, no sign-trick decode needed).
FarFieldRanges RayTraceBuffer::ReadFarFieldRanges(VkDevice device) const {
    FarFieldRanges ranges{};
    if (!IsValid() || !isHostVisible_) {
        return ranges;
    }
    void* data = nullptr;
    if (vkMapMemory(device, memory_, 0, sizeof(TraceBufferHeader), 0, &data) != VK_SUCCESS) {
        return ranges;
    }
    const auto* header = static_cast<const TraceBufferHeader*>(data);
    ranges.lhsMin = std::bit_cast<float>(header->farFieldLhsMinBits);
    ranges.lhsMax = std::bit_cast<float>(header->farFieldLhsMaxBits);
    ranges.rhsMin = std::bit_cast<float>(header->farFieldRhsMinBits);
    ranges.rhsMax = std::bit_cast<float>(header->farFieldRhsMaxBits);
    vkUnmapMemory(device, memory_);
    return ranges;
}

RayTraceBuffer::EntryGateRange RayTraceBuffer::ReadEntryGateRange(VkDevice device) const {
    EntryGateRange ranges{};
    if (!IsValid() || !isHostVisible_) {
        return ranges;
    }
    void* data = nullptr;
    if (vkMapMemory(device, memory_, 0, sizeof(TraceBufferHeader), 0, &data) != VK_SUCCESS) {
        return ranges;
    }
    const auto* header = static_cast<const TraceBufferHeader*>(data);
    ranges.lhsMin = std::bit_cast<float>(header->entryGateLhsMinBits);
    ranges.lhsMax = std::bit_cast<float>(header->entryGateLhsMaxBits);
    vkUnmapMemory(device, memory_);
    return ranges;
}

// Round-6 blocker-2 localization probe: same cheap header-only readback.
uint32_t RayTraceBuffer::ReadRtLoopEntries(VkDevice device) const {
    if (!IsValid() || !isHostVisible_) {
        return 0;
    }
    void* data = nullptr;
    if (vkMapMemory(device, memory_, 0, sizeof(TraceBufferHeader), 0, &data) != VK_SUCCESS) {
        return 0;
    }
    const auto* header = static_cast<const TraceBufferHeader*>(data);
    const uint32_t count = header->rtLoopEntries;
    vkUnmapMemory(device, memory_);
    return count;
}

// Round-17 probe: octree-3-only candidate-loop breakdown readback (see
// rtLoopEntriesOct3/farFieldGateRejectOct3/farFieldCandidatesOct3 field
// comment, DebugRaySample.h).
FarFieldOct3Stats RayTraceBuffer::ReadFarFieldOct3Stats(VkDevice device) const {
    FarFieldOct3Stats stats{};
    if (!IsValid() || !isHostVisible_) {
        return stats;
    }
    void* data = nullptr;
    if (vkMapMemory(device, memory_, 0, sizeof(TraceBufferHeader), 0, &data) != VK_SUCCESS) {
        return stats;
    }
    const auto* header = static_cast<const TraceBufferHeader*>(data);
    stats.loopEntries = header->rtLoopEntriesOct3;
    stats.gateReject = header->farFieldGateRejectOct3;
    stats.candidatesReachingGate = header->farFieldCandidatesOct3;
    vkUnmapMemory(device, memory_);
    return stats;
}

// Round-7 blocker-1 probe: mip-resolve success/fail readback (see
// farFieldMipSuccess/Fail field comment, DebugRaySample.h).
// Batch-24 FARGEN: rect-scoped generation funnel readback (see
// rectRays/rectCellEntries/rectGateCross field comment, DebugRaySample.h).
FarFieldRectStats RayTraceBuffer::ReadFarFieldRectStats(VkDevice device) const {
    FarFieldRectStats stats{};
    if (!IsValid() || !isHostVisible_) {
        return stats;
    }
    void* data = nullptr;
    if (vkMapMemory(device, memory_, 0, sizeof(TraceBufferHeader), 0, &data) != VK_SUCCESS) {
        return stats;
    }
    const auto* header = static_cast<const TraceBufferHeader*>(data);
    stats.rays = header->rectRays;
    stats.cellEntries = header->rectCellEntries;
    stats.gateCross = header->rectGateCross;
    vkUnmapMemory(device, memory_);
    return stats;
}

// Batch-25 JOB 2: rect-scoped FarFieldGateLhs histogram readback (see
// rectLhsHistogram field comment, DebugRaySample.h).
FarFieldRectLhsHistogram RayTraceBuffer::ReadFarFieldRectLhsHistogram(VkDevice device) const {
    FarFieldRectLhsHistogram stats{};
    if (!IsValid() || !isHostVisible_) {
        return stats;
    }
    void* data = nullptr;
    if (vkMapMemory(device, memory_, 0, sizeof(TraceBufferHeader), 0, &data) != VK_SUCCESS) {
        return stats;
    }
    const auto* header = static_cast<const TraceBufferHeader*>(data);
    for (int i = 0; i < 8; ++i) stats.buckets[i] = header->rectLhsHistogram[i];
    vkUnmapMemory(device, memory_);
    return stats;
}

// Batch-27 JOB 2: ESVO's own cutoff criterion operand log readback (see
// esvoLhs*/esvoRhs*/esvoLhsHistogram/esvoCrossLevel* field comment,
// DebugRaySample.h). Same bit_cast/never-reset discipline as
// ReadFarFieldRanges above.
EsvoCutoffOperands RayTraceBuffer::ReadEsvoCutoffOperands(VkDevice device) const {
    EsvoCutoffOperands ops{};
    if (!IsValid() || !isHostVisible_) {
        return ops;
    }
    void* data = nullptr;
    if (vkMapMemory(device, memory_, 0, sizeof(TraceBufferHeader), 0, &data) != VK_SUCCESS) {
        return ops;
    }
    const auto* header = static_cast<const TraceBufferHeader*>(data);
    ops.lhsMin = std::bit_cast<float>(header->esvoLhsMinBits);
    ops.lhsMax = std::bit_cast<float>(header->esvoLhsMaxBits);
    ops.rhsMin = std::bit_cast<float>(header->esvoRhsMinBits);
    ops.rhsMax = std::bit_cast<float>(header->esvoRhsMaxBits);
    for (int i = 0; i < 8; ++i) ops.histogram[i] = header->esvoLhsHistogram[i];
    ops.crossLevelMin = header->esvoCrossLevelMin;
    ops.crossLevelMax = header->esvoCrossLevelMax;
    vkUnmapMemory(device, memory_);
    return ops;
}

// Batch-29 JOB 3: rect-agnostic policy-level histogram readback (see
// policyLevelHistogram field comment, DebugRaySample.h).
PolicyLevelHistogram RayTraceBuffer::ReadPolicyLevelHistogram(VkDevice device) const {
    PolicyLevelHistogram stats{};
    if (!IsValid() || !isHostVisible_) {
        return stats;
    }
    void* data = nullptr;
    if (vkMapMemory(device, memory_, 0, sizeof(TraceBufferHeader), 0, &data) != VK_SUCCESS) {
        return stats;
    }
    const auto* header = static_cast<const TraceBufferHeader*>(data);
    for (int i = 0; i < 8; ++i) stats.buckets[i] = header->policyLevelHistogram[i];
    vkUnmapMemory(device, memory_);
    return stats;
}

// Batch-29 JOB 4: rect-scoped ESVO shadeFromMipSample arm attribution
// readback (see esvoMipArmHits field comment, DebugRaySample.h).
EsvoMipArmStats RayTraceBuffer::ReadEsvoMipArmStats(VkDevice device) const {
    EsvoMipArmStats stats{};
    if (!IsValid() || !isHostVisible_) {
        return stats;
    }
    void* data = nullptr;
    if (vkMapMemory(device, memory_, 0, sizeof(TraceBufferHeader), 0, &data) != VK_SUCCESS) {
        return stats;
    }
    const auto* header = static_cast<const TraceBufferHeader*>(data);
    for (int i = 0; i < 6; ++i) stats.hits[i] = header->esvoMipArmHits[i];  // batch-30 stream B: +policy-level arm
    vkUnmapMemory(device, memory_);
    return stats;
}

// Batch-32 JOB 1: level-sensitive far-field counter readback (see
// farFieldSampledLevelMin/Max/Sum/Count field comment, DebugRaySample.h).
FarFieldSampledLevelStats RayTraceBuffer::ReadFarFieldSampledLevelStats(VkDevice device) const {
    FarFieldSampledLevelStats stats{};
    if (!IsValid() || !isHostVisible_) {
        return stats;
    }
    void* data = nullptr;
    if (vkMapMemory(device, memory_, 0, sizeof(TraceBufferHeader), 0, &data) != VK_SUCCESS) {
        return stats;
    }
    const auto* header = static_cast<const TraceBufferHeader*>(data);
    stats.min = header->farFieldSampledLevelMin;
    stats.max = header->farFieldSampledLevelMax;
    stats.sum = header->farFieldSampledLevelSum;
    stats.count = header->farFieldSampledLevelCount;
    vkUnmapMemory(device, memory_);
    return stats;
}

// Batch-33 JOB 2: far-field sample-intensity readback (see
// farFieldSampleIntensity* field comment, DebugRaySample.h). Mean divides by
// farFieldSampledLevelCount (shared count, same call site as the level stat
// this mirrors) -- kIntensityFixedPointScale must match SceneBindings.glsl's
// const of the same name (1,000,000).
FarFieldSampleIntensityStats RayTraceBuffer::ReadFarFieldSampleIntensityStats(VkDevice device) const {
    FarFieldSampleIntensityStats stats{};
    if (!IsValid() || !isHostVisible_) {
        return stats;
    }
    void* data = nullptr;
    if (vkMapMemory(device, memory_, 0, sizeof(TraceBufferHeader), 0, &data) != VK_SUCCESS) {
        return stats;
    }
    const auto* header = static_cast<const TraceBufferHeader*>(data);
    constexpr float kIntensityFixedPointScale = 1000000.0f;  // must match SceneBindings.glsl
    stats.min = std::bit_cast<float>(header->farFieldSampleIntensityMinBits);
    stats.max = std::bit_cast<float>(header->farFieldSampleIntensityMaxBits);
    uint32_t count = header->farFieldSampledLevelCount;
    stats.mean = count > 0
        ? (static_cast<float>(header->farFieldSampleIntensityFixedSum) / kIntensityFixedPointScale) / static_cast<float>(count)
        : 0.0f;
    vkUnmapMemory(device, memory_);
    return stats;
}

// Batch-35: entry-point dispatch readback (see policyEntryDispatchMip/March
// field comment, DebugRaySample.h).
PolicyEntryDispatchStats RayTraceBuffer::ReadPolicyEntryDispatchStats(VkDevice device) const {
    PolicyEntryDispatchStats stats{};
    if (!IsValid() || !isHostVisible_) {
        return stats;
    }
    void* data = nullptr;
    if (vkMapMemory(device, memory_, 0, sizeof(TraceBufferHeader), 0, &data) != VK_SUCCESS) {
        return stats;
    }
    const auto* header = static_cast<const TraceBufferHeader*>(data);
    stats.mip = header->policyEntryDispatchMip;
    stats.march = header->policyEntryDispatchMarch;
    stats.emptyEntry = header->policyEntryDispatchEmptyEntry;
    vkUnmapMemory(device, memory_);
    return stats;
}

// Regime-3 (cosmic accumulation) first slice: entry/early-out readback (see
// regime3EntryCount/regime3EarlyOutCount field comment, DebugRaySample.h).
Regime3Stats RayTraceBuffer::ReadRegime3Stats(VkDevice device) const {
    Regime3Stats stats{};
    if (!IsValid() || !isHostVisible_) {
        return stats;
    }
    void* data = nullptr;
    if (vkMapMemory(device, memory_, 0, sizeof(TraceBufferHeader), 0, &data) != VK_SUCCESS) {
        return stats;
    }
    const auto* header = static_cast<const TraceBufferHeader*>(data);
    stats.entry = header->regime3EntryCount;
    stats.earlyOut = header->regime3EarlyOutCount;
    vkUnmapMemory(device, memory_);
    return stats;
}

// Compositing-slice part 1 (walkCov source audit): walkCov/walkSampledLevel
// min/max readback (see walkCovMinBits/walkSampledLevelMin field comment,
// DebugRaySample.h).
WalkCovStats RayTraceBuffer::ReadWalkCovStats(VkDevice device) const {
    WalkCovStats stats{};
    if (!IsValid() || !isHostVisible_) {
        return stats;
    }
    void* data = nullptr;
    if (vkMapMemory(device, memory_, 0, sizeof(TraceBufferHeader), 0, &data) != VK_SUCCESS) {
        return stats;
    }
    const auto* header = static_cast<const TraceBufferHeader*>(data);
    stats.covMin = std::bit_cast<float>(header->walkCovMinBits);
    stats.covMax = std::bit_cast<float>(header->walkCovMaxBits);
    stats.levelMin = header->walkSampledLevelMin;
    stats.levelMax = header->walkSampledLevelMax;
    vkUnmapMemory(device, memory_);
    return stats;
}

FarFieldMipStats RayTraceBuffer::ReadFarFieldMipStats(VkDevice device) const {
    FarFieldMipStats stats{};
    if (!IsValid() || !isHostVisible_) {
        return stats;
    }
    void* data = nullptr;
    if (vkMapMemory(device, memory_, 0, sizeof(TraceBufferHeader), 0, &data) != VK_SUCCESS) {
        return stats;
    }
    const auto* header = static_cast<const TraceBufferHeader*>(data);
    stats.success = header->farFieldMipSuccess;
    stats.fail = header->farFieldMipFail;
    vkUnmapMemory(device, memory_);
    return stats;
}

// Round-13 probe: readback of the descent-fail counter (see
// farFieldDescentFail field comment, DebugRaySample.h).
uint32_t RayTraceBuffer::ReadFarFieldDescentFail(VkDevice device) const {
    if (!IsValid() || !isHostVisible_) {
        return 0;
    }
    void* data = nullptr;
    if (vkMapMemory(device, memory_, 0, sizeof(TraceBufferHeader), 0, &data) != VK_SUCCESS) {
        return 0;
    }
    const auto* header = static_cast<const TraceBufferHeader*>(data);
    const uint32_t count = header->farFieldDescentFail;
    vkUnmapMemory(device, memory_);
    return count;
}

// Round-13 probe #2: readback of the descent-fail level min/max (see
// farFieldDescentFailLevelMin/Max field comment, DebugRaySample.h).
std::pair<uint32_t, uint32_t> RayTraceBuffer::ReadFarFieldDescentFailLevelRange(VkDevice device) const {
    if (!IsValid() || !isHostVisible_) {
        return {0, 0};
    }
    void* data = nullptr;
    if (vkMapMemory(device, memory_, 0, sizeof(TraceBufferHeader), 0, &data) != VK_SUCCESS) {
        return {0, 0};
    }
    const auto* header = static_cast<const TraceBufferHeader*>(data);
    const std::pair<uint32_t, uint32_t> range{header->farFieldDescentFailLevelMin, header->farFieldDescentFailLevelMax};
    vkUnmapMemory(device, memory_);
    return range;
}

// Round-7 blocker-1 probe #2: readback of the tight-bounds far-hit-rejection
// discard count (see farFieldRejectedByBounds field comment, DebugRaySample.h).
uint32_t RayTraceBuffer::ReadFarFieldRejectedByBounds(VkDevice device) const {
    if (!IsValid() || !isHostVisible_) {
        return 0;
    }
    void* data = nullptr;
    if (vkMapMemory(device, memory_, 0, sizeof(TraceBufferHeader), 0, &data) != VK_SUCCESS) {
        return 0;
    }
    const auto* header = static_cast<const TraceBufferHeader*>(data);
    const uint32_t count = header->farFieldRejectedByBounds;
    vkUnmapMemory(device, memory_);
    return count;
}

// Round-7 blocker-1 probe #3: readback of the far-field-firings-that-won-
// isCloserHit count (see farFieldWon field comment, DebugRaySample.h).
uint32_t RayTraceBuffer::ReadFarFieldWon(VkDevice device) const {
    if (!IsValid() || !isHostVisible_) {
        return 0;
    }
    void* data = nullptr;
    if (vkMapMemory(device, memory_, 0, sizeof(TraceBufferHeader), 0, &data) != VK_SUCCESS) {
        return 0;
    }
    const auto* header = static_cast<const TraceBufferHeader*>(data);
    const uint32_t count = header->farFieldWon;
    vkUnmapMemory(device, memory_);
    return count;
}

// Round 9: readback of the per-pixel TERMINAL far-field count (see
// farFieldTerminal field comment, DebugRaySample.h).
uint32_t RayTraceBuffer::ReadFarFieldTerminal(VkDevice device) const {
    if (!IsValid() || !isHostVisible_) {
        return 0;
    }
    void* data = nullptr;
    if (vkMapMemory(device, memory_, 0, sizeof(TraceBufferHeader), 0, &data) != VK_SUCCESS) {
        return 0;
    }
    const auto* header = static_cast<const TraceBufferHeader*>(data);
    const uint32_t count = header->farFieldTerminal;
    vkUnmapMemory(device, memory_);
    return count;
}

// Batch 10: readback of the color-resolved/fallback split (see
// FarFieldColorStats above, DebugRaySample.h).
FarFieldColorStats RayTraceBuffer::ReadFarFieldColorStats(VkDevice device) const {
    FarFieldColorStats stats{};
    if (!IsValid() || !isHostVisible_) {
        return stats;
    }
    void* data = nullptr;
    if (vkMapMemory(device, memory_, 0, sizeof(TraceBufferHeader), 0, &data) != VK_SUCCESS) {
        return stats;
    }
    const auto* header = static_cast<const TraceBufferHeader*>(data);
    stats.resolved = header->farFieldColorResolved;
    stats.fallback = header->farFieldColorFallback;
    vkUnmapMemory(device, memory_);
    return stats;
}

// Round 11: per-dispatch-tag readback (see FarFieldByTagStats, DebugRaySample.h).
FarFieldByTagStats RayTraceBuffer::ReadFarFieldByTagStats(VkDevice device) const {
    FarFieldByTagStats stats{};
    if (!IsValid() || !isHostVisible_) {
        return stats;
    }
    void* data = nullptr;
    if (vkMapMemory(device, memory_, 0, sizeof(TraceBufferHeader), 0, &data) != VK_SUCCESS) {
        return stats;
    }
    const auto* header = static_cast<const TraceBufferHeader*>(data);
    for (int i = 0; i < 2; ++i) {
        stats.candidates[i] = header->farFieldCandidatesByTag[i];
        stats.count[i] = header->farFieldCountByTag[i];
        stats.colorResolved[i] = header->farFieldColorResolvedByTag[i];
        stats.colorFallback[i] = header->farFieldColorFallbackByTag[i];
    }
    vkUnmapMemory(device, memory_);
    return stats;
}

std::any RayTraceBuffer::GetData() const {
    return rayTraces_;
}

std::any RayTraceBuffer::GetDataPtr() const {
    if (rayTraces_.empty()) {
        return static_cast<const std::vector<RayTrace>*>(nullptr);
    }
    return &rayTraces_;
}

// ============================================================================
// Helpers
// ============================================================================

uint32_t RayTraceBuffer::FindMemoryType(
    VkPhysicalDevice physicalDevice,
    uint32_t typeFilter,
    VkMemoryPropertyFlags properties
) {
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);

    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) &&
            (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }

    return UINT32_MAX;
}

} // namespace Vixen::RenderGraph::Debug
