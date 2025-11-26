# Stack Optimization + Resource Management Integration

**Document Version**: 1.0
**Created**: 2025-11-11
**Status**: Design Complete
**Related**: `StackOptimization-PhaseH.md`

---

## Executive Summary

This document describes the integration of **stack allocation optimization** with VIXEN's **Resource Management (RM) system**. By combining these systems, we achieve:

1. **Unified resource tracking** - Stack and heap resources use same interface
2. **Automatic lifecycle management** - RM state tracking for stack arrays
3. **Zero-overhead abstraction** - Debug tracking disappears in release builds
4. **Easy migration path** - Drop-in replacement for std::vector
5. **Diagnostics integration** - Stack usage reported through RM metadata

---

## Architecture Overview

### Before: Separate Systems

```
┌─────────────────────┐         ┌─────────────────────┐
│  Heap Resources     │         │  Stack Resources    │
│  (RM<std::vector>)  │         │  (std::array)       │
│                     │         │                     │
│  - State tracking   │         │  - Manual tracking  │
│  - Metadata         │         │  - No state         │
│  - Generation       │         │  - No metadata      │
└─────────────────────┘         └─────────────────────┘
```

### After: Unified System

```
┌────────────────────────────────────────────────────────┐
│               StackAllocatedRM<T, N>                   │
│                                                        │
│  ┌─────────────────┐    ┌────────────────────────┐   │
│  │ std::array<T,N> │    │    RM<size_t> state    │   │
│  │  (stack data)   │    │   - Ready/Outdated     │   │
│  │                 │    │   - Lock/Unlock        │   │
│  │  + count        │    │   - Generation         │   │
│  │  + debugName    │    │   - Metadata           │   │
│  └─────────────────┘    └────────────────────────┘   │
│                                                        │
│  ┌────────────────────────────────────────────────┐   │
│  │     ScopedStackAllocation (StackTracker)       │   │
│  │          (debug builds only)                   │   │
│  └────────────────────────────────────────────────┘   │
└────────────────────────────────────────────────────────┘
```

---

## Key Components

### 1. StackAllocatedRM<T, N>

**Purpose**: Stack-allocated array wrapper with RM state management

**Template Parameters**:
- `T` - Element type (e.g., VkImageView)
- `N` - Maximum capacity (compile-time constant)

**Key Features**:
- Vector-like API (`Add()`, `Size()`, `Data()`, `[]`)
- RM state tracking (Ready, Outdated, Locked, etc.)
- Automatic StackTracker integration
- Metadata storage for diagnostics
- Iterator support for range-based for loops

**Example**:
```cpp
// Old code (heap allocation)
RM<std::vector<VkImageView>> views;
std::vector<VkImageView> viewVec;
viewVec.push_back(view1);
viewVec.push_back(view2);
views.Set(std::move(viewVec));

// New code (stack allocation)
StackAllocatedRM<VkImageView, MAX_SWAPCHAIN_IMAGES> views("swapchain:views");
views.Add(view1);
views.Add(view2);
// Automatic RM state tracking + StackTracker integration
```

---

### 2. Integration with RM State System

**State Management**:
```cpp
StackAllocatedRM<VkImageView, 4> views("my:views");

// RM state operations work transparently
views.MarkOutdated();               // Mark for rebuild
if (views.Ready()) {                // Check readiness
    UseViews(views.Data());
}
views.Lock();                       // Prevent modification
views.GetGeneration();              // Cache invalidation
```

**Metadata Tracking**:
```cpp
// Automatic metadata
views.GetState().GetMetadata<std::string>("allocation_type");  // "stack"
views.GetState().GetMetadata<size_t>("capacity");              // 4
views.GetState().GetMetadata<size_t>("total_size");            // 32 bytes

// Custom metadata
views.GetState().SetMetadata("source", std::string("swapchain"));
```

---

### 3. StackTracker Integration

**Automatic Tracking**:
```cpp
// In debug builds:
StackAllocatedRM<VkDescriptorImageInfo, 32> imageInfos("descriptor:images");
// ↓
// Automatically calls: StackTracker::Instance().Allocate("descriptor:images", sizeof(array))
// ↓
// On destruction: StackTracker::Instance().Deallocate(sizeof(array))

// In release builds:
// Zero tracking overhead (macros become no-ops)
```

