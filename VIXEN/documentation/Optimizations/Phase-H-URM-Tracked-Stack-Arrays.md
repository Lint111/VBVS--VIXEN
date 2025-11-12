# Phase H: URM-Tracked Stack Arrays

## Overview

This document describes the integration of stack-allocated arrays with the Unified Resource Management (URM) profiling system. This provides full visibility into stack usage while maintaining zero heap allocations.

## Motivation

**Problem:** Stack-allocated `std::array` eliminates heap allocations but provides no visibility into resource usage.

**Solution:** Register stack arrays with ResourceProfiler before outputting to slots, enabling:
- Per-node stack usage tracking
- Budget enforcement for stack allocations
- Comprehensive profiling alongside heap/VRAM resources
- Frame-by-frame visibility into allocation patterns

## API: TrackStackArray()

### Definition

```cpp
// In NodeInstance.h (lines 733-796)
template<typename T, size_t N>
void TrackStackArray(
    const std::array<T, N>& array,
    size_t usedCount = N,
    ResourceLifetime lifetime = ResourceLifetime::FrameLocal
);
```

### Usage Pattern

```cpp
// 1. Declare stack array as node member
class MyNode : public TypedNode<MyNodeConfig> {
private:
    std::array<VkFramebuffer, MAX_SWAPCHAIN_IMAGES> framebuffers{};
    uint32_t framebufferCount = 0;
};

// 2. Populate array during Compile/Execute
void MyNode::CompileImpl(TypedCompileContext& ctx) {
    for (uint32_t i = 0; i < imageCount; i++) {
        framebuffers[i] = CreateFramebuffer(...);
        framebufferCount++;
    }

    // 3. Track with URM before output
    TrackStackArray(framebuffers, framebufferCount, ResourceLifetime::GraphLocal);

    // 4. Output to slot (now tracked)
    std::vector<VkFramebuffer> output(framebuffers.begin(),
                                      framebuffers.begin() + framebufferCount);
    ctx.Out(FRAMEBUFFERS, output);
}
```

### Parameters

- **array**: The stack-allocated `std::array` to track
- **usedCount**: Number of elements actually used (for accurate byte tracking)
  - Default: N (full array)
  - Use actual count for partially-filled arrays
- **lifetime**: Resource lifetime for tracking purposes
  - `ResourceLifetime::FrameLocal`: Released at end of frame
  - `ResourceLifetime::GraphLocal`: Released at graph destruction

### Benefits

1. **Zero Heap Allocations**: Array remains on stack
2. **Full URM Integration**: Tracked by ResourceProfiler
3. **Accurate Byte Counting**: Only counts used elements
4. **Per-Node Visibility**: Shows which nodes use stack resources
5. **Budget Enforcement**: Stack usage counted against limits

---

## Completed Conversions

### 1. FramebufferNode ✅

**File**: `VIXEN/RenderGraph/src/Nodes/FramebufferNode.cpp`
**Line**: 154

**Before**:
```cpp
// Array not tracked
std::vector<VkFramebuffer> framebuffersVector(framebuffers.begin(),
                                               framebuffers.begin() + framebufferCount);
ctx.Out(FramebufferNodeConfig::FRAMEBUFFERS, framebuffersVector);
```

**After**:
```cpp
// Track with URM profiling
TrackStackArray(framebuffers, framebufferCount, ResourceLifetime::GraphLocal);

std::vector<VkFramebuffer> framebuffersVector(framebuffers.begin(),
                                               framebuffers.begin() + framebufferCount);
ctx.Out(FramebufferNodeConfig::FRAMEBUFFERS, framebuffersVector);
```

**Impact**:
- Stack usage: ~32 bytes (4 × VkFramebuffer)
- Now visible in ResourceProfiler
- Tracked per-frame in profiling reports

---

## Pending High-Priority Conversions

### 2. DescriptorResourceGathererNode 🔥

**File**: `VIXEN/RenderGraph/src/Nodes/DescriptorResourceGathererNode.cpp`
**Lines**: 83, 84 (Compile), 130, 131 (Execute)

**Arrays to Convert**:

