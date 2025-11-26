# RequestStackResource API - Safe Stack Allocation with Fallback

## Overview

The `RequestStackResource<T, Capacity>()` API provides **safe stack allocation with automatic heap fallback** and comprehensive error handling. It solves the problem of blindly assuming stack space is available.

## Architecture

### Allocation Strategy

```
1. Request stack allocation (fast path)
   ├─ Check stack budget
   ├─ If available → Allocate on stack ✅
   └─ If full → Go to step 2

2. Heap fallback (safe path)
   ├─ Check heap budget (if strict mode)
   ├─ If available → Allocate on heap ⚠️
   └─ If full → Return error ❌
```

### Return Type: std::expected

```cpp
template<typename T, size_t Capacity>
using StackResourceResult = std::expected<
    StackResourceHandle<T, Capacity>,  // Success: stack or heap
    AllocationError                     // Failure: error code
>;

enum class AllocationError {
    StackOverflow,   // Stack budget exceeded, fallback failed
    HeapOverflow,    // Heap budget also exceeded
    InvalidSize,     // Requested size is invalid
    SystemError      // Underlying system allocation failed
};
```

### StackResourceHandle: Unified Interface

The handle provides **the same interface** regardless of whether the allocation is on stack or heap:

```cpp
template<typename T, size_t Capacity>
class StackResourceHandle {
public:
    // Location queries
    bool isStack() const;
    bool isHeap() const;
    ResourceLocation getLocation() const;

    // Unified interface (works for both stack and heap)
    void push_back(const T& value);
    void push_back(T&& value);
    void clear();
    T* data();
    size_t size() const;
    size_t capacity() const;
    bool empty() const;

    // Array access
    T& operator[](size_t index);
    T& at(size_t index);

    // Iterator support
    auto begin();
    auto end();

    // Debugging
    std::string_view getName() const;
    uint32_t getNodeId() const;
};
```

## Usage Patterns

### Pattern 1: Basic Usage with Error Handling

```cpp
void DescriptorSetNode::ExecuteImpl(Context& ctx) {
    // Request stack allocation with fallback
    auto writes = budgetManager->RequestStackResource<VkWriteDescriptorSet, 32>(
        "DescriptorWrites",
        GetNodeId()
    );

    // Error handling
    if (!writes) {
        NODE_LOG_ERROR("Allocation failed: " <<
            VIXEN::AllocationErrorMessage(writes.error()));
        return;
    }

    // Use unified interface (works for stack or heap)
    for (const auto& binding : descriptorBindings) {
        VkWriteDescriptorSet write = { /* ... */ };
        writes->push_back(write);
    }

    // Pass to Vulkan API (same pointer regardless of location)
    vkUpdateDescriptorSets(
        device,
        writes->size(),
        writes->data(),
        0, nullptr
    );

    // Optional: Log if heap fallback occurred
    if (writes->isHeap()) {
        NODE_LOG_WARNING("Heap fallback for DescriptorWrites (stack full)");
    }
}
```

### Pattern 2: Using std::expected Idioms

```cpp
// Pattern 2a: Early return on error
auto writes = budgetManager->RequestStackResource<VkWriteDescriptorSet, 32>(
    "DescriptorWrites", GetNodeId());

if (!writes) {
    // Early return on allocation failure
    return;  // Or throw, or propagate error
}

// Pattern 2b: Transform error
auto writes = budgetManager->RequestStackResource<VkWriteDescriptorSet, 32>(
    "DescriptorWrites", GetNodeId())
    .or_else([](AllocationError error) {
        LOG_ERROR("Failed: " << AllocationErrorMessage(error));
        // Can transform to different error type
        return std::unexpected(MyCustomError::AllocationFailed);
    });

// Pattern 2c: Provide default/fallback
auto writes = budgetManager->RequestStackResource<VkWriteDescriptorSet, 32>(
    "DescriptorWrites", GetNodeId())
    .or_else([](AllocationError error) {
        // Provide fallback behavior
        LOG_WARNING("Using empty descriptor set");
        return StackResourceHandle<VkWriteDescriptorSet, 32>::CreateHeap("Fallback");
    });
```

### Pattern 3: Conditional Logic Based on Location

```cpp
auto writes = budgetManager->RequestStackResource<VkWriteDescriptorSet, 32>(
    "DescriptorWrites", GetNodeId());

if (!writes) {
    return;
}

// Different logic based on allocation location
if (writes->isStack()) {
    // Fast path - stack allocated
    // Can be more aggressive with allocations
    for (const auto& binding : descriptorBindings) {
        writes->push_back(CreateWrite(binding));
    }
} else {
    // Heap fallback path
    // Log for profiling
    NODE_LOG_WARNING("Heap fallback - consider increasing stack budget");

    // Same interface, just slower
    for (const auto& binding : descriptorBindings) {
        writes->push_back(CreateWrite(binding));
    }
}
```

### Pattern 4: Multiple Allocations with Tracking