**Manual Monitoring**:
```cpp
void RenderGraph::Execute() {
    STACK_TRACKER_RESET_FRAME();

    // Use StackAllocatedRM throughout frame...
    StackImageViewArray views("frame:views");
    views.Add(...);

    // Check stats
    if (frameCount % 100 == 0) {
        STACK_TRACKER_PRINT_STATS();
        views.PrintStats();  // Per-resource stats
    }
}
```

---

## Type Aliases for Common Vulkan Resources

Pre-defined types for immediate use:

| Alias | Element Type | Capacity | Use Case |
|-------|-------------|----------|----------|
| `StackImageViewArray` | `VkImageView` | `MAX_SWAPCHAIN_IMAGES` (4) | Swapchain image views |
| `StackDescriptorWriteArray` | `VkWriteDescriptorSet` | `MAX_DESCRIPTOR_BINDINGS` (32) | Descriptor updates |
| `StackDescriptorImageInfoArray` | `VkDescriptorImageInfo` | `MAX_DESCRIPTOR_BINDINGS` (32) | Image descriptors |
| `StackDescriptorBufferInfoArray` | `VkDescriptorBufferInfo` | `MAX_DESCRIPTOR_BINDINGS` (32) | Buffer descriptors |
| `StackShaderStageArray` | `VkPipelineShaderStageCreateInfo` | `MAX_SHADER_STAGES` (8) | Pipeline shader stages |
| `StackPushConstantArray` | `VkPushConstantRange` | `MAX_PUSH_CONSTANT_RANGES` (4) | Push constants |
| `StackVertexAttributeArray` | `VkVertexInputAttributeDescription` | `MAX_VERTEX_ATTRIBUTES` (16) | Vertex attributes |
| `StackVertexBindingArray` | `VkVertexInputBindingDescription` | `MAX_VERTEX_BINDINGS` (16) | Vertex bindings |
| `StackAttachmentArray` | `VkImageView` | `MAX_FRAMEBUFFER_ATTACHMENTS` (9) | Framebuffer attachments |
| `StackCommandBufferArray` | `VkCommandBuffer` | `MAX_FRAMES_IN_FLIGHT` (3) | Command buffers |

**Example**:
```cpp
// Instead of:
std::vector<VkImageView> views;
views.push_back(colorView);
views.push_back(depthView);

// Use:
StackAttachmentArray views("framebuffer:attachments");
views.Add(colorView);
views.Add(depthView);

VkFramebufferCreateInfo fbInfo = {};
fbInfo.attachmentCount = views.Size();
fbInfo.pAttachments = views.Data();
```

---

## Migration Guide

### Pattern 1: Simple Vector Replacement

**Before**:
```cpp
void MyNode::Execute() {
    std::vector<VkImageView> views;
    views.push_back(view1);
    views.push_back(view2);

    UseViews(views.data(), views.size());
}
```

**After**:
```cpp
void MyNode::Execute() {
    StackImageViewArray views("MyNode:views");
    views.Add(view1);
    views.Add(view2);

    UseViews(views.Data(), views.Size());
}
```

**Changes**:
- `std::vector` → `StackImageViewArray` (or custom `StackAllocatedRM<T, N>`)
- `push_back()` → `Add()`
- `data()` → `Data()`
- `size()` → `Size()`

---

### Pattern 2: RM-Wrapped Vector

**Before**:
```cpp
class MyNode {
    RM<std::vector<VkPushConstantRange>> pushConstants;

    void Setup() {
        std::vector<VkPushConstantRange> ranges;
        ranges.push_back(...);
        pushConstants.Set(std::move(ranges));
    }

    void Use() {
        if (pushConstants.Ready()) {
            const auto& ranges = pushConstants.Value();
            CreateLayout(ranges.data(), ranges.size());
        }
    }
};
```

**After**:
```cpp
class MyNode {
    StackPushConstantArray pushConstants{"MyNode:pushConstants"};

    void Setup() {
        pushConstants.Clear();
        pushConstants.Add(...);
        pushConstants.MarkReady();
    }

    void Use() {
        if (pushConstants.Ready()) {
            CreateLayout(pushConstants.Data(), pushConstants.Size());
        }
    }
};
```

