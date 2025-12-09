# Active Context

**Last Updated**: December 9, 2025
**Current Branch**: `claude/phase-k-hardware-rt`
**Status**: Frame Capture Feature COMPLETE - Ready for runtime testing

---

## Current State

### Frame Capture Feature Complete ✅ (December 9, 2025)

Debug frame capture during benchmark tests for artifact identification:

| Capture Type | Resolution | Trigger |
|--------------|------------|---------|
| **Automatic** | Quarter (half W×H) | Mid-frame of each test |
| **Manual** | Full resolution | 'C' keypress |

**Output:** `{output_dir}/debug_images/{test_name}_frame{N}.png`

**Files Created:**
- `libraries/Profiler/include/Profiler/FrameCapture.h` - Frame capture class
- `libraries/Profiler/src/FrameCapture.cpp` - Vulkan readback + stb_image_write PNG save

**Files Modified:**
- `libraries/EventBus/include/InputEvents.h:34` - Added `KeyCode::C`
- `libraries/RenderGraph/include/Nodes/InputNode.h:45` - Added `GetInputState()` getter
- `libraries/RenderGraph/src/Nodes/InputNode.cpp` - Track 'C' key, renamed `inputState` → `inputState_`
- `libraries/Profiler/include/Profiler/BenchmarkRunner.h` - Added FrameCapture member
- `libraries/Profiler/src/BenchmarkRunner.cpp:797-893` - Capture integration in frame loop
- `libraries/Profiler/CMakeLists.txt` - Added FrameCapture.cpp + stb include path
- `libraries/Profiler/src/BenchmarkGraphFactory.cpp:1299` - Fixed VOXEL_SCENE_DATA connection

**Build Status:** Profiler library builds successfully. Benchmark exe needs rebuild (was running during session).

**To test:**
```bash
taskkill /F /IM vixen_benchmark.exe
cmake --build build --config Debug --target vixen_benchmark
./binaries/vixen_benchmark.exe --config ./application/benchmark/benchmark_config.json --render
# Press 'C' during render to capture full-res frame
# Check debug_images/ folder for mid-frame auto-captures
```

---

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

### Cache Persistence Complete ✅

Scene data now persists to disk between benchmark runs:

| Component | Change |
|-----------|--------|
| **RenderGraph::Clear()** | Calls `SaveAllAsync()` before `ExecuteCleanup()` |
| **VoxelSceneCacher::SerializeToFile()** | Binary serialization of CPU data + metadata |
| **VoxelSceneCacher::DeserializeFromFile()** | Loads from disk + re-uploads to GPU |
| **BenchmarkRunner** | Now passes MainCacher + Logger to RenderGraph |

**Cache location:** `cache/devices/Device_<id>/VoxelSceneCacher.cache` (~4.6 MB for test data)

**Verified:** Cache hits observed during benchmark transitions - scene data reused across tests with same (sceneType, resolution, density, seed)

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

## Recent Completion

### Logging Refactor Phase 2 (December 8, 2025) ✅

Extended logging refactor to cover CashSystem library and fix compile errors from previous session:

**Build Fixes:**
- Fixed `logger/ILoggable.h` include paths → `ILoggable.h`
- Fixed duplicate constructor in VulkanLayerAndExtension (header vs cpp)
- Fixed lambda capture issues in MainCacher.h (LOG_* macros need `this`)
- Added GRAPH_LOG_CRITICAL macro to RenderGraph.cpp
- Added missing `voxelSceneData_` member to AccelerationStructureNode.h
- Fixed `std::filesystem::path + string` concatenation in BenchmarkMain.cpp
- Fixed SDI binding reference for outputImage (use literal 0)

**CashSystem Cachers Converted (11 files):**
1. shader_module_cacher.cpp - TypedCacher-based → LOG_* macros
2. shader_compilation_cacher.cpp - TypedCacher-based → LOG_* macros
3. pipeline_cacher.cpp - TypedCacher-based → LOG_* macros
4. pipeline_layout_cacher.cpp - TypedCacher-based → LOG_* macros
5. descriptor_cacher.cpp - TypedCacher-based → LOG_* macros
6. SamplerCacher.cpp - TypedCacher-based → LOG_* macros
7. TextureCacher.cpp - TypedCacher-based → LOG_* macros
8. MeshCacher.cpp - TypedCacher-based → LOG_* macros
9. RenderPassCacher.cpp - TypedCacher-based → LOG_* macros
10. DescriptorSetLayoutCacher.cpp - TypedCacher-based → LOG_* macros
11. device_identifier.cpp - Added ILoggable to DeviceRegistry class

**VulkanResources Converted (3 files):**
- VulkanLayerAndExtension - Added ILoggable inheritance
- VulkanSwapChain - Added ILoggable inheritance
- CommandBufferUtility.cpp - Kept fprintf for standalone function

**Summary:**
- ~150+ additional std::cout/cerr statements converted
- All TypedCacher classes now use LOG_* macros
- Terminal output is now clean (only intentional user-facing messages)
- Build passes with only PDB warnings

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
- [x] ~~Create SceneDataCacher in CashSystem library~~ - VoxelSceneCacher + AccelerationStructureCacher DONE
- [x] ~~Cache persistence (serialization/deserialization)~~ - Binary format with GPU re-upload DONE
- [x] ~~Remove legacy `#if 0` blocks~~ - ~1450 lines removed from VoxelGridNode + AccelerationStructureNode

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
