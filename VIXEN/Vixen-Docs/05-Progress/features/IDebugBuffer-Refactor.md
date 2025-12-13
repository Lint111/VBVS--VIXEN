---
Status: Phase 1 Complete - Phase 2 Ready
---
# IDebugBuffer Interface Refactor

**HacknPlan:** [#58](https://app.hacknplan.com/p/230809/workitems/58)
**Status:** In Progress (Phase 1 Complete)
**Created:** 2025-12-13
**Updated:** 2025-12-13

## Summary

Refactor the debug buffer infrastructure to support polymorphic buffer types, enabling real `avgVoxelsPerRay` measurement via shader atomic counters instead of the hardcoded estimate `octreeDepth * 3.0f`.

## Phase 1: Foundation ✅ COMPLETE

Consolidated debug buffer infrastructure from 8 files to 7 focused files:

### Final Debug/ Structure

| File | Purpose |
|------|---------|
| `IDebugBuffer.h` | GPU buffer interface (Reset/Read/GetVkBuffer/GetData) |
| `IExportable.h` | Pure serialization (ToString/ToCSV/ToJSON) |
| `IDebugCapture.h` | Resource detection marker (GetBuffer→IDebugBuffer*) |
| `RayTraceBuffer.h/.cpp` | Per-ray traversal capture implementing IDebugBuffer |
| `ShaderCountersBuffer.h/.cpp` | Atomic counter metrics implementing IDebugBuffer |
| `DebugRaySample.h` | Data types (RayTrace, TraceStep, etc.) |
| `DebugCaptureResource.h` | Polymorphic wrapper with factory methods |

### Key Changes

- `IDebugCapture::GetBuffer()` returns `IDebugBuffer*` (polymorphic)
- `DebugCaptureResource` uses factory methods: `CreateRayTrace()`, `CreateCounters()`
- Removed `DebugCaptureBuffer.h` and `IDebugExportable.h`
- Fixed `.gitignore` to track Debug/ source directories
- Updated `VoxelGridNode` and `DebugBufferReaderNode` for new API

### Files Modified

- `libraries/RenderGraph/include/Debug/*` - Full refactor
- `libraries/RenderGraph/src/Debug/RayTraceBuffer.cpp` - NEW
- `libraries/RenderGraph/src/Debug/ShaderCountersBuffer.cpp` - NEW
- `libraries/RenderGraph/src/Nodes/VoxelGridNode.cpp` - Use new API
- `libraries/RenderGraph/src/Nodes/DebugBufferReaderNode.cpp` - Polymorphic dispatch
- `libraries/RenderGraph/include/Data/VariantDescriptors.h` - DebugBufferDescriptor
- `.gitignore` - Unignore libraries/**/Debug/

## Phase 2: Integration (Next)

1. Enable ENABLE_SHADER_COUNTERS in VoxelRayMarch shaders
2. Create ShaderCountersBuffer in VoxelGridNode alongside RayTraceBuffer
3. Wire ShaderCountersBuffer readback to BenchmarkRunner
4. Replace hardcoded `avgVoxelsPerRay` estimate with real measurement

## Acceptance Criteria

- [x] IDebugBuffer interface defined
- [x] RayTraceBuffer migrated from DebugCaptureBuffer
- [x] ShaderCountersBuffer implements IDebugBuffer
- [x] DebugBufferReaderNode uses polymorphic interface
- [ ] Real avgVoxelsPerRay in benchmark exports
- [ ] No performance regression