**Changes**:
- `RM<std::vector<T>>` → `StackAllocatedRM<T, N>` (or alias)
- No need for `Set()` / `Value()` dance
- Direct access to array methods
- RM state management still available

---

### Pattern 3: Member Variable Arrays

**Before**:
```cpp
class GraphicsPipelineNode {
    std::vector<VkPipelineShaderStageCreateInfo> shaderStages;
    std::vector<VkPushConstantRange> pushConstantRanges;

public:
    void AddShaderStage(const VkPipelineShaderStageCreateInfo& stage) {
        shaderStages.push_back(stage);
    }

    void CreatePipeline() {
        pipelineInfo.stageCount = shaderStages.size();
        pipelineInfo.pStages = shaderStages.data();
    }
};
```

**After**:
```cpp
class GraphicsPipelineNode {
    StackShaderStageArray shaderStages{"Pipeline:shaderStages"};
    StackPushConstantArray pushConstantRanges{"Pipeline:pushConstants"};

public:
    void AddShaderStage(const VkPipelineShaderStageCreateInfo& stage) {
        shaderStages.Add(stage);
    }

    void CreatePipeline() {
        pipelineInfo.stageCount = shaderStages.Size();
        pipelineInfo.pStages = shaderStages.Data();
    }
};
```

**Changes**:
- Minimal API changes
- Better memory layout (no heap fragmentation)
- Compile-time capacity guarantees

---

### Pattern 4: Temporary Locals (Hot Paths)

**Before**:
```cpp
void DescriptorSetNode::Execute() {
    std::vector<VkDescriptorImageInfo> imageInfos;
    std::vector<VkDescriptorBufferInfo> bufferInfos;
    std::vector<VkWriteDescriptorSet> writes;

    for (const auto& binding : bindings) {
        imageInfos.push_back(...);
        writes.push_back(...);
    }

    vkUpdateDescriptorSets(device, writes.size(), writes.data(), 0, nullptr);
}
```

**After**:
```cpp
void DescriptorSetNode::Execute() {
    StackDescriptorImageInfoArray imageInfos("DescriptorSet:imageInfos");
    StackDescriptorBufferInfoArray bufferInfos("DescriptorSet:bufferInfos");
    StackDescriptorWriteArray writes("DescriptorSet:writes");

    for (const auto& binding : bindings) {
        imageInfos.Add(...);
        writes.Add(...);
    }

    vkUpdateDescriptorSets(device, writes.Size(), writes.Data(), 0, nullptr);
}
```

**Benefits**:
- Zero heap allocations per frame
- Automatic StackTracker integration
- Debug overflow detection

---

## Diagnostics and Monitoring

### Per-Resource Statistics

```cpp
StackDescriptorWriteArray writes("descriptor:writes");
writes.Add(...);
writes.Add(...);

// Print individual resource stats (debug only)
writes.PrintStats();
```

**Output**:
```
[StackAllocatedRM] descriptor:writes
  Count:        8 / 32
  Utilization:  25.0%
  Stack usage:  2048 bytes
  Generation:   5
  Ready:        Yes
```

---

### Global Stack Tracking

```cpp
void RenderGraph::Execute() {
    STACK_TRACKER_RESET_FRAME();

    // Use various StackAllocatedRM resources...

    // Print global stats periodically
    if (frameCount % 100 == 0) {
        STACK_TRACKER_PRINT_STATS();
    }
}
```

**Output**:
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

### Metadata Integration

**Automatic Metadata** (set by StackAllocatedRM):
```cpp
StackImageViewArray views("my:views");

views.GetState().GetMetadata<std::string>("allocation_type");  // "stack"
views.GetState().GetMetadata<size_t>("capacity");              // 4
views.GetState().GetMetadata<size_t>("element_size");          // 8
views.GetState().GetMetadata<size_t>("total_size");            // 32
```

**Custom Metadata**:
```cpp
views.GetState().SetMetadata("source_node", std::string("SwapChainNode"));
views.GetState().SetMetadata("frame_number", uint64_t(frameCount));
```

---

## Safety Features

### 1. Bounds Checking (Debug Builds)