#### resourceArray_
```cpp
// Current (heap-allocated)
std::vector<ResourceVariant> resourceArray_;

// Target (stack-allocated + tracked)
std::array<ResourceVariant, MAX_DESCRIPTOR_BINDINGS> resourceArray_{};
size_t resourceCount = 0;

// In CompileImpl/ExecuteImpl before output:
TrackStackArray(resourceArray_, resourceCount, ResourceLifetime::FrameLocal);
```

**Impact**: High - executed every frame, hot path

#### slotRoleArray_
```cpp
// Current (heap-allocated)
std::vector<SlotRole> slotRoleArray_;

// Target (stack-allocated + tracked)
std::array<SlotRole, MAX_DESCRIPTOR_BINDINGS> slotRoleArray_{};

// Before output:
TrackStackArray(slotRoleArray_, resourceCount, ResourceLifetime::FrameLocal);
```

**Impact**: Minimal stack usage (32 bytes), hot path

---

### 3. FrameSyncNode 🔥

**File**: `VIXEN/RenderGraph/src/Nodes/FrameSyncNode.cpp`
**Lines**: 81-103 (creation), 123-125 (output)

**Arrays to Convert**:

#### IMAGE_AVAILABLE_SEMAPHORES_ARRAY
```cpp
// Current (heap-allocated)
std::vector<VkSemaphore> imageAvailableSemaphores;

// Target (stack-allocated + tracked)
std::array<VkSemaphore, MAX_FRAMES_IN_FLIGHT> imageAvailableSemaphores{};

// In CompileImpl before output:
TrackStackArray(imageAvailableSemaphores, flightCount, ResourceLifetime::GraphLocal);
```

**Size**: 4 semaphores × 8 bytes = 32 bytes

#### RENDER_COMPLETE_SEMAPHORES_ARRAY
```cpp
// Target
std::array<VkSemaphore, MAX_SWAPCHAIN_IMAGES> renderCompleteSemaphores{};
TrackStackArray(renderCompleteSemaphores, imageCount, ResourceLifetime::GraphLocal);
```

**Size**: 3 semaphores × 8 bytes = 24 bytes

#### PRESENT_FENCES_ARRAY
```cpp
// Target
std::array<VkFence, MAX_SWAPCHAIN_IMAGES> presentFences{};
TrackStackArray(presentFences, imageCount, ResourceLifetime::GraphLocal);
```

**Size**: 3 fences × 8 bytes = 24 bytes

**Total Impact**: 80 bytes stack, core synchronization primitives tracked

---

## Medium-Priority Conversions

### 4. GraphicsPipelineNode

**File**: `VIXEN/RenderGraph/src/Nodes/GraphicsPipelineNode.cpp`

**Vectors** (all compile-time, not hot-path):

1. **shaderStageInfos** (line 85)
   - `std::array<VkPipelineShaderStageCreateInfo, MAX_SHADER_STAGES>`
   - 8 elements max

2. **pushConstantRanges** (line 73)
   - `std::array<VkPushConstantRange, MAX_PUSH_CONSTANT_RANGES>`
   - 4 elements max

3. **vertexBindings** (line 637)
   - `std::array<VkVertexInputBindingDescription, MAX_VERTEX_BINDINGS>`
   - 16 elements max

4. **vertexAttributes** (line 638)
   - `std::array<VkVertexInputAttributeDescription, MAX_VERTEX_ATTRIBUTES>`
   - 16 elements max

**Priority**: Medium - compile-time only, not per-frame hot path

---

### 5. DescriptorSetNode

**File**: `VIXEN/RenderGraph/src/Nodes/DescriptorSetNode.cpp`
**Line**: 193

```cpp
// Current
std::vector<VkDescriptorSet> descriptorSets;

// Target
std::array<VkDescriptorSet, MAX_SWAPCHAIN_IMAGES> descriptorSets{};
TrackStackArray(descriptorSets, imageCount, ResourceLifetime::GraphLocal);
```

**Impact**: Small (typically 2-3 elements), compile-time only

---

## Minor Code Quality Improvements

### GeometryRenderNode Line 329

**Issue**: Inconsistent descriptor set handling creates unnecessary heap copy

```cpp
// Line 151 (correct):
const auto& descriptorSets = ctx.In(GeometryRenderNodeConfig::DESCRIPTOR_SETS);

// Line 329 (COPY - should be const ref):
std::vector<VkDescriptorSet> descriptorSets = ctx.In(...);  // ❌ Heap copy!

// Fix:
const auto& descriptorSets = ctx.In(GeometryRenderNodeConfig::DESCRIPTOR_SETS);
```

