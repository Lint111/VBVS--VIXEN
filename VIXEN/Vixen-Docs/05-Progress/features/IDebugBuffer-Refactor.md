# IDebugBuffer Interface Refactor

**HacknPlan:** [#58](https://app.hacknplan.com/p/230809/workitems/58)
**Status:** In Progress
**Created:** 2025-12-13

## Summary

Refactor the debug buffer infrastructure to support polymorphic buffer types, enabling real `avgVoxelsPerRay` measurement via shader atomic counters instead of the hardcoded estimate `octreeDepth * 3.0f`.

## Current Problem

- `IDebugCapture::GetCaptureBuffer()` returns concrete `DebugCaptureBuffer*`
- `DebugCaptureBuffer` is tightly coupled to ray trace data structures
- ShaderCounters infrastructure exists in shaders but has no CPU readback pipeline
- `avgVoxelsPerRay` uses hardcoded estimate in BenchmarkRunner.cpp:667-673

## Proposed Solution

### New IDebugBuffer Interface

```cpp
class IDebugBuffer {
public:
    virtual ~IDebugBuffer() = default;
    virtual DebugBufferType GetType() const = 0;
    virtual VkBuffer GetVkBuffer() const = 0;
    virtual bool Reset(VkDevice device) = 0;
    virtual uint32_t Read(VkDevice device) = 0;
    virtual std::any GetData() const = 0;
};
```

### Implementations

| Class | Purpose | Binding |
|-------|---------|---------|
| RayTraceBuffer | Per-ray traversal data | 4 |
| ShaderCountersBuffer | Atomic counter aggregates | 6 |

## Files to Modify

### Core (Breaking)
- `libraries/RenderGraph/include/Debug/IDebugCapture.h`
- `libraries/RenderGraph/include/Debug/DebugCaptureBuffer.h`
- `libraries/RenderGraph/include/Debug/DebugCaptureResource.h`
- `libraries/RenderGraph/src/Nodes/DebugBufferReaderNode.cpp`

### New Files
- `libraries/RenderGraph/include/Debug/IDebugBuffer.h`
- `libraries/RenderGraph/include/Debug/ShaderCountersBuffer.h`

### Integration
- `shaders/VoxelRayMarch.comp` - Enable ENABLE_SHADER_COUNTERS
- `libraries/Profiler/src/BenchmarkRunner.cpp` - Read real counters

## Implementation Phases

### Phase 1: Foundation (Non-Breaking)
1. Create IDebugBuffer.h interface
2. Create ShaderCountersBuffer implementation

### Phase 2: Migration (Minimally Breaking)
3. Make DebugCaptureBuffer implement IDebugBuffer
4. Add GetDebugBuffer() to IDebugCapture

### Phase 3: Integration
5. Update VoxelGridNode to create counter buffers
6. Enable shader counters
7. Wire to BenchmarkRunner

## Acceptance Criteria

- [ ] IDebugBuffer interface defined
- [ ] RayTraceBuffer migrated from DebugCaptureBuffer
- [ ] ShaderCountersBuffer reads atomic counters
- [ ] DebugBufferReaderNode uses polymorphic interface
- [ ] Real avgVoxelsPerRay in benchmark exports
- [ ] No performance regression
