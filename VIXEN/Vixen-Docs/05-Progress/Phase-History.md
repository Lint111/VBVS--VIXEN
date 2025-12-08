---
title: Phase History
aliases: [Completed Phases, Historical Progress]
tags: [progress, history, archive]
created: 2025-12-08
updated: 2025-12-08
---

# Phase History

Archived progress from completed phases. See [[Current-Status]] for active work.

---

## Phase K: Hardware RT (December 7-8, 2025) ✅ COMPLETE

### Summary
Full VK_KHR_ray_tracing_pipeline implementation with compressed and uncompressed variants.

### Key Bugs Fixed

| Bug | Root Cause | Fix |
|-----|------------|-----|
| Black screen/flickering | `VariadicSlotInfo::binding` defaulted to 0 | UINT32_MAX sentinel + 4 skip checks |
| Grey colors (RT compressed) | Dangling pointer - `std::span` to stack arrays | Two-pass with pre-allocated componentStorage |
| Dark grey scenes | MaterialIdToColor() only had IDs 1-20 | Added ranges 30-40, 50-61, HSV fallback |

### Files Created/Modified
- `shaders/VoxelRT.rgen/rmiss/rchit/rint` - RT shaders
- `shaders/VoxelRT_Compressed.rchit` - Compressed variant
- `shaders/Materials.glsl` - Unified material colors
- `VariadicTypedNode.h` - UINT32_MAX sentinel
- `DescriptorResourceGathererNode.cpp` - Skip checks
- `VoxelGridNode.cpp` - Two-pass component storage
- `SVORebuild.cpp` - Material color mappings

### Descriptor Bindings (VoxelRT_Compressed)
| Binding | Resource |
|---------|----------|
| 0 | outputImage |
| 1 | topLevelAS |
| 2 | aabbBuffer |
| 3 | materialIdBuffer |
| 5 | octreeConfig |
| 6 | compressedColors |
| 7 | compressedNormals |
| 8 | brickMapping |

---

## Phase J: Fragment Pipeline (December 5-6, 2025) ✅ COMPLETE

### Summary
Fragment shader ray marching with push constants for camera control.

### Key Implementations
- VoxelRayMarch.frag (uncompressed)
- VoxelRayMarch_Compressed.frag (compressed)
- Push constant support (64-byte camera data)
- GeometryRenderNode SetPushConstants()

### Files Modified
- `GeometryRenderNode.cpp` - Push constant wiring
- `GeometryRenderNodeConfig.h` - PUSH_CONSTANT_DATA slot
- `BenchmarkGraphFactory.cpp` - Fragment graph builder

---

## Phase I: Profiler System (November-December 2025) ✅ COMPLETE

### Components Built
| Component | Description |
|-----------|-------------|
| ProfilerSystem | Singleton orchestrator |
| RollingStats | Sliding window percentiles |
| DeviceCapabilities | GPU info capture |
| MetricsCollector | VkQueryPool timing + VRAM |
| MetricsExporter | CSV + JSON (Section 5.2) |
| BenchmarkConfig | JSON config loader |
| BenchmarkRunner | Test matrix framework |
| BenchmarkGraphFactory | Graph assembly |

### Test Count: 131 tests passing

---

## Shader Refactoring (December 4, 2025) ✅ COMPLETE

### Shared GLSL Includes Created
| File | Content |
|------|---------|
| RayGeneration.glsl | Ray setup, AABB intersection |
| CoordinateTransforms.glsl | Space conversions |
| ESVOCoefficients.glsl | RayCoefficients struct |
| ESVOTraversal.glsl | PUSH/ADVANCE/POP phases |
| TraceRecording.glsl | Debug capture |
| Lighting.glsl | Shading functions |

### Size Reduction
- VoxelRayMarch.comp: 80KB → 20KB (75%)
- VoxelRayMarch_Compressed.comp: 56KB → 22KB (60%)

---

## VoxelDataCache (December 2025) ✅ COMPLETE

Caching system for benchmark performance:
```cpp
VoxelDataCache::GetOrGenerate(sceneType, resolution, params);
VoxelDataCache::Clear();
VoxelDataCache::GetStats();
```

---

## Earlier Phases

See [[../02-Implementation/Overview]] for SVO/ESVO implementation history.