```cpp
void DescriptorSetNode::ExecuteImpl(Context& ctx) {
    // Multiple stack allocations in same frame
    auto writes = budgetManager->RequestStackResource<VkWriteDescriptorSet, 32>(
        "DescriptorWrites", GetNodeId());

    auto imageInfos = budgetManager->RequestStackResource<VkDescriptorImageInfo, 32>(
        "ImageInfos", GetNodeId());

    auto bufferInfos = budgetManager->RequestStackResource<VkDescriptorBufferInfo, 32>(
        "BufferInfos", GetNodeId());

    // Check all allocations
    if (!writes || !imageInfos || !bufferInfos) {
        NODE_LOG_ERROR("Failed to allocate descriptor data");
        return;
    }

    // Track allocations (automatic via RequestStackResource)
    // Stack usage automatically tracked in budgetManager->GetStackTracker()

    // Use all resources
    BuildDescriptorWrites(*writes, *imageInfos, *bufferInfos, ...);

    // Get current stack usage
    auto stackUsage = budgetManager->GetStackTracker().GetCurrentFrameUsage();
    if (stackUsage.totalStackUsed > VIXEN::StackResourceTracker::WARNING_THRESHOLD) {
        NODE_LOG_WARNING("High stack usage: " << stackUsage.totalStackUsed << " bytes");
    }
}
```

### Pattern 5: RAII with Automatic Cleanup

```cpp
void WindowNode::ExecuteImpl(Context& ctx) {
    // Request stack allocation (automatically cleaned up at end of scope)
    auto events = budgetManager->RequestStackResource<WindowEvent, 64>(
        "WindowEvents", GetNodeId());

    if (!events) {
        return;
    }

    // Populate events
    {
        std::lock_guard<std::mutex> lock(eventMutex);
        for (const auto& event : pendingEvents) {
            events->push_back(event);
        }
        pendingEvents.clear();
    }

    // Process events
    for (const auto& event : *events) {
        ProcessEvent(event);
    }

    // Automatic cleanup when 'events' goes out of scope
    // Stack tracking automatically updated
}
```

## Integration with Existing Code

### Before (Manual StackArray)

```cpp
void ExecuteImpl(Context& ctx) {
    // Manual stack allocation (no tracking, no fallback)
    VIXEN::StackArray<VkWriteDescriptorSet, 32> writes;

    // No error handling if stack is full
    // No automatic tracking
    // No fallback to heap

    for (const auto& binding : descriptorBindings) {
        writes.push_back(write);  // May silently fail if full
    }

    vkUpdateDescriptorSets(device, writes.size(), writes.data(), 0, nullptr);
}
```

### After (RequestStackResource)

```cpp
void ExecuteImpl(Context& ctx) {
    // Safe stack allocation with tracking and fallback
    auto writes = budgetManager->RequestStackResource<VkWriteDescriptorSet, 32>(
        "DescriptorWrites", GetNodeId());

    // Explicit error handling
    if (!writes) {
        NODE_LOG_ERROR("Allocation failed: " <<
            VIXEN::AllocationErrorMessage(writes.error()));
        return;
    }

    // Automatic tracking in URM
    // Automatic heap fallback if stack full
    // Same interface regardless of location

    for (const auto& binding : descriptorBindings) {
        writes->push_back(write);  // Safe - will use heap if needed
    }

    vkUpdateDescriptorSets(device, writes->size(), writes->data(), 0, nullptr);

    // Optional: Check if fallback occurred
    if (writes->isHeap()) {
        NODE_LOG_WARNING("Heap fallback occurred");
    }
}
```

## Error Handling Best Practices

### ✅ DO

```cpp
// Early return on error
auto writes = budgetManager->RequestStackResource<T, N>("Name", nodeId);
if (!writes) {
    LOG_ERROR("Allocation failed: " << AllocationErrorMessage(writes.error()));
    return;  // Clean exit
}

// Check allocation location for profiling
if (writes->isHeap()) {
    profiler.RecordHeapFallback("DescriptorWrites");
}

// Log errors with context
if (!writes) {
    NODE_LOG_ERROR("Failed to allocate DescriptorWrites in node " <<
        GetName() << " frame " << frameNumber);
}
```

### ❌ DON'T

```cpp
// DON'T ignore errors
auto writes = budgetManager->RequestStackResource<T, N>("Name", nodeId);
// Forgot to check if (!writes)
writes->push_back(data);  // CRASH if allocation failed!

// DON'T assume stack allocation
auto writes = budgetManager->RequestStackResource<T, N>("Name", nodeId);
assert(writes->isStack());  // May be heap!

// DON'T cast or dereference without checking
auto* ptr = writes.value().data();  // Use writes->data() instead
```

## Performance Characteristics

| Location | Allocation Time | Access Time | Cache Friendliness | Fragmentation |
|----------|----------------|-------------|-------------------|---------------|
| **Stack** | **~1ns** | **~1ns** | **Excellent** | **None** |
| **Heap (fallback)** | ~100-1000ns | Same as stack | Good | Low (vector reserve) |

## Budget Configuration

### Set Stack Limits

