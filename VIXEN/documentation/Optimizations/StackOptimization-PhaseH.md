# Stack Optimization Plan - Phase H

**Document Version**: 1.0
**Created**: 2025-11-11
**Status**: Planning
**Priority**: High (Performance Impact)

---

## Executive Summary

This document outlines opportunities to optimize VIXEN's memory allocation patterns by moving heap allocations to the stack. The goal is to reduce allocation overhead, improve cache locality, and increase system responsiveness by leveraging compile-time known sizes for frequently-allocated resources.

**Key Benefits**:
- **Zero per-frame allocations** in hot paths (currently dozens per frame)
- **Better cache locality** through contiguous stack memory
- **Reduced memory fragmentation** by avoiding heap allocations
- **Improved responsiveness** through faster allocation/deallocation
- **Compile-time guarantees** on resource limits

**Safety**:
- Conservative stack limits (~11 KB per frame, well under 1 MB safety threshold)
- Debug-only `StackTracker` utility to monitor actual usage
- No risk of stack overflow with documented limits

---

## Table of Contents

1. [Background](#background)
2. [Optimization Opportunities](#optimization-opportunities)
3. [Implementation Strategy](#implementation-strategy)
4. [Stack Safety](#stack-safety)
5. [New Infrastructure](#new-infrastructure)
6. [Performance Impact](#performance-impact)
7. [Migration Plan](#migration-plan)
8. [Validation](#validation)

---

## Background

### Current Problem

VIXEN currently uses `std::vector` extensively throughout the codebase, including in hot paths executed every frame. While `std::vector` is flexible, it has performance costs:

1. **Heap allocations** require system calls (slow)
2. **Memory fragmentation** from repeated allocate/free cycles
3. **Cache misses** from non-contiguous memory
4. **Allocation overhead** from memory manager bookkeeping

### Why Stack Allocation?

For **fixed-size or bounded resources**, stack allocation with `std::array` offers:

1. **Compile-time allocation** - no runtime overhead
2. **Contiguous memory** - better cache utilization
3. **Automatic cleanup** - RAII without heap operations
4. **Predictable performance** - no allocator variance

### When to Use Stack vs Heap

| Use Stack (`std::array`) | Use Heap (`std::vector`) |
|-------------------------|-------------------------|
| ✅ Fixed size known at compile time | ✅ Variable size unknown at compile time |
| ✅ Small-to-medium size (<1 KB) | ✅ Large size (>10 KB) |
| ✅ Short lifetime (function scope) | ✅ Long lifetime (object member) |
| ✅ Hot paths (per-frame) | ✅ Rarely allocated |
| ✅ Bounded by spec (Vulkan limits) | ✅ Unbounded growth needed |

---

## Optimization Opportunities

Based on comprehensive codebase analysis, opportunities are categorized by impact:

### 🔴 HIGH PRIORITY - Hot Path Optimizations

These optimizations target **per-frame allocations** in render loops.

#### 1. SwapChainNode::GetColorImageViews()

**File**: `VIXEN/RenderGraph/src/Nodes/SwapChainNode.cpp:349-365`

**Current Code**:
```cpp
const std::vector<VkImageView>& SwapChainNode::GetColorImageViews() const {
    static thread_local std::vector<VkImageView> views;
    views.clear();

    for (const auto& buffer : swapChainWrapper->scPublicVars.colorBuffers) {
        views.push_back(buffer.view);
    }

    return views;
}
```

**Problem**:
- Called **every frame**
- Clears and rebuilds vector every call
- Heap allocation on first call, reallocation if size changes

**Optimized Code**:
```cpp
struct SwapChainViews {
    std::array<VkImageView, MAX_SWAPCHAIN_IMAGES> views;
    uint32_t count = 0;
};

const SwapChainViews& SwapChainNode::GetColorImageViews() const {
    static thread_local SwapChainViews result;

    if (!swapChainWrapper) {
        result.count = 0;
        return result;
    }

    result.count = 0;
    for (const auto& buffer : swapChainWrapper->scPublicVars.colorBuffers) {
        result.views[result.count++] = buffer.view;
    }

    return result;
}
```

**Benefits**:
- Zero allocations per frame
- Better cache locality (contiguous array)
- Typical size: 2-3 image views = 16-24 bytes

**Stack Cost**: `4 views × 8 bytes = 32 bytes` (negligible)

---

#### 2. DescriptorSetNode Temporary Vectors

**File**: `VIXEN/RenderGraph/src/Nodes/DescriptorSetNode.cpp:440-630`

**Current Code**:
```cpp
void DescriptorSetNode::ExecuteImpl(...) {
    std::vector<VkDescriptorImageInfo> imageInfos;
    std::vector<VkDescriptorBufferInfo> bufferInfos;
    std::vector<VkWriteDescriptorSet> writes;

    // Build descriptor writes...
    writes.push_back(...);

    vkUpdateDescriptorSets(device, writes.size(), writes.data(), 0, nullptr);
}
```

**Problem**:
- Created **every frame** during descriptor updates
- Three heap allocations per update
- Hot path for dynamic descriptor binding

**Optimized Code**:
```cpp
void DescriptorSetNode::ExecuteImpl(...) {
    std::array<VkDescriptorImageInfo, MAX_DESCRIPTOR_BINDINGS> imageInfos;
    std::array<VkDescriptorBufferInfo, MAX_DESCRIPTOR_BINDINGS> bufferInfos;
    std::array<VkWriteDescriptorSet, MAX_DESCRIPTOR_BINDINGS> writes;
    size_t imageCount = 0;
    size_t bufferCount = 0;
    size_t writeCount = 0;

    TRACK_STACK_ARRAY(imageInfos, "DescriptorSetNode:imageInfos");
    TRACK_STACK_ARRAY(bufferInfos, "DescriptorSetNode:bufferInfos");
    TRACK_STACK_ARRAY(writes, "DescriptorSetNode:writes");

    // Build descriptor writes...
    writes[writeCount++] = ...;

    vkUpdateDescriptorSets(device, writeCount, writes.data(), 0, nullptr);
}
```

**Benefits**:
- Eliminates 3 heap allocations per frame
- Typical descriptor count: 4-8 bindings
- Stack-friendly sizes

**Stack Cost**:
```
imageInfos: 32 × 32 bytes = 1024 bytes
bufferInfos: 32 × 24 bytes = 768 bytes
writes: 32 × 64 bytes = 2048 bytes
Total: ~3.8 KB
```

---

#### 3. FramebufferNode Attachment Lists

**File**: `VIXEN/RenderGraph/src/Nodes/FramebufferNode.cpp:91-97`

**Current Code**:
```cpp
std::vector<VkImageView> attachments;
attachments.push_back(colorView);
if (hasDepth) {
    attachments.push_back(depthView);
}

VkFramebufferCreateInfo fbInfo = {};
fbInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
fbInfo.pAttachments = attachments.data();
```

**Problem**:
- Created during framebuffer setup (per swapchain image, on resize)
- Small, predictable size (1-2 attachments typically, max 8)

**Optimized Code**:
```cpp
std::array<VkImageView, MAX_FRAMEBUFFER_ATTACHMENTS> attachments;
uint32_t attachmentCount = 0;

attachments[attachmentCount++] = colorView;
if (hasDepth) {
    attachments[attachmentCount++] = depthView;
}

TRACK_STACK_ARRAY(attachments, "FramebufferNode:attachments");

VkFramebufferCreateInfo fbInfo = {};
fbInfo.attachmentCount = attachmentCount;
fbInfo.pAttachments = attachments.data();
```

**Benefits**:
- Zero allocations during resize events
- Typical size: 1-2 attachments = 8-16 bytes

**Stack Cost**: `9 attachments × 8 bytes = 72 bytes` (negligible)

---

#### 4. WindowNode Event Processing

**File**: `VIXEN/RenderGraph/src/Nodes/WindowNode.cpp:166`

**Current Code**:
```cpp
std::vector<WindowEvent> eventsToProcess;
{
    std::lock_guard<std::recursive_mutex> lock(eventMutex);
    eventsToProcess.swap(pendingEvents);
}

for (const auto& event : eventsToProcess) {
    // Process event...
}
```

**Problem**:
- Swaps vector every frame (allocation still occurs in pendingEvents)
- Event bursts can cause reallocation

**Optimized Code**:
```cpp
std::array<WindowEvent, MAX_WINDOW_EVENTS_PER_FRAME> eventsToProcess;
size_t eventCount = 0;

{
    std::lock_guard<std::recursive_mutex> lock(eventMutex);
    eventCount = std::min(pendingEvents.size(), MAX_WINDOW_EVENTS_PER_FRAME);
    std::copy_n(pendingEvents.begin(), eventCount, eventsToProcess.begin());
    pendingEvents.erase(pendingEvents.begin(), pendingEvents.begin() + eventCount);
}

TRACK_STACK_ARRAY(eventsToProcess, "WindowNode:events");

for (size_t i = 0; i < eventCount; ++i) {
    // Process eventsToProcess[i]...
}
```

**Benefits**:
- Bounded event processing per frame (prevents stalls)
- Typical event count: 0-10 per frame

**Stack Cost**: `64 events × 32 bytes = 2 KB` (conservative)

---

### 🟡 MEDIUM PRIORITY - Pipeline Configuration

These optimizations target **setup-time allocations** during pipeline creation.

#### 5. GraphicsPipelineNode Shader Stages

**File**: `VIXEN/RenderGraph/include/Nodes/GraphicsPipelineNode.h:85`

**Current Code**:
```cpp
class GraphicsPipelineNode : public TypedNode<GraphicsPipelineNodeConfig> {
    // ...
    std::vector<VkPipelineShaderStageCreateInfo> shaderStageInfos;
    std::vector<VkPushConstantRange> pushConstantRanges;
};
```

**Problem**:
- Member vectors allocate on heap
- Fixed size for lifetime of pipeline (typically 2-5 stages)
- Shader stages: vertex, fragment, geometry, tessellation, etc.

**Optimized Code**:
```cpp
class GraphicsPipelineNode : public TypedNode<GraphicsPipelineNodeConfig> {
    // ...
    std::array<VkPipelineShaderStageCreateInfo, MAX_SHADER_STAGES> shaderStageInfos;
    std::array<VkPushConstantRange, MAX_PUSH_CONSTANT_RANGES> pushConstantRanges;
    uint32_t shaderStageCount = 0;
    uint32_t pushConstantRangeCount = 0;
};
```

**Benefits**:
- No heap allocation for pipeline objects
- Better memory layout for node instances
- Typical usage: 2 stages (vert+frag) = small overhead

**Stack Cost**: Minimal (stored in object, not per-frame)

---

#### 6. DescriptorSetNode Layout Bindings

**File**: `VIXEN/RenderGraph/src/Nodes/DescriptorSetNode.cpp:121`

**Current Code**:
```cpp
std::vector<VkDescriptorSetLayoutBinding> vkBindings;
vkBindings.reserve(descriptorBindings.size());

for (const auto& binding : descriptorBindings) {
    vkBindings.push_back(...);
}

VkDescriptorSetLayoutCreateInfo layoutInfo = {};
layoutInfo.bindingCount = static_cast<uint32_t>(vkBindings.size());
layoutInfo.pBindings = vkBindings.data();
```

**Problem**:
- Created during descriptor set layout creation
- Bounded by MAX_DESCRIPTOR_BINDINGS (typically ≤16)

**Optimized Code**:
```cpp
std::array<VkDescriptorSetLayoutBinding, MAX_DESCRIPTOR_BINDINGS> vkBindings;
uint32_t bindingCount = 0;

for (const auto& binding : descriptorBindings) {
    vkBindings[bindingCount++] = ...;
}

TRACK_STACK_ARRAY(vkBindings, "DescriptorSetNode:layoutBindings");

VkDescriptorSetLayoutCreateInfo layoutInfo = {};
layoutInfo.bindingCount = bindingCount;
layoutInfo.pBindings = vkBindings.data();
```

**Benefits**:
- No allocation during layout creation
- Typical size: 4-8 bindings

**Stack Cost**: `32 bindings × 24 bytes = 768 bytes`

---

#### 7. Vertex Input Descriptions

**File**: `VIXEN/RenderGraph/src/Nodes/GraphicsPipelineNode.cpp:637-638`

**Current Code**:
```cpp
std::vector<VkVertexInputBindingDescription> vertexBindings;
std::vector<VkVertexInputAttributeDescription> vertexAttributes;

// Populate from shader reflection...

VkPipelineVertexInputStateCreateInfo vertexInputInfo = {};
vertexInputInfo.vertexBindingDescriptionCount = vertexBindings.size();
vertexInputInfo.pVertexBindingDescriptions = vertexBindings.data();
vertexInputInfo.vertexAttributeDescriptionCount = vertexAttributes.size();
vertexInputInfo.pVertexAttributeDescriptions = vertexAttributes.data();
```

**Problem**:
- Created during pipeline compilation
- Bounded by Vulkan spec (max 16 attributes)

**Optimized Code**:
```cpp
std::array<VkVertexInputBindingDescription, MAX_VERTEX_BINDINGS> vertexBindings;
std::array<VkVertexInputAttributeDescription, MAX_VERTEX_ATTRIBUTES> vertexAttributes;
uint32_t bindingCount = 0;
uint32_t attributeCount = 0;

TRACK_STACK_ARRAY(vertexBindings, "Pipeline:vertexBindings");
TRACK_STACK_ARRAY(vertexAttributes, "Pipeline:vertexAttributes");

// Populate from shader reflection...
vertexAttributes[attributeCount++] = ...;

VkPipelineVertexInputStateCreateInfo vertexInputInfo = {};
vertexInputInfo.vertexBindingDescriptionCount = bindingCount;
vertexInputInfo.pVertexBindingDescriptions = vertexBindings.data();
vertexInputInfo.vertexAttributeDescriptionCount = attributeCount;
vertexInputInfo.pVertexAttributeDescriptions = vertexAttributes.data();
```

**Benefits**:
- No allocation during pipeline creation
- Typical size: 3-8 attributes (position, normal, UV, etc.)

**Stack Cost**:
```
Bindings: 16 × 12 bytes = 192 bytes
Attributes: 16 × 20 bytes = 320 bytes
Total: 512 bytes
```

---

### 🟢 LOW PRIORITY - Setup-Time Optimizations

#### 8. DeviceNode GPU Enumeration

**File**: `VIXEN/RenderGraph/include/Nodes/DeviceNode.h:75`

**Current Code**:
```cpp
class DeviceNode : public TypedNode<DeviceNodeConfig> {
    std::vector<VkPhysicalDevice> availableGPUs;
    std::vector<const char*> deviceExtensions;
    std::vector<const char*> deviceLayers;
};
```

**Problem**:
- Setup-time only (once at startup)
- Small, bounded sizes

**Optimized Code**:
```cpp
class DeviceNode : public TypedNode<DeviceNodeConfig> {
    std::array<VkPhysicalDevice, MAX_PHYSICAL_DEVICES> availableGPUs;
    std::array<const char*, MAX_DEVICE_EXTENSIONS> deviceExtensions;
    std::array<const char*, MAX_VALIDATION_LAYERS> deviceLayers;
    uint32_t gpuCount = 0;
    uint32_t extensionCount = 0;
    uint32_t layerCount = 0;
};
```

**Benefits**:
- Cleaner memory layout
- No heap fragmentation during startup

**Stack Cost**: Stored in object, minimal per-frame impact

---

#### 9. PerFrameResources Frame Array

**File**: `VIXEN/RenderGraph/include/Core/PerFrameResources.h:153`

**Current Code**:
```cpp
class PerFrameResources {
    std::vector<FrameData> frames;
};
```

**Problem**:
- Fixed size (MAX_FRAMES_IN_FLIGHT, typically 2-3)
- Critical synchronization structure

**Optimized Code**:
```cpp
class PerFrameResources {
    std::array<FrameData, MAX_FRAMES_IN_FLIGHT> frames;
    uint32_t frameCount = MAX_FRAMES_IN_FLIGHT;
};
```

**Benefits**:
- Better cache locality for frame-in-flight synchronization
- Compile-time known size

**Stack Cost**: Stored in object, not per-frame

---

## Implementation Strategy

### Phase 1: Infrastructure (Week 1)

1. **Create VulkanLimits.h** ✅
   - Define all MAX_* constants
   - Document Vulkan spec limits
   - Conservative estimates for safety

2. **Create StackTracker.h/.cpp** ✅
   - Debug-only tracking utility
   - RAII scope-based tracking
   - Warning/critical thresholds
   - Statistics reporting

3. **Add CMake Integration**
   - Include new headers in build
   - PCH updates if needed

### Phase 2: Hot Path Optimizations (Week 2)

Priority order (highest impact first):

1. SwapChainNode::GetColorImageViews()
2. DescriptorSetNode temporary vectors
3. FramebufferNode attachments
4. WindowNode event processing

**Process for each**:
1. Add stack tracking macros
2. Replace std::vector with std::array
3. Add size tracking (count variable)
4. Test with StackTracker
5. Validate no stack overflow warnings
6. Profile performance improvement

### Phase 3: Pipeline Configuration (Week 3)

1. GraphicsPipelineNode shader stages
2. DescriptorSetNode layout bindings
3. Vertex input descriptions
4. Command buffer handle conversions

### Phase 4: Long-Lived Objects (Week 4)

1. DeviceNode member arrays
2. PerFrameResources frame array
3. Cache structure optimizations

### Phase 5: Validation & Profiling (Week 5)

1. Run full test suite with StackTracker enabled
2. Verify no warnings/critical errors
3. Profile frame timing improvements
4. Measure allocation counts (should be near zero)
5. Document actual stack usage vs. estimates

---

## Stack Safety

### Stack Size Limits

**Typical Stack Sizes**:
- Windows: 1 MB default (configurable to 8+ MB)
- Linux: 8 MB default
- Thread stacks: Often smaller (1-2 MB)

**VIXEN Safety Margins**:
- **Warning threshold**: 512 KB (50% of typical 1 MB stack)
- **Critical threshold**: 1 MB (100% of conservative estimate)
- **Estimated usage**: ~11 KB per frame (1% of stack)

**Safety Factor**: ~90x headroom (11 KB used / 1 MB available)

### Stack Overflow Prevention

1. **Compile-time bounds** via MAX_* constants
2. **Debug tracking** with StackTracker
3. **Conservative estimates** (overestimate sizes)
4. **Per-frame reset** prevents accumulation
5. **Warning system** alerts before critical thresholds

### What to Monitor

```cpp
void RenderGraph::Execute() {
    STACK_TRACKER_RESET_FRAME();  // Reset at frame start

    // Execute render graph...

    // Optional: Print stats every N frames
    if (frameCount % 100 == 0) {
        STACK_TRACKER_PRINT_STATS();
    }
}
```

**Monitor These Metrics**:
- Peak stack usage per frame (should be <50 KB)
- Lifetime peak (should never exceed 512 KB warning)
- Allocation count (verify it drops to near-zero after optimization)

---

## New Infrastructure

### VulkanLimits.h

Defines compile-time constants for all Vulkan resource limits:

```cpp
constexpr size_t MAX_FRAMES_IN_FLIGHT = 3;
constexpr size_t MAX_SWAPCHAIN_IMAGES = 4;
constexpr size_t MAX_SHADER_STAGES = 8;
constexpr size_t MAX_PUSH_CONSTANT_RANGES = 4;
constexpr size_t MAX_DESCRIPTOR_BINDINGS = 32;
constexpr size_t MAX_VERTEX_ATTRIBUTES = 16;
constexpr size_t MAX_FRAMEBUFFER_ATTACHMENTS = 9;
// ... etc
```

**Usage**:
```cpp
#include "Core/VulkanLimits.h"

std::array<VkImageView, MAX_SWAPCHAIN_IMAGES> views;
```

### StackTracker

Debug utility for monitoring stack allocations:

```cpp
#include "Core/StackTracker.h"

void MyFunction() {
    std::array<VkImageView, 4> views;
    TRACK_STACK_ARRAY(views, "MyFunction:views");

    // Use views...

    // Automatic cleanup on scope exit
}
```

**Features**:
- Zero overhead in release builds (macros become no-ops)
- Thread-local tracking (safe for multi-threading)
- Automatic reporting when thresholds exceeded
- Per-frame statistics

**Example Output**:
```
=== Stack Tracker Statistics ===
Current Frame:
  Current usage:    8.75 KB
  Peak usage:       12.50 KB
  Allocations:      24

Lifetime:
  Peak usage:       15.25 KB
  Total allocs:     2,448
  Frames tracked:   102

Thresholds:
  Warning:          512.00 KB
  Critical:         1.00 MB

Estimated overhead: 11.00 KB per frame
Peak usage is 1.5% of critical threshold
================================
```

---

## Performance Impact

### Expected Improvements

**Allocation Counts**:
- **Before**: 30-50 heap allocations per frame (hot paths)
- **After**: 0-5 heap allocations per frame
- **Reduction**: ~90% fewer allocations

**Frame Time Improvements**:
- **SwapChainNode**: ~1-2 μs per frame (eliminated vector operations)
- **DescriptorSetNode**: ~5-10 μs per frame (3 vector eliminations)
- **FramebufferNode**: ~0.5 μs (small but frequent)
- **Total estimated**: 10-20 μs per frame (~0.5-1% at 60 FPS)

**Cache Performance**:
- Better spatial locality (contiguous stack memory)
- Reduced TLB pressure (fewer heap pages)
- Improved instruction cache (simpler allocation paths)

**Memory Fragmentation**:
- Eliminates dozens of small allocations per frame
- Reduces heap fragmentation over time
- More predictable memory usage

### Profiling Methodology

**Before Optimization**:
```bash
# Profile allocation counts
valgrind --tool=massif ./VIXEN

# Profile frame timing
# (Enable internal profiler)
```

**After Optimization**:
1. Compare allocation counts (should drop dramatically)
2. Compare frame times (should improve slightly)
3. Verify no stack warnings in StackTracker output
4. Check cache miss rates (hardware counters)

---

## Migration Plan

### Step-by-Step Migration

#### Example: SwapChainNode::GetColorImageViews()

**Step 1**: Add Stack Tracking (Baseline)
```cpp
const std::vector<VkImageView>& SwapChainNode::GetColorImageViews() const {
    static thread_local std::vector<VkImageView> views;
    views.clear();

    TRACK_STACK_ALLOCATION("SwapChainNode:views", views.capacity() * sizeof(VkImageView));

    for (const auto& buffer : swapChainWrapper->scPublicVars.colorBuffers) {
        views.push_back(buffer.view);
    }

    return views;
}
```

**Step 2**: Run StackTracker, note baseline allocation

**Step 3**: Replace with std::array
```cpp
struct SwapChainViews {
    std::array<VkImageView, MAX_SWAPCHAIN_IMAGES> views;
    uint32_t count = 0;
};

const SwapChainViews& SwapChainNode::GetColorImageViews() const {
    static thread_local SwapChainViews result;

    TRACK_STACK_ARRAY(result.views, "SwapChainNode:views");

    if (!swapChainWrapper) {
        result.count = 0;
        return result;
    }

    result.count = 0;
    for (const auto& buffer : swapChainWrapper->scPublicVars.colorBuffers) {
        result.views[result.count++] = buffer.view;
    }

    return result;
}
```

**Step 4**: Verify no warnings from StackTracker

**Step 5**: Update call sites (if API changed)

**Step 6**: Run tests, verify functionality

**Step 7**: Profile improvement

### API Compatibility

**Breaking Changes**:
- Functions returning `std::vector&` need new signature
- Callers expecting vector API must adapt

**Mitigation**:
1. Use wrapper struct (e.g., `SwapChainViews`) with `.data()` and `.size()` methods
2. Update callers incrementally
3. Keep old API temporarily if needed (deprecated)

**Example Wrapper**:
```cpp
template<typename T, size_t N>
struct BoundedArray {
    std::array<T, N> data;
    size_t count = 0;

    // Vector-compatible API
    T* begin() { return data.data(); }
    T* end() { return data.data() + count; }
    size_t size() const { return count; }
    T& operator[](size_t i) { return data[i]; }
    const T& operator[](size_t i) const { return data[i]; }
};
```

---

## Validation

### Testing Strategy

1. **Unit Tests**
   - Verify MAX_* constants are sufficient
   - Test boundary conditions (full arrays)
   - Validate count tracking

2. **Integration Tests**
   - Run full render graph execution
   - Verify StackTracker reports reasonable usage
   - Test resize events (framebuffer reallocation)

3. **Stress Tests**
   - Maximum descriptor bindings
   - Maximum vertex attributes
   - Rapid resize events
   - Event bursts

4. **Profiling**
   - Allocation counts (before/after)
   - Frame timing (before/after)
   - Cache miss rates
   - Stack usage statistics

### Success Criteria

✅ **Functional**:
- All tests pass
- No visual regressions
- No crashes/errors

✅ **Performance**:
- ≥80% reduction in per-frame allocations
- ≥5 μs improvement in frame time
- Zero StackTracker warnings

✅ **Safety**:
- Peak stack usage <50 KB per frame
- Lifetime peak <512 KB
- No stack overflow in stress tests

---

## Appendix A: Quick Reference

### Common Patterns

**Replace std::vector parameter**:
```cpp
// Before
void Function(const std::vector<VkImageView>& views);

// After
void Function(const VkImageView* views, uint32_t viewCount);
```

**Replace std::vector return**:
```cpp
// Before
std::vector<VkImageView> GetViews();

// After
struct ViewArray {
    std::array<VkImageView, MAX_SWAPCHAIN_IMAGES> views;
    uint32_t count;
};
ViewArray GetViews();
```

**Replace std::vector member**:
```cpp
// Before
class MyNode {
    std::vector<VkPushConstantRange> ranges;
};

// After
class MyNode {
    std::array<VkPushConstantRange, MAX_PUSH_CONSTANT_RANGES> ranges;
    uint32_t rangeCount = 0;
};
```

### Stack Tracking Template

```cpp
void MyFunction() {
    // Declare array
    std::array<MyType, MAX_MY_TYPE> items;
    uint32_t itemCount = 0;

    // Track allocation (debug builds only)
    TRACK_STACK_ARRAY(items, "MyFunction:items");

    // Use array
    items[itemCount++] = ...;

    // Automatic cleanup on scope exit
}
```

---

## Appendix B: Performance Data (To Be Filled)

| Optimization | Allocs Before | Allocs After | Time Saved (μs) | Stack Used (bytes) |
|--------------|---------------|--------------|-----------------|-------------------|
| SwapChainNode views | 1/frame | 0 | TBD | 32 |
| DescriptorSet writes | 3/frame | 0 | TBD | 3,840 |
| Framebuffer attachments | 2-3/resize | 0 | TBD | 72 |
| WindowNode events | 1/frame | 0 | TBD | 2,048 |
| Pipeline shader stages | 1/pipeline | 0 | TBD | Object member |
| Descriptor layout bindings | 1/layout | 0 | TBD | 768 |
| Vertex input descriptions | 2/pipeline | 0 | TBD | 512 |
| **TOTAL** | **~30-50/frame** | **~0/frame** | **TBD** | **~11 KB** |

---

## Appendix C: Resources

**Vulkan Specification Limits**:
- https://registry.khronos.org/vulkan/specs/1.3/html/vkspec.html#limits

**Stack Size Configuration**:
- Windows: `/STACK` linker flag
- Linux: `ulimit -s` or pthread_attr_setstacksize()

**Related Documentation**:
- `VulkanLimits.h` - Constant definitions
- `StackTracker.h` - Debug tracking utility
- `memory-bank/systemPatterns.md` - VIXEN architecture patterns

---

**End of Document**
