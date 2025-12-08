---
title: Current Status
aliases: [Active Work, Current Focus]
tags: [progress, status, active]
created: 2025-12-06
updated: 2025-12-08
related:
  - "[[Overview]]"
  - "[[Roadmap]]"
  - "[[Phase-History]]"
---

# Current Status

Active development focus, recent changes, and immediate priorities.

**Last Updated:** 2025-12-08

---

## 1. Active Phase

### Phase K: Hardware Ray Tracing - IN PROGRESS ✅

**Status:** RT Compressed Color Rendering FIXED (December 8, 2025)

**Branch:** `claude/phase-k-hardware-rt`

**Achievements:**
- All 24 RT benchmark tests running without black screens or flickering
- Three critical bugs fixed this session:
  1. **Dangling pointer in VoxelGridNode** - `std::span` stored pointers to stack arrays
  2. **Missing material color mappings** - IDs 30-61 had no color definitions
  3. **DXT compression artifacts** - Documented as expected behavior

---

## 2. Recent Accomplishments

### Session 2 (Dec 8, 2025) - RT Compressed Color Fixes

| Issue | Root Cause | Fix |
|-------|------------|-----|
| Grey colors in RT compressed | Dangling pointer - `std::span` to stack arrays | Two-pass with pre-allocated componentStorage |
| Non-cornell scenes dark grey | MaterialIdToColor() only had IDs 1-20 | Added ranges 30-40 (noise/tunnel), 50-61 (cityscape) |
| Color offset at boundaries | DXT compression artifact | Documented as inherent to DXT |

**New File:** `shaders/Materials.glsl` - Single source of truth for material colors

### Session 1 (Dec 8, 2025) - RT Black Screen Fix

| Fix | File | Description |
|-----|------|-------------|
| UINT32_MAX sentinel | `VariadicTypedNode.h:37` | Fixed binding=0 collision with uninitialized slots |
| 4 skip checks | `DescriptorResourceGathererNode.cpp` | Skip slots with binding == UINT32_MAX |
| FOV radians | `VoxelRT.rgen:64-65` | Added radians() conversion |
| Command buffer indexing | `TraceRaysNode.cpp` | Changed from currentFrame to imageIndex |

### Week 2 (Dec 2-7, 2025)

| Accomplishment | Details |
|----------------|---------|
| Hardware RT pipeline | VK_KHR_ray_tracing_pipeline complete |
| Brick mapping buffer | Binding 8 for compressed RTX |
| Fragment pipeline | VoxelRayMarch.frag, VoxelRayMarch_Compressed.frag |
| Push constants | 64-byte camera data working |
| Shader refactoring | 6 shared GLSL includes, 60-75% reduction |

---

## 3. Material ID Ranges (Documented)

| Scene Type | Material IDs | Colors |
|------------|--------------|--------|
| Cornell Box | 1-20 | Red, green, white walls, cubes, lights |
| Noise/Tunnel | 30-40 | Stone variants, stalactites, ore |
| Cityscape | 50-61 | Asphalt, concrete, glass |
| Unknown | * | HSV color wheel fallback |

---

## 4. Test Results

### Current Pass/Fail

| Suite | Pass | Skip | Fail |
|-------|------|------|------|
| test_octree_queries | 98 | 0 | 0 |
| test_entity_brick_view | 36 | 0 | 0 |
| test_ray_casting | 11 | 0 | 0 |
| test_rebuild_hierarchy | 4 | 0 | 0 |
| test_cornell_box | 7 | 2 | 0 |
| test_benchmark_config | 44 | 0 | 0 |
| test_benchmark_graph | 87 | 0 | 0 |
| test_profiler | 131 | 0 | 0 |
| **Total** | **~470** | **2** | **0** |

### RT Benchmark Tests

- All 24 RT benchmark configurations pass
- No black screens or flickering
- Colors render correctly for all scene types

---

## 5. Current Descriptor Bindings (VoxelRT_Compressed)

| Binding | Resource | Type |
|---------|----------|------|
| 0 | outputImage | STORAGE_IMAGE |
| 1 | topLevelAS | ACCELERATION_STRUCTURE_KHR |
| 2 | aabbBuffer | STORAGE_BUFFER |
| 3 | materialIdBuffer | STORAGE_BUFFER |
| 5 | octreeConfig | UNIFORM_BUFFER |
| 6 | compressedColors | STORAGE_BUFFER |
| 7 | compressedNormals | STORAGE_BUFFER |
| 8 | brickMapping | STORAGE_BUFFER |

---

## 6. Modified Files (Session 2)

| File | Change |
|------|--------|
| `VoxelGridNode.cpp:163-204` | Fixed dangling pointer with two-pass approach |
| `SVORebuild.cpp:68-120` | Added material colors for all scene types |
| `shaders/Materials.glsl` | **NEW** - Unified material color definitions |
| `shaders/VoxelRT_Compressed.rchit` | Include Materials.glsl |
| `shaders/VoxelRT.rchit` | Include Materials.glsl |
| `shaders/VoxelRayMarch.comp` | Include Materials.glsl |
| `shaders/VoxelRayMarch.frag` | Include Materials.glsl |

---

## 7. Known Issues

### DXT Compression Artifact (Expected Behavior)

Color bleeding at wall boundaries is inherent to DXT:
- 16 voxels share 2 reference colors per block
- When block spans material boundary, colors interpolate
- Present in all compressed pipelines (compute, fragment, RT)
- Uncompressed pipelines don't have this artifact

### Technical Debt

| Item | Priority | Notes |
|------|----------|-------|
| Brick index mismatch | Low | Lookup buffer created, consumption deferred |
| Window resolution bug | Low | Render resolution vs window size |

---

## 8. Next Steps

### Immediate Priorities

1. **Phase K completion** - Final testing and validation
2. **Benchmark data collection** - 180-config test matrix execution
3. **Research paper data** - Performance metrics across all pipelines

### Shader Variants Status

| Pipeline | Uncompressed | Compressed |
|----------|--------------|------------|
| Compute | ✅ VoxelRayMarch.comp | ✅ VoxelRayMarch_Compressed.comp |
| Fragment | ✅ VoxelRayMarch.frag | ✅ VoxelRayMarch_Compressed.frag |
| Hardware RT | ✅ VoxelRT.rgen/rmiss/rchit/rint | ✅ VoxelRT_Compressed.rchit |

---

## 9. Related Pages

- [[Overview]] - Progress overview
- [[Roadmap]] - Future plans
- [[Phase-History]] - Past milestones
- [[../03-Research/Hardware-RT|Hardware RT]] - Implementation details
