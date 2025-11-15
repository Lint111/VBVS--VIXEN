# Deferred Destruction System - Usage Guide

## Overview

The `DeferredDestructionQueue` enables **zero-stutter hot-reload** by deferring Vulkan resource destruction until the GPU has finished using them, without blocking on `vkDeviceWaitIdle()`.

**Files Created/Modified**:
- ✅ `RenderGraph/include/Core/DeferredDestruction.h` - Queue implementation
- ✅ `RenderGraph/include/EventTypes/RenderGraphEvents.h` - Device sync messages added
- ✅ `RenderGraph/include/Core/RenderGraph.h` - Added member + getter

---

## How It Works

### Traditional Approach (Blocking)
```cpp
// In shader hot-reload handler
VkPipeline oldPipeline = pipeline.Value();

vkDeviceWaitIdle(device);  // ❌ BLOCKS entire GPU - causes stutter
vkDestroyPipeline(device, oldPipeline, nullptr);

pipeline.Set(newPipeline);  // Safe to use immediately
```

**Problem**: `vkDeviceWaitIdle()` blocks the main thread until **all GPU work** finishes, causing visible frame drops during hot-reload.

### Deferred Destruction Approach (Zero-Stutter)
```cpp
// In shader hot-reload handler
VkPipeline oldPipeline = pipeline.Value();

// Queue for destruction (non-blocking)
auto* queue = renderGraph->GetDeferredDestructionQueue();
queue->Add(device, oldPipeline, currentFrame, vkDestroyPipeline);

pipeline.Set(newPipeline);  // Use immediately - old pipeline still valid for N frames
```

**Benefit**: Old pipeline destroyed after N frames (typically 3) have passed, ensuring GPU has finished using it without blocking.

---

## Integration Steps

### Step 1: Access Queue from RenderGraph

Nodes can access the queue via `RenderGraph`:

```cpp
// In NodeInstance::Compile() or Setup()
auto* deferredQueue = GetRenderGraph()->GetDeferredDestructionQueue();
```

### Step 2: Queue Resources for Deferred Destruction

Use the template `Add<T>()` method:

```cpp
// Pipeline hot-reload
deferredQueue->Add(device, oldPipeline, currentFrame, vkDestroyPipeline);

// Image recreation
deferredQueue->Add(device, oldImage, currentFrame, vkDestroyImage);

// Buffer reallocation
deferredQueue->Add(device, oldBuffer, currentFrame, vkDestroyBuffer);

// Framebuffer resize
deferredQueue->Add(device, oldFramebuffer, currentFrame, vkDestroyFramebuffer);
```

**Supported Types**: Any Vulkan handle with a standard destroyer:
- `VkPipeline` → `vkDestroyPipeline`
- `VkImage` → `vkDestroyImage`
- `VkBuffer` → `vkDestroyBuffer`
- `VkFramebuffer` → `vkDestroyFramebuffer`
- `VkImageView` → `vkDestroyImageView`
- `VkSampler` → `vkDestroySampler`
- etc.

### Step 3: Process Queue in Main Loop

The queue must be processed **once per frame** before rendering:

```cpp
// main.cpp or main render loop
while (!shouldQuit) {
    // 1. Process events
    messageBus.ProcessMessages();

    // 2. Recompile dirty nodes
    renderGraph->RecompileDirtyNodes();

    // 3. Process deferred destructions (BEFORE GPU work starts)
    renderGraph->GetDeferredDestructionQueue()->ProcessFrame(frameNumber);

    // 4. Wait for previous frame (fences/semaphores)
    // ...

    // 5. Render current frame
    renderGraph->RenderFrame();

    frameNumber++;
}
```

**Critical**: Call `ProcessFrame()` **before** GPU work to avoid race conditions.

### Step 4: Flush on Shutdown

Ensure all pending destructions execute during shutdown:

```cpp
// Shutdown sequence
renderGraph->GetDeferredDestructionQueue()->Flush();
renderGraph->ExecuteCleanup();
// ... destroy devices, instances, etc.
```

---

## Example: PipelineNode Hot-Reload

### Before (Blocking)
```cpp
void PipelineNode::HandleCompilationResult(const PipelineCompilationResult& result) {
    if (!result.success) {
        LOG_ERROR("Pipeline compilation failed");
        return;
    }

    VkDevice device = In<0, VkDevice>();

    // ❌ Blocking wait - causes stutter
    vkDeviceWaitIdle(device);

    // Destroy old pipeline
    if (pipeline.Ready()) {
        vkDestroyPipeline(device, pipeline.Value(), nullptr);
    }

    // Set new pipeline
    pipeline.Set(result.pipeline);
    pipeline.MarkReady();

    Out<0>(pipeline.Value());
}
```

