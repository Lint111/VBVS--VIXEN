# Active Context

**Last Updated**: December 8, 2025
**Current Branch**: `claude/phase-k-hardware-rt`
**Status**: Cacher Integration COMPLETE - Ready for Phase L (Benchmark Data Collection)

---

## Current State

### All Shader Variants Complete ✅

| Pipeline | Uncompressed | Compressed |
|----------|--------------|------------|
| Compute | VoxelRayMarch.comp | VoxelRayMarch_Compressed.comp |
| Fragment | VoxelRayMarch.frag | VoxelRayMarch_Compressed.frag |
| Hardware RT | VoxelRT.rgen/rmiss/rchit/rint | VoxelRT_Compressed.rchit |

### Cacher Integration Complete ✅

Both scene data and acceleration structure creation now use the cacher system exclusively:

| Cacher | Node | Responsibilities |
|--------|------|------------------|
| **VoxelSceneCacher** | VoxelGridNode | Scene generation, octree building, DXT compression, GPU buffers, OctreeConfig UBO |
| **AccelerationStructureCacher** | AccelerationStructureNode | AABB conversion, BLAS/TLAS creation, instance buffer, device addresses |

**Key Changes:**
- Legacy manual creation paths wrapped in `#if 0 ... #endif`
- Cacher registration is on-demand and idempotent
- Nodes release `shared_ptr` to cacher-owned resources on destroy
- VOXEL_SCENE_DATA input now **required** (was optional) in AccelerationStructureNode

### Test Results
- **24/24 RT benchmark tests passing**
- **~470 total tests passing** across all suites
- All pipelines rendering correctly

---

## Next Phase: L - Benchmark Data Collection

### Research Question
How do different Vulkan ray tracing pipeline architectures affect performance, bandwidth, and scalability?

### Test Matrix (180 configurations)

| Dimension | Values |
|-----------|--------|
| **Pipeline** | Compute, Fragment, Hardware RT |
| **Compression** | Uncompressed, DXT Compressed |
| **Resolution** | 64³, 128³, 256³, 512³ |
| **Scene** | Cornell, Noise, Tunnel, Cityscape |
| **Density** | 10%, 25%, 50%, 75%, 90% |

### Metrics to Collect (Section 5.2 Schema)

```json
{
  "test_info": { "pipeline", "resolution", "scene", "density" },
  "timing": { "frame_time_ms", "gpu_time_ms", "fps", "p99", "stddev" },
  "throughput": { "ray_throughput_mrays", "primary_rays_cast" },
  "memory": { "vram_usage_mb", "vram_budget_mb" },
  "device": { "gpu_name", "driver_version", "vram_gb" }
}
```

### Implementation Tasks

- [ ] Run full 180-config test matrix
- [ ] Export JSON results per test
- [ ] Aggregate statistics across runs
- [ ] Generate comparison charts
- [ ] Validate against hypothesis

---

## Todo List

### Phase L - Benchmark Execution
- [ ] Configure benchmark_config.json for full matrix
- [ ] Run compute pipeline tests (4 scenes × 4 resolutions × 5 densities × 2 compression = 160 tests)
- [ ] Run fragment pipeline tests (same matrix)
- [ ] Run hardware RT tests (same matrix)
- [ ] Export results to test_results/

### Phase M - Analysis & Paper
- [ ] Statistical analysis of results
- [ ] Performance comparison charts
- [ ] Write findings section
- [ ] Validate/refute hypothesis

### Deferred Tasks (Post-Benchmark)
- [ ] BuildHybridGraph() - Combine compute + fragment approaches
- [ ] GLSL shader counter queries (avg_voxels_per_ray, cache_line_hits)
- [ ] VK_KHR_performance_query (hardware bandwidth measurement)
- [ ] gpu_utilization_percent (vendor-specific APIs)
- [ ] Nsight Graphics validation
- [ ] GPU integration test (requires running Vulkan application)
- [ ] Node-specific hook registration (O(1) dispatch vs O(N) broadcast)
- [ ] Window resolution bug - render resolution vs window size mismatch
- [ ] Brick index lookup buffer consumption in VoxelAABBConverterNode
- [x] ~~Create SceneDataCacher in CashSystem library~~ → VoxelSceneCacher + AccelerationStructureCacher DONE
- [ ] Remove legacy `#if 0` blocks after benchmark validation confirms stability

---

## Quick Reference

### Build Commands
```bash
cmake --build build --config Debug --parallel 16
```

### Run Benchmarks
```bash
./binaries/vixen_benchmark.exe --config ./application/benchmark/benchmark_config.json --render -i 100 -w 10
```

### Test Commands
```bash
./build/libraries/SVO/tests/Debug/test_*.exe --gtest_brief=1
./build/libraries/Profiler/tests/Debug/test_profiler.exe --gtest_brief=1
```

---

## Key Files

| Purpose | Location |
|---------|----------|
| Benchmark config | `application/benchmark/benchmark_config.json` |
| Graph factory | `libraries/Profiler/src/BenchmarkGraphFactory.cpp` |
| RT shaders | `shaders/VoxelRT*.rgen/rmiss/rchit/rint` |
| Compute shaders | `shaders/VoxelRayMarch*.comp` |
| Fragment shaders | `shaders/VoxelRayMarch*.frag` |
| Material colors | `shaders/Materials.glsl` |

---

## Historical Sessions

Archived to `Vixen-Docs/05-Progress/Phase-History.md`

---

**End of Active Context**