```cpp
// Stack limits (from StackResourceTracker.h)
constexpr size_t MAX_STACK_PER_FRAME = 64 * 1024;      // 64KB default
constexpr size_t WARNING_THRESHOLD = 48 * 1024;         // Warn at 75%
constexpr size_t CRITICAL_THRESHOLD = 56 * 1024;        // Critical at 87.5%
```

### Set Heap Budgets

```cpp
// Configure heap budget (optional, for strict mode)
ResourceBudget heapBudget{
    .maxBytes = 10 * 1024 * 1024,  // 10MB limit
    .warningThreshold = 8 * 1024 * 1024,
    .strict = true  // Enable strict enforcement
};
budgetManager->SetBudget(BudgetResourceType::HostMemory, heapBudget);
```

### Query Stack Usage

```cpp
// Per-frame usage
auto currentUsage = budgetManager->GetStackTracker().GetCurrentFrameUsage();
LOG_INFO("Stack used: " << currentUsage.totalStackUsed << " / " <<
    VIXEN::StackResourceTracker::MAX_STACK_PER_FRAME);

// Historical statistics
auto stats = budgetManager->GetStackUsageStats();
LOG_INFO("Average: " << stats.averageStackPerFrame << " bytes");
LOG_INFO("Peak: " << stats.peakStackUsage << " bytes");
LOG_INFO("Warning frames: " << stats.warningFrames);
LOG_INFO("Critical frames: " << stats.criticalFrames);
```

## Common Use Cases

### Descriptor Updates (Hot Path)

```cpp
auto writes = budgetManager->RequestStackResource<VkWriteDescriptorSet, 32>(
    "DescriptorWrites", GetNodeId());
if (!writes) return;

BuildDescriptorWrites(*writes, ...);
vkUpdateDescriptorSets(device, writes->size(), writes->data(), 0, nullptr);
```

### Event Processing

```cpp
auto events = budgetManager->RequestStackResource<WindowEvent, 64>(
    "WindowEvents", GetNodeId());
if (!events) return;

// Copy events from queue
for (const auto& event : pendingEvents) {
    events->push_back(event);
}

// Process
for (const auto& event : *events) {
    HandleEvent(event);
}
```

### Temporary Buffers

```cpp
auto tempIndices = budgetManager->RequestStackResource<uint32_t, 1024>(
    "TempIndices", GetNodeId());
if (!tempIndices) return;

// Use for temporary computation
ComputeIndices(*tempIndices);
SubmitDraw(tempIndices->data(), tempIndices->size());
```

## Debugging and Profiling

### Enable Stack Tracking Logs

```cpp
// At frame start
budgetManager->BeginFrameStackTracking(frameNumber);

// ... execute nodes ...

// At frame end
budgetManager->EndFrameStackTracking();

// Automatic warnings if thresholds exceeded:
// [StackResourceTracker] WARNING: Frame ended with elevated stack usage
// (Frame 1234, Used: 49152/65536 bytes)
```

### Profile Allocation Patterns

```cpp
// Check allocation location distribution
size_t stackAllocations = 0;
size_t heapFallbacks = 0;

auto writes = budgetManager->RequestStackResource<T, N>("Name", nodeId);
if (writes) {
    if (writes->isStack()) {
        stackAllocations++;
    } else {
        heapFallbacks++;
    }
}

// Log at end of profiling session
LOG_INFO("Stack allocations: " << stackAllocations);
LOG_INFO("Heap fallbacks: " << heapFallbacks <<
    " (" << (100.0 * heapFallbacks / (stackAllocations + heapFallbacks)) << "%)");
```

## Migration Guide

### Step 1: Replace Manual StackArray

```cpp
// Before
VIXEN::StackArray<VkWriteDescriptorSet, 32> writes;

// After
auto writes = budgetManager->RequestStackResource<VkWriteDescriptorSet, 32>(
    "DescriptorWrites", GetNodeId());
if (!writes) return;
```

### Step 2: Update Usage Sites

```cpp
// Before
writes.push_back(write);
vkUpdateDescriptorSets(device, writes.size(), writes.data(), 0, nullptr);

// After (same interface!)
writes->push_back(write);  // Note: -> instead of .
vkUpdateDescriptorSets(device, writes->size(), writes->data(), 0, nullptr);
```

### Step 3: Add Error Handling

```cpp
// Before (silent failure)
writes.push_back(write);  // May fail silently if full

// After (explicit error)
if (!writes) {
    NODE_LOG_ERROR("Allocation failed");
    return;  // Or handle appropriately
}
writes->push_back(write);  // Safe - will use heap if needed
```

### Step 4: Monitor Heap Fallbacks

```cpp
// Log heap fallbacks during development
if (writes->isHeap()) {
    NODE_LOG_WARNING("Heap fallback in " << GetName() <<
        " - consider increasing stack budget");
}
```

## References

- **API Header:** `VIXEN/RenderGraph/include/Core/StackResourceHandle.h`
- **Integration:** `VIXEN/RenderGraph/include/Core/ResourceBudgetManager.h:290-294`
- **Limits:** `VIXEN/RenderGraph/include/Core/StackResourceTracker.h`
- **Stack Optimization Guide:** `VIXEN/documentation/Optimizations/Phase-H-Stack-Optimization.md`