### After (Zero-Stutter)
```cpp
void PipelineNode::HandleCompilationResult(const PipelineCompilationResult& result) {
    if (!result.success) {
        LOG_ERROR("Pipeline compilation failed");
        return;
    }

    VkDevice device = In<0, VkDevice>();

    // ✅ Defer old pipeline destruction (non-blocking)
    if (pipeline.Ready()) {
        VkPipeline oldPipeline = pipeline.Value();

        auto* deferredQueue = GetRenderGraph()->GetDeferredDestructionQueue();
        deferredQueue->Add(device, oldPipeline, currentFrame, vkDestroyPipeline);

        LOG_INFO("Queued old pipeline for deferred destruction");
    }

    // Immediately use new pipeline (old one still valid for N frames)
    pipeline.Set(result.pipeline);
    pipeline.MarkReady();

    Out<0>(pipeline.Value());

    LOG_INFO("Pipeline hot-swapped (generation: " +
             std::to_string(pipeline.GetGeneration()) + ")");
}
```

**Key Changes**:
1. Removed `vkDeviceWaitIdle()` call
2. Added `deferredQueue->Add()` for old pipeline
3. New pipeline set immediately
4. Old pipeline destroyed automatically after N frames

---

## How to Get Current Frame Number

Nodes need to pass the current frame number to `Add()`. Options:

### Option 1: Store in RenderGraph
```cpp
// RenderGraph.h
class RenderGraph {
public:
    uint64_t GetCurrentFrame() const { return currentFrame; }

private:
    uint64_t currentFrame = 0;
};

// RenderGraph.cpp - in RenderFrame()
void RenderGraph::RenderFrame() {
    // ... rendering logic ...
    currentFrame++;
}

// In node:
uint64_t frameNum = GetRenderGraph()->GetCurrentFrame();
deferredQueue->Add(device, oldPipeline, frameNum, vkDestroyPipeline);
```

### Option 2: Pass via Event Message
```cpp
// ShaderHotReloadReadyMessage includes current frame
struct ShaderHotReloadReadyMessage : public BaseEventMessage {
    // ... existing fields ...
    uint64_t currentFrame;
};

// In node handler:
void PipelineNode::OnShaderReload(const ShaderHotReloadReadyMessage& msg) {
    // ... trigger async compilation ...
    this->currentFrameForHotReload = msg.currentFrame;
}

void PipelineNode::HandleCompilationResult(const PipelineCompilationResult& result) {
    deferredQueue->Add(device, oldPipeline, currentFrameForHotReload, vkDestroyPipeline);
}
```

### Option 3: Store in Node Instance
```cpp
// NodeInstance.h
class NodeInstance {
protected:
    uint64_t lastExecutedFrame = 0;

public:
    void SetLastExecutedFrame(uint64_t frame) { lastExecutedFrame = frame; }
    uint64_t GetLastExecutedFrame() const { return lastExecutedFrame; }
};

// RenderGraph.cpp - in Execute()
void RenderGraph::Execute(VkCommandBuffer cmd) {
    for (NodeInstance* node : executionOrder) {
        node->SetLastExecutedFrame(currentFrame);
        node->Execute(cmd);
    }
}

// In node:
deferredQueue->Add(device, oldPipeline, GetLastExecutedFrame(), vkDestroyPipeline);
```

**Recommendation**: Use Option 1 (store in RenderGraph) for simplicity.

---

## Performance Characteristics

### Memory Overhead
```
Per queued resource:
- std::function destructor: ~32 bytes
- Frame number: 8 bytes
- Queue overhead: ~8 bytes
Total: ~48 bytes per resource

Typical hot-reload: 1-5 resources queued
Max overhead: ~250 bytes
```

### Processing Time
```
ProcessFrame() performance (1000 pending resources):
- Check frame number: O(1) per resource
- Execute destructor: ~1-5μs per Vulkan handle
- Total: ~1-5ms worst case (extremely rare)

Typical hot-reload (3 resources):
- Total: ~3-15μs (negligible)
```

### Frame Latency
```
Default MAX_FRAMES_IN_FLIGHT = 3

Frame 100: Resource queued
Frame 101: Still in use (GPU rendering frame 100)
Frame 102: Still in use (GPU rendering frame 101)
Frame 103: Destroyed (3 frames have passed)

Total latency: 3 frames (~50ms @ 60 FPS)
```

---

## Debug Statistics (Debug Builds Only)

