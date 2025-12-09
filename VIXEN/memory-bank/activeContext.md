# Active Context

**Last Updated**: December 9, 2025 (Session 3)
**Current Branch**: `claude/phase-k-hardware-rt`
**Status**: HW RT Pipeline WORKING - All pipelines rendering correctly

---

## Current State

### Hardware RT Pipeline Fixes ✅ (December 9, 2025 - Session 3)

Critical fixes for Hardware RT sparse rendering issue:

| Issue | Root Cause | Fix | Status |
|-------|------------|-----|--------|
| **HW RT sparse voxels** | Rays in voxel space, AABBs in world space | Keep rays in world space, match AABB coords | ✅ Fixed |
| **Voxel size mismatch** | voxelSize=1.0 creates [0,res] not [0,10] world | `voxelWorldSize = worldGridSize / resolution` | ✅ Fixed |
| **AABB data flow** | Cacher regenerated AABBs instead of using node's | Added precomputedAABBData to AccelStructCreateInfo | ✅ Fixed |
| **ESC freeze** | PostQuitMessage during frame loop | Publish WindowCloseEvent, check after ProcessEvents | ✅ Fixed |
| **Orange artifact** | DEBUG_MATERIAL_ID enabled in compressed frag | Disabled debug flag | ✅ Fixed |

**Key Insight:** AABBs from VoxelAABBConverterNode (61,192 for Cornell @ res=64) must use world-space coordinates [0, worldGridSize=10], and rays in VoxelRT.rgen must NOT be transformed to voxel space.

### Type Consolidation ✅

Removed duplicate struct definitions - RenderGraph now uses CashSystem types:

| Type | Now Aliased To |
|------|----------------|
| `VoxelAABB` | `CashSystem::VoxelAABB` |
| `VoxelBrickMapping` | `CashSystem::VoxelBrickMapping` |
| `VoxelAABBData` | `CashSystem::VoxelAABBData` |
| `AccelerationStructureData` | `CashSystem::AccelerationStructureData` |

### Previous Fixes ✅ (Sessions 1-2)

| Issue | Root Cause | Fix | Status |
|-------|------------|-----|--------|
| **Upside-down scenes** | Vulkan UV (0,0) at top-left | Added `ndc.y = -ndc.y` in `getRayDir()` | ✅ Fixed |
| **SDI naming collision** | Same program name for all pipelines | Added `_Compute_`, `_Fragment_`, `_RayTracing_` suffixes | ✅ Fixed |
| **Dark materials** | `brickMaterialData` stored only 0/1 | Store actual material ID from Material component | ✅ Fixed |
| **Empty brick corner** | Wrong leaf count formula in uncompressed shaders | Use `countLeavesBefore(validMask, leafMask, idx)` | ✅ Fixed |
| **Yellow floor** | Material ID 4 mapped to yellow instead of gray | Changed mat 4 to `(0.85, 0.85, 0.85)` | ✅ Fixed |

**Files Modified:**
- `shaders/RayGeneration.glsl:64` - Vulkan Y-flip
- `libraries/Profiler/src/BenchmarkGraphFactory.cpp` - SDI naming suffixes
- `libraries/SVO/src/SVORebuild.cpp:645-656` - Store actual material IDs
- `libraries/SVO/src/SVORebuild.cpp:74` - Material ID 4 color fix
- `libraries/Profiler/src/BenchmarkRunner.cpp:967-976` - vkDeviceWaitIdle on ESC
- `shaders/Materials.glsl:23` - Material ID 4 color (yellow → gray)
- `shaders/VoxelRayMarch.comp:246` - Leaf count formula fix
- `shaders/VoxelRayMarch.comp:252-256` - Brick index 0 validation fix
- `shaders/VoxelRayMarch.comp:257-264` - posInBrick calculation (use coef.normOrigin)
- `shaders/VoxelRayMarch.frag:212` - Leaf count formula fix
- `shaders/VoxelRayMarch.frag:218-221` - Brick index 0 validation fix
- `shaders/VoxelRayMarch.frag:222-229` - posInBrick calculation (use coef.normOrigin)
- `shaders/VoxelRayMarch_Compressed.comp:17` - Disable DEBUG_MATERIAL_ID

