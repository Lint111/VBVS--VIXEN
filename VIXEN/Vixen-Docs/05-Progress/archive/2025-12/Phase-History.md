---
title: Phase History
aliases: [Completed Phases, Historical Progress]
tags: [progress, history, archive]
created: 2025-12-08
updated: 2025-12-10
---

# Phase History

Archived progress from completed phases. See [[Current-Status]] for active work.

---

## Phase L: Data Pipeline (December 10, 2025) ✅ COMPLETE

### Summary
Data visualization infrastructure for benchmark analysis and multi-tester coordination.

### Key Components
| Component | Purpose |
|-----------|---------|
| aggregate_results.py | JSON → Excel aggregation with normalized schema |
| generate_charts.py | 9 chart types (FPS, frame time, heatmaps) |
| refresh_visualizations.py | Master pipeline orchestration |
| TesterPackage | Automatic ZIP packaging for benchmark results |

### Key Findings (RTX 3060 Laptop GPU, 144 tests)
| Pipeline | FPS @ 256³ | VRAM | Notes |
|----------|------------|------|-------|
| Compute | 80-130 | 320 MB | Best general performance |
| Fragment | 100-130 | 319 MB | Graphics integration |
| Hardware RT | ~40 | 1098 MB | 3.4x VRAM overhead |

### TesterPackage Feature
- CLI: `--tester "Name"`, `--no-package`
- Creates: `VIXEN_benchmark_YYYYMMDD_HHMMSS_<GPU>.zip`
- Contents: JSON results, debug_images/, system_info.json

### Commits
- `8c2a358` - feat(Profiler): Add automatic ZIP packaging for benchmark results
- `e43232c` - feat(data-pipeline): Implement Phase L data visualization pipeline

---

## Phase K Sessions 3-5 (December 9-10, 2025) ✅ COMPLETE

### Session 5: Tester Package (December 10)
Added miniz dependency, TesterPackage class for ZIP creation with system_info.json metadata.

### Session 4: Data Visualization (December 10)
Created aggregate_results.py, generate_charts.py. Analyzed 144 benchmarks.

### Session 3: HW RT Sparse Fix (December 9)
| Issue | Root Cause | Fix |
|-------|------------|-----|
| HW RT sparse voxels | Rays in voxel space, AABBs in world space | Keep rays in world space |
| Voxel size mismatch | voxelSize=1.0 creates [0,res] not [0,10] | `voxelWorldSize = worldGridSize / resolution` |
| AABB data flow | Cacher regenerated AABBs | Added precomputedAABBData to AccelStructCreateInfo |
| ESC freeze | PostQuitMessage during frame loop | WindowCloseEvent pattern |
| Orange artifact | DEBUG_MATERIAL_ID enabled | Disabled debug flag |

### Type Consolidation
RenderGraph now uses CashSystem types via aliases:
- `VoxelAABB` → `CashSystem::VoxelAABB`
- `VoxelBrickMapping` → `CashSystem::VoxelBrickMapping`
- `AccelerationStructureData` → `CashSystem::AccelerationStructureData`

### Sessions 1-2: Visual Fixes (December 8-9)
| Issue | Root Cause | Fix |
|-------|------------|-----|
| Upside-down scenes | Vulkan UV (0,0) at top-left | `ndc.y = -ndc.y` in getRayDir() |
| SDI naming collision | Same program name | Added `_Compute_`, `_Fragment_`, `_RayTracing_` suffixes |
| Dark materials | brickMaterialData stored 0/1 | Store actual material ID |
| Empty brick corner | Wrong leaf count formula | Use `countLeavesBefore()` |
| Yellow floor | Material ID 4 = yellow | Changed to gray (0.85) |

### InputNode QoL Features
- MouseCaptureMode enum: CenterLock, Free, Disabled
- Parameters: `enabled`, `mouse_capture_mode`

### Frame Capture Feature
- Automatic: Quarter-res mid-frame captures
- Manual: 'C' keypress full-res capture
- Output: `{output_dir}/debug_images/{test_name}_frame{N}.png`

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