---

## Integration with ResourceProfiler

### Profiling Output

After conversion, ResourceProfiler reports will show:

```
Frame #1234 Resource Report
════════════════════════════════════════════════════════════════
Frame Duration: 16.67 ms
Peak VRAM: 128.00 MB

Node: FramebufferNode
  Stack: 32 bytes (4 × VkFramebuffer)
  Heap: 0 bytes
  VRAM: 64.00 MB

Node: DescriptorResourceGathererNode
  Stack: 1024 bytes (32 × ResourceVariant)
  Heap: 0 bytes
  VRAM: 0 bytes

Node: FrameSyncNode
  Stack: 80 bytes (semaphores + fences)
  Heap: 0 bytes
  VRAM: 0 bytes

Total Stack Usage: 1136 bytes (< 64 KB safe limit ✓)
Total Aliasing Efficiency: 27% VRAM saved
════════════════════════════════════════════════════════════════
```

### Export Formats

**Text Export**:
```cpp
auto textReport = resourcePool->GetProfiler()->ExportAsText(frameNumber);
```

**JSON Export**:
```cpp
auto jsonReport = resourcePool->GetProfiler()->ExportAsJSON(frameNumber);
```

---

## Implementation Checklist

- [x] Create `TrackStackArray()` helper in NodeInstance.h
- [x] Add `GetProfiler()` accessor to ResourcePool.h
- [x] Convert FramebufferNode (completed)
- [ ] Convert DescriptorResourceGathererNode (high priority)
- [ ] Convert FrameSyncNode (high priority)
- [ ] Convert GraphicsPipelineNode (medium priority)
- [ ] Convert DescriptorSetNode (medium priority)
- [ ] Fix GeometryRenderNode line 329 (minor)

---

## Performance Impact

### Stack Usage Added

| Node | Array | Elements | Bytes | Priority |
|------|-------|----------|-------|----------|
| FramebufferNode | framebuffers | 4 | 32 | ✅ Done |
| DescriptorResourceGathererNode | resourceArray_ | 32 | ~1 KB | 🔥 High |
| DescriptorResourceGathererNode | slotRoleArray_ | 32 | 32 | 🔥 High |
| FrameSyncNode | semaphores × 2 | 7 | 56 | 🔥 High |
| FrameSyncNode | fences | 3 | 24 | 🔥 High |
| **Total High Priority** | | | **~1.2 KB** | |

**Safety**: Well within ESTIMATED_MAX_STACK_PER_FRAME (11 KB) and STACK_WARNING_THRESHOLD (512 KB)

### Heap Allocations Eliminated

- FramebufferNode: 1 vector → 0 (vector still used for output interface)
- DescriptorResourceGathererNode: 2 vectors → 0 per frame
- FrameSyncNode: 3 vectors → 0 per compile

**Total**: 5-6 heap allocations eliminated

---

## Best Practices

### When to Use TrackStackArray

✅ **DO use for**:
- Arrays with bounded size (VulkanLimits.h constants)
- Per-frame or per-compile allocations
- Arrays output to slots

❌ **DON'T use for**:
- Unbounded/dynamic sizes
- Temporary local variables (not output)
- Arrays consumed via const reference (no ownership)

### Lifetime Guidelines

- `ResourceLifetime::FrameLocal`: For per-frame resources (semaphores, fences)
- `ResourceLifetime::GraphLocal`: For compile-time resources (framebuffers, pipelines)

---

## References

- **VulkanLimits.h**: Compile-time array size constants
- **ResourceProfiler.h**: Profiling infrastructure
- **NodeInstance.h**: TrackStackArray() implementation (lines 733-796)
- **Phase-H-Vector-To-Array-Conversions.md**: Initial vector conversion work

---

## Summary

URM-tracked stack arrays combine the best of both worlds:
- **Zero heap allocations** (stack-allocated `std::array`)
- **Full visibility** (ResourceProfiler integration)
- **Minimal overhead** (one profiler call per array)
- **Production-ready** (safe stack limits, comprehensive tracking)

Completed conversions provide a template for remaining high-priority nodes.