**Commits Created:**
1. `40a643e` - Frame capture feature
2. `04ba64a` - InputNode mouse capture mode
3. `1a37138` - SDI naming + projection Y-flip
4. `994d4fe` - Store actual material IDs in brickMaterialData
5. `dd45d26` - Shader Y-flip in RayGeneration.glsl + improved SDI naming
6. `782fd8d` - vkDeviceWaitIdle before cleanup on ESC exit

---

### InputNode QoL Features Complete ✅ (December 9, 2025)

Added configurable input behavior for different use cases:

| Parameter | Type | Values | Description |
|-----------|------|--------|-------------|
| `enabled` | bool | true/false | Enable/disable all input polling |
| `mouse_capture_mode` | int | 0-2 | MouseCaptureMode enum value |

**MouseCaptureMode enum:**
- `CenterLock` (0) - Mouse locked to window center (FPS camera)
- `Free` (1) - Mouse moves freely (GUI/editor mode)
- `Disabled` (2) - No mouse capture (benchmark/headless)

**Files Modified:**
- `libraries/RenderGraph/include/Data/Nodes/InputNodeConfig.h` - Added MouseCaptureMode enum + parameters
- `libraries/RenderGraph/include/Nodes/InputNode.h` - Added member variables
- `libraries/RenderGraph/src/Nodes/InputNode.cpp` - Parameter reading + conditional logic
- `libraries/Profiler/src/BenchmarkGraphFactory.cpp` - Set Disabled mode for benchmarks

---

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

#### Architecture: VoxelAABBCacher (High Priority)
- [ ] **Create VoxelAABBCacher** - Specialized cacher for AABB extraction
  - Pattern: Each node has its own cacher (like VoxelGridNode → VoxelSceneCacher)
  - VoxelAABBConverterNode → VoxelAABBCacher
  - Remove `precomputedAABBData` hack from AccelStructCreateInfo
  - Key: Cacher.GetOrCreate() returns cached VoxelAABBData, node just passes it along
  - AccelerationStructureCacher focuses only on BLAS/TLAS building

#### Other Deferred Tasks
- [ ] BuildHybridGraph() - Combine compute + fragment approaches
- [ ] GLSL shader counter queries (avg_voxels_per_ray, cache_line_hits)
- [ ] VK_KHR_performance_query (hardware bandwidth measurement)
- [ ] gpu_utilization_percent (vendor-specific APIs)
- [ ] Nsight Graphics validation
- [ ] GPU integration test (requires running Vulkan application)
- [ ] Node-specific hook registration (O(1) dispatch vs O(N) broadcast)
- [ ] Window resolution bug - render resolution vs window size mismatch
- [ ] **Unified Parameter/Resource Type System** - Merge `NodeParameterManager` with compile-time resource system
  - Current: Two separate type registries (`PARAMETER_TYPES` macro vs `RESOURCE_TYPE_REGISTRY`)
  - Goal: Single registry handles both slots AND parameters
  - Benefit: Any type valid in ResourceTypeTraits automatically usable as parameter
  - Files: `ParameterDataTypes.h`, `ResourceTypeTraits.h`, `NodeParameterManager.h`

#### Completed
- [x] ~~Create SceneDataCacher in CashSystem library~~ - VoxelSceneCacher + AccelerationStructureCacher DONE
- [x] ~~Cache persistence (serialization/deserialization)~~ - Binary format with GPU re-upload DONE
- [x] ~~Remove legacy `#if 0` blocks~~ - ~1450 lines removed from VoxelGridNode + AccelerationStructureNode
- [x] ~~Type consolidation~~ - RenderGraph uses CashSystem types via aliases

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