```cpp
StackImageViewArray views("test");
views.Add(view1);
views.Add(view2);

// Debug build: throws std::out_of_range
views[5];  // Index out of range

// Debug build: throws std::runtime_error
for (int i = 0; i < 100; ++i) {
    views.Add(view);  // Overflow at i = MAX_SWAPCHAIN_IMAGES
}
```

### 2. Capacity Warnings

```cpp
// Helper detects overflow during migration
std::vector<VkImageView> hugeVector(100, view);
auto stackViews = ToStackAllocated<VkImageView, 4>(hugeVector, "test");
// Output: [WARNING] Vector overflow during conversion: test (100 > 4)
```

### 3. StackTracker Warnings

```cpp
// Automatic warnings if stack usage exceeds thresholds
StackAllocatedRM<LargeStruct, 1000> hugeArray("danger");
// Output: [STACK WARNING] Stack usage exceeded warning threshold!
//   Allocation: danger (XXX KB)
//   Current usage: XXX KB
//   Threshold: 512.00 KB
```

---

## Performance Characteristics

### Memory Layout

**Heap Allocation (std::vector)**:
```
Stack:                 Heap:
┌─────────────┐       ┌──────────┐
│ std::vector │──────>│ Element 1│
│  - data*    │       │ Element 2│
│  - size     │       │ Element 3│
│  - capacity │       │   ...    │
└─────────────┘       └──────────┘
  24 bytes            N × sizeof(T) bytes
  + heap overhead     + allocator overhead
```

**Stack Allocation (StackAllocatedRM)**:
```
Stack:
┌────────────────────┐
│ StackAllocatedRM   │
│  - data[N]         │  <-- Contiguous, no indirection
│  - count           │
│  - debugName       │
│  - RM state        │
└────────────────────┘
  N × sizeof(T) + ~64 bytes
```

**Cache Performance**:
- Stack: Sequential access, single cache line
- Heap: Pointer chase, two cache lines minimum

---

### Allocation Costs

| Operation | Heap (std::vector) | Stack (StackAllocatedRM) |
|-----------|-------------------|-------------------------|
| Construction | ~100-500 ns (malloc) | 0 ns (compile-time) |
| Add element | 0-500 ns (realloc if needed) | 0 ns (increment count) |
| Destruction | ~50-200 ns (free) | 0 ns (no-op) |
| Access | ~2-5 ns (dereference) | ~1-2 ns (direct) |

**Per-Frame Impact** (typical descriptor update):
- Before: 3 vectors × 100 ns = 300 ns
- After: 0 ns
- **Savings**: ~300 ns per frame (~5% of 6 μs descriptor update)

---

## Testing Strategy

### 1. Unit Tests

```cpp
TEST(StackAllocatedRM, BasicOperations) {
    StackAllocatedRM<int, 10> array("test");

    EXPECT_EQ(array.Size(), 0);
    EXPECT_TRUE(array.Empty());
    EXPECT_FALSE(array.Full());

    array.Add(42);
    EXPECT_EQ(array.Size(), 1);
    EXPECT_EQ(array[0], 42);

    array.Clear();
    EXPECT_TRUE(array.Empty());
}

TEST(StackAllocatedRM, RMStateIntegration) {
    StackAllocatedRM<int, 10> array("test");

    EXPECT_TRUE(array.Ready());

    array.MarkOutdated();
    EXPECT_FALSE(array.Ready());

    array.MarkReady();
    EXPECT_TRUE(array.Ready());
}

TEST(StackAllocatedRM, BoundsChecking) {
    StackAllocatedRM<int, 2> array("test");

    array.Add(1);
    array.Add(2);

    #ifndef NDEBUG
    EXPECT_THROW(array.Add(3), std::runtime_error);  // Overflow
    EXPECT_THROW(array[5], std::out_of_range);       // Out of range
    #endif
}
```

---

### 2. Integration Tests

```cpp
TEST(SwapChainNode, StackAllocatedViews) {
    SwapChainNode node("test", nodeType);

    // Setup swapchain...

    auto views = node.GetColorImageViews();  // Returns StackImageViewArray

    EXPECT_LE(views.Size(), MAX_SWAPCHAIN_IMAGES);
    EXPECT_TRUE(views.Ready());

    // Should not trigger stack warnings
    EXPECT_LT(views.GetStackUsage(), STACK_WARNING_THRESHOLD);
}
```