```cpp
#ifdef _DEBUG
auto stats = deferredQueue->GetStats();

std::cout << "Total queued:    " << stats.totalQueued << "\n";
std::cout << "Total destroyed: " << stats.totalDestroyed << "\n";
std::cout << "Total flushed:   " << stats.totalFlushed << "\n";
std::cout << "Current pending: " << stats.currentPending << "\n";

deferredQueue->ResetStats();
#endif
```

**Example Output**:
```
Total queued:    47
Total destroyed: 44
Total flushed:   0
Current pending: 3
```

---

## Best Practices

### ✅ DO:
- Use deferred destruction for **hot-reload** scenarios (shaders, textures, frequent changes)
- Call `ProcessFrame()` **before** GPU work starts (after event processing)
- Call `Flush()` during shutdown to ensure cleanup
- Use for resources that change frequently during development

### ❌ DON'T:
- Use for **initial compilation** (no need - nothing in use yet)
- Use for **shutdown cleanup** (Flush() handles this)
- Use for **window resize** if immediate wait is acceptable (rare event)
- Queue resources with `VK_NULL_HANDLE` (automatically skipped, but wasteful)

### When to Use Immediate Wait vs Deferred
| Scenario | Use | Rationale |
|----------|-----|-----------|
| Shader hot-reload | Deferred | Frequent during development, user expects smooth iteration |
| Texture reload | Deferred | May happen often during asset work |
| Window resize | Immediate | Rare event, brief stutter acceptable |
| Swapchain recreation | Immediate | Already pausing rendering, no benefit from deferral |
| Application shutdown | Flush | Ensure all resources destroyed before device cleanup |

---

## Troubleshooting

### Issue: Validation Layer Errors About Destroyed Resources

**Symptom**: `vkDestroyPipeline` called while pipeline still in use

**Cause**: `MAX_FRAMES_IN_FLIGHT` set too low, or frame counter incorrect

**Fix**:
```cpp
// Increase frame latency (conservative)
deferredQueue->ProcessFrame(frameNumber, 5);  // Wait 5 frames instead of 3
```

### Issue: Memory Leak - Pending Resources Never Destroyed

**Symptom**: `GetPendingCount()` grows indefinitely

**Cause**: `ProcessFrame()` not called, or frame counter not incrementing

**Fix**:
```cpp
// Ensure ProcessFrame() called every frame
void MainLoop() {
    while (running) {
        messageBus.ProcessMessages();
        renderGraph->RecompileDirtyNodes();

        // CRITICAL: Must call this
        deferredQueue->ProcessFrame(frameNumber);

        renderGraph->RenderFrame();
        frameNumber++;  // CRITICAL: Must increment
    }
}
```

### Issue: Crash on Shutdown

**Symptom**: Vulkan validation errors about leaked resources

**Cause**: Pending destructions not flushed before device destruction

**Fix**:
```cpp
// Correct shutdown order
deferredQueue->Flush();              // 1. Destroy pending resources
renderGraph->ExecuteCleanup();       // 2. Execute cleanup stack
vkDestroyDevice(device, nullptr);    // 3. Destroy device
```

---

## Testing Checklist

- [ ] Unit test: Add resource, ProcessFrame after N frames, verify destroyed
- [ ] Unit test: Add multiple resources, verify FIFO ordering
- [ ] Integration test: Shader hot-reload with deferred destruction
- [ ] Manual test: Hot-reload shader 10 times rapidly, verify no stutter
- [ ] Manual test: Hot-reload during rendering, verify no validation errors
- [ ] Manual test: Shutdown with pending destructions, verify Flush() works
- [ ] Performance test: Measure ProcessFrame() overhead (< 100μs for 100 resources)
- [ ] Stress test: Queue 1000 resources, verify memory usage acceptable

---

## Summary

**What we've implemented**:
- ✅ `DeferredDestructionQueue` class with FIFO queue
- ✅ Template `Add<T>()` for any Vulkan handle type
- ✅ `ProcessFrame()` with configurable frame latency
- ✅ `Flush()` for shutdown cleanup
- ✅ Debug statistics tracking
- ✅ Integration into RenderGraph

**Next steps for full zero-stutter hot-reload**:
1. Add `currentFrame` tracking to RenderGraph
2. Update PipelineNode to use deferred destruction
3. Test shader hot-reload with frame time monitoring
4. Validate no Vulkan errors during hot-reload
5. Document in memory bank

**Performance**: ~48 bytes per queued resource, ~1-5μs destruction overhead per resource

**Status**: ✅ Ready for integration into hot-reload nodes