---

### 3. Stress Tests

```cpp
TEST(StackAllocatedRM, MaxCapacity) {
    StackDescriptorWriteArray writes("stress");

    // Fill to maximum capacity
    for (size_t i = 0; i < MAX_DESCRIPTOR_BINDINGS; ++i) {
        VkWriteDescriptorSet write = {};
        writes.Add(write);
    }

    EXPECT_TRUE(writes.Full());
    EXPECT_EQ(writes.Size(), MAX_DESCRIPTOR_BINDINGS);

    // Verify StackTracker doesn't warn
    EXPECT_LT(writes.GetStackUsage(), STACK_WARNING_THRESHOLD);
}
```

---

## Best Practices

### ✅ DO

1. **Use type aliases for common Vulkan types**
   ```cpp
   StackImageViewArray views("name");  // Good
   ```

2. **Provide descriptive debug names**
   ```cpp
   StackAllocatedRM<VkImageView, 4> views("SwapChainNode:colorViews");  // Good
   ```

3. **Check capacity before loops**
   ```cpp
   for (size_t i = 0; i < std::min(count, array.Capacity()); ++i) {
       array.Add(items[i]);
   }
   ```

4. **Use RM state management**
   ```cpp
   if (views.Ready()) {
       UseViews(views.Data(), views.Size());
   }
   ```

5. **Monitor stack usage in debug builds**
   ```cpp
   if (frameCount % 100 == 0) {
       STACK_TRACKER_PRINT_STATS();
   }
   ```

---

### ❌ DON'T

1. **Don't use for large arrays**
   ```cpp
   StackAllocatedRM<BigStruct, 10000> huge("bad");  // BAD: Risk stack overflow
   ```

2. **Don't use for unbounded growth**
   ```cpp
   while (hasMore) {
       array.Add(GetNext());  // BAD: Can overflow
   }
   ```

3. **Don't skip debug names**
   ```cpp
   StackAllocatedRM<int, 10> array;  // OK but not ideal (no tracking name)
   ```

4. **Don't ignore Full() checks in critical paths**
   ```cpp
   array.Add(item);  // BAD: No check, might overflow
   ```

5. **Don't forget to Clear() reused arrays**
   ```cpp
   void PerFrameFunction() {
       static StackImageViewArray views("reused");
       views.Clear();  // GOOD: Reset for new frame
       // ...
   }
   ```

---

## Future Enhancements

### 1. Small Vector Optimization

Hybrid stack/heap allocation for arrays that occasionally exceed capacity:

```cpp
template<typename T, size_t N>
class SmallVectorRM {
    std::array<T, N> stackData;
    std::vector<T> heapData;
    bool useHeap = false;

    void Add(const T& value) {
        if (count < N) {
            stackData[count++] = value;
        } else {
            if (!useHeap) {
                heapData.insert(heapData.end(), stackData.begin(), stackData.end());
                useHeap = true;
            }
            heapData.push_back(value);
        }
    }
};
```

---

### 2. Arena Allocator Integration

For temporary allocations that exceed stack safety:

```cpp
template<typename T, size_t N>
class ArenaAllocatedRM {
    // Use thread-local arena for overflow
    // Bulk reset at frame end
};
```

---

### 3. Telemetry Integration

```cpp
// Automatic metrics collection
StackAllocatedRM<T, N> array("my:array");
// → Reports utilization to telemetry system
// → Alerts if consistently under/over-utilized
```

---

## Conclusion

The integration of **StackAllocatedRM** with VIXEN's **Resource Management** system provides:

✅ **Best of both worlds**: Stack performance + RM features
✅ **Easy migration**: Vector-like API, drop-in replacement
✅ **Safety**: Debug bounds checking + StackTracker warnings
✅ **Zero overhead**: Release builds have no tracking cost
✅ **Diagnostics**: Rich metadata and monitoring

**Next Steps**:
1. Integrate StackAllocatedRM.h into CMake build
2. Migrate high-priority hot paths (SwapChainNode, DescriptorSetNode)
3. Validate with StackTracker monitoring
4. Profile performance improvements
5. Document results in StackOptimization-PhaseH.md

---

**End of Document**
